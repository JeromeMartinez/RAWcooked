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
#include "Lib/Utils/CRC32/ZenCRC32.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
}
//---------------------------------------------------------------------------

//***************************************************************************
// Info
//***************************************************************************

//---------------------------------------------------------------------------
extern const state_transitions_struct default_state_transitions;

static inline uint32_t BigEndian2int24u(const uint8_t* B)
{
    return (((uint32_t)B[0]) << 16) | (((uint32_t)B[1]) << 8) | B[2];
}

static inline bool IsLittleEndian()
{
    const uint16_t Value = 1;
    return *((const uint8_t*)&Value) ? true : false;
}

//***************************************************************************
// Transfer from the FFmpeg decoded picture to the raw frame
//***************************************************************************

//---------------------------------------------------------------------------
struct ffmpeg_transfer
{
    raw_frame*                  RawFrame;
    const parameters*           P;
    const AVFrame*              Frame;
    const AVPixFmtDescriptor*   Desc;
};

//---------------------------------------------------------------------------
// Read w samples of component c (in component coordinates) to Line
static void ReadLine(const AVFrame* Frame, const AVPixFmtDescriptor* Desc, int c, size_t x, size_t y, size_t w, pixel_t* Line)
{
    const auto& Comp = Desc->comp[c];
    const auto Mask = (pixel_t)((1 << Comp.depth) - 1);
    const auto Src = Frame->data[Comp.plane] + y * Frame->linesize[Comp.plane] + x * Comp.step + Comp.offset;

    if (!Comp.shift && !(Desc->flags & AV_PIX_FMT_FLAG_BITSTREAM))
    {
        // Fast path: 8-bit samples or up to 16-bit native endian samples
        if (Comp.depth <= 8)
        {
            for (size_t i = 0; i < w; i++)
                Line[i] = Src[i * Comp.step] & Mask;
            return;
        }
        if (Comp.depth <= 16 && !(Comp.step & 1) && ((Desc->flags & AV_PIX_FMT_FLAG_BE) ? !IsLittleEndian() : IsLittleEndian()))
        {
            const auto Src16 = (const uint16_t*)Src;
            const auto Step16 = Comp.step / 2;
            for (size_t i = 0; i < w; i++)
                Line[i] = Src16[i * Step16] & Mask;
            return;
        }
    }

    // Generic
    av_read_image_line2(Line, (const uint8_t**)Frame->data, Frame->linesize, Desc, (int)x, (int)y, c, (int)w, 0, (int)sizeof(pixel_t));
}

//---------------------------------------------------------------------------
// Same order of lines and same samples as slice::SliceContent_*, so same transforms
static int Slice_Transfer(const ffmpeg_transfer* T, const slice* Slice)
{
    const auto& P = *T->P;
    const auto Frame = T->Frame;
    const auto Desc = T->Desc;
    size_t x = Slice->X();
    size_t y = Slice->Y();
    size_t w = Slice->W();
    size_t h = Slice->H();
    if (!w || !h || x + w > (size_t)Frame->width || y + h > (size_t)Frame->height)
        return 0; // Slice header is buggy (error is already reported)

    // 4 lines with some room after the end of the line, like in slice
    auto Line_Size = w + 3;
    auto Lines = new pixel_t[4 * Line_Size];
    memset(Lines, 0, 4 * Line_Size * sizeof(pixel_t));
    pixel_t* Line[4];
    for (size_t i = 0; i < 4; i++)
        Line[i] = Lines + i * Line_Size;

    switch (P.colorspace_type)
    {
        case 0:
        {
            auto Transform = Transform_Init(T->RawFrame, pix_style::YUVA, P.bits_per_raw_sample, x, y, w, h);

            // Luma
            for (size_t j = 0; j < h; j++)
            {
                ReadLine(Frame, Desc, 0, x, y + j, w, Line[0]);
                Transform->From(Line[0]);
            }

            // Chroma
            if (P.chroma_planes)
            {
                auto c_x = x >> P.log2_h_chroma_subsample;
                auto c_y = y >> P.log2_v_chroma_subsample;
                auto c_w = w >> P.log2_h_chroma_subsample;
                if (w & ((1 << P.log2_h_chroma_subsample) - 1))
                    c_w++; //Is ceil
                auto c_h = h >> P.log2_v_chroma_subsample;
                if (h & ((1 << P.log2_v_chroma_subsample) - 1))
                    c_h++; //Is ceil
                auto Plane_w = (size_t)AV_CEIL_RSHIFT(Frame->width, Desc->log2_chroma_w);
                auto Plane_h = (size_t)AV_CEIL_RSHIFT(Frame->height, Desc->log2_chroma_h);
                if (c_x + c_w > Plane_w)
                    c_w = c_x < Plane_w ? (Plane_w - c_x) : 0;
                if (c_y + c_h > Plane_h)
                    c_h = c_y < Plane_h ? (Plane_h - c_y) : 0;
                for (int c = 1; c <= 2; c++)
                    for (size_t j = 0; j < c_h; j++)
                    {
                        ReadLine(Frame, Desc, c, c_x, c_y + j, c_w, Line[0]);
                        Transform->From(Line[0]);
                    }
            }

            // Alpha
            if (P.alpha_plane)
            {
                auto c = Desc->nb_components - 1;
                for (size_t j = 0; j < h; j++)
                {
                    ReadLine(Frame, Desc, c, x, y + j, w, Line[0]);
                    Transform->From(Line[0]);
                }
            }

            delete Transform;
            break;
        }
        case 1:
        {
            auto Transform = Transform_Init(T->RawFrame, pix_style::RGBA, P.bits_per_raw_sample, x, y, w, h);

            // Transforms expect JPEG2000-RCT content (as coded in the bitstream), FFmpeg provides RGB so we apply the forward RCT
            // Exception in FFV1 specs: with 9-15 bits and no alpha, g and b are inverted in the RCT
            const auto Offset = ((pixel_t)1) << Desc->comp[0].depth;
            const auto GB_Inverted = P.bits_per_raw_sample > 8 && P.bits_per_raw_sample < 16 && !P.alpha_plane;
            auto Y = Line[0];
            auto Cb = Line[1];
            auto Cr = Line[2];
            auto A = P.alpha_plane ? Line[3] : nullptr;
            for (size_t j = 0; j < h; j++)
            {
                ReadLine(Frame, Desc, 0, x, y + j, w, Cr);                     // R
                ReadLine(Frame, Desc, 1, x, y + j, w, GB_Inverted ? Cb : Y);   // G
                ReadLine(Frame, Desc, 2, x, y + j, w, GB_Inverted ? Y : Cb);   // B
                if (A)
                    ReadLine(Frame, Desc, 3, x, y + j, w, A);
                for (size_t i = 0; i < w; i++)
                {
                    pixel_t g = Y[i];
                    pixel_t b = Cb[i] - g;
                    pixel_t r = Cr[i] - g;
                    Y[i] = g + ((b + r) >> 2);
                    Cb[i] = b + Offset;
                    Cr[i] = r + Offset;
                }
                Transform->From(Y, Cb, Cr, A);
            }

            delete Transform;
            break;
        }
        default:;
    }

    delete[] Lines;
    return 0;
}

//---------------------------------------------------------------------------
static int Slice_Header(slice* Slice)
{
    return Slice->Parse_Header() ? 1 : 0;
}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
ffv1_frame_ffmpeg::ffv1_frame_ffmpeg(ThreadPool* Pool_) :
    // Decoded frame
    RawFrame(NULL),
    // Temp
    KeyFrame_IsPresent(false),
    Slices(NULL),
    Pool(Pool_),
    // FFmpeg
    Context(NULL),
    Packet(NULL),
    Frame(NULL)
{
    E.AssignStateTransitions(default_state_transitions);
}

//---------------------------------------------------------------------------
ffv1_frame_ffmpeg::~ffv1_frame_ffmpeg()
{
    Clear();
    Decoder_Close();
}

//***************************************************************************
// External metadata
//***************************************************************************

//---------------------------------------------------------------------------
void ffv1_frame_ffmpeg::SetWidth(uint32_t width)
{
    P.width = width;
}

//---------------------------------------------------------------------------
void ffv1_frame_ffmpeg::SetHeight(uint32_t height)
{
    P.height = height;
}

//***************************************************************************
// Before - Global
//***************************************************************************

//---------------------------------------------------------------------------
bool ffv1_frame_ffmpeg::OutOfBand(const uint8_t* Buffer, size_t Buffer_Size)
{
    if (!Buffer_Size)
        return true;

    P.ConfigurationRecord_IsPresent = true;
    Clear();

    // FFmpeg decoder will be (re)opened with this configuration record
    Decoder_Close();
    Extradata.assign(Buffer, Buffer + Buffer_Size);

    //Coherency tests
    if (Buffer_Size<4)
        return P.Error("FFV1-HEADER-END:1");
    if (ZenCRC32(Buffer, (size_t)Buffer_Size))
        return P.Error("FFV1-HEADER-configuration_record_crc_parity:1");

    E.AssignBuffer(Buffer, Buffer_Size-4);

    if (P.Parse(E, true))
        return true;

    // Slices
    Slices = new slice_struct[P.num_h_slices*P.num_v_slices];
    memset(Slices, 0x00, P.num_h_slices*P.num_v_slices * sizeof(slice_struct));

    return true;
}

//---------------------------------------------------------------------------
bool ffv1_frame_ffmpeg::Process(const uint8_t* Buffer, size_t Buffer_Size)
{
    if (!Slices)
    {
        if (P.ConfigurationRecord_IsPresent)
            return true; //There is a problem

        // No slice in this stream, fake 1 slice
        Slices = new slice_struct[1];
        memset(Slices, 0x00, sizeof(slice_struct));
    }

    E.AssignBuffer(Buffer, Buffer_Size);

    // keyframe
    uint8_t State = states_default;
    bool keyframe = E.b(State);
    if (P.ConfigurationRecord_IsPresent && P.intra && !keyframe)
        return P.Error("FFV1-FRAME-key_frame-ISNOTINTRA:1");
    if (keyframe)
        KeyFrame_IsPresent = true;
    if (!KeyFrame_IsPresent)
        return P.Error("FFV1-FRAME-key_frame-NOINFIRSTFRAME:1");

    size_t Slices_Count = 0;
    if (P.ConfigurationRecord_IsPresent)
    {
        if (P.num_h_slices >= P.width)
            return P.Error("FFV1-HEADER-num_h_slices:1");
        if (P.num_v_slices >= P.height)
            return P.Error("FFV1-HEADER-num_v_slices:1");

        size_t info = 0
        | ((P.bits_per_raw_sample - 1)          <<  0)
        | (P.chroma_planes                      <<  6)
        | (P.alpha_plane                        <<  7)
        | ((1 << P.log2_h_chroma_subsample)     <<  8)
        | ((1 << P.log2_v_chroma_subsample)     << 12)
        | (P.colorspace_type                    << 16)
        | ((P.num_h_slices - 1)                 << 24)
        ;
        RawFrame->Create(P.width, P.height, info);

        uint64_t Slices_BufferPos = Buffer_Size;
        while (Slices_BufferPos)
        {
            if (Slices_BufferPos < P.TailSize)
                return true; //There is a problem
            if (Slices_Count >= P.num_h_slices * P.num_v_slices)
                return P.Error("FFV1-FRAME-slice_count:1");

            size_t Size = BigEndian2int24u(Buffer + (size_t)Slices_BufferPos - P.TailSize);
            Size += P.TailSize;

            if (Size > Slices_BufferPos)
                return true; //There is a problem
            Slices_BufferPos -= Size;

            Slices[Slices_Count].BufferSize = Size;
            slice_struct& Slice_Current = Slices[Slices_Count];
            slice*& Slice_Content = Slice_Current.Content;
            if (!Slice_Content)
                Slice_Content = new slice(&P);
            Slice_Content->Init(Buffer + Slices_BufferPos, Size, keyframe, !Slices_BufferPos, RawFrame);
            Slices_Count++;
        }
    }
    else
    {
        slice_struct& Slice_Current = Slices[0];
        Slice_Current.BufferSize = Buffer_Size;
        slice*& Slice_Content = Slice_Current.Content;
        if (!Slice_Content)
            Slice_Content = new slice(&P);
        Slice_Content->Init(Buffer, Buffer_Size, keyframe, true, RawFrame);
        Slices_Count = 1;
    }

    // Parameters (if not in the configuration record) and slice headers, for the slice positions
    bool HeaderIsBuggy = false;
    if (Pool && Slices_Count > 1)
    {
        std::vector<std::future<int>> Futures;
        for (size_t i = 0; i < Slices_Count; i++)
            Futures.push_back(Pool->submit(Slice_Header, Slices[i].Content));
        for (size_t i = 0; i < Slices_Count; i++)
            if (Futures[i].get())
                HeaderIsBuggy = true;
    }
    else
    {
        for (size_t i = 0; i < Slices_Count; i++)
            if (Slice_Header(Slices[i].Content))
                HeaderIsBuggy = true;
    }
    if (HeaderIsBuggy)
        return P.Error() ? true : P.Error("FFV1-SLICE-header:1");

    // Decoding by FFmpeg
    if (!Context && Decoder_Open())
        return true;
    av_packet_unref(Packet);
    if (av_new_packet(Packet, (int)Buffer_Size) < 0) // Packet must be padded, so we copy it
        return P.Error("FFV1-FFMPEG-av_new_packet:1");
    memcpy(Packet->data, Buffer, Buffer_Size);
    if (avcodec_send_packet(Context, Packet) < 0)
        return P.Error("FFV1-FFMPEG-avcodec_send_packet:1");
    av_frame_unref(Frame);
    if (avcodec_receive_frame(Context, Frame) < 0)
        return P.Error("FFV1-FFMPEG-avcodec_receive_frame:1");
    if ((Frame->flags & AV_FRAME_FLAG_CORRUPT) || Frame->decode_error_flags)
        P.Error("FFV1-FFMPEG-corrupted:1"); // We do not stop here, like our decoder

    // Coherency of FFmpeg output with our parsing
    auto Desc = av_pix_fmt_desc_get((AVPixelFormat)Frame->format);
    if (!Desc
     || (Desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_FLOAT))
     || (uint32_t)Frame->width != P.width
     || (uint32_t)Frame->height != P.height
     || Desc->comp[0].depth != (int)P.bits_per_raw_sample
     || (P.colorspace_type == 0 && ((Desc->flags & AV_PIX_FMT_FLAG_RGB) || Desc->nb_components < 1 + (P.chroma_planes ? 2 : 0) + (P.alpha_plane ? 1 : 0)))
     || (P.colorspace_type == 1 && (!(Desc->flags & AV_PIX_FMT_FLAG_RGB) || Desc->nb_components < 3 + (P.alpha_plane ? 1 : 0)))
     || (P.chroma_planes && (Desc->log2_chroma_w != P.log2_h_chroma_subsample || Desc->log2_chroma_h != P.log2_v_chroma_subsample)))
        return P.Error("FFV1-FFMPEG-pix_fmt:1");

    // Transfer to the raw frame
    ffmpeg_transfer Transfer = { RawFrame, &P, Frame, Desc };
    if (Pool && Slices_Count > 1)
    {
        std::vector<std::future<int>> Futures;
        for (size_t i = 0; i < Slices_Count; i++)
            Futures.push_back(Pool->submit(Slice_Transfer, &Transfer, Slices[i].Content));
        for (size_t i = 0; i < Slices_Count; i++)
            Futures[i].get();
    }
    else
    {
        for (size_t i = 0; i < Slices_Count; i++)
            Slice_Transfer(&Transfer, Slices[i].Content);
    }
    av_frame_unref(Frame);

    RawFrame->Finalize(P.num_h_slices, P.num_v_slices);

    return P.Error() ? true : false;
}

//***************************************************************************
// Errors
//***************************************************************************

//---------------------------------------------------------------------------
const char* ffv1_frame_ffmpeg::ErrorMessage() const
{
    return P.Error();
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
void ffv1_frame_ffmpeg::Clear()
{
    if (Slices)
    {
        size_t Slices_Count = (P.ConfigurationRecord_IsPresent ? (P.num_h_slices*P.num_v_slices) : 1);
        for (size_t i = 0; i < Slices_Count; i++)
            delete Slices[i].Content;
        delete[] Slices;
    }
    Slices = nullptr;
}

//---------------------------------------------------------------------------
bool ffv1_frame_ffmpeg::Decoder_Open()
{
    const AVCodec* Codec = avcodec_find_decoder(AV_CODEC_ID_FFV1);
    if (!Codec)
        return P.Error("FFV1-FFMPEG-avcodec_find_decoder:1");

    Context = avcodec_alloc_context3(Codec);
    Packet = av_packet_alloc();
    Frame = av_frame_alloc();
    if (!Context || !Packet || !Frame)
    {
        Decoder_Close();
        return P.Error("FFV1-FFMPEG-alloc:1");
    }

    // Width and height are needed for streams without configuration record
    Context->width = (int)P.width;
    Context->height = (int)P.height;

    // Configuration record
    if (!Extradata.empty())
    {
        Context->extradata = (uint8_t*)av_mallocz(Extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!Context->extradata)
        {
            Decoder_Close();
            return P.Error("FFV1-FFMPEG-alloc:1");
        }
        memcpy(Context->extradata, Extradata.data(), Extradata.size());
        Context->extradata_size = (int)Extradata.size();
    }

    // CRC are already tested by our slice header parser
    Context->err_recognition = 0;

    // Slice threads only, so a frame is output immediately
    Context->thread_type = FF_THREAD_SLICE;
    Context->thread_count = Pool ? 0 : 1;

    if (avcodec_open2(Context, Codec, nullptr) < 0)
    {
        Decoder_Close();
        return P.Error("FFV1-FFMPEG-avcodec_open2:1");
    }

    return false;
}

//---------------------------------------------------------------------------
void ffv1_frame_ffmpeg::Decoder_Close()
{
    av_frame_free(&Frame);
    av_packet_free(&Packet);
    avcodec_free_context(&Context);
}

#endif //RAWCOOKED_FFMPEG
