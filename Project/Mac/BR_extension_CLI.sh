#! /bin/sh

# Because of the autotools bug
cd Project/GNU/CLI
./autogen.sh
cd ../../..

./CLI_Compile.sh --enable-arch-x86_64
