#pragma once
#include <glad/glad.h>
#include <string>
#include <iostream>

// stb_image must be included in one translation unit with STB_IMAGE_IMPLEMENTATION defined.
// Ensure the .cpp that includes this header defines STB_IMAGE_IMPLEMENTATION before including this header.
#include "stb_image.h"

struct Texture {
    GLuint id = 0;
    int width = 1;
    int height = 1;

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept : id(other.id), width(other.width), height(other.height) {
        other.id = 0;
    }
    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            destroy();
            id = other.id;
            width = other.width;
            height = other.height;
            other.id = 0;
        }
        return *this;
    }
    ~Texture() { destroy(); }

    bool loadFromFile(const std::string& path) {
        destroy();
        stbi_set_flip_vertically_on_load(true);
        int nChannels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &nChannels, 0);
        if (!data) {
            std::cerr << "Failed to load texture: " << path << std::endl;
            createEmpty();
            return false;
        }

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        GLenum format = (nChannels == 3) ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        // Sampling policy: linear filtering, no mipmaps, consistent with runtime buffers
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        stbi_image_free(data);
        return true;
    }

    void createEmpty() {
        destroy();
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        unsigned char black[4] = { 0, 0, 0, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        width = height = 1;
    }

    void bind(int unit) const {
        if (id) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, id);
        }
    }

    void destroy() {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    }
};

class VertexBuffer {
public:
    GLuint id = 0;
    VertexBuffer() = default;
    VertexBuffer(const float* data, size_t size) {
        glGenBuffers(1, &id);
        glBindBuffer(GL_ARRAY_BUFFER, id);
        glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    }
    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;
    VertexBuffer(VertexBuffer&& other) noexcept : id(other.id) { other.id = 0; }
    VertexBuffer& operator=(VertexBuffer&& other) noexcept {
        if (this != &other) { destroy(); id = other.id; other.id = 0; }
        return *this;
    }
    ~VertexBuffer() { destroy(); }
    void bind() const { if (id) glBindBuffer(GL_ARRAY_BUFFER, id); }
    static void unbind() { glBindBuffer(GL_ARRAY_BUFFER, 0); }
    void destroy() { if (id) { glDeleteBuffers(1, &id); id = 0; } }
};

class VertexArray {
public:
    GLuint id = 0;
    VertexArray() { glGenVertexArrays(1, &id); }
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept : id(other.id) { other.id = 0; }
    VertexArray& operator=(VertexArray&& other) noexcept {
        if (this != &other) { destroy(); id = other.id; other.id = 0; }
        return *this;
    }
    ~VertexArray() { destroy(); }
    void bind() const { if (id) glBindVertexArray(id); }
    static void unbind() { glBindVertexArray(0); }
    void destroy() { if (id) { glDeleteVertexArrays(1, &id); id = 0; } }
};

class Framebuffer {
public:
    GLuint fbo = 0;
    Texture colorTex;
    Framebuffer() = default;
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept : fbo(other.fbo), colorTex(std::move(other.colorTex)) { other.fbo = 0; }
    Framebuffer& operator=(Framebuffer&& other) noexcept {
        if (this != &other) { destroy(); fbo = other.fbo; colorTex = std::move(other.colorTex); other.fbo = 0; }
        return *this;
    }
    ~Framebuffer() { destroy(); }

    bool create(int w, int h) {
        destroy();
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &colorTex.id);
        glBindTexture(GL_TEXTURE_2D, colorTex.id);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        colorTex.width = w;
        colorTex.height = h;

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex.id, 0);
        bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    void bind() const { if (fbo) glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
    static void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    void destroy() {
        // Ensure attached color texture is released to avoid GPU memory leaks
        colorTex.destroy();
        if (fbo) {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
    }
};
