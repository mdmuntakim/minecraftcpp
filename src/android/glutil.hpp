// Thin OpenGL ES 3.0 helpers shared by the Android renderer.
#pragma once

#include <GLES3/gl3.h>

#include <cstdio>
#include <string>

namespace mcc {

inline GLuint compileShader(GLenum type, const char* source, std::string* error) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        if (error) *error = std::string("shader compile failed: ") + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

inline GLuint linkProgram(const char* vsSource, const char* fsSource, std::string* error) {
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource, error);
    if (!vs) return 0;
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource, error);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        if (error) *error = std::string("program link failed: ") + log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

}  // namespace mcc
