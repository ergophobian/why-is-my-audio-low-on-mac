/*
 * AudioDriverPlugin.h
 * WhyIsMyAudioLow - Virtual Audio Driver
 *
 * CoreAudio HAL plugin that creates a virtual audio device.
 * This device captures all system audio output, allowing the app to
 * process it (volume boost, EQ, per-app mixing) before routing to
 * the real hardware output.
 *
 * Architecture:
 *   Apps -> Virtual Device (this plugin) -> WhyIsMyAudioLow App -> Physical Output
 *
 * Implements the AudioServerPlugin interface (kAudioServerPlugInDriverInterfaceID).
 */

#ifndef AudioDriverPlugin_h
#define AudioDriverPlugin_h

#include <CoreAudio/AudioServerPlugIn.h>

// Plugin identification
#define kPlugIn_BundleID        "com.kylekumar.WhyIsMyAudioLow.AudioDriver"
#define kPlugIn_Manufacturer    "WhyIsMyAudioLow"

// Virtual device properties
#define kDevice_Name            "Audio Boost"
#define kDevice_Manufacturer    kPlugIn_Manufacturer
#define kDevice_UID             "WhyIsMyAudioLow_VirtualDevice"
#define kDevice_ModelUID        "WhyIsMyAudioLow_Model"

// Audio format constants
#define kDevice_SampleRate      48000.0
#define kDevice_ChannelCount    2
#define kDevice_BitsPerChannel  32
#define kDevice_RingBufferSize  65536

#ifdef __cplusplus
extern "C" {
#endif

/// Entry point called by CoreAudio to instantiate the driver plugin.
/// Returns an AudioServerPlugInDriverRef implementing the HAL plugin interface.
void* WhyIsMyAudioLow_Create(CFAllocatorRef allocator, CFUUIDRef requestedTypeUUID);

#ifdef __cplusplus
}
#endif

#endif /* AudioDriverPlugin_h */
