/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "Lib/CoDec/FFV1/FFV1_Frame_FFmpeg.h"
#ifdef RAWCOOKED_FFMPEG
#include "ThreadPool.h"
#include "Lib/Transform/Transform.h"
#include "Lib/Utils/RawFrame/RawFrame.h"
#include <cstring>
#include <vector>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}
//---------------------------------------------------------------------------

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
static AVPixelFormat Vulkan_Format(AVCodecContext*, const AVPixelFormat* Formats)
{
    // Vulkan only, so decoding fails if Vulkan decoding is not possible
    while (*Formats != AV_PIX_FMT_NONE && *Formats != AV_PIX_FMT_VULKAN)
        Formats++;
    return *Formats;
}

//---------------------------------------------------------------------------
ffv1_frame_ffmpeg::ffv1_frame_ffmpeg(ThreadPool* Pool_, bool Vulkan) :
    RawFrame(nullptr),
    Pool(Pool_),
    Context(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_FFV1))),
    Packet(av_packet_alloc()),
    Frame(av_frame_alloc()),
    Frame_Sw(av_frame_alloc())
{
    Context->thread_type = FF_THREAD_SLICE; // Frame is output immediately
    Context->thread_count = Pool ? 0 : 1;
    if (Vulkan)
    {
        av_hwdevice_ctx_create(&Context->hw_device_ctx, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0);
        Context->get_format = Vulkan_Format;
    }
}

//---------------------------------------------------------------------------
ffv1_frame_ffmpeg::~ffv1_frame_ffmpeg()
{
    av_frame_free(&Frame_Sw);
    av_frame_free(&Frame);
    av_packet_free(&Packet);
    avcodec_free_context(&Context);
}

//***************************************************************************
// External metadata
//***************************************************************************

//---------------------------------------------------------------------------
void ffv1_frame_ffmpeg::SetWidth(uint32_t width)
{
    Context->width = (int)width;
}

//---------------------------------------------------------------------------
void ffv1_frame_ffmpeg::SetHeight(uint32_t height)
{
    Context->height = (int)height;
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
bool ffv1_frame_ffmpeg::OutOfBand(const uint8_t* Buffer, size_t Buffer_Size)
{
    av_freep(&Context->extradata);
    Context->extradata = (uint8_t*)av_mallocz(Buffer_Size + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(Context->extradata, Buffer, Buffer_Size);
    Context->extradata_size = (int)Buffer_Size;
    return false;
}

//---------------------------------------------------------------------------
static void Buffer_Free(void*, uint8_t*)
{
    // Buffer is owned by the caller
}

//---------------------------------------------------------------------------
bool ffv1_frame_ffmpeg::Process(const uint8_t* Buffer, size_t Buffer_Size, size_t Buffer_Padding)
{
    // Decode, without copy of the buffer if FFmpeg padding is available (else e.g. at the end of the file, copy to a padded buffer)
    // Vulkan imports the buffer in GPU memory space and keeps it after the decoding, so it is copied in that case
    if (!avcodec_is_open(Context) && avcodec_open2(Context, nullptr, nullptr) < 0)
        return true;
    if (Buffer_Padding >= AV_INPUT_BUFFER_PADDING_SIZE && !Context->hw_device_ctx)
    {
        Packet->buf = av_buffer_create(const_cast<uint8_t*>(Buffer), Buffer_Size + AV_INPUT_BUFFER_PADDING_SIZE, Buffer_Free, nullptr, AV_BUFFER_FLAG_READONLY);
        Packet->data = const_cast<uint8_t*>(Buffer);
        Packet->size = (int)Buffer_Size;
    }
    else if (av_new_packet(Packet, (int)Buffer_Size) < 0)
        return true;
    else
        memcpy(Packet->data, Buffer, Buffer_Size);
    auto Result = avcodec_send_packet(Context, Packet); // Decoding is synchronous (slice threads only), FFmpeg does not keep a reference to the buffer after it
    av_packet_unref(Packet);
    if (Result < 0 || avcodec_receive_frame(Context, Frame) < 0)
        return true;

    // Download from GPU memory if hardware decoding, then release the GPU frame (including its possible reference to the buffer)
    auto Picture = Frame;
    if (Frame->hw_frames_ctx)
    {
        Result = av_hwframe_transfer_data(Frame_Sw, Frame, 0);
        av_frame_unref(Frame);
        if (Result < 0)
            return true;
        Picture = Frame_Sw;
    }

    // Raw frame configuration, from the decoded pixel format
    auto Desc = av_pix_fmt_desc_get((AVPixelFormat)Picture->format);
    size_t Bits = Desc->comp[0].depth;
    size_t IsRGB = (Desc->flags & AV_PIX_FMT_FLAG_RGB) ? 1 : 0;
    size_t HasAlpha = (Desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? 1 : 0;
    size_t w = Picture->width;
    size_t h = Picture->height;
    RawFrame->Create(w, h, 0
        | ((Bits - 1)                           <<  0)
        | ((Desc->nb_components > 2 ? 1 : 0)    <<  6)
        | (HasAlpha                             <<  7)
        | ((1 << Desc->log2_chroma_w)           <<  8)
        | ((1 << Desc->log2_chroma_h)           << 12)
        | (IsRGB                                << 16)
        ); // Transfered as 1 horizontal slice

    // Transfer of a band of lines, with the same transforms as our decoder
    auto Transfer = [&](size_t y_Begin, size_t y_End) -> int
    {
        // Lines with some room after the end of the line, like in our decoder
        std::vector<pixel_t> Lines(4 * (w + 3));
        pixel_t* Line[4];
        for (size_t i = 0; i < 4; i++)
            Line[i] = Lines.data() + i * (w + 3);
        auto Read = [&](int c, size_t y, size_t Width, pixel_t* Dest)
        {
            av_read_image_line2(Dest, (const uint8_t**)Picture->data, Picture->linesize, Desc, 0, (int)y, c, (int)Width, 0, (int)sizeof(pixel_t));
        };
        auto Transform = Transform_Init(RawFrame, IsRGB ? pix_style::RGBA : pix_style::YUVA, Bits, 0, y_Begin, w, y_End - y_Begin);
        if (IsRGB)
        {
            // Transforms expect content as coded (JPEG2000-RCT, with g and b inverted if 9-15 bits without alpha)
            auto Offset = ((pixel_t)1) << Bits;
            auto Inverted = Bits > 8 && Bits < 16 && !HasAlpha;
            for (size_t y = y_Begin; y < y_End; y++)
            {
                Read(0, y, w, Line[2]);
                Read(1, y, w, Line[Inverted ? 1 : 0]);
                Read(2, y, w, Line[Inverted ? 0 : 1]);
                if (HasAlpha)
                    Read(3, y, w, Line[3]);
                for (size_t x = 0; x < w; x++)
                {
                    pixel_t g = Line[0][x];
                    pixel_t b = Line[1][x] - g;
                    pixel_t r = Line[2][x] - g;
                    Line[0][x] = g + ((b + r) >> 2);
                    Line[1][x] = b + Offset;
                    Line[2][x] = r + Offset;
                }
                Transform->From(Line[0], Line[1], Line[2], Line[3]);
            }
        }
        else
        {
            // Plane then line: Y, Cb, Cr, A
            for (int c = 0; c < Desc->nb_components; c++)
            {
                auto IsChroma = c == 1 || c == 2; // No chroma subsampling if luma + alpha only
                auto Shift_h = IsChroma ? Desc->log2_chroma_h : 0;
                auto Plane_w = (size_t)AV_CEIL_RSHIFT((int)w, IsChroma ? Desc->log2_chroma_w : 0);
                auto Plane_y_End = (size_t)AV_CEIL_RSHIFT((int)y_End, Shift_h);
                for (size_t y = y_Begin >> Shift_h; y < Plane_y_End; y++)
                {
                    Read(c, y, Plane_w, Line[0]);
                    Transform->From(Line[0]);
                }
            }
        }
        delete Transform;
        return 0;
    };

    // Bands of lines in parallel, as many as FFmpeg threads, chroma lines not split between bands
    size_t Bands = Pool ? Context->thread_count : 1;
    size_t Band_Align = (size_t)1 << Desc->log2_chroma_h;
    size_t Band_h = ((h + Bands - 1) / Bands + Band_Align - 1) / Band_Align * Band_Align;
    std::vector<std::future<int>> Futures;
    for (size_t y = 0; y < h; y += Band_h)
    {
        auto y_End = y + Band_h < h ? y + Band_h : h;
        if (Pool)
            Futures.push_back(Pool->submit(Transfer, y, y_End));
        else
            Transfer(y, y_End);
    }
    for (auto& Future : Futures)
        Future.get();
    RawFrame->Finalize(1, 1);

    return false;
}

#endif //RAWCOOKED_FFMPEG
