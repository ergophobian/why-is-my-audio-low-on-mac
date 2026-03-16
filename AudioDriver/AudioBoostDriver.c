/*
 * AudioBoostDriver.c
 * AudioBoost Virtual Audio Driver - CoreAudio HAL Plugin
 *
 * A proxy audio device that:
 *   1. Appears as the default output device ("Audio Boost")
 *   2. Captures all system audio into a ring buffer
 *   3. Forwards the audio to the real hardware output via an IOProc
 *
 * Implements all 23 AudioServerPlugInDriverInterface functions.
 *
 * Based on the BlackHole/NullAudio single-file C style.
 * Build: clang -dynamiclib AudioBoostDriver.c RingBuffer.c -framework CoreAudio -framework CoreFoundation
 *
 * Copyright 2024 Kyle Kumar. All rights reserved.
 */

// ---------------------------------------------------------------------------
#pragma mark - Includes
// ---------------------------------------------------------------------------

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/AudioHardwareBase.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <os/log.h>

#include "RingBuffer.h"

// ---------------------------------------------------------------------------
#pragma mark - Constants
// ---------------------------------------------------------------------------

// Plugin identification
#define kPlugIn_BundleID        "com.whyismyaudiolow.driver"
#define kDevice_Name            "Audio Boost"
#define kDevice_Manufacturer    "WhyIsMyAudioLow"
#define kDevice_UID             "AudioBoost_UID"
#define kDevice_ModelUID        "AudioBoost_ModelUID"

// Audio format
#define kDevice_SampleRate              48000.0
#define kDevice_RingBufferFrameSize     16384
#define kNumber_Of_Channels             2
#define kBits_Per_Channel               32
#define kBytes_Per_Frame                (kNumber_Of_Channels * (kBits_Per_Channel / 8))

// Zero timestamp period (how often GetZeroTimeStamp returns a new stamp)
#define kDevice_ZeroTimeStampPeriod     kDevice_RingBufferFrameSize

// Latency / safety
#define kDevice_Latency                 0
#define kDevice_SafetyOffset            0

// Volume range
#define kVolume_MinDB                   (-96.0f)
#define kVolume_MaxDB                   (0.0f)

// Factory UUID: 147EE600-0815-4D6D-898E-A4DDE19228E1
#define kFactory_UUIDString             "147EE600-0815-4D6D-898E-A4DDE19228E1"

// Apple-defined type UUID for AudioServer plugins
// {443ABAB8-E7B3-491A-B985-BEB9187030DB}
#define kAudioServerPlugInTypeUUID_String "443ABAB8-E7B3-491A-B985-BEB9187030DB"

// ---------------------------------------------------------------------------
#pragma mark - Object IDs
// ---------------------------------------------------------------------------

enum {
    kObjectID_PlugIn            = kAudioObjectPlugInObject,  // = 1
    kObjectID_Device            = 2,
    kObjectID_Stream_Output     = 3,
    kObjectID_Volume_Master     = 4,
    kObjectID_Mute_Master       = 5
};

// ---------------------------------------------------------------------------
#pragma mark - Forward Declarations
// ---------------------------------------------------------------------------

// The 23 interface functions
static HRESULT  AudioBoost_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface);
static ULONG    AudioBoost_AddRef(void* inDriver);
static ULONG    AudioBoost_Release(void* inDriver);

static OSStatus AudioBoost_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost);
static OSStatus AudioBoost_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID);
static OSStatus AudioBoost_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID);

static OSStatus AudioBoost_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus AudioBoost_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);

static OSStatus AudioBoost_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static OSStatus AudioBoost_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);

static Boolean  AudioBoost_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus AudioBoost_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus AudioBoost_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus AudioBoost_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus AudioBoost_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

static OSStatus AudioBoost_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus AudioBoost_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);

static OSStatus AudioBoost_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);

static OSStatus AudioBoost_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace);
static OSStatus AudioBoost_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
static OSStatus AudioBoost_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
static OSStatus AudioBoost_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

// Output forwarding helpers
static void     AudioBoost_SetupOutputDevice(void);
static void     AudioBoost_TeardownOutputDevice(void);
static AudioObjectID AudioBoost_FindRealOutputDevice(void);
static OSStatus AudioBoost_OutputIOProc(AudioObjectID inDevice, const AudioTimeStamp* inNow, const AudioBufferList* inInputData, const AudioTimeStamp* inInputTime, AudioBufferList* outOutputData, const AudioTimeStamp* inOutputTime, void* inClientData);

// Utility
static Float32  AudioBoost_VolumeToDB(Float32 scalar);
static Float32  AudioBoost_VolumeToScalar(Float32 dB);

// ---------------------------------------------------------------------------
#pragma mark - Interface Vtable
// ---------------------------------------------------------------------------

static AudioServerPlugInDriverInterface gAudioServerPlugInDriverInterface = {
    NULL,   // _reserved (set in factory)
    AudioBoost_QueryInterface,
    AudioBoost_AddRef,
    AudioBoost_Release,
    AudioBoost_Initialize,
    AudioBoost_CreateDevice,
    AudioBoost_DestroyDevice,
    AudioBoost_AddDeviceClient,
    AudioBoost_RemoveDeviceClient,
    AudioBoost_PerformDeviceConfigurationChange,
    AudioBoost_AbortDeviceConfigurationChange,
    AudioBoost_HasProperty,
    AudioBoost_IsPropertySettable,
    AudioBoost_GetPropertyDataSize,
    AudioBoost_GetPropertyData,
    AudioBoost_SetPropertyData,
    AudioBoost_StartIO,
    AudioBoost_StopIO,
    AudioBoost_GetZeroTimeStamp,
    AudioBoost_WillDoIOOperation,
    AudioBoost_BeginIOOperation,
    AudioBoost_DoIOOperation,
    AudioBoost_EndIOOperation
};

static AudioServerPlugInDriverInterface* gAudioServerPlugInDriverInterfacePtr = &gAudioServerPlugInDriverInterface;
static AudioServerPlugInDriverRef gAudioServerPlugInDriverRef = &gAudioServerPlugInDriverInterfacePtr;

// ---------------------------------------------------------------------------
#pragma mark - Global State
// ---------------------------------------------------------------------------

static os_log_t                     gLog                        = NULL;

// Plugin
static UInt32                       gPlugIn_RefCount            = 0;
static AudioServerPlugInHostRef     gPlugIn_Host                = NULL;
static pthread_mutex_t              gPlugIn_StateMutex          = PTHREAD_MUTEX_INITIALIZER;

// Device IO
static pthread_mutex_t              gDevice_IOMutex             = PTHREAD_MUTEX_INITIALIZER;
static UInt64                       gDevice_IOIsRunning         = 0;    // ref count

// Virtual clock
static Float64                      gDevice_SampleRate          = kDevice_SampleRate;
static Float64                      gDevice_HostTicksPerFrame   = 0.0;
static UInt64                       gDevice_AnchorHostTime      = 0;
static Float64                      gDevice_AnchorSampleTime    = 0.0;
static UInt64                       gDevice_NumberTimeStamps     = 0;

// Controls
static Float32                      gVolume_Master_Value        = 1.0f;
static UInt32                       gMute_Master_Value          = 0;

// Ring buffer for audio proxy
static RingBuffer                   gRingBuffer;
static Boolean                      gRingBufferInitialized      = false;
static SInt64                       gRingBuffer_WriteHead       = 0;

// Output device forwarding
static AudioObjectID                gOutputDevice               = kAudioObjectUnknown;
static AudioDeviceIOProcID          gOutputDeviceIOProcID       = NULL;
static Boolean                      gOutputDeviceRunning        = false;

// ---------------------------------------------------------------------------
#pragma mark - Logging
// ---------------------------------------------------------------------------

// Use a helper to avoid C23 variadic macro warnings when called with no extra args
static inline void DriverLog_impl(os_log_t log, const char* msg) {
    if (log) os_log(log, "%{public}s", msg);
}
#define DriverLog(fmt, ...)     os_log(gLog, fmt __VA_OPT__(,) __VA_ARGS__)
#define DriverLogMsg(msg)       DriverLog_impl(gLog, (msg))

// ---------------------------------------------------------------------------
#pragma mark - Volume Conversion Utilities
// ---------------------------------------------------------------------------

// Convert scalar (0.0-1.0) to decibels (-96 to 0)
static Float32 AudioBoost_VolumeToDB(Float32 scalar)
{
    if (scalar <= 0.0f)
        return kVolume_MinDB;
    if (scalar >= 1.0f)
        return kVolume_MaxDB;
    // Simple log curve
    return kVolume_MinDB + (kVolume_MaxDB - kVolume_MinDB) * scalar;
}

// Convert decibels (-96 to 0) to scalar (0.0-1.0)
static Float32 AudioBoost_VolumeToScalar(Float32 dB)
{
    if (dB <= kVolume_MinDB)
        return 0.0f;
    if (dB >= kVolume_MaxDB)
        return 1.0f;
    return (dB - kVolume_MinDB) / (kVolume_MaxDB - kVolume_MinDB);
}

// ---------------------------------------------------------------------------
#pragma mark - Output Device Forwarding
// ---------------------------------------------------------------------------

/*
 * Find the real hardware output device. We search all audio devices and
 * pick the first one that:
 *   - Is NOT our virtual device (by UID comparison)
 *   - Has output channels
 *   - Preferably is the system's default output device
 */
static AudioObjectID AudioBoost_FindRealOutputDevice(void)
{
    // First try the system default output device
    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    addr.mScope    = kAudioObjectPropertyScopeGlobal;
    addr.mElement  = kAudioObjectPropertyElementMain;

    AudioObjectID defaultDevice = kAudioObjectUnknown;
    UInt32 size = sizeof(AudioObjectID);
    OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &defaultDevice);
    if (err != noErr || defaultDevice == kAudioObjectUnknown) {
        DriverLog("AudioBoost: Could not get default output device: %d", (int)err);
        return kAudioObjectUnknown;
    }

    // Check if the default device is our virtual device by comparing UID
    addr.mSelector = kAudioDevicePropertyDeviceUID;
    addr.mScope    = kAudioObjectPropertyScopeGlobal;
    addr.mElement  = kAudioObjectPropertyElementMain;

    CFStringRef uid = NULL;
    size = sizeof(CFStringRef);
    err = AudioObjectGetPropertyData(defaultDevice, &addr, 0, NULL, &size, &uid);
    if (err == noErr && uid != NULL) {
        CFStringRef ourUID = CFSTR(kDevice_UID);
        if (CFStringCompare(uid, ourUID, 0) != kCFCompareEqualTo) {
            // Default device is not us - use it
            CFRelease(uid);
            return defaultDevice;
        }
        CFRelease(uid);
    }

    // Default is us - enumerate all devices and find a suitable output
    addr.mSelector = kAudioHardwarePropertyDevices;
    addr.mScope    = kAudioObjectPropertyScopeGlobal;
    addr.mElement  = kAudioObjectPropertyElementMain;

    UInt32 dataSize = 0;
    err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &dataSize);
    if (err != noErr || dataSize == 0)
        return kAudioObjectUnknown;

    UInt32 deviceCount = dataSize / sizeof(AudioObjectID);
    AudioObjectID* devices = (AudioObjectID*)malloc(dataSize);
    if (devices == NULL)
        return kAudioObjectUnknown;

    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &dataSize, devices);
    if (err != noErr) {
        free(devices);
        return kAudioObjectUnknown;
    }

    AudioObjectID foundDevice = kAudioObjectUnknown;

    for (UInt32 i = 0; i < deviceCount; i++) {
        AudioObjectID dev = devices[i];

        // Skip our virtual device
        addr.mSelector = kAudioDevicePropertyDeviceUID;
        addr.mScope    = kAudioObjectPropertyScopeGlobal;
        addr.mElement  = kAudioObjectPropertyElementMain;
        CFStringRef devUID = NULL;
        UInt32 uidSize = sizeof(CFStringRef);
        err = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &uidSize, &devUID);
        if (err == noErr && devUID != NULL) {
            Boolean isOurs = (CFStringCompare(devUID, CFSTR(kDevice_UID), 0) == kCFCompareEqualTo);
            CFRelease(devUID);
            if (isOurs)
                continue;
        }

        // Check if device has output channels
        addr.mSelector = kAudioDevicePropertyStreamConfiguration;
        addr.mScope    = kAudioDevicePropertyScopeOutput;
        addr.mElement  = kAudioObjectPropertyElementMain;

        UInt32 bufListSize = 0;
        err = AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &bufListSize);
        if (err != noErr || bufListSize == 0)
            continue;

        AudioBufferList* bufList = (AudioBufferList*)malloc(bufListSize);
        if (bufList == NULL)
            continue;

        err = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &bufListSize, bufList);
        if (err == noErr) {
            UInt32 outputChannels = 0;
            for (UInt32 b = 0; b < bufList->mNumberBuffers; b++) {
                outputChannels += bufList->mBuffers[b].mNumberChannels;
            }
            if (outputChannels >= 2) {
                foundDevice = dev;
                free(bufList);
                break;
            }
        }
        free(bufList);
    }

    free(devices);
    return foundDevice;
}

/*
 * IOProc installed on the real hardware output device.
 * Reads audio from our ring buffer and writes it to the real device's output buffers.
 */
static OSStatus AudioBoost_OutputIOProc(
    AudioObjectID           inDevice,
    const AudioTimeStamp*   inNow,
    const AudioBufferList*  inInputData,
    const AudioTimeStamp*   inInputTime,
    AudioBufferList*        outOutputData,
    const AudioTimeStamp*   inOutputTime,
    void*                   inClientData)
{
    (void)inDevice;
    (void)inNow;
    (void)inInputData;
    (void)inInputTime;
    (void)inOutputTime;
    (void)inClientData;

    if (outOutputData == NULL)
        return noErr;

    // Calculate the frame position to read from
    // Use the output time's sample time if available, otherwise use our write head
    pthread_mutex_lock(&gDevice_IOMutex);
    SInt64 readHead = gRingBuffer_WriteHead;
    Float32 volume  = gVolume_Master_Value;
    UInt32 muted    = gMute_Master_Value;
    pthread_mutex_unlock(&gDevice_IOMutex);

    for (UInt32 buf = 0; buf < outOutputData->mNumberBuffers; buf++) {
        AudioBuffer* outBuf = &outOutputData->mBuffers[buf];
        UInt32 frameCount = outBuf->mDataByteSize / (outBuf->mNumberChannels * sizeof(Float32));

        if (!gRingBufferInitialized || muted) {
            // Silence
            memset(outBuf->mData, 0, outBuf->mDataByteSize);
            continue;
        }

        // Read from ring buffer
        // Position: readHead - frameCount ensures we read the latest data
        SInt64 fetchPos = readHead - (SInt64)frameCount;
        if (fetchPos < 0) fetchPos = 0;

        if (outBuf->mNumberChannels == kNumber_Of_Channels) {
            // Interleaved stereo matches our ring buffer format
            RingBuffer_Fetch(&gRingBuffer, (Float32*)outBuf->mData, frameCount, fetchPos);

            // Apply volume
            if (volume < 1.0f) {
                Float32* samples = (Float32*)outBuf->mData;
                UInt32 sampleCount = frameCount * outBuf->mNumberChannels;
                for (UInt32 s = 0; s < sampleCount; s++) {
                    samples[s] *= volume;
                }
            }
        } else {
            // Channel mismatch - fetch to temp buffer and redistribute
            Float32* tempBuf = (Float32*)calloc(frameCount * kNumber_Of_Channels, sizeof(Float32));
            if (tempBuf != NULL) {
                RingBuffer_Fetch(&gRingBuffer, tempBuf, frameCount, fetchPos);

                Float32* outSamples = (Float32*)outBuf->mData;
                UInt32 outChannels = outBuf->mNumberChannels;
                for (UInt32 f = 0; f < frameCount; f++) {
                    for (UInt32 c = 0; c < outChannels; c++) {
                        UInt32 srcCh = (c < kNumber_Of_Channels) ? c : (kNumber_Of_Channels - 1);
                        outSamples[f * outChannels + c] = tempBuf[f * kNumber_Of_Channels + srcCh] * volume;
                    }
                }
                free(tempBuf);
            } else {
                memset(outBuf->mData, 0, outBuf->mDataByteSize);
            }
        }
    }

    return noErr;
}

/*
 * Set up the real output device for audio forwarding.
 * Called from Initialize via dispatch_after to avoid coreaudiod deadlock.
 */
static void AudioBoost_SetupOutputDevice(void)
{
    if (gOutputDevice != kAudioObjectUnknown)
        return;

    AudioObjectID realDevice = AudioBoost_FindRealOutputDevice();
    if (realDevice == kAudioObjectUnknown) {
        DriverLogMsg("AudioBoost: No suitable real output device found");
        return;
    }

    gOutputDevice = realDevice;
    DriverLog("AudioBoost: Found real output device ID: %u", (unsigned)gOutputDevice);

    // Create an IOProc on the real device
    OSStatus err = AudioDeviceCreateIOProcID(gOutputDevice, AudioBoost_OutputIOProc, NULL, &gOutputDeviceIOProcID);
    if (err != noErr) {
        DriverLog("AudioBoost: Failed to create IOProc on output device: %d", (int)err);
        gOutputDevice = kAudioObjectUnknown;
        gOutputDeviceIOProcID = NULL;
        return;
    }

    DriverLogMsg("AudioBoost: Output device IOProc created successfully");
}

/*
 * Tear down the real output device forwarding.
 */
static void AudioBoost_TeardownOutputDevice(void)
{
    if (gOutputDeviceRunning && gOutputDevice != kAudioObjectUnknown) {
        AudioDeviceStop(gOutputDevice, gOutputDeviceIOProcID);
        gOutputDeviceRunning = false;
    }

    if (gOutputDeviceIOProcID != NULL && gOutputDevice != kAudioObjectUnknown) {
        AudioDeviceDestroyIOProcID(gOutputDevice, gOutputDeviceIOProcID);
        gOutputDeviceIOProcID = NULL;
    }

    gOutputDevice = kAudioObjectUnknown;
}

// ===========================================================================
#pragma mark - Factory Function
// ===========================================================================

/*
 * Entry point called by CoreAudio to instantiate this driver plugin.
 * Listed in Info.plist as the factory function for our type UUID.
 */
void* AudioBoost_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID)
{
    (void)inAllocator;

    // Verify the requested type UUID matches the AudioServerPlugIn type
    CFUUIDRef typeUUID = CFUUIDCreateFromString(NULL, CFSTR(kAudioServerPlugInTypeUUID_String));
    if (typeUUID == NULL)
        return NULL;

    Boolean typeMatch = CFEqual(inRequestedTypeUUID, typeUUID);
    CFRelease(typeUUID);

    if (!typeMatch)
        return NULL;

    // Initialize logging
    gLog = os_log_create(kPlugIn_BundleID, "driver");

    // Return the driver reference (AddRef will be called by the host)
    AudioBoost_AddRef(gAudioServerPlugInDriverRef);
    return gAudioServerPlugInDriverRef;
}

// ===========================================================================
#pragma mark - IUnknown Methods
// ===========================================================================

static HRESULT AudioBoost_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface)
{
    // Validate
    if (outInterface == NULL)
        return E_POINTER;

    *outInterface = NULL;

    // Verify it's our driver
    if (inDriver != gAudioServerPlugInDriverRef)
        return E_NOINTERFACE;

    // Check for IUnknown
    CFUUIDRef unknownUUID = CFUUIDGetConstantUUIDWithBytes(NULL,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

    // The interface UUID for AudioServerPlugInDriverInterface
    CFUUIDRef pluginUUID = CFUUIDGetConstantUUIDWithBytes(NULL,
        0xEE, 0xA5, 0x77, 0x3D, 0xCC, 0x43, 0x49, 0xF1,
        0x8E, 0x00, 0x8F, 0x96, 0x39, 0x93, 0xA3, 0x68);

    CFUUIDRef requestedUUID = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
    if (requestedUUID == NULL)
        return E_NOINTERFACE;

    Boolean isUnknown = CFEqual(requestedUUID, unknownUUID);
    Boolean isPlugin  = CFEqual(requestedUUID, pluginUUID);
    CFRelease(requestedUUID);

    if (isUnknown || isPlugin) {
        AudioBoost_AddRef(inDriver);
        *outInterface = inDriver;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG AudioBoost_AddRef(void* inDriver)
{
    if (inDriver != gAudioServerPlugInDriverRef)
        return 0;

    ULONG result;
    pthread_mutex_lock(&gPlugIn_StateMutex);
    gPlugIn_RefCount++;
    result = gPlugIn_RefCount;
    pthread_mutex_unlock(&gPlugIn_StateMutex);
    return result;
}

static ULONG AudioBoost_Release(void* inDriver)
{
    if (inDriver != gAudioServerPlugInDriverRef)
        return 0;

    ULONG result;
    pthread_mutex_lock(&gPlugIn_StateMutex);
    if (gPlugIn_RefCount > 0)
        gPlugIn_RefCount--;
    result = gPlugIn_RefCount;
    pthread_mutex_unlock(&gPlugIn_StateMutex);

    if (result == 0) {
        AudioBoost_TeardownOutputDevice();
    }

    return result;
}

// ===========================================================================
#pragma mark - Initialize
// ===========================================================================

static OSStatus AudioBoost_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost)
{
    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;

    // Save the host reference - this is how we notify CoreAudio of changes
    gPlugIn_Host = inHost;

    DriverLogMsg("AudioBoost: Initialize called");

    // Calculate host ticks per frame for the virtual clock
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    Float64 hostTicksPerSecond = (Float64)timebase.denom / (Float64)timebase.numer * 1000000000.0;
    gDevice_HostTicksPerFrame = hostTicksPerSecond / gDevice_SampleRate;

    DriverLog("AudioBoost: HostTicksPerFrame = %f", gDevice_HostTicksPerFrame);

    // Defer the real output device setup to avoid deadlocking coreaudiod
    // during plugin initialization. CoreAudio holds a global lock when
    // calling Initialize, and querying devices would try to acquire it again.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       AudioBoost_SetupOutputDevice();
                   });

    return noErr;
}

// ===========================================================================
#pragma mark - Device Lifecycle (Not Supported)
// ===========================================================================

static OSStatus AudioBoost_CreateDevice(
    AudioServerPlugInDriverRef      inDriver,
    CFDictionaryRef                 inDescription,
    const AudioServerPlugInClientInfo* inClientInfo,
    AudioObjectID*                  outDeviceObjectID)
{
    (void)inDriver;
    (void)inDescription;
    (void)inClientInfo;
    (void)outDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus AudioBoost_DestroyDevice(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError;
}

// ===========================================================================
#pragma mark - Client Management (No-op)
// ===========================================================================

static OSStatus AudioBoost_AddDeviceClient(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    const AudioServerPlugInClientInfo* inClientInfo)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientInfo;
    return noErr;
}

static OSStatus AudioBoost_RemoveDeviceClient(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    const AudioServerPlugInClientInfo* inClientInfo)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientInfo;
    return noErr;
}

// ===========================================================================
#pragma mark - Configuration Change (No-op)
// ===========================================================================

static OSStatus AudioBoost_PerformDeviceConfigurationChange(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt64                          inChangeAction,
    void*                           inChangeInfo)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inChangeAction;
    (void)inChangeInfo;
    return noErr;
}

static OSStatus AudioBoost_AbortDeviceConfigurationChange(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt64                          inChangeAction,
    void*                           inChangeInfo)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inChangeAction;
    (void)inChangeInfo;
    return noErr;
}

// ===========================================================================
#pragma mark - HasProperty
// ===========================================================================

static Boolean AudioBoost_HasProperty(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inObjectID,
    pid_t                           inClientProcessID,
    const AudioObjectPropertyAddress* inAddress)
{
    (void)inClientProcessID;

    if (inDriver != gAudioServerPlugInDriverRef)
        return false;

    Boolean result = false;

    switch (inObjectID) {
        // ---- PlugIn ----
        case kObjectID_PlugIn:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyDeviceList:
                case kAudioPlugInPropertyTranslateUIDToDevice:
                case kAudioPlugInPropertyBoxList:
                case kAudioPlugInPropertyTranslateUIDToBox:
                case kAudioPlugInPropertyClockDeviceList:
                case kAudioPlugInPropertyTranslateUIDToClockDevice:
                case kAudioObjectPropertyManufacturer:
                case kAudioPlugInPropertyResourceBundle:
                    result = true;
                    break;
            }
            break;

        // ---- Device ----
        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioObjectPropertyName:
                case kAudioObjectPropertyManufacturer:
                case kAudioDevicePropertyDeviceUID:
                case kAudioDevicePropertyModelUID:
                case kAudioDevicePropertyTransportType:
                case kAudioDevicePropertyRelatedDevices:
                case kAudioDevicePropertyClockDomain:
                case kAudioDevicePropertyDeviceIsAlive:
                case kAudioDevicePropertyDeviceIsRunning:
                case kAudioObjectPropertyControlList:
                case kAudioDevicePropertyNominalSampleRate:
                case kAudioDevicePropertyAvailableNominalSampleRates:
                case kAudioDevicePropertyIsHidden:
                case kAudioDevicePropertyZeroTimeStampPeriod:
                case kAudioDevicePropertyIcon:
                case kAudioDevicePropertyStreams:
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                case kAudioDevicePropertyLatency:
                case kAudioDevicePropertySafetyOffset:
                case kAudioDevicePropertyPreferredChannelsForStereo:
                case kAudioDevicePropertyPreferredChannelLayout:
                    result = true;
                    break;
            }
            break;

        // ---- Stream ----
        case kObjectID_Stream_Output:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioStreamPropertyIsActive:
                case kAudioStreamPropertyDirection:
                case kAudioStreamPropertyTerminalType:
                case kAudioStreamPropertyStartingChannel:
                case kAudioStreamPropertyLatency:
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                case kAudioStreamPropertyAvailableVirtualFormats:
                case kAudioStreamPropertyAvailablePhysicalFormats:
                    result = true;
                    break;
            }
            break;

        // ---- Volume Control ----
        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioLevelControlPropertyScalarValue:
                case kAudioLevelControlPropertyDecibelValue:
                case kAudioLevelControlPropertyDecibelRange:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                    result = true;
                    break;
            }
            break;

        // ---- Mute Control ----
        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioBooleanControlPropertyValue:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                    result = true;
                    break;
            }
            break;
    }

    return result;
}

// ===========================================================================
#pragma mark - IsPropertySettable
// ===========================================================================

static OSStatus AudioBoost_IsPropertySettable(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inObjectID,
    pid_t                           inClientProcessID,
    const AudioObjectPropertyAddress* inAddress,
    Boolean*                        outIsSettable)
{
    (void)inClientProcessID;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (outIsSettable == NULL)
        return kAudioHardwareIllegalOperationError;

    *outIsSettable = false;

    switch (inObjectID) {
        case kObjectID_PlugIn:
            // No settable properties on the plugin
            *outIsSettable = false;
            break;

        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioDevicePropertyNominalSampleRate:
                    *outIsSettable = true;
                    break;
                default:
                    *outIsSettable = false;
                    break;
            }
            break;

        case kObjectID_Stream_Output:
            switch (inAddress->mSelector) {
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                    *outIsSettable = true;
                    break;
                default:
                    *outIsSettable = false;
                    break;
            }
            break;

        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioLevelControlPropertyScalarValue:
                case kAudioLevelControlPropertyDecibelValue:
                    *outIsSettable = true;
                    break;
                default:
                    *outIsSettable = false;
                    break;
            }
            break;

        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioBooleanControlPropertyValue:
                    *outIsSettable = true;
                    break;
                default:
                    *outIsSettable = false;
                    break;
            }
            break;

        default:
            return kAudioHardwareBadObjectError;
    }

    return noErr;
}

// ===========================================================================
#pragma mark - GetPropertyDataSize
// ===========================================================================

static OSStatus AudioBoost_GetPropertyDataSize(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inObjectID,
    pid_t                           inClientProcessID,
    const AudioObjectPropertyAddress* inAddress,
    UInt32                          inQualifierDataSize,
    const void*                     inQualifierData,
    UInt32*                         outDataSize)
{
    (void)inClientProcessID;
    (void)inQualifierDataSize;
    (void)inQualifierData;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (outDataSize == NULL)
        return kAudioHardwareIllegalOperationError;

    *outDataSize = 0;

    switch (inObjectID) {
        // ---- PlugIn ----
        case kObjectID_PlugIn:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = sizeof(AudioObjectID);  // 1 device
                    break;
                case kAudioObjectPropertyManufacturer:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioPlugInPropertyDeviceList:
                    *outDataSize = sizeof(AudioObjectID);  // 1 device
                    break;
                case kAudioPlugInPropertyTranslateUIDToDevice:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioPlugInPropertyBoxList:
                    *outDataSize = 0;  // no boxes
                    break;
                case kAudioPlugInPropertyTranslateUIDToBox:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioPlugInPropertyClockDeviceList:
                    *outDataSize = 0;  // no clock devices
                    break;
                case kAudioPlugInPropertyTranslateUIDToClockDevice:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioPlugInPropertyResourceBundle:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            break;

        // ---- Device ----
        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    // stream + volume + mute = 3
                    *outDataSize = 3 * sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyName:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioObjectPropertyManufacturer:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyDeviceUID:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyModelUID:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyTransportType:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyRelatedDevices:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyClockDomain:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceIsAlive:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceIsRunning:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioObjectPropertyControlList:
                    *outDataSize = 2 * sizeof(AudioObjectID);  // volume + mute
                    break;
                case kAudioDevicePropertyNominalSampleRate:
                    *outDataSize = sizeof(Float64);
                    break;
                case kAudioDevicePropertyAvailableNominalSampleRates:
                    // Support 44100 and 48000
                    *outDataSize = 2 * sizeof(AudioValueRange);
                    break;
                case kAudioDevicePropertyIsHidden:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyZeroTimeStampPeriod:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyIcon:
                    *outDataSize = sizeof(CFURLRef);
                    break;
                case kAudioDevicePropertyStreams:
                    if (inAddress->mScope == kAudioObjectPropertyScopeGlobal ||
                        inAddress->mScope == kAudioDevicePropertyScopeOutput)
                        *outDataSize = sizeof(AudioObjectID);
                    else
                        *outDataSize = 0;
                    break;
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyLatency:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertySafetyOffset:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyPreferredChannelsForStereo:
                    *outDataSize = 2 * sizeof(UInt32);
                    break;
                case kAudioDevicePropertyPreferredChannelLayout:
                    *outDataSize = (UInt32)(offsetof(AudioChannelLayout, mChannelDescriptions) +
                                   kNumber_Of_Channels * sizeof(AudioChannelDescription));
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            break;

        // ---- Stream ----
        case kObjectID_Stream_Output:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 0;
                    break;
                case kAudioStreamPropertyIsActive:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyDirection:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyTerminalType:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyStartingChannel:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyLatency:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                    *outDataSize = sizeof(AudioStreamBasicDescription);
                    break;
                case kAudioStreamPropertyAvailableVirtualFormats:
                case kAudioStreamPropertyAvailablePhysicalFormats:
                    // 2 formats: 44100 and 48000
                    *outDataSize = 2 * sizeof(AudioStreamRangedDescription);
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            break;

        // ---- Volume Control ----
        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 0;
                    break;
                case kAudioLevelControlPropertyScalarValue:
                    *outDataSize = sizeof(Float32);
                    break;
                case kAudioLevelControlPropertyDecibelValue:
                    *outDataSize = sizeof(Float32);
                    break;
                case kAudioLevelControlPropertyDecibelRange:
                    *outDataSize = sizeof(AudioValueRange);
                    break;
                case kAudioControlPropertyScope:
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;
                case kAudioControlPropertyElement:
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            break;

        // ---- Mute Control ----
        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 0;
                    break;
                case kAudioBooleanControlPropertyValue:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioControlPropertyScope:
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;
                case kAudioControlPropertyElement:
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;
                default:
                    return kAudioHardwareUnknownPropertyError;
            }
            break;

        default:
            return kAudioHardwareBadObjectError;
    }

    return noErr;
}

// ===========================================================================
#pragma mark - GetPropertyData
// ===========================================================================

static OSStatus AudioBoost_GetPropertyData(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inObjectID,
    pid_t                           inClientProcessID,
    const AudioObjectPropertyAddress* inAddress,
    UInt32                          inQualifierDataSize,
    const void*                     inQualifierData,
    UInt32                          inDataSize,
    UInt32*                         outDataSize,
    void*                           outData)
{
    (void)inClientProcessID;
    (void)inQualifierDataSize;
    (void)inQualifierData;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (outDataSize == NULL || outData == NULL)
        return kAudioHardwareIllegalOperationError;

    switch (inObjectID) {

    // ===================================================================
    // PlugIn Object
    // ===================================================================
    case kObjectID_PlugIn:
        switch (inAddress->mSelector) {
            case kAudioObjectPropertyBaseClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioObjectClassID;
                break;

            case kAudioObjectPropertyClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioPlugInClassID;
                break;

            case kAudioObjectPropertyOwner:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kAudioObjectUnknown;
                break;

            case kAudioObjectPropertyOwnedObjects:
            case kAudioPlugInPropertyDeviceList:
            {
                UInt32 itemCount = 1;
                UInt32 needed = itemCount * sizeof(AudioObjectID);
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                AudioObjectID* ids = (AudioObjectID*)outData;
                ids[0] = kObjectID_Device;
                break;
            }

            case kAudioObjectPropertyManufacturer:
                if (inDataSize < sizeof(CFStringRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFStringRef);
                *((CFStringRef*)outData) = CFSTR(kDevice_Manufacturer);
                break;

            case kAudioPlugInPropertyTranslateUIDToDevice:
            {
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                AudioObjectID result = kAudioObjectUnknown;
                if (inQualifierDataSize == sizeof(CFStringRef)) {
                    CFStringRef uid = *((CFStringRef*)inQualifierData);
                    if (uid != NULL && CFStringCompare(uid, CFSTR(kDevice_UID), 0) == kCFCompareEqualTo) {
                        result = kObjectID_Device;
                    }
                }
                *((AudioObjectID*)outData) = result;
                break;
            }

            case kAudioPlugInPropertyBoxList:
                *outDataSize = 0;
                break;

            case kAudioPlugInPropertyTranslateUIDToBox:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kAudioObjectUnknown;
                break;

            case kAudioPlugInPropertyClockDeviceList:
                *outDataSize = 0;
                break;

            case kAudioPlugInPropertyTranslateUIDToClockDevice:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kAudioObjectUnknown;
                break;

            case kAudioPlugInPropertyResourceBundle:
                if (inDataSize < sizeof(CFStringRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFStringRef);
                *((CFStringRef*)outData) = CFSTR("");
                break;

            default:
                return kAudioHardwareUnknownPropertyError;
        }
        break;

    // ===================================================================
    // Device Object
    // ===================================================================
    case kObjectID_Device:
        switch (inAddress->mSelector) {
            case kAudioObjectPropertyBaseClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioObjectClassID;
                break;

            case kAudioObjectPropertyClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioDeviceClassID;
                break;

            case kAudioObjectPropertyOwner:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kObjectID_PlugIn;
                break;

            case kAudioObjectPropertyOwnedObjects:
            {
                // Stream, Volume, Mute
                UInt32 needed = 3 * sizeof(AudioObjectID);
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                AudioObjectID* ids = (AudioObjectID*)outData;
                ids[0] = kObjectID_Stream_Output;
                ids[1] = kObjectID_Volume_Master;
                ids[2] = kObjectID_Mute_Master;
                break;
            }

            case kAudioObjectPropertyName:
                if (inDataSize < sizeof(CFStringRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFStringRef);
                *((CFStringRef*)outData) = CFSTR(kDevice_Name);
                break;

            case kAudioObjectPropertyManufacturer:
                if (inDataSize < sizeof(CFStringRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFStringRef);
                *((CFStringRef*)outData) = CFSTR(kDevice_Manufacturer);
                break;

            case kAudioDevicePropertyDeviceUID:
                if (inDataSize < sizeof(CFStringRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFStringRef);
                *((CFStringRef*)outData) = CFSTR(kDevice_UID);
                break;

            case kAudioDevicePropertyModelUID:
                if (inDataSize < sizeof(CFStringRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFStringRef);
                *((CFStringRef*)outData) = CFSTR(kDevice_ModelUID);
                break;

            case kAudioDevicePropertyTransportType:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = kAudioDeviceTransportTypeVirtual;
                break;

            case kAudioDevicePropertyRelatedDevices:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kObjectID_Device;
                break;

            case kAudioDevicePropertyClockDomain:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 0;
                break;

            case kAudioDevicePropertyDeviceIsAlive:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 1;
                break;

            case kAudioDevicePropertyDeviceIsRunning:
            {
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                pthread_mutex_lock(&gDevice_IOMutex);
                *((UInt32*)outData) = (gDevice_IOIsRunning > 0) ? 1 : 0;
                pthread_mutex_unlock(&gDevice_IOMutex);
                break;
            }

            case kAudioObjectPropertyControlList:
            {
                UInt32 needed = 2 * sizeof(AudioObjectID);
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                AudioObjectID* ids = (AudioObjectID*)outData;
                ids[0] = kObjectID_Volume_Master;
                ids[1] = kObjectID_Mute_Master;
                break;
            }

            case kAudioDevicePropertyNominalSampleRate:
            {
                if (inDataSize < sizeof(Float64))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(Float64);
                pthread_mutex_lock(&gPlugIn_StateMutex);
                *((Float64*)outData) = gDevice_SampleRate;
                pthread_mutex_unlock(&gPlugIn_StateMutex);
                break;
            }

            case kAudioDevicePropertyAvailableNominalSampleRates:
            {
                UInt32 needed = 2 * sizeof(AudioValueRange);
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                AudioValueRange* ranges = (AudioValueRange*)outData;
                ranges[0].mMinimum = 44100.0;
                ranges[0].mMaximum = 44100.0;
                ranges[1].mMinimum = 48000.0;
                ranges[1].mMaximum = 48000.0;
                break;
            }

            case kAudioDevicePropertyIsHidden:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 0;  // Not hidden
                break;

            case kAudioDevicePropertyZeroTimeStampPeriod:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = kDevice_ZeroTimeStampPeriod;
                break;

            case kAudioDevicePropertyIcon:
                if (inDataSize < sizeof(CFURLRef))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(CFURLRef);
                *((CFURLRef*)outData) = NULL;
                break;

            case kAudioDevicePropertyStreams:
            {
                if (inAddress->mScope == kAudioObjectPropertyScopeGlobal ||
                    inAddress->mScope == kAudioDevicePropertyScopeOutput) {
                    if (inDataSize < sizeof(AudioObjectID))
                        return kAudioHardwareBadPropertySizeError;
                    *outDataSize = sizeof(AudioObjectID);
                    *((AudioObjectID*)outData) = kObjectID_Stream_Output;
                } else {
                    // No input streams
                    *outDataSize = 0;
                }
                break;
            }

            case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 1;  // Yes, can be default
                break;

            case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 1;  // Yes, can be default system device
                break;

            case kAudioDevicePropertyLatency:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = kDevice_Latency;
                break;

            case kAudioDevicePropertySafetyOffset:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = kDevice_SafetyOffset;
                break;

            case kAudioDevicePropertyPreferredChannelsForStereo:
            {
                UInt32 needed = 2 * sizeof(UInt32);
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                UInt32* channels = (UInt32*)outData;
                channels[0] = 1;  // Left
                channels[1] = 2;  // Right
                break;
            }

            case kAudioDevicePropertyPreferredChannelLayout:
            {
                UInt32 needed = (UInt32)(offsetof(AudioChannelLayout, mChannelDescriptions) +
                                kNumber_Of_Channels * sizeof(AudioChannelDescription));
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                AudioChannelLayout* layout = (AudioChannelLayout*)outData;
                layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
                layout->mChannelBitmap = 0;
                layout->mNumberChannelDescriptions = kNumber_Of_Channels;
                layout->mChannelDescriptions[0].mChannelLabel = kAudioChannelLabel_Left;
                layout->mChannelDescriptions[0].mChannelFlags = 0;
                layout->mChannelDescriptions[0].mCoordinates[0] = 0.0f;
                layout->mChannelDescriptions[0].mCoordinates[1] = 0.0f;
                layout->mChannelDescriptions[0].mCoordinates[2] = 0.0f;
                layout->mChannelDescriptions[1].mChannelLabel = kAudioChannelLabel_Right;
                layout->mChannelDescriptions[1].mChannelFlags = 0;
                layout->mChannelDescriptions[1].mCoordinates[0] = 0.0f;
                layout->mChannelDescriptions[1].mCoordinates[1] = 0.0f;
                layout->mChannelDescriptions[1].mCoordinates[2] = 0.0f;
                break;
            }

            default:
                return kAudioHardwareUnknownPropertyError;
        }
        break;

    // ===================================================================
    // Stream Object
    // ===================================================================
    case kObjectID_Stream_Output:
        switch (inAddress->mSelector) {
            case kAudioObjectPropertyBaseClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioObjectClassID;
                break;

            case kAudioObjectPropertyClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioStreamClassID;
                break;

            case kAudioObjectPropertyOwner:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kObjectID_Device;
                break;

            case kAudioObjectPropertyOwnedObjects:
                *outDataSize = 0;
                break;

            case kAudioStreamPropertyIsActive:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 1;  // Always active
                break;

            case kAudioStreamPropertyDirection:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 1;  // 1 = output
                break;

            case kAudioStreamPropertyTerminalType:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = kAudioStreamTerminalTypeLine;
                break;

            case kAudioStreamPropertyStartingChannel:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 1;
                break;

            case kAudioStreamPropertyLatency:
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                *((UInt32*)outData) = 0;
                break;

            case kAudioStreamPropertyVirtualFormat:
            case kAudioStreamPropertyPhysicalFormat:
            {
                if (inDataSize < sizeof(AudioStreamBasicDescription))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioStreamBasicDescription);
                AudioStreamBasicDescription* desc = (AudioStreamBasicDescription*)outData;
                desc->mSampleRate       = gDevice_SampleRate;
                desc->mFormatID         = kAudioFormatLinearPCM;
                desc->mFormatFlags      = kAudioFormatFlagIsFloat |
                                          kAudioFormatFlagsNativeEndian |
                                          kAudioFormatFlagIsPacked;
                desc->mBytesPerPacket   = kBytes_Per_Frame;
                desc->mFramesPerPacket  = 1;
                desc->mBytesPerFrame    = kBytes_Per_Frame;
                desc->mChannelsPerFrame = kNumber_Of_Channels;
                desc->mBitsPerChannel   = kBits_Per_Channel;
                break;
            }

            case kAudioStreamPropertyAvailableVirtualFormats:
            case kAudioStreamPropertyAvailablePhysicalFormats:
            {
                UInt32 needed = 2 * sizeof(AudioStreamRangedDescription);
                if (inDataSize < needed)
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = needed;
                AudioStreamRangedDescription* descs = (AudioStreamRangedDescription*)outData;

                // 44100 Hz
                descs[0].mFormat.mSampleRate       = 44100.0;
                descs[0].mFormat.mFormatID         = kAudioFormatLinearPCM;
                descs[0].mFormat.mFormatFlags      = kAudioFormatFlagIsFloat |
                                                     kAudioFormatFlagsNativeEndian |
                                                     kAudioFormatFlagIsPacked;
                descs[0].mFormat.mBytesPerPacket   = kBytes_Per_Frame;
                descs[0].mFormat.mFramesPerPacket  = 1;
                descs[0].mFormat.mBytesPerFrame    = kBytes_Per_Frame;
                descs[0].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                descs[0].mFormat.mBitsPerChannel   = kBits_Per_Channel;
                descs[0].mSampleRateRange.mMinimum = 44100.0;
                descs[0].mSampleRateRange.mMaximum = 44100.0;

                // 48000 Hz
                descs[1].mFormat.mSampleRate       = 48000.0;
                descs[1].mFormat.mFormatID         = kAudioFormatLinearPCM;
                descs[1].mFormat.mFormatFlags      = kAudioFormatFlagIsFloat |
                                                     kAudioFormatFlagsNativeEndian |
                                                     kAudioFormatFlagIsPacked;
                descs[1].mFormat.mBytesPerPacket   = kBytes_Per_Frame;
                descs[1].mFormat.mFramesPerPacket  = 1;
                descs[1].mFormat.mBytesPerFrame    = kBytes_Per_Frame;
                descs[1].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                descs[1].mFormat.mBitsPerChannel   = kBits_Per_Channel;
                descs[1].mSampleRateRange.mMinimum = 48000.0;
                descs[1].mSampleRateRange.mMaximum = 48000.0;
                break;
            }

            default:
                return kAudioHardwareUnknownPropertyError;
        }
        break;

    // ===================================================================
    // Volume Control
    // ===================================================================
    case kObjectID_Volume_Master:
        switch (inAddress->mSelector) {
            case kAudioObjectPropertyBaseClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioControlClassID;
                break;

            case kAudioObjectPropertyClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioVolumeControlClassID;
                break;

            case kAudioObjectPropertyOwner:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kObjectID_Device;
                break;

            case kAudioObjectPropertyOwnedObjects:
                *outDataSize = 0;
                break;

            case kAudioLevelControlPropertyScalarValue:
            {
                if (inDataSize < sizeof(Float32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(Float32);
                pthread_mutex_lock(&gPlugIn_StateMutex);
                *((Float32*)outData) = gVolume_Master_Value;
                pthread_mutex_unlock(&gPlugIn_StateMutex);
                break;
            }

            case kAudioLevelControlPropertyDecibelValue:
            {
                if (inDataSize < sizeof(Float32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(Float32);
                pthread_mutex_lock(&gPlugIn_StateMutex);
                *((Float32*)outData) = AudioBoost_VolumeToDB(gVolume_Master_Value);
                pthread_mutex_unlock(&gPlugIn_StateMutex);
                break;
            }

            case kAudioLevelControlPropertyDecibelRange:
            {
                if (inDataSize < sizeof(AudioValueRange))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioValueRange);
                AudioValueRange* range = (AudioValueRange*)outData;
                range->mMinimum = kVolume_MinDB;
                range->mMaximum = kVolume_MaxDB;
                break;
            }

            case kAudioControlPropertyScope:
                if (inDataSize < sizeof(AudioObjectPropertyScope))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectPropertyScope);
                *((AudioObjectPropertyScope*)outData) = kAudioDevicePropertyScopeOutput;
                break;

            case kAudioControlPropertyElement:
                if (inDataSize < sizeof(AudioObjectPropertyElement))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectPropertyElement);
                *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
                break;

            default:
                return kAudioHardwareUnknownPropertyError;
        }
        break;

    // ===================================================================
    // Mute Control
    // ===================================================================
    case kObjectID_Mute_Master:
        switch (inAddress->mSelector) {
            case kAudioObjectPropertyBaseClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioControlClassID;
                break;

            case kAudioObjectPropertyClass:
                if (inDataSize < sizeof(AudioClassID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioClassID);
                *((AudioClassID*)outData) = kAudioMuteControlClassID;
                break;

            case kAudioObjectPropertyOwner:
                if (inDataSize < sizeof(AudioObjectID))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectID);
                *((AudioObjectID*)outData) = kObjectID_Device;
                break;

            case kAudioObjectPropertyOwnedObjects:
                *outDataSize = 0;
                break;

            case kAudioBooleanControlPropertyValue:
            {
                if (inDataSize < sizeof(UInt32))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(UInt32);
                pthread_mutex_lock(&gPlugIn_StateMutex);
                *((UInt32*)outData) = gMute_Master_Value;
                pthread_mutex_unlock(&gPlugIn_StateMutex);
                break;
            }

            case kAudioControlPropertyScope:
                if (inDataSize < sizeof(AudioObjectPropertyScope))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectPropertyScope);
                *((AudioObjectPropertyScope*)outData) = kAudioDevicePropertyScopeOutput;
                break;

            case kAudioControlPropertyElement:
                if (inDataSize < sizeof(AudioObjectPropertyElement))
                    return kAudioHardwareBadPropertySizeError;
                *outDataSize = sizeof(AudioObjectPropertyElement);
                *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
                break;

            default:
                return kAudioHardwareUnknownPropertyError;
        }
        break;

    default:
        return kAudioHardwareBadObjectError;
    }

    return noErr;
}

// ===========================================================================
#pragma mark - SetPropertyData
// ===========================================================================

static OSStatus AudioBoost_SetPropertyData(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inObjectID,
    pid_t                           inClientProcessID,
    const AudioObjectPropertyAddress* inAddress,
    UInt32                          inQualifierDataSize,
    const void*                     inQualifierData,
    UInt32                          inDataSize,
    const void*                     inData)
{
    (void)inClientProcessID;
    (void)inQualifierDataSize;
    (void)inQualifierData;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;

    OSStatus result = noErr;

    switch (inObjectID) {
        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioDevicePropertyNominalSampleRate:
                {
                    if (inDataSize != sizeof(Float64))
                        return kAudioHardwareBadPropertySizeError;

                    Float64 newRate = *((const Float64*)inData);
                    // Validate the rate
                    if (newRate != 44100.0 && newRate != 48000.0)
                        return kAudioDeviceUnsupportedFormatError;

                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    if (gDevice_SampleRate != newRate) {
                        gDevice_SampleRate = newRate;

                        // Recalculate ticks per frame
                        mach_timebase_info_data_t timebase;
                        mach_timebase_info(&timebase);
                        Float64 hostTicksPerSecond = (Float64)timebase.denom / (Float64)timebase.numer * 1000000000.0;
                        gDevice_HostTicksPerFrame = hostTicksPerSecond / gDevice_SampleRate;
                    }
                    pthread_mutex_unlock(&gPlugIn_StateMutex);

                    // Notify clients of the change
                    if (gPlugIn_Host != NULL) {
                        AudioObjectPropertyAddress changedAddr;
                        changedAddr.mSelector = kAudioDevicePropertyNominalSampleRate;
                        changedAddr.mScope    = kAudioObjectPropertyScopeGlobal;
                        changedAddr.mElement  = kAudioObjectPropertyElementMain;
                        gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Device, 1, &changedAddr);
                    }
                    break;
                }
                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            }
            break;

        case kObjectID_Stream_Output:
            switch (inAddress->mSelector) {
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                {
                    if (inDataSize != sizeof(AudioStreamBasicDescription))
                        return kAudioHardwareBadPropertySizeError;

                    const AudioStreamBasicDescription* newFormat = (const AudioStreamBasicDescription*)inData;

                    // Only accept our supported format (Float32 stereo, supported sample rates)
                    if (newFormat->mFormatID != kAudioFormatLinearPCM)
                        return kAudioDeviceUnsupportedFormatError;
                    if (newFormat->mChannelsPerFrame != kNumber_Of_Channels)
                        return kAudioDeviceUnsupportedFormatError;
                    if (newFormat->mSampleRate != 44100.0 && newFormat->mSampleRate != 48000.0)
                        return kAudioDeviceUnsupportedFormatError;

                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    if (gDevice_SampleRate != newFormat->mSampleRate) {
                        gDevice_SampleRate = newFormat->mSampleRate;

                        mach_timebase_info_data_t timebase;
                        mach_timebase_info(&timebase);
                        Float64 hostTicksPerSecond = (Float64)timebase.denom / (Float64)timebase.numer * 1000000000.0;
                        gDevice_HostTicksPerFrame = hostTicksPerSecond / gDevice_SampleRate;
                    }
                    pthread_mutex_unlock(&gPlugIn_StateMutex);

                    // Notify
                    if (gPlugIn_Host != NULL) {
                        AudioObjectPropertyAddress addrs[2];
                        addrs[0].mSelector = kAudioStreamPropertyVirtualFormat;
                        addrs[0].mScope    = kAudioObjectPropertyScopeGlobal;
                        addrs[0].mElement  = kAudioObjectPropertyElementMain;
                        addrs[1].mSelector = kAudioStreamPropertyPhysicalFormat;
                        addrs[1].mScope    = kAudioObjectPropertyScopeGlobal;
                        addrs[1].mElement  = kAudioObjectPropertyElementMain;
                        gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Stream_Output, 2, addrs);
                    }
                    break;
                }
                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            }
            break;

        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioLevelControlPropertyScalarValue:
                {
                    if (inDataSize != sizeof(Float32))
                        return kAudioHardwareBadPropertySizeError;

                    Float32 newValue = *((const Float32*)inData);
                    // Clamp
                    if (newValue < 0.0f) newValue = 0.0f;
                    if (newValue > 1.0f) newValue = 1.0f;

                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    gVolume_Master_Value = newValue;
                    pthread_mutex_unlock(&gPlugIn_StateMutex);

                    // Notify
                    if (gPlugIn_Host != NULL) {
                        AudioObjectPropertyAddress addrs[2];
                        addrs[0].mSelector = kAudioLevelControlPropertyScalarValue;
                        addrs[0].mScope    = kAudioObjectPropertyScopeGlobal;
                        addrs[0].mElement  = kAudioObjectPropertyElementMain;
                        addrs[1].mSelector = kAudioLevelControlPropertyDecibelValue;
                        addrs[1].mScope    = kAudioObjectPropertyScopeGlobal;
                        addrs[1].mElement  = kAudioObjectPropertyElementMain;
                        gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Volume_Master, 2, addrs);
                    }
                    break;
                }

                case kAudioLevelControlPropertyDecibelValue:
                {
                    if (inDataSize != sizeof(Float32))
                        return kAudioHardwareBadPropertySizeError;

                    Float32 newDB = *((const Float32*)inData);
                    Float32 newScalar = AudioBoost_VolumeToScalar(newDB);

                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    gVolume_Master_Value = newScalar;
                    pthread_mutex_unlock(&gPlugIn_StateMutex);

                    // Notify
                    if (gPlugIn_Host != NULL) {
                        AudioObjectPropertyAddress addrs[2];
                        addrs[0].mSelector = kAudioLevelControlPropertyScalarValue;
                        addrs[0].mScope    = kAudioObjectPropertyScopeGlobal;
                        addrs[0].mElement  = kAudioObjectPropertyElementMain;
                        addrs[1].mSelector = kAudioLevelControlPropertyDecibelValue;
                        addrs[1].mScope    = kAudioObjectPropertyScopeGlobal;
                        addrs[1].mElement  = kAudioObjectPropertyElementMain;
                        gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Volume_Master, 2, addrs);
                    }
                    break;
                }

                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            }
            break;

        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioBooleanControlPropertyValue:
                {
                    if (inDataSize != sizeof(UInt32))
                        return kAudioHardwareBadPropertySizeError;

                    UInt32 newValue = *((const UInt32*)inData);

                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    gMute_Master_Value = (newValue != 0) ? 1 : 0;
                    pthread_mutex_unlock(&gPlugIn_StateMutex);

                    // Notify
                    if (gPlugIn_Host != NULL) {
                        AudioObjectPropertyAddress changedAddr;
                        changedAddr.mSelector = kAudioBooleanControlPropertyValue;
                        changedAddr.mScope    = kAudioObjectPropertyScopeGlobal;
                        changedAddr.mElement  = kAudioObjectPropertyElementMain;
                        gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Mute_Master, 1, &changedAddr);
                    }
                    break;
                }

                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            }
            break;

        default:
            result = kAudioHardwareBadObjectError;
            break;
    }

    return result;
}

// ===========================================================================
#pragma mark - IO Operations
// ===========================================================================

static OSStatus AudioBoost_StartIO(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt32                          inClientID)
{
    (void)inClientID;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device)
        return kAudioHardwareBadObjectError;

    DriverLog("AudioBoost: StartIO called (clientID=%u)", (unsigned)inClientID);

    pthread_mutex_lock(&gDevice_IOMutex);

    if (gDevice_IOIsRunning == 0) {
        // First client starting IO - initialize the ring buffer
        if (!gRingBufferInitialized) {
            int err = RingBuffer_Init(&gRingBuffer, kDevice_RingBufferFrameSize, kNumber_Of_Channels);
            if (err != 0) {
                pthread_mutex_unlock(&gDevice_IOMutex);
                DriverLogMsg("AudioBoost: Failed to initialize ring buffer");
                return kAudioHardwareUnspecifiedError;
            }
            gRingBufferInitialized = true;
            DriverLog("AudioBoost: Ring buffer initialized (%u frames)", kDevice_RingBufferFrameSize);
        } else {
            RingBuffer_Reset(&gRingBuffer);
        }

        // Reset timestamp anchors
        gDevice_AnchorHostTime      = mach_absolute_time();
        gDevice_AnchorSampleTime    = 0.0;
        gDevice_NumberTimeStamps     = 0;
        gRingBuffer_WriteHead       = 0;

        // Start the real output device if available
        if (gOutputDevice != kAudioObjectUnknown && gOutputDeviceIOProcID != NULL && !gOutputDeviceRunning) {
            OSStatus startErr = AudioDeviceStart(gOutputDevice, gOutputDeviceIOProcID);
            if (startErr == noErr) {
                gOutputDeviceRunning = true;
                DriverLogMsg("AudioBoost: Started real output device IOProc");
            } else {
                DriverLog("AudioBoost: Failed to start real output device: %d", (int)startErr);
                // Continue anyway - audio just won't be forwarded
            }
        }
    }

    gDevice_IOIsRunning++;
    pthread_mutex_unlock(&gDevice_IOMutex);

    DriverLog("AudioBoost: IO running (refcount=%llu)", gDevice_IOIsRunning);
    return noErr;
}

static OSStatus AudioBoost_StopIO(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt32                          inClientID)
{
    (void)inClientID;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device)
        return kAudioHardwareBadObjectError;

    DriverLog("AudioBoost: StopIO called (clientID=%u)", (unsigned)inClientID);

    pthread_mutex_lock(&gDevice_IOMutex);

    if (gDevice_IOIsRunning > 0)
        gDevice_IOIsRunning--;

    if (gDevice_IOIsRunning == 0) {
        // Last client stopped - stop forwarding
        if (gOutputDeviceRunning && gOutputDevice != kAudioObjectUnknown && gOutputDeviceIOProcID != NULL) {
            AudioDeviceStop(gOutputDevice, gOutputDeviceIOProcID);
            gOutputDeviceRunning = false;
            DriverLogMsg("AudioBoost: Stopped real output device IOProc");
        }

        // Clean up ring buffer
        if (gRingBufferInitialized) {
            RingBuffer_Destroy(&gRingBuffer);
            gRingBufferInitialized = false;
            DriverLogMsg("AudioBoost: Ring buffer destroyed");
        }
    }

    pthread_mutex_unlock(&gDevice_IOMutex);

    DriverLog("AudioBoost: IO stopped (refcount=%llu)", gDevice_IOIsRunning);
    return noErr;
}

// ===========================================================================
#pragma mark - GetZeroTimeStamp
// ===========================================================================

/*
 * Provides the virtual clock for this device.
 * CoreAudio calls this to synchronize IO cycles.
 * We use mach_absolute_time() to generate a monotonic clock
 * that advances at our declared sample rate.
 */
static OSStatus AudioBoost_GetZeroTimeStamp(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt32                          inClientID,
    Float64*                        outSampleTime,
    UInt64*                         outHostTime,
    UInt64*                         outSeed)
{
    (void)inClientID;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device)
        return kAudioHardwareBadObjectError;

    // Calculate current virtual time based on host clock
    UInt64 currentHostTime = mach_absolute_time();
    Float64 hostTicksSinceAnchor = (Float64)(currentHostTime - gDevice_AnchorHostTime);
    Float64 samplesSinceAnchor = hostTicksSinceAnchor / gDevice_HostTicksPerFrame;

    // Quantize to ZeroTimeStampPeriod boundaries
    // This gives us the most recent period boundary
    Float64 totalSamples = gDevice_AnchorSampleTime + samplesSinceAnchor;
    UInt64 periodCount = (UInt64)(totalSamples / (Float64)kDevice_ZeroTimeStampPeriod);

    Float64 zeroSampleTime = (Float64)(periodCount * kDevice_ZeroTimeStampPeriod);
    Float64 sampleOffset = zeroSampleTime - gDevice_AnchorSampleTime;
    UInt64 zeroHostTime = gDevice_AnchorHostTime + (UInt64)(sampleOffset * gDevice_HostTicksPerFrame);

    *outSampleTime = zeroSampleTime;
    *outHostTime   = zeroHostTime;
    *outSeed       = gDevice_NumberTimeStamps;

    return noErr;
}

// ===========================================================================
#pragma mark - WillDoIOOperation
// ===========================================================================

static OSStatus AudioBoost_WillDoIOOperation(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt32                          inClientID,
    UInt32                          inOperationID,
    Boolean*                        outWillDo,
    Boolean*                        outWillDoInPlace)
{
    (void)inClientID;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device)
        return kAudioHardwareBadObjectError;

    *outWillDo        = false;
    *outWillDoInPlace = true;

    switch (inOperationID) {
        case kAudioServerPlugInIOOperationWriteMix:
            // Apps write their mixed audio to our device
            *outWillDo        = true;
            *outWillDoInPlace = true;
            break;

        case kAudioServerPlugInIOOperationReadInput:
            // Allow reading back from the ring buffer (loopback recording)
            *outWillDo        = true;
            *outWillDoInPlace = true;
            break;

        default:
            break;
    }

    return noErr;
}

// ===========================================================================
#pragma mark - BeginIOOperation / EndIOOperation (No-op)
// ===========================================================================

static OSStatus AudioBoost_BeginIOOperation(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt32                          inClientID,
    UInt32                          inOperationID,
    UInt32                          inIOBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientID;
    (void)inOperationID;
    (void)inIOBufferFrameSize;
    (void)inIOCycleInfo;
    return noErr;
}

static OSStatus AudioBoost_EndIOOperation(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    UInt32                          inClientID,
    UInt32                          inOperationID,
    UInt32                          inIOBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientID;
    (void)inOperationID;
    (void)inIOBufferFrameSize;
    (void)inIOCycleInfo;
    return noErr;
}

// ===========================================================================
#pragma mark - DoIOOperation (THE CORE)
// ===========================================================================

/*
 * This is where audio actually flows through our virtual device.
 *
 * WriteMix: CoreAudio sends us the mixed audio from all apps playing
 *           through our device. We write it into the ring buffer.
 *
 * ReadInput: Something wants to record/capture from our device
 *            (loopback). We read from the ring buffer.
 */
static OSStatus AudioBoost_DoIOOperation(
    AudioServerPlugInDriverRef      inDriver,
    AudioObjectID                   inDeviceObjectID,
    AudioObjectID                   inStreamObjectID,
    UInt32                          inClientID,
    UInt32                          inOperationID,
    UInt32                          inIOBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* inIOCycleInfo,
    void*                           ioMainBuffer,
    void*                           ioSecondaryBuffer)
{
    (void)inStreamObjectID;
    (void)inClientID;
    (void)ioSecondaryBuffer;

    if (inDriver != gAudioServerPlugInDriverRef)
        return kAudioHardwareBadObjectError;
    if (inDeviceObjectID != kObjectID_Device)
        return kAudioHardwareBadObjectError;

    switch (inOperationID) {
        case kAudioServerPlugInIOOperationWriteMix:
        {
            // Apps are sending us their mixed output audio.
            // Write it into the ring buffer for forwarding to real hardware.
            if (ioMainBuffer == NULL || inIOBufferFrameSize == 0)
                break;

            if (!gRingBufferInitialized)
                break;

            // Use the IO cycle's output time as our sample position
            SInt64 sampleTime;
            if (inIOCycleInfo->mOutputTime.mFlags & kAudioTimeStampSampleTimeValid) {
                sampleTime = (SInt64)inIOCycleInfo->mOutputTime.mSampleTime;
            } else {
                // Fallback: use our monotonic write head
                sampleTime = gRingBuffer_WriteHead;
            }

            RingBuffer_Store(&gRingBuffer, (const Float32*)ioMainBuffer, inIOBufferFrameSize, sampleTime);

            // Advance write head
            pthread_mutex_lock(&gDevice_IOMutex);
            gRingBuffer_WriteHead = sampleTime + (SInt64)inIOBufferFrameSize;
            pthread_mutex_unlock(&gDevice_IOMutex);
            break;
        }

        case kAudioServerPlugInIOOperationReadInput:
        {
            // Something wants to read (record) from our virtual device.
            // Provide the audio from the ring buffer.
            if (ioMainBuffer == NULL || inIOBufferFrameSize == 0)
                break;

            if (!gRingBufferInitialized) {
                // No data available - output silence
                memset(ioMainBuffer, 0, inIOBufferFrameSize * kNumber_Of_Channels * sizeof(Float32));
                break;
            }

            SInt64 sampleTime;
            if (inIOCycleInfo->mInputTime.mFlags & kAudioTimeStampSampleTimeValid) {
                sampleTime = (SInt64)inIOCycleInfo->mInputTime.mSampleTime;
            } else {
                // Read from write head minus buffer size
                sampleTime = gRingBuffer_WriteHead - (SInt64)inIOBufferFrameSize;
                if (sampleTime < 0) sampleTime = 0;
            }

            RingBuffer_Fetch(&gRingBuffer, (Float32*)ioMainBuffer, inIOBufferFrameSize, sampleTime);
            break;
        }

        default:
            break;
    }

    return noErr;
}
