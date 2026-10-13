// Benchmark of MD5_Update_Lanes() (SIMD lanes) against MD5_Update() (scalar), frames hashed by several threads as the output threads of rawcooked (1 to 16 threads)
// Build and run with md5_lanes_bench.sh (from the root of the RAWcooked source tree)
// Run: ./md5_lanes_bench [frame size in MiB, default 48] [frames per thread, default 8] [interval between the frames of a thread in ms, default 0]
// With an interval, the threads start at different times and wait between their frames (frames decoded at a given rate), else all frames are given at the same time
#include "Lib/Utils/MD5/MD5_Lanes.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/resource.h>

static double CpuTime() // Process CPU time (all threads), in seconds
{
    rusage Usage;
    getrusage(RUSAGE_SELF, &Usage);
    return Usage.ru_utime.tv_sec + Usage.ru_utime.tv_usec / 1e6 + Usage.ru_stime.tv_sec + Usage.ru_stime.tv_usec / 1e6;
}

static void Worker(const std::vector<uint8_t>* Frame, unsigned Frames, unsigned Start_ms, unsigned Interval_ms, bool Lanes, unsigned char* Result)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(Start_ms));
    MD5_CTX Ctx;
    MD5_Init(&Ctx);
    for (unsigned i = 0; i < Frames; i++)
    {
        if (i && Interval_ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(Interval_ms));
        if (Lanes)
        {
            md5_part Part = { Frame->data(), Frame->size() };
            MD5_Update_Lanes(&Ctx, &Part, 1, true); // As the output threads (frames may wait for other frames)
        }
        else
            MD5_Update(&Ctx, Frame->data(), (unsigned long)Frame->size());
    }
    if (Lanes)
        MD5_Lanes_Flush(true); // No more frames from this thread
    MD5_Final(Result, &Ctx);
}

int main(int argc, char* argv[])
{
    size_t Size = (argc > 1 ? atoi(argv[1]) : 48) * 1024 * 1024;
    unsigned Frames = argc > 2 ? atoi(argv[2]) : 8;
    unsigned Interval = argc > 3 ? atoi(argv[3]) : 0;
    std::vector<uint8_t> Frame(Size);
    for (size_t i = 0; i < Size; i++)
        Frame[i] = (uint8_t)(i * 2654435761u >> 13);

    printf("%zu MiB per frame, %u frames per thread, interval %u ms\n", Size / 1024 / 1024, Frames, Interval);
    printf("threads  scalar: wall   CPU  CPU/frame   lanes: wall   CPU  CPU/frame  (s, ms)\n");
    for (unsigned Threads : { 1, 2, 4, 8, 12, 16 })
    {
        double Wall[2], Cpu[2];
        std::vector<unsigned char> Results[2];
        for (int Lanes = 0; Lanes < 2; Lanes++)
        {
            Results[Lanes].resize(Threads * 16);
            auto Start = std::chrono::steady_clock::now();
            auto Cpu_Start = CpuTime();
            std::vector<std::thread> List;
            for (unsigned t = 0; t < Threads; t++)
                List.emplace_back(Worker, &Frame, Frames, Interval * t / Threads, Interval, Lanes != 0, Results[Lanes].data() + t * 16);
            for (auto& Thread : List)
                Thread.join();
            if (Lanes)
                for (unsigned t = 0; t < Threads; t++)
                    MD5_Lanes_Flush(false);
            Wall[Lanes] = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
            Cpu[Lanes] = CpuTime() - Cpu_Start;
        }
        auto Count = (double)Threads * Frames;
        printf("%7u  %12.2f %5.2f %9.1f  %11.2f %5.2f %9.1f%s\n", Threads, Wall[0], Cpu[0], Cpu[0] * 1000 / Count, Wall[1], Cpu[1], Cpu[1] * 1000 / Count, Results[0] != Results[1] ? "  MISMATCH" : "");
    }
    return 0;
}
