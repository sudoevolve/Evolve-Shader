#pragma once
#include <glad/glad.h>
#include <string>
#include <iostream>
#include <unordered_map>

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[10240];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        std::cerr << (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment")
            << " shader compile error:\n" << buf << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint CreateProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertSrc);
    if (!vs) return 0;
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[10240];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        std::cerr << "Program link error:\n" << buf << std::endl;
        glDeleteShader(vs); glDeleteShader(fs); glDeleteProgram(prog);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

class GLProgram {
public:
    GLuint id = 0;
    mutable std::unordered_map<std::string, GLint> uniformCache;
    GLProgram() = default;
    explicit GLProgram(const char* vertSrc, const char* fragSrc) {
        id = CreateProgram(vertSrc, fragSrc);
        uniformCache.clear();
    }
    GLProgram(const GLProgram&) = delete;
    GLProgram& operator=(const GLProgram&) = delete;
    GLProgram(GLProgram&& other) noexcept : id(other.id), uniformCache(std::move(other.uniformCache)) { other.id = 0; }
    GLProgram& operator=(GLProgram&& other) noexcept {
        if (this != &other) { destroy(); id = other.id; other.id = 0; uniformCache.clear(); }
        return *this;
    }
    ~GLProgram() { destroy(); }
    void use() const { if (id) glUseProgram(id); }
    GLint getUniformLocation(const std::string& name) const {
        if (!id) return -1;
        auto it = uniformCache.find(name);
        if (it != uniformCache.end()) return it->second;
        GLint loc = glGetUniformLocation(id, name.c_str());
        uniformCache.emplace(name, loc);
        return loc;
    }
    void destroy() { if (id) { glDeleteProgram(id); id = 0; } uniformCache.clear(); }
};