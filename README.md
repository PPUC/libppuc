# libppuc
PPUC i/o boards communication library

## Compiling

#### Windows (x64)

```shell
platforms/win/x64/external.sh
cmake -G "Visual Studio 17 2022" -DPLATFORM=win -DARCH=x64 -B build
cmake --build build --config Release
```

#### Windows (x86)

```shell
platforms/win/x86/external.sh
cmake -G "Visual Studio 17 2022" -A Win32 -DPLATFORM=win -DARCH=x86 -B build
cmake --build build --config Release
```

#### Linux (x64)
```shell
platforms/linux/x64/external.sh
cmake -DPLATFORM=linux -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### Linux (aarch64)
```shell
platforms/linux/aarch64/external.sh
cmake -DPLATFORM=linux -DARCH=aarch64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### MacOS (arm64)
```shell
platforms/macos/arm64/external.sh
cmake -DPLATFORM=macos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### MacOS (x64)
```shell
platforms/macos/x64/external.sh
cmake -DPLATFORM=macos -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### iOS (arm64)
```shell
platforms/ios/arm64/external.sh
cmake -DPLATFORM=ios -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### tvOS (arm64)
```shell
platforms/tvos/arm64/external.sh
cmake -DPLATFORM=tvos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### Android (arm64-v8a)
```shell
platforms/android/arm64-v8a/external.sh
cmake -DPLATFORM=android -DARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```
