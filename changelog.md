
## 更新文件

### include/GLTypes.h
- **修复** `Framebuffer::destroy()`  
  同时释放附着的 `colorTex`，避免窗口缩放或重复创建 FBO 时的 GPU 内存泄漏。  
- **统一** 图片纹理采样参数  
  线性过滤 / 禁用 mipmap / `GL_CLAMP_TO_EDGE`，与运行时缓冲采样策略保持一致。

### include/ShaderProgram.h
- **新增** `uniformCache`  
  缓存 uniform 位置，减少频繁 `glGetUniformLocation` 的开销。  
- **更新** 移动构造  
  确保 `uniformCache` 一并移动，维护缓存生命周期。

### main.cpp
- **sRGB 处理**  
  仅在最终屏幕输出时启用 `GL_FRAMEBUFFER_SRGB`；  
  中间浮点 FBO 渲染明确禁用，避免与 `GL_RGBA32F` 色域不匹配。  
- **复制性能**  
  用 `glBlitFramebuffer` + 临时 DRAW FBO 替换逐纹理拷贝，提升帧间复制效率。  
- **采样策略**  
  统一上一帧缓冲采样为线性 + `CLAMP_TO_EDGE`，与图片通道一致。  
- **iMouse 语义**（对齐 Shadertoy）  
  `iMouse.xy`：当前光标坐标（Y 轴左下为原点）；  
  `iMouse.zw`：按下时的当前坐标；未按下时为上次点击坐标的负值。  
- **修复** 自反馈缩放黑屏  
  在 `framebufferSizeCallback` 中重置 `g_frame = 0`，确保缩放后重新进入初始化帧，避免读取未初始化的上一帧。

### frag/1.frag
- **鼠标交互**  
  仅左键按下时使用当前坐标；未按下时采用“上次点击位置”进行斥力计算：  
  ```glsl
  mPos = (iMouse.w >= 0.0) ? iMouse.xy : -iMouse.zw;
  ```  
  避免未按下状态仍影响画面。

---

## 修复的 Bug
- GPU 显存泄漏（FBO 附着纹理未释放）——窗口缩放 / 反复创建场景。  
- 自反馈窗口放大后黑屏（未初始化上一帧）——通过重置帧计数进入初始化阶段。  
- 降低 uniform 查询开销（缓存 uniform 位置）。

---

## 性能与一致性优化
- 复制路径由逐纹理拷贝改为帧缓冲 blit，降低 GPU 带宽压力。  
- 统一图片与缓冲采样策略为线性，消除跨 pass 采样不一致。  
- sRGB 仅在最终输出启用，确保中间浮点计算在线性空间进行，提升色彩一致性。

---

## 实现方式
- **RAII 管理 Texture / Framebuffer**  
  创建前调用 `destroy()`，析构时释放资源，确保生命周期安全。  
- **GLProgram 轻量级 uniformCache**  
  首次查询后缓存位置；移动构造转移缓存。  
- **渲染循环**  
  按 pass 管理 sRGB 状态；使用 `blitFBO` 将 FBO 结果复制到上一帧纹理。  
- **鼠标事件**  
  仅跟踪左键；在 uniform 设置时生成符合 Shadertoy 的 `iMouse` 值。

---

## 验证建议
1. 反复缩放窗口：显存占用应按新分辨率跳变且稳定，无持续增长；自反馈在 1–3 帧初始化后恢复正常。  
2. 观察鼠标交互：未按下左键移动不影响画面，按下移动才产生斥力；松开后基于上次点击位置。  
3. 使用 GPU 调试工具或驱动面板：  
   - 检查 `GL_FRAMEBUFFER_SRGB` 仅在最终输出启用；  
   - 确认 blit 后上一帧纹理分辨率与采样参数正确。

---

## 后续可选改进
- 将中间缓冲格式从 `GL_RGBA32F` 降级为 `GL_RGBA16F`（若视觉允许），减少显存占用约一半。  
- 仅为存在反馈或被其他 pass 引用的通道分配上一帧纹理，按需分配减少内存占用。  
- 在缩放时尝试对上一帧进行按比例 blit，保留状态跨尺寸变化。
