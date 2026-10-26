# Samsung external display capture

`samsung-external-native-capture.patch` applies to the official scrcpy v4.1 source tree. It adds a narrowly selected native Android screenrecord capture processor and preserves the existing scrcpy socket framing and controller position mapping.

To reproduce, download the official v4.1 source from https://github.com/Genymobile/scrcpy/releases/tag/v4.1 and apply from its root:

```sh
patch -p1 -i /absolute/path/to/samsung-external-native-capture.patch
cd server
PATH=/opt/homebrew/opt/openjdk@17/bin:$PATH \
  ANDROID_HOME=/absolute/path/to/android-sdk \
  BUILD_DIR=/absolute/path/to/server-build \
  bash ./build_without_gradle.sh
```

The SDK must contain platform android-36 and build-tools 36.0.0. Copy the resulting `server-build/scrcpy-server` to `src/third_party/scrcpy-server` in this submodule before packaging QtScrcpy.

The native branch is selected on Samsung Android 16 or later for an existing non-primary local display, H.264, no crop, and orientation 0. It uses the display's physical local ID for capture and its logical ID for control. Custom encoder options and max-fps are not applied by the system recorder. Other configurations retain the upstream capture path. Real device sleep/lock behavior is not guaranteed by this patch.
