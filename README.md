# Neversnooze

[![Linux](https://github.com/danielkrupinski/Osiris/actions/workflows/linux.yml/badge.svg?branch=master&event=push)](https://github.com/danielkrupinski/Osiris/actions/workflows/linux.yml)

 Linux game hack for **Counter-Strike 2** with GUI and rendering based on game's Panorama UI. Compatible with the latest game update on Steam.



## What's new
* 18 August 2026
    * Added skin and knife changer with it's own tab, every weapon currently only have 10 skins to choose from. - will be fixed later
    <img width="2632" height="1041" alt="image" src="https://github.com/user-attachments/assets/d678fa7b-0a00-4ce2-be53-070ca904b816" />
    <img width="1942" height="1028" alt="image" src="https://github.com/user-attachments/assets/feb8ce91-f3b4-4ff0-90f9-064a3d3574f1" />
    <img width="1425" height="1076" alt="image" src="https://github.com/user-attachments/assets/3aa2b593-81e4-4ab8-a86e-02057159afc9" />

    * added team damage tracker
    * added vote revealer
    * added cooldown revealer
    * added blockbot - hardcoded the keybind to E, change it to anything you want in code, might fix it later idk
    * added bunnyhop
    * added auto strafe
    * added fake prime
    * added fake level + level changer
    * added match auto accept
    <img width="1554" height="1270" alt="image" src="https://github.com/user-attachments/assets/f9dfb52e-2917-493e-a866-7595267b6a4c" />


    * added hit sound
    * added spawn protection end sound
    <img width="1308" height="319" alt="image" src="https://github.com/user-attachments/assets/29c3878f-40be-44f6-b0b2-f42d7e09fe13" />


    * added a really annoying sound upon injection :)
    * added a WIP triggerbot that needs to get fixed soon
     <img width="1238" height="393" alt="image" src="https://github.com/user-attachments/assets/ad40b6d4-945a-403f-a067-dcd73433be5b" />



* 04 November 2025
    * Improved smoothness of "Player Info in World" on moving players

* 30 October 2025
    * Added Bomb Plant Alert feature
        * Green color means the bomb will be planted before the end of the round if uninterrupted
        * Red color means the bomb can not be planted before the end of the round

    <img width="201" height="146" alt="Bomb Plant Alert" src="https://github.com/user-attachments/assets/21c0f8fb-a20d-42df-9857-f578cfc9b9f9" />

* 23 October 2025
    * Hostage Outline Glow hue is now customizable

* 20 October 2025
    * Added "No Scope Inaccuracy Visualization" feature

    <img height="300" alt="no scope inaccuracy visualization" src="https://github.com/user-attachments/assets/860c944a-00b1-4b67-9d41-6f43e46f4252" />

* 09 October 2025
    * Added viewmodel fov modification

    ![Viewmodel fov modification](https://github.com/user-attachments/assets/3b9d6bde-a68c-4739-913c-d3b6caba4117)

## Technical features

* C++ runtime library (CRT) is not used in release builds
* No heap memory allocations
* No static imports in release build on Windows
* No threads are created
* Exceptions are not used
* No external dependencies

## Compiling

### Prerequisites

#### Linux

* **CMake 3.24** or newer
* **g++ 14 or newer** or **clang++ 18 or newer**

### Compiling from source

#### Linux

Configure with CMake:

    cmake -DCMAKE_BUILD_TYPE=Release -B build

Build:

    cmake --build build -j $(nproc --all)

After following these steps you should receive **libOsiris.so** file in **build/Source/** directory.

### Loading / Injecting into game process


#### Linux

You can simply run inject.sh
 with **sudo bash inject.sh**

## FAQ

### Where are the settings stored on disk?

In a configuration file `default.cfg` inside `%appdata%\OsirisCS2\configs` directory on Windows and `$HOME/OsirisCS2/configs` on Linux.

## License

> Copyright (c) 2018-2025 Daniel Krupiński

This project is licensed under the [MIT License](https://opensource.org/licenses/mit-license.php) - see the [LICENSE](https://github.com/danielkrupinski/Osiris/blob/master/LICENSE) file for details.
