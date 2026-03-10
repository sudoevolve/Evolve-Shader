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
├─ Evolve shader.sln
├─ Evolve shader.vcxproj
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
- FPS shown in the window title.

## Runtime Behavior

At startup the app does the following:

1. Scans `iChannel/` recursively for `.png`, `.jpg`, and `.jpeg` images.
2. Scans `frag/` non-recursively for `.frag` files.
3. Sorts shader passes by the first number found in each filename.
4. Prompts you to configure `iChannel0`-`iChannel3` for each pass.
5. Renders all passes off-screen, then displays the last pass on screen.

Important behavior details:

- Files in `frag/` without a number in the filename are ignored.
- `frag/` is not scanned recursively.
- `iChannel/` is scanned recursively.
- On the very first frame, buffer inputs fall back to an empty texture.
- Configured global images are preloaded before the render loop starts.
- VSync is enabled by default; set `EVOLVE_SHADER_VSYNC=0` to disable it.

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

This repository does not vendor all third-party headers and libraries.
The Visual Studio project expects your environment to provide these dependencies:

- OpenGL 3.3 compatible GPU / driver
- `glfw3`
- `glad`
- `stb_image.h`

The included project file currently targets MSVC toolset `v145`.
If your Visual Studio installation uses a different toolset, the IDE may ask you to retarget the project.

## Recommended Windows Setup

The easiest path on Windows is Visual Studio + vcpkg integration.

### 1. Install vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

### 2. Install dependencies

```powershell
.\vcpkg install glfw3 glad stb --triplet x64-windows
```

### 3. Open and build

- Open `Evolve shader.sln` in Visual Studio.
- Select `x64` and either `Debug` or `Release`.
- Build the solution.

### 4. Run with the correct working directory

The executable expects `frag/` and optional `iChannel/` to exist relative to the working directory.

Use the repository root as the working directory, not the build output folder.

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

- The app is architecturally portable, but this repository currently ships only a Visual Studio solution.
- Channel configuration is interactive only; it is not saved to disk.
- Shader files are loaded only once at startup; there is no hot reload.
- The last pass is always the one presented to the screen.
- Channel configuration is still interactive only; image bindings are preloaded after configuration.
- The sample shaders in `frag/` include their own upstream attribution comments.

## License

The project is distributed under the MIT license. See `LICENSE`.

Third-party shader or image assets may have their own licenses or attribution requirements.
