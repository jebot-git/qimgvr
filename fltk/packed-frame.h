// SPDX-License-Identifier: MIT
#pragma once
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <FL/gl.h>
#include <GL/glext.h>
// Pack a mono framebuffer into two identical eyes without changing logical UI
// coordinates. The window and its GL content use separate native contexts.
class PackedFrame {
  GLuint fbo = 0, texture = 0;
  int width = 0, height = 0;
public:
  void abandon() { fbo = texture = 0; width = height = 0; }
  void release() {
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &texture); abandon();
  }
  void pack(int w, int h) {
    GLint read, draw, binding;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    if (!fbo || width != w || height != h) {
      release(); width = w; height = h;
      glGenFramebuffers(1, &fbo); glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    int half = w / 2;
    glBlitFramebuffer(0,0,w,h,0,0,half,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBlitFramebuffer(0,0,w,h,w-half,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, binding);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    if (scissor) glEnable(GL_SCISSOR_TEST);
  }
};
