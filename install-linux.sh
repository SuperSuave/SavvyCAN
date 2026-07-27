#!/bin/bash

APP_PATH="/usr/bin/SavvyLens"
DESKTOP_PATH="/usr/share/applications/SavvyLens.desktop"
ICON_PATH="/usr/share/pixmaps/SavvyLensIcon.png"

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "This script must be run as root."
        exit 1
    fi
}

show_help() {
    echo "Usage: $0 [option]"
    echo ""
    echo "Options:"
    echo "  --help        Show this help message"
    echo "  --uninstall   Remove SavvyLens from the system"
    echo "  (no option)   Install or reinstall SavvyLens"
}

install_error() {
    echo "SavvyLens installation failed."
    exit 1
}

uninstall_error() {
    echo "SavvyLens uninstallation failed."
    exit 1
}

if [ "$1" = "--help" ]; then
    show_help
    exit 0
fi

require_root

# Uninstall
if [ "$1" = "--uninstall" ]; then
    echo "Uninstalling SavvyLens..."
    trap uninstall_error ERR
    rm -f "$APP_PATH" "$DESKTOP_PATH" "$ICON_PATH"
    trap - ERR
    echo "SavvyLens has been uninstalled."
    exit 0
fi

# Check required files
if [ ! -f "SavvyLens" ]; then
    echo "Missing file \"SavvyLens\". You need to build first."
    install_error
fi

if [ ! -f "SavvyLens.desktop" ]; then
    echo "Missing file \"SavvyLens.desktop\"."
    install_error
fi

if [ ! -f "icons/SavvyLensIcon.png" ]; then
    echo "Missing file \"icons/SavvyLensIcon.png\"."
    install_error
fi

# Install
if [ -f "$APP_PATH" ]; then
    echo "Re-installing SavvyLens..."
else
    echo "Installing SavvyLens..."
fi

trap install_error ERR
install -Dm755 SavvyLens "$APP_PATH"
install -Dm644 SavvyLens.desktop "$DESKTOP_PATH"
install -Dm644 icons/SavvyLensIcon.png "$ICON_PATH"
trap - ERR

echo "SavvyLens is installed."
