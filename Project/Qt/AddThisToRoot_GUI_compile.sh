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
 chmod u+x prepare
 ./prepare "$@"
 if test -e Makefile; then
  make clean
  Zen_Make
  if test -e rawcooked-gui || test -e RAWcooked.app; then
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
echo "For installing, cd Project/Qt && make install"
