#!/bin/sh

platforms="fedora-i386 fedora-x86_64 stable-i386 stable-x86_64 debian-unstable-win32-x86_64"
#platforms="debian-unstable-win32-x86_64"
version="$1"

for i in $platforms;
do
	echo "Building package for $i"
	umount /var/meow-builds/$i/home
	umount /var/meow-builds/$i/proc
	umount /var/meow-builds/$i/dev
	umount /var/meow-builds/$i/home/charles/dlls
	mount --bind /var/meow-builds/home /var/meow-builds/$i/home/
	mount --bind /home/charles/dev/dlls /var/meow-builds/$i/home/charles/dlls
	mount proc -t proc /var/meow-builds/$i/proc/
	mount udev -t devtmpfs /var/meow-builds/$i/dev


	arch=`echo $i | sed 's/^.*-//'`
	package_fmt=deb
	if [[ $i =~ fedora ]]; then package_fmt=rpm; fi
	if [[ $i =~ win32 ]]; then package_fmt=exe; fi
	$arch chroot /var/meow-builds/$i su - charles -c \
		"cd ~/meow; bash releasetools/build-platform.sh $i $package_fmt $version 2> build-$i.log"

	umount /var/meow-builds/$i/home
	umount /var/meow-builds/$i/proc
	umount /var/meow-builds/$i/dev
	umount /var/meow-builds/$i/home/charles/dlls
done

