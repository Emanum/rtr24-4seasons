# 4 Seasons 

## General

Group: 2

Members:
*  Manuel Eiweck 01633012
*  Peter Kain 12119833

Github Link: https://github.com/Emanum/rtr24-4seasons 

Credits to the original example we used for this project https://github.com/cg-tuwien/Auto-Vk-Toolkit

### Tested on:

* Windows 11
* Nvidia RTX 3070
* Nvidia RTX 4080 Super

## Description

Originally we wanted to create a Rendering demo which showcases one scene (or very similar looking
scenes) in all 4 seasons. The base theme of the scene should be in a Japanese/Chinese style.
E.g temples, lanterns, Trees, nature.  
The transition between seasons will be hidden by either the camera movement itself (blocking
scene with one object) or just a fade in black animation.  
For music a general background music is planned that fits the scene. 

Due to time constraints we decided to only implement one season but this with a lot of detail.

## Controls

By default, all effects are enabled. You can disable them in the menu via the checkboxes with the mouse cursor.

There are two camera modes:
 * Circle Camera
 * First Person (Quake Camera)

To switch from circle Camera to First Person Camera click the checkbox in the menu.
To switch back press ESC. (Don't press twice otherwise the app closes)

* F... follow the prerecorded camera path
* R... reset the camera path and record a new one
* ESC... disable first person camera and show a mouse cursor
* Other controls are shown in the menu and can be changed via the mouse

## Tasks

Manuel Eiweck:

* Setup of the project with basic rasterizer and camera movement
* Basics for camera path recording and playback
* Skybox
* Sculpting of the environment and building two demo scenes (simpleScene.fbx and fullScene.fbx)
* Depth of field effect

Peter Kain:
* Extend the rasterizer with a deferred shading pipeline + optimize/fix Vulkan calls
* Extend camera path recording and playback
* SSAO
* Deferred Shading

Note: The project uses the following libraries:
* https://github.com/cg-tuwien/Auto-Vk-Toolkit

We used the model loader template from the Auto-Vk-Toolkit and extended it with our own code.

## Overview of the Render passes

* Skybox
* Rasterizer (Deferred Shading)
* SSAO
* Blur SSAO
* Illimumination
* Depth of Field - Mask (Near, Center, Far)
* Depth of Field - Bleed Near Field (Max kernel filter)
* Depth of Field - Center Field (could be skipped not used in the end only for debug purposes)
* Depth of Field - Far Field ((could be skipped not used in the end only for debug purposes)
* Depth of Field - Blur & Blend together

All major code is in model_loader which is located here: (examples/fourSeasons/source/model_loader.cpp)

## Build and Run

Build on Win11 with Jetbrains Rider and VS Build Tools. 

Note: add a setup.ini file with you settings beside the executable..
Like this:
```ini
[window]
width=1920
height=1080
[scene]
model=fullScene.fbx
```

The assets are not on Github due to size constraints. You can download them from here:
https://1drv.ms/f/s!AvyVguN0Z2p8nIEhZrysOSzuLdBEzQ?e=X3ISYj

Place the assets in the same folder as the executable. So
```
4Seasons.exe
setup.ini
assets/
```