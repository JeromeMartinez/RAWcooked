#! /bin/sh

#############################################################################
# Configure
Home=`pwd`

#############################################################################
# Setup for parallel builds
Zen_Make()
{
 if test -e /proc/stat; then
  numprocs=`grep -E -c '^cpu[0-9]+' /proc/stat || :`
  if [ "$numprocs" = "0" ]; then
   numprocs=1
  fi
  make -s -j$numprocs
 else
  make
 fi
}

#############################################################################
# RAWcooked
if test -e Project/Qt/prepare; then
 cd Project/Qt
 test -e Makefile && rm Makefile
 sh prepare "$@"
 if test -e Makefile; then
  make clean
  # make clean keeps the program of a previous build: the result of the build says if it is compiled, not the file
  if Zen_Make; then
   echo RAWcooked compiled
  else
   echo Problem while compiling RAWcooked
   exit 1
  fi
 else
  echo Problem while configuring RAWcooked
  exit 1
 fi
else
 echo RAWcooked directory is not found
 exit 1
fi
cd "$Home"

#############################################################################
# Going home
cd "$Home"
echo "RAWcooked (GUI) executable is in Project/Qt"
# The application bundle of macOS has no install target
if test -e Project/Qt/rawcooked-gui; then
 echo "For installing, cd Project/Qt && make install"
fi
