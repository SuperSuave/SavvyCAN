#!/bin/sh

CURDIR=`pwd`

# Set absolute path
sed -e "s,<DIR>,$CURDIR,g" SavvyLens.desktop > SavvyLens.desktop.temp

cp SavvyLens.desktop.temp ~/.local/share/applications/SavvyLens.desktop
cp SavvyLens.desktop.temp ~/Desktop/SavvyLens.desktop
rm SavvyLens.desktop.temp

echo "Installed SavvyLens icons on menu and desktop !"
