QT += core gui widgets svg

# On Windows the command line program is rawcooked.exe and both programs are in the same directory, with the FFmpeg they share
win32 {
    TARGET = RAWcooked-GUI
} else:macx {
    TARGET = RAWcooked
} else {
    TARGET = rawcooked-gui
}

win32 {
    RC_FILE = $$PWD/rawcooked-gui.rc
    contains(QT_ARCH, i386): DESTDIR = Win32
    contains(QT_ARCH, x86_64): DESTDIR = x64
    contains(QT_ARCH, arm64): DESTDIR = ARM64
}

macx {
    QMAKE_INFO_PLIST = $$PWD/../Mac/Info.plist
    ICON = $$PWD/../../Source/Resource/Image/Icon.icns
}

unix {
    isEmpty(BINDIR): BINDIR = /usr/bin
    target.path = $$BINDIR
    INSTALLS += target
}

CONFIG += qt release c++17
CONFIG += no_keywords

# A rule for each source: with the rules for several sources, nmake and jom do not find the sources whose path has a space
msvc: CONFIG += no_batch

# The library is built with the same settings as for the command line program (Project/GNU/CLI, Project/MSVC2026)
DEFINES += FLAC__NO_DLL FLAC__HAS_OGG=0 FLAC__NO_ASM

# FFmpeg libraries, used by some decoders: not with 32-bit and ARM Windows, as for the command line program
!win32|contains(QT_ARCH, x86_64): DEFINES += RAWCOOKED_FFMPEG

win32 {
    DEFINES *= _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
    LIBS += -ladvapi32 -lshell32
}

unix {
    # What configure finds on these platforms
    DEFINES += HAVE_STDINT_H HAVE_INTTYPES_H HAVE_UNISTD_H HAVE_SYS_PARAM_H HAVE_FSEEKO HAVE_LROUND
    linux: DEFINES += HAVE_BYTESWAP_H
}

# macOS needs nothing more for the threads and for the FFmpeg libraries
unix:!macx {
    QMAKE_CXXFLAGS += -pthread
    QMAKE_LFLAGS += -pthread

    # FFmpeg libraries are loaded at run time, so they stay optional (always the case on Windows and macOS)
    DEFINES += RAWCOOKED_FFMPEG_DYNAMIC
    LIBS += $$QMAKE_LIBS_DYNLOAD
}

# Intel JCC erratum (Skylake and derived CPUs): jumps are aligned, else the decoding is slower
win32-msvc*:contains(QT_ARCH, x86_64) {
    QMAKE_CFLAGS_RELEASE += -QIntel-jcc-erratum
    QMAKE_CXXFLAGS_RELEASE += -QIntel-jcc-erratum
}
unix:!macx {
    # Only if the assembler supports it (x86). Not on macOS: it would break the ARM part of an universal build
    JCC_ERRATUM = -Wa,-mbranches-within-32B-boundaries
    system("$$QMAKE_CXX $$JCC_ERRATUM -x c++ -c -o /dev/null /dev/null 2>/dev/null") {
        QMAKE_CFLAGS += $$JCC_ERRATUM
        QMAKE_CXXFLAGS += $$JCC_ERRATUM
    }
}

# Headers of Qt 5 have a warning with the recent versions of this compiler
msvc:lessThan(QT_MAJOR_VERSION, 6): QMAKE_CXXFLAGS += -wd5240

INCLUDEPATH += \
    $$PWD/../../Source \
    $$PWD/../../Source/Lib/ThirdParty/flac/include \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include \
    $$PWD/../../Source/Lib/ThirdParty/md5 \
    $$PWD/../../Source/Lib/ThirdParty/thread-pool/include \
    $$PWD/../../Source/Lib/ThirdParty/zlib

# The user interface, then the library
HEADERS += \
    $$PWD/../../Source/GUI/Qt/GUI_About.h \
    $$PWD/../../Source/GUI/Qt/GUI_Help.h \
    $$PWD/../../Source/GUI/Qt/GUI_Job.h \
    $$PWD/../../Source/GUI/Qt/GUI_License.h \
    $$PWD/../../Source/GUI/Qt/GUI_Main.h \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Files.h \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Metadata.h \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Output_Log.h \
    $$PWD/../../Source/GUI/Qt/GUI_Options.h \
    $$PWD/../../Source/GUI/Qt/GUI_Package.h \
    $$PWD/../../Source/GUI/Qt/GUI_Preferences.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/Coder/FFV1_Coder.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/Coder/FFV1_Coder_GolombRice.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/Coder/FFV1_Coder_RangeCoder.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Frame.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Frame_FFmpeg.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Frame_Hybrid.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Parameters.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_RangeCoder.h \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Slice.h \
    $$PWD/../../Source/Lib/CoDec/FFmpeg_Functions.h \
    $$PWD/../../Source/Lib/CoDec/FFmpeg_Library.h \
    $$PWD/../../Source/Lib/CoDec/Wrapper.h \
    $$PWD/../../Source/Lib/Common/Common.h \
    $$PWD/../../Source/Lib/Compressed/Matroska/Matroska.h \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/IntermediateWrite.h \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/RAWcooked.h \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/Reversibility.h \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/Track.h \
    $$PWD/../../Source/Lib/Core/Config.h \
    $$PWD/../../Source/Lib/Core/Core.h \
    $$PWD/../../Source/Lib/Core/Global.h \
    $$PWD/../../Source/Lib/Core/Help.h \
    $$PWD/../../Source/Lib/Core/Input.h \
    $$PWD/../../Source/Lib/Core/Metadata.h \
    $$PWD/../../Source/Lib/Core/Output.h \
    $$PWD/../../Source/Lib/License/License.h \
    $$PWD/../../Source/Lib/License/License_Internal.h \
    $$PWD/../../Source/Lib/ThirdParty/endianness.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/FLAC/assert.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/FLAC/export.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/FLAC/format.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/FLAC/ordinals.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/FLAC/stream_decoder.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/share/alloc.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/share/compat.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/include/share/endswap.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/bitmath.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/bitreader.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/cpu.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/crc.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/fixed.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/float.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/format.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/lpc.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/macros.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/md5.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/private/memory.h \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/include/protected/stream_decoder.h \
    $$PWD/../../Source/Lib/ThirdParty/md5/md5.h \
    $$PWD/../../Source/Lib/ThirdParty/thread-pool/include/SafeQueue.h \
    $$PWD/../../Source/Lib/ThirdParty/thread-pool/include/ThreadPool.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/crc32.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/deflate.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/gzguts.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inffast.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inffixed.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inflate.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inftrees.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/trees.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/zconf.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/zlib.h \
    $$PWD/../../Source/Lib/ThirdParty/zlib/zutil.h \
    $$PWD/../../Source/Lib/Transform/Transform.h \
    $$PWD/../../Source/Lib/Uncompressed/AIFF/AIFF.h \
    $$PWD/../../Source/Lib/Uncompressed/AVI/AVI.h \
    $$PWD/../../Source/Lib/Uncompressed/DPX/DPX.h \
    $$PWD/../../Source/Lib/Uncompressed/EXR/EXR.h \
    $$PWD/../../Source/Lib/Uncompressed/HashSum/HashSum.h \
    $$PWD/../../Source/Lib/Uncompressed/TIFF/TIFF.h \
    $$PWD/../../Source/Lib/Uncompressed/WAV/WAV.h \
    $$PWD/../../Source/Lib/Utils/BitStream/BitStream.h \
    $$PWD/../../Source/Lib/Utils/Buffer/Buffer.h \
    $$PWD/../../Source/Lib/Utils/CRC32/ZenCRC32.h \
    $$PWD/../../Source/Lib/Utils/Errors/Errors.h \
    $$PWD/../../Source/Lib/Utils/FileIO/FileChecker.h \
    $$PWD/../../Source/Lib/Utils/FileIO/FileIO.h \
    $$PWD/../../Source/Lib/Utils/FileIO/FileWriter.h \
    $$PWD/../../Source/Lib/Utils/FileIO/Input_Base.h \
    $$PWD/../../Source/Lib/Utils/MD5/MD5_Lanes.h \
    $$PWD/../../Source/Lib/Utils/RawFrame/RawFrame.h \
    $$PWD/../../Source/Lib/Utils/Trace/Trace.h

SOURCES += \
    $$PWD/../../Source/GUI/Qt/App.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_About.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Help.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Job.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_License.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Main.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Files.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Menu.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Metadata.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Main_Output_Log.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Options.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Package.cpp \
    $$PWD/../../Source/GUI/Qt/GUI_Preferences.cpp

# The library: same sources as in Project/GNU/CLI/Makefile.am, they are not compiled as the user interface (see below)
SOURCES_LIB = \
    $$PWD/../../Source/Lib/CoDec/FFV1/Coder/FFV1_Coder_GolombRice.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/Coder/FFV1_Coder_RangeCoder.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Frame.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Frame_FFmpeg.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Frame_Hybrid.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Parameters.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_RangeCoder.cpp \
    $$PWD/../../Source/Lib/CoDec/FFV1/FFV1_Slice.cpp \
    $$PWD/../../Source/Lib/CoDec/FFmpeg_Library.cpp \
    $$PWD/../../Source/Lib/CoDec/Wrapper.cpp \
    $$PWD/../../Source/Lib/Common/Common.cpp \
    $$PWD/../../Source/Lib/Compressed/Matroska/Matroska.cpp \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/IntermediateWrite.cpp \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/RAWcooked.cpp \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/Reversibility.cpp \
    $$PWD/../../Source/Lib/Compressed/RAWcooked/Track.cpp \
    $$PWD/../../Source/Lib/Core/Core.cpp \
    $$PWD/../../Source/Lib/Core/Global.cpp \
    $$PWD/../../Source/Lib/Core/Help.cpp \
    $$PWD/../../Source/Lib/Core/Input.cpp \
    $$PWD/../../Source/Lib/Core/Metadata.cpp \
    $$PWD/../../Source/Lib/Core/Output.cpp \
    $$PWD/../../Source/Lib/License/License.cpp \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/bitmath.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/bitreader.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/cpu.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/crc.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/fixed.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/format.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/lpc.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/md5.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/memory.c \
    $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/stream_decoder.c \
    $$PWD/../../Source/Lib/ThirdParty/md5/md5.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/adler32.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/compress.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/crc32.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/deflate.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/infback.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inffast.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inflate.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/inftrees.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/trees.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/uncompr.c \
    $$PWD/../../Source/Lib/ThirdParty/zlib/zutil.c \
    $$PWD/../../Source/Lib/Transform/Transform.cpp \
    $$PWD/../../Source/Lib/Uncompressed/AIFF/AIFF.cpp \
    $$PWD/../../Source/Lib/Uncompressed/AVI/AVI.cpp \
    $$PWD/../../Source/Lib/Uncompressed/DPX/DPX.cpp \
    $$PWD/../../Source/Lib/Uncompressed/EXR/EXR.cpp \
    $$PWD/../../Source/Lib/Uncompressed/HashSum/HashSum.cpp \
    $$PWD/../../Source/Lib/Uncompressed/TIFF/TIFF.cpp \
    $$PWD/../../Source/Lib/Uncompressed/WAV/WAV.cpp \
    $$PWD/../../Source/Lib/Utils/CRC32/ZenCRC32.cpp \
    $$PWD/../../Source/Lib/Utils/Errors/Errors.cpp \
    $$PWD/../../Source/Lib/Utils/FileIO/FileChecker.cpp \
    $$PWD/../../Source/Lib/Utils/FileIO/FileIO.cpp \
    $$PWD/../../Source/Lib/Utils/FileIO/FileWriter.cpp \
    $$PWD/../../Source/Lib/Utils/FileIO/Input_Base.cpp \
    $$PWD/../../Source/Lib/Utils/MD5/MD5_Lanes.cpp \
    $$PWD/../../Source/Lib/Utils/Trace/Trace.cpp \
    $$PWD/../../Source/Lib/Utils/RawFrame/RawFrame.cpp

# Third-party files which are not for all platforms, as in Project/MSVC2026 for Windows
win32 {
    HEADERS += $$PWD/../../Source/Lib/ThirdParty/flac/include/share/win_utf8_io.h
    SOURCES_LIB += $$PWD/../../Source/Lib/ThirdParty/flac/src/share/win_utf8_io/win_utf8_io.c
} else {
    SOURCES_LIB += $$PWD/../../Source/Lib/ThirdParty/flac/src/libFLAC/float.c
}

# The rules below are for Makefiles, qmake can not write them in a Visual Studio project
!build_pass:contains(TEMPLATE, "vc.*"): warning("qmake -tp vc is not supported by this project: its sources of the library would not be compiled, use the Makefiles (nmake or jom)")

# C++ sources of the library: with the warnings of their own build files (Project/GNU/CLI, Project/MSVC2026), the ones Qt adds are for the user interface
SOURCES_LIB_CXX = $$find(SOURCES_LIB, \\.cpp$)
msvc: LIB_CXXFLAGS = -w44100 -w44189
else: LIB_CXXFLAGS = -Wno-all -Wno-extra
lib_cxx.name = lib_cxx
lib_cxx.input = SOURCES_LIB_CXX
lib_cxx.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
lib_cxx.commands = $(CXX) -c $(CXXFLAGS) $$LIB_CXXFLAGS $(INCPATH) ${QMAKE_FILE_IN}
msvc: lib_cxx.commands += -Fo${QMAKE_FILE_OUT}
else: lib_cxx.commands += -o ${QMAKE_FILE_OUT}
lib_cxx.dependency_type = TYPE_C
lib_cxx.variable_out = OBJECTS
QMAKE_EXTRA_COMPILERS += lib_cxx

# C sources of the library: all third-party code, its warnings are not ours to fix
SOURCES_LIB_C = $$find(SOURCES_LIB, \\.c$)
QMAKE_CFLAGS_WARN_ON = $$QMAKE_CFLAGS_WARN_OFF

# Two of them are named md5.c and their object files would have the same name: this one gets another name
SOURCES_LIB_MD5 = $$find(SOURCES_LIB_C, /md5/md5\\.c$)
SOURCES_LIB_C -= $$SOURCES_LIB_MD5
SOURCES += $$SOURCES_LIB_C
md5_openwall.name = md5_openwall
md5_openwall.input = SOURCES_LIB_MD5
md5_openwall.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}_openwall$${first(QMAKE_EXT_OBJ)}
md5_openwall.commands = $(CC) -c $(CFLAGS) $(INCPATH) ${QMAKE_FILE_IN}
msvc: md5_openwall.commands += -Fo${QMAKE_FILE_OUT}
else: md5_openwall.commands += -o ${QMAKE_FILE_OUT}
md5_openwall.dependency_type = TYPE_C
md5_openwall.variable_out = OBJECTS
QMAKE_EXTRA_COMPILERS += md5_openwall

RESOURCES += \
    $$PWD/../../Source/Resource/GUI_Main.qrc

OBJECTS_DIR = _Automated
MOC_DIR = _Automated
RCC_DIR = _Automated
