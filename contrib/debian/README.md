
Debian
====================
This directory contains files used to package Divitaed/Divitae-qt
for Debian-based Linux systems. If you compile Divitaed/Divitae-qt yourself, there are some useful files here.

## Divitae: URI support ##


Divitae-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install Divitae-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your Divitaeqt binary to `/usr/bin`
and the `../../share/pixmaps/Divitae128.png` to `/usr/share/pixmaps`

Divitae-qt.protocol (KDE)

