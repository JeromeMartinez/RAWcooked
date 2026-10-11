/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef FFV1_Frame_HybridH
#define FFV1_Frame_HybridH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
struct ffv1_frame_hybrid_private;
//---------------------------------------------------------------------------

//***************************************************************************
// Class frame, decoded in other threads by FFmpeg decoder (CPU or Vulkan GPU) and optionally our decoder (CPU) in parallel (available if RAWCOOKED_FFMPEG is defined)
//***************************************************************************

// Each frame is sent to the first available decoder (to the decoder of the previous frame if it is not a keyframe),
// frames decoded by FFmpeg are transferred to raw frames in another thread, during the decoding of next frames.

class ThreadPool;
class raw_frame;
class raw_frame_ready;

class ffv1_frame_hybrid
{
public:
    // Constructor/Destructor
    ffv1_frame_hybrid(ThreadPool* Pool, bool UseOurDecoder, bool Vulkan); // UseOurDecoder: our decoder in parallel with FFmpeg decoder, Vulkan: FFmpeg decoder with Vulkan hardware decoding
    ~ffv1_frame_hybrid();

    // External metadata
    void SetWidth(uint32_t width);
    void SetHeight(uint32_t height);

    // Actions
    bool Process(const uint8_t* Buffer, size_t Buffer_Size, size_t Buffer_Padding); // To RawFrame, synchronous
    bool OutOfBand(const uint8_t* Buffer, size_t Buffer_Size);

    // Decoding to Frame in other threads, FrameReady->Ready(Opaque, Error) is called when the frame is decoded (frames may be ready not in order)
    void Decode(const uint8_t* Buffer, size_t Buffer_Size, size_t Buffer_Padding, raw_frame* Frame, void* Opaque);
    size_t Depth(); // Max count of frames between Decode() and the end of their use by the caller
    void End(); // No more frames, decoders are released after the decoding of pending frames (during the output of last frames)

    // Decoded frame (Process()) or configuration of decoded frames (flavor)
    raw_frame*                  RawFrame;
    raw_frame_ready*            FrameReady;
    size_t                      Remaining; // Count of frames after the frame given to Decode(), (size_t)-1 if unknown

private:
    ffv1_frame_hybrid_private*  P;
};

//---------------------------------------------------------------------------
#endif
