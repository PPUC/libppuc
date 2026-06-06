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

## Game YAML Metadata

`libppuc` parses optional switch and coil roles from the game YAML and exposes
them to the application layer.

Switches may set `button: true`:

```yaml
switches:
  -
    description: 'LEFT FLIPPER BUTTON'
    number: 63
    board: 1
    port: 17
    debounce: 3
    debounceMode: fastFlip
    button: true
```

This does not change firmware switch behavior. It marks cabinet/player controls
so `ppuc-pinmame` can ignore them when deciding whether normal playfield switch
activity has gone quiet, and can suppress ball search while a button is held.

PWM outputs may set `ballSearch: true`:

```yaml
pwmOutput:
  -
    description: 'Outhole Kicker'
    number: 7
    board: 4
    port: 3
    type: solenoid
    ballSearch: true
```

`libppuc` exposes this flag through `PPUCCoil::ballSearch`; the host
application decides whether to use it. `ppuc-pinmame` keeps host-side ball
search disabled by default because newer ROMs often implement their own search.

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
