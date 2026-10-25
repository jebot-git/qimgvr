// SPDX-License-Identifier: GPL-3.0-or-later
#define GL_GLEXT_PROTOTYPES
#include "model-view.h"
#include "model/window-stereo.h"
#include <FL/Fl_Menu_Button.H>
#include <FL/gl.h>
#include <GL/glext.h>
#include <fstream>
namespace qimgvr {
using namespace fsv::gltf;
namespace {
Quat axis(float angle, Vec3 v) {
  return {v.x * std::sin(angle / 2), v.y * std::sin(angle / 2),
          v.z * std::sin(angle / 2), std::cos(angle / 2)};
}
GLuint shader(GLenum kind, const char *source) {
  auto s = glCreateShader(kind);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(s);
    throw std::runtime_error("Cannot compile stereo surface shader");
  }
  return s;
}
} // namespace
struct ModelView::Targets {
  GLuint fbo = 0, color = 0, depth = 0, program = 0, vao = 0, vbo = 0;
  int width = 0, height = 0;
  void abandon() {
    fbo = color = depth = program = vao = vbo = 0;
    width = height = 0;
  }
  void release() {
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color);
    glDeleteRenderbuffers(1, &depth);
    glDeleteProgram(program);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    abandon();
  }
  void prepare(int w, int h) {
    if (!program) {
      auto v = shader(GL_VERTEX_SHADER,
                      "#version 330 core\nlayout(location=0)in vec2 "
                      "xy;layout(location=1)in vec2 uv;out vec2 t;void "
                      "main(){t=uv;gl_Position=vec4(xy,0,1);}");
      auto f = shader(
          GL_FRAGMENT_SHADER,
          "#version 330 core\nin vec2 t;out vec4 color;uniform sampler2D "
          "image;void main(){color=texture(image,clamp(t,vec2(0),vec2(1)));}");
      program = glCreateProgram();
      glAttachShader(program, v);
      glAttachShader(program, f);
      glLinkProgram(program);
      glDeleteShader(v);
      glDeleteShader(f);
      GLint ok;
      glGetProgramiv(program, GL_LINK_STATUS, &ok);
      if (!ok)
        throw std::runtime_error("Cannot link stereo surface shader");
      glGenVertexArrays(1, &vao);
      glGenBuffers(1, &vbo);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            (void *)(2 * sizeof(float)));
      glGenFramebuffers(1, &fbo);
      glGenTextures(1, &color);
      glGenRenderbuffers(1, &depth);
    }
    if (width == w && height == h)
      return;
    width = w;
    height = h;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("Incomplete model stereo framebuffer");
  }
  void composite(const Mat4 &vp,
                 const std::function<Vec3(float, float)> &surface, int eye,
                 int w, int h) {
    std::vector<float> mesh;
    mesh.reserve(32 * 32 * 6 * 4);
    auto vertex = [&](float u, float v) {
      auto p = point(vp, surface(u, v));
      mesh.insert(mesh.end(),
                  {-1.f + eye + u, 1 - 2 * v, (p.x + 1) / 2, (p.y + 1) / 2});
    };
    for (int row = 0; row < 32; ++row)
      for (int col = 0; col < 32; ++col) {
        float u = col / 32.f, v = row / 32.f, a = (col + 1) / 32.f,
              b = (row + 1) / 32.f;
        vertex(u, v);
        vertex(a, v);
        vertex(a, b);
        vertex(u, v);
        vertex(a, b);
        vertex(u, b);
      }
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(float), mesh.data(),
                 GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, color);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glDrawArrays(GL_TRIANGLES, 0, mesh.size() / 4);
  }
};
ModelView::ModelView(int x, int y, int w, int h,
                     std::shared_ptr<ModelCamera> value, bool sbs)
    : Fl_Gl_Window(x, y, w, h), targets(std::make_unique<Targets>()),
      camera(std::move(value)), stereo(sbs) {
  mode(FL_RGB | FL_ALPHA | FL_DOUBLE | FL_DEPTH | FL_OPENGL3);
  end();
}
ModelView::~ModelView() {
  if (context()) {
    make_current();
    renderer.clear();
    targets->release();
  } else {
    renderer.abandon();
    targets->abandon();
  }
}
void ModelView::setModel(std::shared_ptr<const Model> value) {
  if (model == value)
    return;
  if (context()) {
    make_current();
    renderer.clear();
  } else
    renderer.abandon();
  model = std::move(value);
  error.clear();
  redraw();
}
void ModelView::reset() {
  camera->reset();
  error.clear();
  redraw();
}
void ModelView::draw() {
  if (!context_valid()) {
    renderer.abandon();
    targets->abandon();
    context_valid(1);
  }
  unsigned char r, g, b;
  Fl::get_color(FL_BACKGROUND2_COLOR, r, g, b);
  glViewport(0, 0, pixel_w(), pixel_h());
  glClearColor(r / 255.f, g / 255.f, b / 255.f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!model)
    return;
  GLint original = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &original);
  try {
    renderer.prepare(model, camera->seconds, camera->animation);
    if (stereo)
      drawStereo();
    else {
      float up = .45f, right = std::atan(std::tan(up) * float(pixel_w()) /
                                         std::max(1, pixel_h()));
      Pose eye{{camera->panX, camera->panY, camera->distance}, {}, 1};
      auto rotation =
          axis(camera->yaw, {0, 1, 0}) * axis(camera->pitch, {1, 0, 0});
      auto root = trs({}, rotation) * model->bounds.fit();
      auto projection = perspective(-right, right, -up, up);
      if (model->stereoReference && camera->reference) {
        auto ref = *model->stereoReference;
        float aspect = float(pixel_w()) / std::max(1, pixel_h());
        float height = std::max(ref.height, ref.width / aspect),
              width = height * aspect;
        eye.position = {-ref.baseline / 2 + camera->panX, camera->panY,
                        ref.distance * camera->distance / 1.5f};
        projection = perspective(
            std::atan((-width / 2 + ref.baseline / 2) / ref.distance),
            std::atan((width / 2 + ref.baseline / 2) / ref.distance),
            std::atan(-height / 2 / ref.distance),
            std::atan(height / 2 / ref.distance));
        auto center = model->bounds.center();
        root = trs(center, rotation) * trs(center * -1, {});
      }
      renderer.draw(projection * matrix(inverse(eye)), root, eye.position,
                    pixel_w(), pixel_h(), {r / 255.f, g / 255.f, b / 255.f, 1});
    }
    error.clear();
    ++frames;
  } catch (const std::exception &e) {
    error = e.what();
  }
  glBindFramebuffer(GL_FRAMEBUFFER, original);
  glUseProgram(0);
  glBindVertexArray(0);
}
void ModelView::drawStereo() {
  using namespace qimgvr_model;
  unsigned char bgR,bgG,bgB;
  Fl::get_color(FL_BACKGROUND2_COLOR,bgR,bgG,bgB);
  GLint original = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &original);
  int pw = std::max(2, pixel_w()), ph = std::max(1, pixel_h());
  targets->prepare(pw, ph);
  float distance =
      presentation.hasWindowQuad ? presentation.pulloutDistance : 1.25f;
  float width = presentation.hasWindowQuad ? presentation.windowSize[0] : 1.3f;
  float height =
      presentation.hasWindowQuad ? presentation.windowSize[1] : width * ph / pw;
  CylinderSurface cylinder;
  Pose tangent, panel;
  if (presentation.hasWindowQuad) {
    auto p = presentation.windowPosition;
    auto q = presentation.windowOrientation;
    panel = {{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, 1};
  }
  if (presentation.hasCylinder) {
    auto p = presentation.cylinderPosition;
    auto q = presentation.cylinderOrientation;
    cylinder = {{{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, 1},
                presentation.cylinderRadius,
                presentation.cylinderHorizontal,
                presentation.cylinderTop,
                presentation.cylinderBottom};
    tangent = cylinder.tangent(.5f, .5f);
  }
  auto surface = [&](float u, float v) {
    if (!presentation.hasCylinder)
      return Vec3{(u - .5f) * width, 1.6f + (.5f - v) * height, -distance};
    auto local = transform(inverse(tangent), cylinder.point(u, v));
    return Vec3{local.x, 1.6f + local.y, local.z - distance};
  };
  float scale = .82f * std::min(width, height) * 1.5f / camera->distance;
  auto root =
      trs({-camera->panX * scale, 1.6f - camera->panY * scale, -distance},
          axis(camera->yaw, {0, 1, 0}) * axis(camera->pitch, {1, 0, 0}),
          {scale, scale, scale}) *
      model->bounds.fit();
  if (model->stereoReference && camera->reference) {
    auto ref = *model->stereoReference;
    float s = .96f * std::min(width / ref.width, height / ref.height) * 1.5f /
              camera->distance;
    auto center = model->bounds.center();
    auto rotation =
        axis(camera->yaw, {0, 1, 0}) * axis(camera->pitch, {1, 0, 0});
    root = trs({-camera->panX * s, 1.6f - camera->panY * s, -distance}, {},
               {s, s, s}) *
           trs(center, rotation) * trs(center * -1, {});
  }
  for (int eye = 0; eye < 2; ++eye) {
    Vec3 relative{eye ? .032f : -.032f, 0, 0};
    if (presentation.hasWindowQuad) {
      auto e = presentation.eyes[eye];
      Vec3 world{e[0], e[1], e[2]};
      if (!presentation.trackedEyes) {
        auto p = presentation.headPosition;
        auto q = presentation.headOrientation;
        world = transform(Pose{{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, 1},
                          {eye ? .032f : -.032f, 0, 0});
      }
      auto local =
          transform(inverse(presentation.hasCylinder ? tangent : panel), world);
      relative = {local.x, local.y, local.z - distance};
    }
    auto vp = windowEyeProjection(relative, width, height, distance);
    Vec3 position{relative.x, 1.6f + relative.y, relative.z};
    if (presentation.hasCylinder) {
      float l = 1e9f, r = -1e9f, d = 1e9f, u = -1e9f;
      for (int y = 0; y <= 8; ++y)
        for (int x = 0; x <= 32; ++x) {
          auto ray = surface(x / 32.f, y / 8.f) - position;
          if (ray.z >= -.02f)
            continue;
          l = std::min(l, ray.x / -ray.z);
          r = std::max(r, ray.x / -ray.z);
          d = std::min(d, ray.y / -ray.z);
          u = std::max(u, ray.y / -ray.z);
        }
      if (!(l < r && d < u))
        throw std::runtime_error("Model window is behind the viewer");
      vp = perspective(std::atan(l), std::atan(r), std::atan(d), std::atan(u)) *
           matrix(inverse(Pose{position, {}, 1}));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targets->fbo);
    renderer.draw(vp, root, position, pw, ph,{bgR/255.f,bgG/255.f,bgB/255.f,1});
    glBindFramebuffer(GL_FRAMEBUFFER, original);
    targets->composite(vp, surface, eye, pw, ph);
  }
  ++stereoFrames;
}
int ModelView::handle(int event) {
  if (event == FL_PUSH) {
    take_focus();
    button = Fl::event_button();
    downX = Fl::event_x();
    downY = Fl::event_y();
    return 1;
  }
  if (event == FL_DRAG) {
    int dx = Fl::event_x() - downX, dy = Fl::event_y() - downY;
    downX = Fl::event_x();
    downY = Fl::event_y();
    if (button == FL_MIDDLE_MOUSE || Fl::event_state(FL_SHIFT)) {
      camera->panX -= dx * camera->distance / std::max(1, w());
      camera->panY += dy * camera->distance / std::max(1, h());
    } else {
      camera->orbit = false;
      camera->yaw += dx * .01f;
      camera->pitch = std::clamp(camera->pitch + dy * .01f, -1.5f, 1.5f);
    }
    redraw();
    return 1;
  }
  if (event == FL_RELEASE) {
    button = 0;
    return 1;
  }
  if (event == FL_MOUSEWHEEL) {
    camera->distance = std::clamp(
        camera->distance * std::exp(Fl::event_dy() * .12f), .15f, 50.f);
    redraw();
    return 1;
  }
  if ((event == FL_KEYDOWN || event == FL_SHORTCUT) && Fl::event_key() == 'r') {
    reset();
    return 1;
  }
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  return Fl_Gl_Window::handle(event);
}
bool ModelView::snapshot(const std::filesystem::path &path) {
  make_current();
  draw();
  std::vector<unsigned char> data(size_t(pixel_w()) * pixel_h() * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, pixel_w(), pixel_h(), GL_RGB, GL_UNSIGNED_BYTE,
               data.data());
  std::ofstream file(path, std::ios::binary);
  file << "P6\n" << pixel_w() << ' ' << pixel_h() << "\n255\n";
  for (int y = pixel_h() - 1; y >= 0; --y)
    file.write((char *)data.data() + size_t(y) * pixel_w() * 3, pixel_w() * 3);
  return bool(file);
}
ModelPresenter::ModelPresenter(
    std::shared_ptr<ModelCamera> camera,
    std::function<void(const std::string &)> callback)
    : ModelView(0, 0, 1000, 700, std::move(camera), true),
      command(std::move(callback)) {
  xclass("org.qimgvr.Sbs");
  copy_label("qimgvr — glTF stereo view");
  size_range(300, 200);
  menu.hide();
  for (auto label :
       {"File/Previous", "File/Next", "File/Open...", "File/Metadata",
        "File/Close stereo view", "View/Reset model camera",
        "Model/Play or pause animation", "Model/Next animation"})
    menu.add(
        label,
        std::string(label) == "File/Previous"                   ? FL_Left
        : std::string(label) == "File/Next"                     ? FL_Right
        : std::string(label) == "File/Open..."                  ? FL_CTRL + 'o'
        : std::string(label) == "File/Metadata"                 ? 'i'
        : std::string(label) == "View/Reset model camera"       ? 'r'
        : std::string(label) == "Model/Play or pause animation" ? ' '
                                                                : 0,
        [](Fl_Widget *w, void *p) {
          auto self = static_cast<ModelPresenter *>(p);
          auto item = static_cast<Fl_Menu_ *>(w)->mvalue();
          if (item)
            self->command(item->label());
        },
        this);
  desktop = std::make_unique<DesktopMenu>(*this, menu);
  this->callback(
      [](Fl_Widget *, void *p) {
        // The presenter is the model's XR document window. A native window
        // close must close its owner too, rather than leave a loaded mono
        // window behind for the launcher to reactivate on another workspace.
        // Escape and the explicit stereo-only menu action still disable stereo.
        static_cast<ModelPresenter *>(p)->command("Quit");
      },
      this);
}
int ModelPresenter::handle(int event) {
  if ((event == FL_SHORTCUT || event == FL_KEYDOWN) &&
      Fl::event_key() == FL_Escape) {
    command("Close stereo view");
    return 1;
  }
  if (event == FL_SHORTCUT && menu.handle(event))
    return 1;
  if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
    Fl_Menu_Button popup(Fl::event_x(), Fl::event_y(), 0, 0);
    popup.menu(menu.menu());
    popup.popup();
    return 1;
  }
  return ModelView::handle(event);
}
} // namespace qimgvr
