#!/bin/bash

PROJECT_ROOT="$(pwd)"

# To add this tool in Qt Creator:
# Category: Linguist
# Name: Update Translations (lupdate)
# Executable: lupdate
# Arguments: -project SavvyLens.pro
# Working Directory: %{ActiveProject:Path}

echo "Updating translation files..."

lupdate -verbose "$PROJECT_ROOT"/SavvyLens.pro

echo "Translation files updated."
