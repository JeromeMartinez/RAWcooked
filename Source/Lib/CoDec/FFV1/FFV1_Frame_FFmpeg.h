/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef FFV1_Frame_FFmpegH
#define FFV1_Frame_FFmpegH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
//---------------------------------------------------------------------------

//***************************************************************************
// Class frame, decoded by FFmpeg libavcodec (available if RAWCOOKED_FFMPEG is defined)
//***************************************************************************

class ThreadPool;
class raw_frame;

class ffv1_frame_ffmpeg
{
public:
    // Constructor/Destructor
    ffv1_frame_ffmpeg(ThreadPool* Pool, bool Vulkan); // Vulkan: hardware decoding with Vulkan
    ~ffv1_frame_ffmpeg();

    // External metadata
    void SetWidth(uint32_t width);
    void SetHeight(uint32_t height);

    // Actions
    bool Process(const uint8_t* Buffer, size_t Buffer_Size, size_t Buffer_Padding); // Buffer_Padding: count of bytes readable after Buffer + Buffer_Size
    bool OutOfBand(const uint8_t* Buffer, size_t Buffer_Size);

    // Decoded frame
    raw_frame*                  RawFrame;

private:
    ThreadPool*                 Pool;

    // FFmpeg
    AVCodecContext*             Context;
    AVPacket*                   Packet;
    AVFrame*                    Frame;
    AVFrame*                    Frame_Sw; // Downloaded from GPU memory
};

//---------------------------------------------------------------------------
#endif
