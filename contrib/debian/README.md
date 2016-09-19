
Debian
====================
This directory contains files used to package eternityd/eternity-qt
for Debian-based Linux systems. If you compile eternityd/eternity-qt yourself, there are some useful files here.

## eternity: URI support ##


eternity-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install eternity-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your eternity-qt binary to `/usr/bin`
and the `../../share/pixmaps/eternity128.png` to `/usr/share/pixmaps`

eternity-qt.protocol (KDE)

