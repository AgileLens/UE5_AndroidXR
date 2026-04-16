# UE5 AndroidXR Template

![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5.4-blue?logo=unrealengine)
![Platform](https://img.shields.io/badge/Platform-Android%20XR-green)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

A streamlined Unreal Engine project based on the official VR Template, pre-configured for **Android XR** development. This repository includes necessary project setting adjustments and the **AndroidXR plugin** (originally by Owlchemy Labs, now maintained by Agile Lens) to enable seamless packaging for devices like the **Galaxy XR**.

## 📺 Video Overview
[![Android XR for Unreal Intro](https://img.youtube.com/vi/vNpNcAKw12o/0.jpg)](https://www.youtube.com/watch?v=vNpNcAKw12o)

*Click the image above to watch on YouTube.*

---

## 🚀 Key Features
* **Pre-configured AndroidXR Plugin:** Started by **Owlchemy Labs** and updated/maintained by **Agile Lens** for UE5 compatibility.
* **Optimized Project Settings:** Ready-to-go Android manifest settings, SDK/NDK levels, and rendering paths.
* **VR Template Foundation:** Built on top of Epic's standard VR Template, ensuring all your favorite XR interactions work out of the box.
* **Passthrough Enabled:** Includes OpenXR project settings adjustments to allow for mixed reality environments.

## 🛠 Prerequisites
Ensure you have the following installed:
1. **Unreal Engine 5.5.4** (5.7 was working in October 2025 but is now broken... can be explored in **dev_57** branch)
2. **Android Studio:** Flamingo or Ladybug

## 🛠 Unreal Engine Android SDK Settings
3. **Accept SDK License**
4. **Minimum and Target SDK Version:** both set to 32
5. **Location of Android NDK:** Path to ndk 25.1.8937393
6. **Android SDK API Level:** android-34
7. **Android NDK API Level:** android-32
8. **Java JDK 17**
9. **Note: Check out this incredibly helpful repo for setting up Android for UE5** [NuLL3rr0r Unreal Android Installer](https://github.com/NuLL3rr0r/unreal-android-installer/tree/5.4)* 

*To run the Powershell (.ps1) script you can use:
```bash
   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```
## 💻 Getting Started
1. **Clone the Repo:**
   ```bash
   git clone [https://github.com/AgileLens/UE5_AndroidXR.git](https://github.com/AgileLens/UE5_AndroidXR.git)

2.  Open: Launch the project in Unreal Engine.
3.  Package:
- Navigate to Platforms > Android > Android_ASTC.
- Select Package Project.
- Deploy the APK to your Galaxy XR or compatible Android XR device.

## Things to Try

1. **Open the project in UE 5.5.4 and hit Play in the Editor** — the VR Template scene loads in the viewport; verify the AndroidXR plugin is listed as active under Edit → Plugins → Installed → AndroidXR.
2. **Navigate to Platforms → Android → Android_ASTC and click Package Project** — UE generates an APK; sideload it to a Galaxy XR device with `adb install` and confirm it launches.
3. **Enable passthrough on the device** — the mixed reality environment should show your real room behind the UE scene; check OpenXR project settings under Project Settings → Platforms → Android if it doesn't appear.
4. **Check out the `dev_57` branch and open in UE 5.7** — verify whether the plugin issues are resolved on your setup; the branch is exploratory but useful for tracking 5.7 compatibility status.
5. **Swap the VR Template pawn for your own character Blueprint** — the project's optimized Android manifest and SDK settings carry over; package and deploy to confirm the custom pawn works on-device without re-configuring SDK paths.

## 🤝 Credits & Lineage
Owlchemy Labs: For the foundational development of the AndroidXR plugin.

Agile Lens: For ongoing maintenance, UE5 compatibility updates, and template optimization.

## 📄 License
This project is licensed under the MIT License.
Copyright (c) 2026 Agile Lens LLC

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
