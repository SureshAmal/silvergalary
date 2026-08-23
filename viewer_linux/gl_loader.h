#pragma once

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include <GL/glext.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

inline bool init_gl_loader() {
    // All OpenGL symbols are directly resolved from libGL.so via GL_GLEXT_PROTOTYPES
    return true;
}
