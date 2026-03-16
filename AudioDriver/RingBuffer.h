/*
 * RingBuffer.h
 * AudioBoost Virtual Audio Driver
 *
 * Thread-safe circular buffer for Float32 stereo audio data.
 * Used to pass audio between the HAL IO thread (producer) and
 * the real output device IOProc (consumer).
 */

#ifndef RingBuffer_h
#define RingBuffer_h

#include <CoreAudio/CoreAudioTypes.h>
#include <pthread.h>

#define kRingBuffer_FrameCount  65536
#define kRingBuffer_Channels    2

typedef struct {
    Float32*        mBuffer;            // interleaved stereo samples
    UInt32          mFrameCount;        // total frames in buffer (power of 2)
    UInt32          mChannelCount;      // channels per frame
    SInt64          mStartFrame;        // earliest valid frame
    SInt64          mEndFrame;          // one past last valid frame
    pthread_mutex_t mMutex;
} RingBuffer;

/*
 * Initialize the ring buffer. Allocates internal storage.
 * Returns 0 on success, non-zero on failure.
 */
int     RingBuffer_Init(RingBuffer* rb, UInt32 frameCount, UInt32 channelCount);

/*
 * Destroy the ring buffer. Frees internal storage and mutex.
 */
void    RingBuffer_Destroy(RingBuffer* rb);

/*
 * Store interleaved Float32 frames into the ring buffer.
 * sampleTime is the absolute sample-time of the first frame.
 * Overwrites old data if the buffer wraps.
 */
void    RingBuffer_Store(RingBuffer* rb, const Float32* data, UInt32 frameCount, SInt64 sampleTime);

/*
 * Fetch interleaved Float32 frames from the ring buffer.
 * startFrame is the absolute sample-time of the first requested frame.
 * Frames outside the valid range are zero-filled.
 * Returns 0 on success.
 */
int     RingBuffer_Fetch(RingBuffer* rb, Float32* outData, UInt32 frameCount, SInt64 startFrame);

/*
 * Reset the ring buffer to empty state, zeroing all data.
 */
void    RingBuffer_Reset(RingBuffer* rb);

#endif /* RingBuffer_h */
