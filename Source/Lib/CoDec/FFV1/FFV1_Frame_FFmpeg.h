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
#include "Lib/CoDec/FFV1/FFV1_Slice.h"
#include <vector>
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
//---------------------------------------------------------------------------

//***************************************************************************
// Class frame, decoded by FFmpeg libavcodec
//***************************************************************************

// Same interface as ffv1_frame.
// Parameters and slice headers are still parsed by our code (for the raw frame
// configuration and for the slice positions), the pixel content is decoded by
// libavcodec then written to the raw frame with the same transforms as ours.
// Available only if RAWCOOKED_FFMPEG is defined.

class ThreadPool;
class raw_frame;

class ffv1_frame_ffmpeg
{
public:
    // Constructor/Destructor
    ffv1_frame_ffmpeg(ThreadPool* Pool);
    ~ffv1_frame_ffmpeg();

    // External metadata
    void SetWidth(uint32_t width);
    void SetHeight(uint32_t width);

    // Actions
    bool Process(const uint8_t* Buffer, size_t Buffer_Size);
    bool OutOfBand(const uint8_t* Buffer, size_t Buffer_Size);

    // Decoded frame
    raw_frame*                  RawFrame;

    // Error message
    const char* ErrorMessage() const;

private:
    // Parameters
    parameters                  P;
    rangecoder                  E;

    // Temp
    bool                        KeyFrame_IsPresent;
    slice_struct*               Slices;
    ThreadPool*                 Pool;

    // FFmpeg
    AVCodecContext*             Context;
    AVPacket*                   Packet;
    AVFrame*                    Frame;
    std::vector<uint8_t>        Extradata;

    // Helpers
    void Clear();
    bool Decoder_Open();
    void Decoder_Close();
};

//---------------------------------------------------------------------------
#endif
