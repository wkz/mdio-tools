#!/bin/sh

# Required Packages
REQPKG="autoconf automake pkg-config libmnl-dev"


# check for all dependencies
pkg2install=""
for actPkg in ${REQPKG}; do
    if [ -z "$(dpkg -s ${actPkg} | grep -o "install ok installed")" ]; then
        pkg2install="${pkg2install} ${actPkg}"
    fi
done
pkg2install=${pkg2install##*( )}    # remove leading blanks
if [ ! -z ${pkg2install} ]; then
    echo "Required Packages missing, try:"
    echo "  sudo apt-get update && apt-get install ${pkg2install} -y"
    #exit 1;
fi


# create if not exist
if [ ! -d "./m4" ]; then
   mkdir -p ./m4
fi


# No git repo, make static version in 'configure.ac'
#  https://www.gnu.org/software/autoconf/manual/autoconf-2.67/html_node/Initializing-configure.html
if [ ! -d ".git" ]; then
   # make backup
   cp ./configure.ac ./configure.bak
   # replace 'm4_esyscmd_s(git describe --always --dirty --tags)' with 'Unknown'
   sed -i 's/m4_esyscmd_s(git describe --always --dirty --tags)/Unknown/g' ./configure.ac
fi


# build make scripts
autoreconf -W portability -vifm
