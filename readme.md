# Evolve Shader

[中文说明 / Chinese README](README.zh-CN.md)

Evolve Shader is a minimal multi-pass shader viewer inspired by Shadertoy.
It loads fragment shaders from `frag/`, lets you wire `iChannel0`-`iChannel3` interactively, renders every pass into ping-pong framebuffers, and presents the last pass to the window.

## Screenshots

![Example Shader 1](2.png)
![Example Shader 2](1.png)

## What This Repository Actually Contains

```text
Evolve-Shader/
├─ main.cpp
├─ include/
│  ├─ ChannelConfig.h
│  ├─ GLTypes.h
│  ├─ Resources.h
│  ├─ ShaderIO.h
│  └─ ShaderProgram.h
├─ frag/
│  ├─ 1.frag
│  └─ 2.frag
├─ iChannel/
│  ├─ bw_noise.png
│  ├─ color_noise.png
│  ├─ london.png
│  └─ rock.png
├─ CMakeLists.txt
├─ LICENSE
└─ readme.md
```

## Features

- Shadertoy-style shader entry via `mainImage(out vec4 fragColor, in vec2 fragCoord)`.
- Multi-pass rendering driven by the numeric order of `.frag` files in `frag/`.
- Interactive `iChannel` routing for `none`, `self`, other buffers, or images from `iChannel/`.
- Ping-pong framebuffer feedback for recursive / temporal effects.
- Common Shadertoy uniforms such as `iResolution`, `iTime`, `iMouse`, `iDate`, and `iChannelResolution`.
- Lazy global texture cache for images discovered under `iChannel/`.
- **Preset System**: Save your configuration (channels + shader files + images) to a folder and reload it later.
- FPS shown in the window title.

## Runtime Behavior

At startup the app does the following:

1. Checks for presets in `presets/`.
2. If presets exist, asks to load one or start new.
3. If new configuration selected:
    a. Scans `iChannel/` recursively for `.png`, `.jpg`, and `.jpeg` images.
    b. Scans `frag/` non-recursively for `.frag` files.
    c. Sorts shader passes by the first number found in each filename.
    d. Prompts you to configure `iChannel0`-`iChannel3` for each pass.
    e. Asks if you want to save the configuration as a new preset.
4. Renders all passes off-screen, then displays the last pass on screen.

Important behavior details:

- Files in `frag/` without a number in the filename are ignored.
- `frag/` is not scanned recursively.
- `iChannel/` is scanned recursively.
- On the very first frame, buffer inputs fall back to an empty texture.
- Configured global images are preloaded before the render loop starts.
- VSync is **disabled** by default. Use the startup **Settings** menu to toggle VSync on/off.

## Shader Pass Ordering

The renderer uses the first numeric token in the filename to determine pass order.

Recommended naming:

```text
0_main.frag
1_blur.frag
2_feedback.frag
3_present.frag
```

Current repository examples use:

```text
frag/1.frag
frag/2.frag
```

## Supported Uniforms

The wrapper injected by `include/ShaderIO.h` exposes these uniforms:

| Uniform | Type | Notes |
| --- | --- | --- |
| `iResolution` | `vec3` | Framebuffer size in pixels; current code sets `z = 1.0`. |
| `iTime` | `float` | Seconds since startup. |
| `iTimeDelta` | `float` | Time between frames. |
| `iFrame` | `int` | Frame counter. |
| `iFrameRate` | `float` | Computed from `iTimeDelta`. |
| `iDate` | `vec4` | Year, month, day, seconds since midnight. |
| `iMouse` | `vec4` | Shadertoy-like mouse state. |
| `iChannel0`-`iChannel3` | `sampler2D` | Texture or buffer inputs. |
| `iChannelTime[4]` | `float[4]` | Currently all set to `iTime`. |
| `iChannelResolution[4]` | `vec3[4]` | Bound texture resolution for each channel. |
| `iSampleRate` | `float` | Currently fixed to `44100.0`. |

`iMouse` follows Shadertoy-style semantics:

- `xy`: current mouse position in pixels, origin at bottom-left.
- `zw`: current press position while the button is down; negative last click position after release.

## Dependencies

The CMake build fetches third-party source trees into `3rd/` automatically:

- OpenGL 3.3 compatible GPU / driver
- `glfw3`
- `glew`
- `stb_image.h`

You only need CMake, a C++17 compiler, Git, and platform OpenGL development support.

## Cross-Platform CMake Build

### Windows

Install Visual Studio 2022 with the C++ workload, CMake, and Git. Then run:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

### macOS

Install Xcode Command Line Tools, CMake, and Git. Then run:

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Linux

Install CMake, Git, a C++17 compiler, and OpenGL/X11 development packages. For example, on Ubuntu/Debian:

```bash
sudo apt install build-essential cmake git libgl1-mesa-dev xorg-dev
```

Then run:

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Run

CMake copies `frag/`, `iChannel/`, and `presets/` next to the executable after each build, so you can run from the build output directory:

```bash
./build/EvolveShader
```

For multi-config generators such as Visual Studio, run the executable under the configuration folder, for example `build/Release/EvolveShader.exe`.

## Usage

### Quick start

1. Put one or more `.frag` files in `frag/`.
2. Optionally put images in `iChannel/`.
3. Launch the app.
4. Press `Enter` to use the automatic pass chain, or type a pass index to configure channels manually.

### Automatic chain mode

If you press `Enter` at the first prompt, the renderer creates a simple chain:

- pass 2 reads pass 1 in `iChannel0`
- pass 3 reads pass 2 in `iChannel0`
- and so on

### Manual mode

For a selected pass, each channel can be assigned to:

- `none`
- `self` for feedback
- another previously or later defined buffer
- a global image from `iChannel/`

## Example Shader

```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
```

You do not need to provide `main()` yourself.
The runtime wraps your shader automatically.

## Notes and Limitations

- Third-party sources are fetched into `3rd/` during CMake configure.
- Channel configuration is interactive only; it is not saved to disk.
- Shader files are loaded only once at startup; there is no hot reload.
- The last pass is always the one presented to the screen.
- Channel configuration is still interactive only; image bindings are preloaded after configuration.
- The sample shaders in `frag/` include their own upstream attribution comments.

## License

The project is distributed under the MIT license. See `LICENSE`.

Third-party shader or image assets may have their own licenses or attribution requirements.
