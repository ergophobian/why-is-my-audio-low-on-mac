/*
 * RingBuffer.c
 * AudioBoost Virtual Audio Driver
 *
 * Thread-safe circular buffer implementation for Float32 stereo audio.
 * Uses pthread_mutex for synchronization between the HAL IO thread
 * and the real output device IOProc thread.
 */

#include "RingBuffer.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
#pragma mark - Init / Destroy
// ---------------------------------------------------------------------------

int RingBuffer_Init(RingBuffer* rb, UInt32 frameCount, UInt32 channelCount)
{
    if (rb == NULL || frameCount == 0 || channelCount == 0)
        return -1;

    // Ensure power of 2 for efficient masking
    UInt32 pow2 = 1;
    while (pow2 < frameCount)
        pow2 <<= 1;

    size_t byteCount = (size_t)pow2 * channelCount * sizeof(Float32);
    rb->mBuffer = (Float32*)calloc(1, byteCount);
    if (rb->mBuffer == NULL)
        return -1;

    rb->mFrameCount   = pow2;
    rb->mChannelCount = channelCount;
    rb->mStartFrame   = 0;
    rb->mEndFrame     = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
    int err = pthread_mutex_init(&rb->mMutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (err != 0) {
        free(rb->mBuffer);
        rb->mBuffer = NULL;
        return -1;
    }

    return 0;
}

void RingBuffer_Destroy(RingBuffer* rb)
{
    if (rb == NULL)
        return;

    pthread_mutex_destroy(&rb->mMutex);

    if (rb->mBuffer != NULL) {
        free(rb->mBuffer);
        rb->mBuffer = NULL;
    }

    rb->mFrameCount   = 0;
    rb->mChannelCount = 0;
    rb->mStartFrame   = 0;
    rb->mEndFrame     = 0;
}

// ---------------------------------------------------------------------------
#pragma mark - Store
// ---------------------------------------------------------------------------

void RingBuffer_Store(RingBuffer* rb, const Float32* data, UInt32 frameCount, SInt64 sampleTime)
{
    if (rb == NULL || rb->mBuffer == NULL || data == NULL || frameCount == 0)
        return;

    pthread_mutex_lock(&rb->mMutex);

    UInt32 mask        = rb->mFrameCount - 1;       // works because mFrameCount is power of 2
    UInt32 channels    = rb->mChannelCount;
    UInt32 framesToCopy = frameCount;

    // If we're asked to store more frames than the buffer holds, skip the early ones
    if (framesToCopy > rb->mFrameCount) {
        UInt32 skip = framesToCopy - rb->mFrameCount;
        data       += skip * channels;
        sampleTime += skip;
        framesToCopy = rb->mFrameCount;
    }

    // Write in up to 2 segments (before wrap and after wrap)
    UInt32 startIdx  = (UInt32)(sampleTime & mask);
    UInt32 firstPart = rb->mFrameCount - startIdx;
    if (firstPart > framesToCopy)
        firstPart = framesToCopy;
    UInt32 secondPart = framesToCopy - firstPart;

    // First segment
    memcpy(rb->mBuffer + (startIdx * channels),
           data,
           firstPart * channels * sizeof(Float32));

    // Second segment (wrap-around)
    if (secondPart > 0) {
        memcpy(rb->mBuffer,
               data + (firstPart * channels),
               secondPart * channels * sizeof(Float32));
    }

    // Update valid range
    SInt64 newEnd = sampleTime + (SInt64)framesToCopy;
    if (newEnd > rb->mEndFrame)
        rb->mEndFrame = newEnd;

    // Ensure start doesn't lag more than buffer size behind end
    if (rb->mEndFrame - rb->mStartFrame > (SInt64)rb->mFrameCount)
        rb->mStartFrame = rb->mEndFrame - (SInt64)rb->mFrameCount;

    pthread_mutex_unlock(&rb->mMutex);
}

// ---------------------------------------------------------------------------
#pragma mark - Fetch
// ---------------------------------------------------------------------------

int RingBuffer_Fetch(RingBuffer* rb, Float32* outData, UInt32 frameCount, SInt64 startFrame)
{
    if (rb == NULL || rb->mBuffer == NULL || outData == NULL || frameCount == 0)
        return -1;

    pthread_mutex_lock(&rb->mMutex);

    UInt32 mask     = rb->mFrameCount - 1;
    UInt32 channels = rb->mChannelCount;

    SInt64 endFrame = startFrame + (SInt64)frameCount;

    // Clamp to valid range, zero-fill out-of-range regions
    SInt64 validStart = rb->mStartFrame;
    SInt64 validEnd   = rb->mEndFrame;

    // If entirely before or after valid data, zero-fill
    if (startFrame >= validEnd || endFrame <= validStart) {
        memset(outData, 0, frameCount * channels * sizeof(Float32));
        pthread_mutex_unlock(&rb->mMutex);
        return 0;
    }

    // Zero-fill leading frames before valid data
    UInt32 outOffset = 0;
    if (startFrame < validStart) {
        UInt32 leadingZeros = (UInt32)(validStart - startFrame);
        memset(outData, 0, leadingZeros * channels * sizeof(Float32));
        outOffset   = leadingZeros;
        startFrame  = validStart;
    }

    // Zero-fill trailing frames after valid data
    UInt32 trailingZeros = 0;
    if (endFrame > validEnd) {
        trailingZeros = (UInt32)(endFrame - validEnd);
        endFrame      = validEnd;
    }

    // Copy the valid portion
    UInt32 validFrames = (UInt32)(endFrame - startFrame);
    UInt32 srcIdx      = (UInt32)(startFrame & mask);
    UInt32 firstPart   = rb->mFrameCount - srcIdx;
    if (firstPart > validFrames)
        firstPart = validFrames;
    UInt32 secondPart = validFrames - firstPart;

    // First segment
    memcpy(outData + (outOffset * channels),
           rb->mBuffer + (srcIdx * channels),
           firstPart * channels * sizeof(Float32));

    // Second segment (wrap-around)
    if (secondPart > 0) {
        memcpy(outData + ((outOffset + firstPart) * channels),
               rb->mBuffer,
               secondPart * channels * sizeof(Float32));
    }

    // Zero-fill trailing
    if (trailingZeros > 0) {
        UInt32 trailOffset = outOffset + validFrames;
        memset(outData + (trailOffset * channels), 0,
               trailingZeros * channels * sizeof(Float32));
    }

    pthread_mutex_unlock(&rb->mMutex);
    return 0;
}

// ---------------------------------------------------------------------------
#pragma mark - Reset
// ---------------------------------------------------------------------------

void RingBuffer_Reset(RingBuffer* rb)
{
    if (rb == NULL || rb->mBuffer == NULL)
        return;

    pthread_mutex_lock(&rb->mMutex);

    memset(rb->mBuffer, 0, rb->mFrameCount * rb->mChannelCount * sizeof(Float32));
    rb->mStartFrame = 0;
    rb->mEndFrame   = 0;

    pthread_mutex_unlock(&rb->mMutex);
}
