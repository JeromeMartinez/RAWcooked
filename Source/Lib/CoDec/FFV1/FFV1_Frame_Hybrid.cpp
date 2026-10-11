/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "Lib/CoDec/FFV1/FFV1_Frame_Hybrid.h"
#ifdef RAWCOOKED_FFMPEG
#include "Lib/CoDec/FFV1/FFV1_Frame.h"
#include "Lib/CoDec/FFV1/FFV1_Frame_FFmpeg.h"
#include "Lib/CoDec/FFmpeg_Functions.h"
#include "Lib/Utils/RawFrame/RawFrame.h"
#include "ThreadPool.h"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
extern const state_transitions_struct default_state_transitions;

//---------------------------------------------------------------------------
// Lower priority for the threads of our decoder, so the threads feeding FFmpeg decoder and the output threads are not delayed by our decoder
static void LowPriority()
{
    #if defined(_WIN32)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    #elif defined(__linux__)
        setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
    #elif defined(__APPLE__)
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
    #endif
}

//---------------------------------------------------------------------------
enum decoder_index : size_t
{
    Decoder_Ours,
    Decoder_FFmpeg,
    Decoder_Max
};

//---------------------------------------------------------------------------
struct hybrid_job
{
    AVBufferRef*                Packet = nullptr; // Copy with FFmpeg padding, shared with FFmpeg decoder
    size_t                      Packet_Size = 0;
    raw_frame*                  Frame = nullptr; // Decoded frame
    raw_frame_ready*            FrameReady = nullptr;
    void*                       Opaque = nullptr;
    bool                        Error = false;
    bool                        WarmUp = false; // Not output, for the initialization of FFmpeg decoder
    bool                        First = false; // First frame for FFmpeg decoder, decoded by our decoder if FFmpeg decoder fails
};

//---------------------------------------------------------------------------
struct ffv1_frame_hybrid_private
{
    // Thread pools
    ThreadPool*                 Ours_Pool = nullptr; // Our decoder, low priority
    ThreadPool*                 FFmpeg_Pool = nullptr; // CPU side of FFmpeg decoder (transfer to raw frames), else its tasks would wait for the many tasks of our decoder

    // Decoders, each one in its own thread
    ffv1_frame*                 Ours = nullptr;
    ffv1_frame_ffmpeg*          FFmpeg = nullptr; // Created in its thread (long initialization)
    bool                        Vulkan;
    uint32_t                    Width = 0;
    uint32_t                    Height = 0;
    std::vector<uint8_t>        Extradata;
    std::thread                 Threads[Decoder_Max];
    std::thread                 Transfer_Thread; // Frames decoded by FFmpeg to raw frames
    hybrid_job*                 Next[Decoder_Max] = {}; // Job to decode
    bool                        Busy[Decoder_Max] = {}; // Stays busy if the decoder is not used
    bool                        Working[Decoder_Max] = {}; // Decoding a frame
    double                      Duration[Decoder_Max] = {}; // Average duration of the decoding of a frame, in seconds
    bool                        FFmpeg_Disabled = false; // FFmpeg decoder can not decode the stream
    bool                        Ending = false; // No more frames
    size_t                      Last = Decoder_FFmpeg; // Decoder of the previous frame
    bool                        WarmUp_Pending;
    bool                        Intra = false; // All frames are keyframes
    hybrid_job*                 Redo = nullptr; // First frame for FFmpeg decoder, to be decoded by our decoder
    bool                        First_Pending = false; // First frame for FFmpeg decoder is not decoded, our decoder may have to decode it

    // Jobs
    std::deque<hybrid_job*>     Transfer_Jobs; // Decoded by FFmpeg, in decoding order
    std::vector<hybrid_job*>    Jobs_Free; // For reuse

    // Packets, from a pool so memory pages are reused
    AVBufferPool*               Packet_Pool = nullptr;
    size_t                      Packet_Pool_Size = 0;

    // Synchronization
    bool                        Stop = false;
    std::mutex                  Mutex;
    std::condition_variable     Condition;

    // Keyframe detection
    rangecoder                  E;

    ffv1_frame_hybrid_private(ThreadPool* Pool, bool UseOurDecoder, bool Vulkan_) :
        Vulkan(Vulkan_),
        WarmUp_Pending(UseOurDecoder)
    {
        auto Count = std::thread::hardware_concurrency();
        if (!Count)
            Count = 4;
        if (Pool)
        {
            FFmpeg_Pool = new ThreadPool(Count);
            FFmpeg_Pool->init();
        }
        if (UseOurDecoder)
        {
            if (Pool)
            {
                Ours_Pool = new ThreadPool(Count, LowPriority);
                Ours_Pool->init();
            }
            Ours = new ffv1_frame(Ours_Pool);
        }
        else
            Busy[Decoder_Ours] = true;
        E.AssignStateTransitions(default_state_transitions);
    }

    ~ffv1_frame_hybrid_private()
    {
        delete FFmpeg; // Before the packet pool, FFmpeg may keep references to packets
        delete Ours;
        for (auto Pool : { Ours_Pool, FFmpeg_Pool })
            if (Pool)
            {
                Pool->shutdown();
                delete Pool;
            }
        ffmpeg::av_buffer_pool_uninit(&Packet_Pool);
    }

    void Run(size_t i);
    void Transfer();
    hybrid_job* Job_New();
    void Job_Free(hybrid_job* Job);
    AVBufferRef* Packet_New(const uint8_t* Buffer, size_t Buffer_Size);
};

//---------------------------------------------------------------------------
// Decoder thread
void ffv1_frame_hybrid_private::Run(size_t i)
{
    if (i == Decoder_Ours)
        LowPriority();

    std::unique_lock<std::mutex> Lock(Mutex);
    for (;;)
    {
        Condition.wait(Lock, [&] { return Next[i] || (i == Decoder_Ours && Redo) || Stop || (i == Decoder_Ours && Ending && Ours && !First_Pending); });
        auto Job = Next[i];
        if (Job)
            Next[i] = nullptr;
        else if (i == Decoder_Ours && Redo)
        {
            Job = Redo;
            Redo = nullptr;
        }
        if (!Job)
        {
            if (Stop)
                return;

            // No more frames, our decoder is released during the output of last frames
            auto Ours_Old = Ours;
            auto Ours_Pool_Old = Ours_Pool;
            Ours = nullptr;
            Ours_Pool = nullptr;
            Lock.unlock();
            delete Ours_Old;
            if (Ours_Pool_Old)
            {
                Ours_Pool_Old->shutdown();
                delete Ours_Pool_Old;
            }
            Lock.lock();
            continue;
        }
        Working[i] = true;
        Lock.unlock();

        bool Error;
        auto Start = std::chrono::steady_clock::now();
        if (i == Decoder_Ours)
        {
            Ours->RawFrame = Job->Frame;
            Error = Ours->Process(Job->Packet->data, Job->Packet_Size, AV_INPUT_BUFFER_PADDING_SIZE);
            ffmpeg::av_buffer_unref(&Job->Packet);
            Job->FrameReady->Ready(Job->Opaque, Error);
        }
        else
        {
            if (!FFmpeg)
            {
                FFmpeg = new ffv1_frame_ffmpeg(FFmpeg_Pool, Vulkan);
                FFmpeg->SetWidth(Width);
                FFmpeg->SetHeight(Height);
                if (!Extradata.empty())
                    FFmpeg->OutOfBand(Extradata.data(), Extradata.size());
            }
            Error = FFmpeg->Decode(Job->Packet, Job->Packet_Size);
            if (Job->WarmUp || (Job->First && Error))
                FFmpeg->Discard(); // Not output
            if (!Job->First || !Error)
                ffmpeg::av_buffer_unref(&Job->Packet); // FFmpeg has its own reference if needed, else the packet is kept for our decoder
        }
        auto Job_Duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();

        Lock.lock();
        Working[i] = false;
        if (Job->First)
            First_Pending = false;
        if (!Job->WarmUp && !Job->First)
            Duration[i] = Duration[i] ? (Duration[i] * 3 + Job_Duration) / 4 : Job_Duration; // Duration of the first frame includes the initialization
        if (i == Decoder_Ours)
        {
            Job_Free(Job);
            Busy[i] = Redo != nullptr; // Our decoder is not available for a new frame until the first frame for FFmpeg decoder is decoded
        }
        else if (Job->First && Error)
        {
            // FFmpeg decoder can not decode the stream, the frame is decoded by our decoder
            FFmpeg_Disabled = true; // FFmpeg decoder stays busy
            Job->First = false;
            Redo = Job;
            Busy[Decoder_Ours] = true;
        }
        else if (Job->WarmUp)
        {
            Job_Free(Job);
            if (Error)
                FFmpeg_Disabled = true; // FFmpeg decoder stays busy
            else
                Busy[i] = false;
        }
        else
        {
            Job->Error = Error;
            Transfer_Jobs.push_back(Job);
            Busy[i] = false;
        }
        Condition.notify_all();
    }
}

//---------------------------------------------------------------------------
// Transfer thread, frames decoded by FFmpeg to raw frames
void ffv1_frame_hybrid_private::Transfer()
{
    std::unique_lock<std::mutex> Lock(Mutex);
    for (;;)
    {
        Condition.wait(Lock, [&] { return !Transfer_Jobs.empty() || Stop || (Ending && FFmpeg && !Next[Decoder_FFmpeg] && !Working[Decoder_FFmpeg]); });
        if (Transfer_Jobs.empty())
        {
            if (Stop)
                return;

            // No more frames, FFmpeg decoder is released during the output of last frames
            auto FFmpeg_Old = FFmpeg;
            auto FFmpeg_Pool_Old = FFmpeg_Pool;
            FFmpeg = nullptr;
            FFmpeg_Pool = nullptr;
            Lock.unlock();
            delete FFmpeg_Old;
            if (FFmpeg_Pool_Old)
            {
                FFmpeg_Pool_Old->shutdown();
                delete FFmpeg_Pool_Old;
            }
            Lock.lock();
            continue;
        }
        auto Job = Transfer_Jobs.front();
        Transfer_Jobs.pop_front();
        Lock.unlock();

        FFmpeg->RawFrame = Job->Frame;
        auto Error = FFmpeg->Output() || Job->Error;
        Job->FrameReady->Ready(Job->Opaque, Error);

        Lock.lock();
        Job_Free(Job);
    }
}

//---------------------------------------------------------------------------
hybrid_job* ffv1_frame_hybrid_private::Job_New()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Jobs_Free.empty())
        return new hybrid_job;
    auto Job = Jobs_Free.back();
    Jobs_Free.pop_back();
    return Job;
}

//---------------------------------------------------------------------------
// Mutex must be locked
void ffv1_frame_hybrid_private::Job_Free(hybrid_job* Job)
{
    Job->Frame = nullptr;
    Job->FrameReady = nullptr;
    Job->Opaque = nullptr;
    Job->Error = false;
    Job->WarmUp = false;
    Job->First = false;
    Jobs_Free.push_back(Job);
}

//---------------------------------------------------------------------------
// Copy of the buffer with FFmpeg padding, as the buffer may be no more valid after the call to Decode()
AVBufferRef* ffv1_frame_hybrid_private::Packet_New(const uint8_t* Buffer, size_t Buffer_Size)
{
    if (Buffer_Size + AV_INPUT_BUFFER_PADDING_SIZE > Packet_Pool_Size)
    {
        ffmpeg::av_buffer_pool_uninit(&Packet_Pool); // Freed when all its buffers are released
        Packet_Pool_Size = (Buffer_Size + AV_INPUT_BUFFER_PADDING_SIZE) * 5 / 4; // Some room for bigger next frames
        Packet_Pool = ffmpeg::av_buffer_pool_init(Packet_Pool_Size, nullptr);
        if (!Packet_Pool)
        {
            Packet_Pool_Size = 0;
            return nullptr;
        }
    }
    auto Packet = ffmpeg::av_buffer_pool_get(Packet_Pool);
    if (!Packet)
        return nullptr;
    memcpy(Packet->data, Buffer, Buffer_Size);
    memset(Packet->data + Buffer_Size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    return Packet;
}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
ffv1_frame_hybrid::ffv1_frame_hybrid(ThreadPool* Pool, bool UseOurDecoder, bool Vulkan) :
    RawFrame(nullptr),
    FrameReady(nullptr),
    Remaining((size_t)-1),
    P(new ffv1_frame_hybrid_private(Pool, UseOurDecoder, Vulkan))
{
    for (size_t i = 0; i < Decoder_Max; i++)
        if (i != Decoder_Ours || P->Ours)
            P->Threads[i] = std::thread(&ffv1_frame_hybrid_private::Run, P, i);
    P->Transfer_Thread = std::thread(&ffv1_frame_hybrid_private::Transfer, P);
}

//---------------------------------------------------------------------------
ffv1_frame_hybrid::~ffv1_frame_hybrid()
{
    // End of threads, after the end of their current job (the caller waited for the decoded frames)
    {
        std::lock_guard<std::mutex> Lock(P->Mutex);
        P->Stop = true;
    }
    P->Condition.notify_all();
    for (auto& Thread : P->Threads)
        if (Thread.joinable())
            Thread.join();
    P->Transfer_Thread.join();

    for (auto Job : P->Jobs_Free)
        delete Job;
    delete P;
}

//***************************************************************************
// External metadata
//***************************************************************************

//---------------------------------------------------------------------------
void ffv1_frame_hybrid::SetWidth(uint32_t width)
{
    if (P->Ours)
        P->Ours->SetWidth(width);
    P->Width = width;
}

//---------------------------------------------------------------------------
void ffv1_frame_hybrid::SetHeight(uint32_t height)
{
    if (P->Ours)
        P->Ours->SetHeight(height);
    P->Height = height;
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
bool ffv1_frame_hybrid::OutOfBand(const uint8_t* Buffer, size_t Buffer_Size)
{
    auto Result = false;
    if (P->Ours)
    {
        P->Ours->RawFrame = RawFrame;
        Result = P->Ours->OutOfBand(Buffer, Buffer_Size);
        P->Intra = P->Ours->Intra();
    }
    P->Extradata.assign(Buffer, Buffer + Buffer_Size);
    return Result;
}

//---------------------------------------------------------------------------
bool ffv1_frame_hybrid::Process(const uint8_t* Buffer, size_t Buffer_Size, size_t Buffer_Padding)
{
    // Decoding in other threads, waiting for the end
    struct sync_ready : public raw_frame_ready
    {
        std::mutex              Mutex;
        std::condition_variable Condition;
        bool                    Done = false;
        bool                    Error = false;

        void Ready(void*, bool Error_)
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            Error = Error_;
            Done = true;
            Condition.notify_all();
        }
    } Sync;
    auto FrameReady_Save = FrameReady;
    FrameReady = &Sync;
    Decode(Buffer, Buffer_Size, Buffer_Padding, RawFrame, nullptr);
    FrameReady = FrameReady_Save;
    std::unique_lock<std::mutex> Lock(Sync.Mutex);
    Sync.Condition.wait(Lock, [&] { return Sync.Done; });
    return Sync.Error;
}

//---------------------------------------------------------------------------
void ffv1_frame_hybrid::Decode(const uint8_t* Buffer, size_t Buffer_Size, size_t, raw_frame* Frame, void* Opaque)
{
    // A keyframe can be decoded by any decoder, other frames need the decoding states of the previous frame
    auto KeyFrame = true;
    if (P->Ours)
    {
        P->E.AssignBuffer(Buffer, Buffer_Size);
        uint8_t State = states_default;
        KeyFrame = P->E.b(State);
    }

    // Job, with a copy of the buffer
    auto Packet = P->Packet_New(Buffer, Buffer_Size);
    if (!Packet)
    {
        FrameReady->Ready(Opaque, true);
        return;
    }
    auto Job = P->Job_New();
    Job->Packet = Packet;
    Job->Packet_Size = Buffer_Size;
    Job->Frame = Frame;
    Job->FrameReady = FrameReady;
    Job->Opaque = Opaque;

    // First keyframe: decoded by FFmpeg decoder during its initialization (long) while our decoder decodes next frames if all frames are keyframes (else our decoder may have to decode this frame before next frames),
    // else decoded by our decoder and also by FFmpeg decoder for its initialization without delaying the output
    hybrid_job* WarmUp_Job = nullptr;
    auto First = false;
    if (KeyFrame && P->WarmUp_Pending)
    {
        P->WarmUp_Pending = false;
        if (P->Intra)
            First = true;
        else if (auto WarmUp_Packet = ffmpeg::av_buffer_ref(Packet))
        {
            WarmUp_Job = P->Job_New();
            WarmUp_Job->Packet = WarmUp_Packet;
            WarmUp_Job->Packet_Size = Buffer_Size;
            WarmUp_Job->WarmUp = true;
        }
    }

    // At the end of the stream, our decoder is not used if FFmpeg decoder would decode this frame and the next frames before our decoder decodes this frame
    auto Remaining_ = Remaining;
    auto Ours_Allowed = [&]()
    {
        if (Remaining_ == (size_t)-1 || P->FFmpeg_Disabled || !P->Duration[Decoder_Ours] || !P->Duration[Decoder_FFmpeg])
            return true;
        return P->Duration[Decoder_Ours] < (Remaining_ + 2) * P->Duration[Decoder_FFmpeg]; // FFmpeg decoder: end of the current frame, this frame, next frames
    };

    // First available decoder (FFmpeg decoder if both are available), or decoder of the previous frame
    std::unique_lock<std::mutex> Lock(P->Mutex);
    P->Condition.wait(Lock, [&] { return WarmUp_Job ? (!P->Busy[Decoder_FFmpeg] && !P->Busy[Decoder_Ours]) : First ? !P->Busy[Decoder_FFmpeg] : KeyFrame ? (!P->Busy[Decoder_FFmpeg] || (!P->Busy[Decoder_Ours] && Ours_Allowed())) : !P->Busy[P->Last]; });
    auto i = WarmUp_Job ? Decoder_Ours : First ? Decoder_FFmpeg : !KeyFrame ? P->Last : (!P->Busy[Decoder_FFmpeg] ? Decoder_FFmpeg : Decoder_Ours);
    Job->First = First;
    if (First)
        P->First_Pending = true;
    if (WarmUp_Job)
    {
        P->Busy[Decoder_FFmpeg] = true;
        P->Next[Decoder_FFmpeg] = WarmUp_Job;
    }
    P->Busy[i] = true;
    P->Next[i] = Job;
    P->Last = i;
    P->Condition.notify_all();
}

//---------------------------------------------------------------------------
size_t ffv1_frame_hybrid::Depth()
{
    return 8; // Several frames from the faster decoder during the decoding of a frame by the slower decoder
}

//---------------------------------------------------------------------------
void ffv1_frame_hybrid::End()
{
    {
        std::lock_guard<std::mutex> Lock(P->Mutex);
        P->Ending = true;
    }
    P->Condition.notify_all();
}

#endif //RAWCOOKED_FFMPEG
