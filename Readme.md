# 4 Seasons - Realtime rendering demo for the course "186.140 Echtzeitgraphik (VU 3,0) 2024W" at TU Wien

## Description

Members:

* Manuel Eiweck 01633012
* Peter Kain 12119833


We want to create a Rendering demo which showcases one scene (or very similar looking scenes) in all 4 seasons. The base theme of the scene should be in an Japanese/Chinese style. E.g temples, laterns, Trees, nature. 
The transition between seasons will be hidden by either the camera movement itself (blocking scene with one object) or just a fade in black animation. 
For music a general background music is planned that fits the scene.

## Setup

Windows Only

### Install VCPKG

Obsolte, for now. If we want additional libraries for sound etc we can use this package manager.

Details: https://learn.microsoft.com/de-de/vcpkg/get_started/get-started-msbuild?pivots=shell-powershell

TLDR:

```powershell
git clone git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
```

Setup Environment Variables:

Either one time via command line:

```powershell
$env:VCPKG_ROOT = "F:\dev\studium\realtimegraphics\vcpkg"
$env:PATH = "$env:VCPKG_ROOT;$env:PATH"
```
 
Or permanently via Windows settings.
