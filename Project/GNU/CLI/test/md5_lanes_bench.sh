#!/bin/sh
# MD5 lanes benchmark (md5_lanes_bench.cpp), from the root of the RAWcooked source tree:
# - old: MD5 lanes of MultiDecoders (MD5_Lanes_old.cpp: lanes from 3 frames, frames wait for 3 frames during 1 s)
# - new8: MD5 lanes of Speedup7 (MD5_Lanes_8.cpp: lanes from a count of frames measured on the CPU, frames wait for all lanes during 3 s), 8 lanes
# - new: MD5 lanes of this branch, 16 lanes on AArch64 (4 NEON vectors of 4 lanes, 2 vectors for up to 8 frames), same as new8 on x86
# With the trace enabled, the count of frames from which lanes are used and the measured CPU time of all lanes compared to 1 frame with scalar code are shown
set -e
CXX=${CXX:-c++}
CC=${CC:-cc}
Dir=$(mktemp -d)

if [ "$(uname)" = "Darwin" ] ; then
    echo "CPU: $(sysctl -n machdep.cpu.brand_string), $(sysctl -n hw.ncpu) logical CPUs"
else
    echo "CPU:$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-), $(nproc) logical CPUs"
fi
${CXX} --version | head -1

${CC} -O2 -ISource/Lib/ThirdParty/md5 -c Source/Lib/ThirdParty/md5/md5.c -o ${Dir}/md5.o
${CXX} -O2 -std=c++11 -ISource -c Source/Lib/Utils/Trace/Trace.cpp -o ${Dir}/trace.o
build() {
    Name=${1}
    Source=${2}
    shift 2
    ${CXX} -O2 -std=c++11 -ISource -ISource/Lib/ThirdParty/md5 "$@" Project/GNU/CLI/test/md5_lanes_bench.cpp ${Source} ${Dir}/trace.o ${Dir}/md5.o -o ${Dir}/${Name} -pthread
}
Variants="old new8 new"
build old Project/GNU/CLI/test/MD5_Lanes_old.cpp
build new8 Project/GNU/CLI/test/MD5_Lanes_8.cpp
build new Source/Lib/Utils/MD5/MD5_Lanes.cpp

for Interval in 0 100 ; do
    for Variant in ${Variants} ; do
        echo "== ${Variant}"
        RAWCOOKED_TRACE=${Dir}/${Variant}.json ${Dir}/${Variant} 48 8 ${Interval} 2>${Dir}/${Variant}.err
        grep -E "^MD5 lanes (from|not)" ${Dir}/${Variant}.err || true
        grep -o '"MD5 lanes cost (%)"[^}]*' ${Dir}/${Variant}.json | grep -o '[0-9]*$' | sed 's/^/CPU time of all lanes compared to 1 frame with scalar code (%): /' || true
    done
done
rm -rf ${Dir}
