
Debian
====================
This directory contains files used to package swiftcashd/swiftcash-qt
for Debian-based Linux systems. If you compile swiftcashd/swiftcash-qt yourself, there are some useful files here.

## swiftcash: URI support ##


swiftcash-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install swiftcash-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your swiftcashqt binary to `/usr/bin`
and the `../../share/pixmaps/swiftcash128.png` to `/usr/share/pixmaps`

swiftcash-qt.protocol (KDE)

