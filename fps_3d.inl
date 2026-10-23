// GPU scene and marker use one double-buffered OpenGL child surface.
// Display lists/textures are built once. No GPU readback or full-frame uploads.
namespace gpu {
struct State {
  HWND window = nullptr;
  HDC dc = nullptr;
  HGLRC context = nullptr;
  GLuint list = 0, textures[4]{};
  int scene = -1, pressure = -1;
  bool failed = false, software = false;
  world::Mesh mesh = world::Build();
  world::Camera camera{};
  LONGLONG movementAt = 0;
} state;
void Release() {
  if (state.context) {
    wglMakeCurrent(state.dc, state.context);
    if (state.list)
      glDeleteLists(state.list, 1);
    glDeleteTextures(4, state.textures);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(state.context);
  }
  if (state.dc)
    ReleaseDC(state.window, state.dc);
  if (state.window && IsWindow(state.window))
    DestroyWindow(state.window);
  state.window = nullptr;
  state.dc = nullptr;
  state.context = nullptr;
  state.list = 0;
  state.scene = -1;
  std::fill_n(state.textures, 4, 0U);
}
LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_LBUTTONDOWN) {
    SetFocus(g.hwnd);
    SendMessageW(g.hwnd, message, wp, lp);
    return 0;
  }
  if (message == WM_SETCURSOR && g.fpsCaptured) {
    SetCursor(nullptr);
    return TRUE;
  }
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    InvalidateRect(g.hwnd, nullptr, FALSE);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}
bool BindSurface(const RECT &scene) {
  RECT current{};
  if (!GetWindowRect(state.window, &current)) return false;
  POINT origin{current.left, current.top};
  if (!ScreenToClient(g.hwnd, &origin)) return false;
  if (scene.right <= scene.left || scene.bottom <= scene.top)
    return false;
  const bool changed = origin.x != scene.left || origin.y != scene.top ||
      current.right - current.left != scene.right - scene.left ||
      current.bottom - current.top != scene.bottom - scene.top;
  if (changed || !IsWindowVisible(state.window)) {
    // Release the current drawable before the driver resizes its buffers.
    // Defer/disable window-manager painting and old-bitmap copying here;
    // the surrounding frame will paint the newly sized surface itself.
    if (!wglMakeCurrent(nullptr, nullptr)) return false;
    UINT flags=SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW |
               SWP_NOCOPYBITS | SWP_DEFERERASE;
    if (!IsWindowVisible(state.window)) flags |= SWP_SHOWWINDOW;
    if (!SetWindowPos(state.window, nullptr, scene.left, scene.top,
                      scene.right-scene.left, scene.bottom-scene.top, flags))
      return false;
  }
  return wglMakeCurrent(state.dc, state.context) != FALSE;
}
bool Ensure(const RECT &scene) {
  if (state.failed)
    return false;
  if (!state.window) {
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = Proc;
    wc.hInstance = g.instance;
    wc.lpszClassName = L"CloudLatencyGPU3D";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    state.window =
        CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0,
                        0, 1, 1, g.hwnd, nullptr, g.instance, nullptr);
    if (!state.window) {
      state.failed = true;
      return false;
    }
    state.dc = GetDC(state.window);
    PIXELFORMATDESCRIPTOR pf{};
    pf.nSize = sizeof(pf);
    pf.nVersion = 1;
    pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pf.iPixelType = PFD_TYPE_RGBA;
    pf.cColorBits = 24;
    pf.cDepthBits = 24;
    const int format = ChoosePixelFormat(state.dc, &pf);
    if (!format || !SetPixelFormat(state.dc, format, &pf) ||
        !(state.context = wglCreateContext(state.dc)) ||
        !wglMakeCurrent(state.dc, state.context)) {
      Release();
      state.failed = true;
      return false;
    }
    PIXELFORMATDESCRIPTOR actual{};
    DescribePixelFormat(state.dc, format, sizeof(actual), &actual);
    state.software = (actual.dwFlags & PFD_GENERIC_FORMAT) &&
                     !(actual.dwFlags & PFD_GENERIC_ACCELERATED);
    // Keep the existing rate scheduler authoritative, when the driver supports
    // it.
    using SwapInterval = BOOL(WINAPI *)(int);
    const auto address = wglGetProcAddress("wglSwapIntervalEXT");
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (value > 3 && value != static_cast<std::uintptr_t>(-1))
      reinterpret_cast<SwapInterval>(address)(0);
  }
  return BindSurface(scene);
}

void Hide() {
  if (state.window && IsWindowVisible(state.window))
    ShowWindow(state.window, SW_HIDE);
}
void ResetCamera() {
  state.camera = {};
  state.movementAt = 0;
}
void Move(LONGLONG now) {
  const double dt = state.movementAt
                        ? TicksToMilliseconds(now - state.movementAt) / 1000.
                        : 0;
  state.movementAt = now;
  if (!g.fpsManual || !g.fpsCaptured || g.fpsScene >= 2 ||
      GetForegroundWindow() != g.hwnd)
    return;
  const auto down = [](int key) {
    return (GetAsyncKeyState(key) & 0x8000) ? 1. : 0.;
  };
  world::Move(state.mesh, state.camera, g.fpsYaw, down('W') - down('S'),
              down('D') - down('A'), dt);
}
void Build(int scene) {
  if (state.scene == scene && state.pressure == g.fpsPressure && state.list)
    return;
  if (state.list)
    glDeleteLists(state.list, 1);
  glDeleteTextures(4, state.textures);
  glGenTextures(4, state.textures);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const int pressure = g.fpsPressure;
  state.mesh = world::BuildPressure(pressure);
  const int size = pressure ? 1024 : 256;
  std::vector<unsigned char> pixels(size * size * 3);
  for (int material = 0; material < 4; ++material) {
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const unsigned h =
            fps::StressHash(static_cast<unsigned>(x), static_cast<unsigned>(y));
        int r = 0, gr = 0, b = 0;
        if (scene == 0) {
          const bool line = x % 64 < 3 || y % 64 < 3;
          const bool check = ((x / 64 + y / 64) & 1) != 0;
          r = gr = b = line ? 245 : (check ? 95 : 45);
        } else {
          const int grain = static_cast<int>(h & 31) - 15;
          if (material == 0) {
            const bool line = x % 64 < 2 || y % 64 < 2;
            const int tile = ((x / 64 + y / 64) & 1) * 25;
            r = line ? 35 : 115 + grain + tile;
            gr = line ? 39 : 125 + grain + tile;
            b = line ? 43 : 132 + grain + tile;
          } else if (material == 1) {
            const bool mortar = (x + (y / 32 % 2) * 32) % 64 < 3 || y % 32 < 3;
            r = mortar ? 48 : 158 + grain;
            gr = mortar ? 52 : 109 + grain;
            b = mortar ? 57 : 79 + grain;
          } else if (material == 2) {
            const bool edge = x < 9 || x > 246 || y < 9 || y > 246 ||
                              std::abs(x - y) < 4 || std::abs(x + y - 255) < 4;
            r = edge ? 48 : 107 + grain;
            gr = edge ? 55 : 135 + grain;
            b = edge ? 60 : 116 + grain;
          } else {
            const bool stripe = ((x + y) / 28) & 1;
            r = stripe ? 230 + grain : 32;
            gr = stripe ? 177 + grain : 38;
            b = stripe ? 44 + grain : 45;
          }
        }
        const int i = (y * size + x) * 3;
        pixels[i] = static_cast<unsigned char>(std::clamp(r, 0, 255));
        pixels[i + 1] = static_cast<unsigned char>(std::clamp(gr, 0, 255));
        pixels[i + 2] = static_cast<unsigned char>(std::clamp(b, 0, 255));
        if (pressure)
          detail::Pixel(scene, pressure, material, static_cast<unsigned>(x),
                        static_cast<unsigned>(y), 0, pixels.data() + i);
      }
    glBindTexture(GL_TEXTURE_2D, state.textures[material]);
    // Box-filtered mip pyramid keeps detailed receding surfaces stable.
    int level = 0, side = size;
    auto mip = pixels;
    while (true) {
      if (pressure && level > 0)
        for (int y = 0; y < side; ++y)
          for (int x = 0; x < side; ++x)
            detail::Pixel(scene, pressure, material, static_cast<unsigned>(x),
                          static_cast<unsigned>(y), level,
                          mip.data() + (y * side + x) * 3);
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, side, side, 0, GL_RGB,
                   GL_UNSIGNED_BYTE, mip.data());
      if (side == 1)
        break;
      const int next = side / 2;
      std::vector<unsigned char> small(next * next * 3);
      for (int y = 0; y < next; ++y)
        for (int x = 0; x < next; ++x)
          for (int c = 0; c < 3; ++c) {
            int sum = 0;
            for (int dy = 0; dy < 2; ++dy)
              for (int dx = 0; dx < 2; ++dx)
                sum += mip[((y * 2 + dy) * side + x * 2 + dx) * 3 + c];
            small[(y * next + x) * 3 + c] = static_cast<unsigned char>(sum / 4);
          }
      mip.swap(small);
      side = next;
      ++level;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    pressure == 2 ? GL_NEAREST
                                  : (pressure == 1 ? GL_NEAREST_MIPMAP_NEAREST
                                                   : GL_LINEAR_MIPMAP_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    pressure ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  state.list = glGenLists(1);
  glNewList(state.list, GL_COMPILE);
  for (const auto &face : state.mesh.faces) {
    glBindTexture(GL_TEXTURE_2D, state.textures[face.material]);
    glColor3f(face.light, face.light, face.light);
    glBegin(GL_QUADS);
    for (const auto &v : face.v) {
      glTexCoord2f(v.u, v.v);
      glVertex3f(v.x, v.y, v.z);
    }
    glEnd();
  }
  glEndList();
  state.scene = scene;
  state.pressure = pressure;
}
void Rect(double x0, double y0, double x1, double y1) {
  glVertex2d(x0, y0);
  glVertex2d(x1, y0);
  glVertex2d(x1, y1);
  glVertex2d(x0, y1);
}
bool Paint(const RECT &scene, const RECT &code) {
  if (!Ensure(scene))
    return false;
  Build(g.fpsScene);
  glViewport(0, 0, scene.right - scene.left, scene.bottom - scene.top);
  glClearColor(.12f, .18f, .24f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  const double nearPlane = .08,
               halfHeight = nearPlane * std::tan(70. * world::kPi / 360.);
  const double aspect = static_cast<double>(scene.right - scene.left) /
                        std::max(1L, scene.bottom - scene.top);
  glFrustum(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight,
            nearPlane, 100.);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glRotated(g.fpsPose.pitch, 1, 0, 0);
  glRotated(g.fpsPose.yaw, 0, 1, 0);
  glTranslated(-state.camera.x, -1.7, -state.camera.z);
  glCallList(state.list);
  glDisable(GL_DITHER);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_TEXTURE_2D);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(scene.left, scene.right, scene.bottom, scene.top, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  // Crosshair and exact marker pixels are unfiltered HUD quads in this same
  // swap.
  const double x = (scene.left + scene.right) / 2.,
               y = (scene.top + scene.bottom) / 2.;
  const double arm = Scale(14), thick = std::max(Scale(1), 1);
  glBegin(GL_QUADS);
  glColor3ub(0, 0, 0);
  Rect(x - arm - 2, y - thick - 2, x + arm + 3, y + thick + 3);
  Rect(x - thick - 2, y - arm - 2, x + thick + 3, y + arm + 3);
  glColor3ub(255, 255, 255);
  Rect(x - arm, y - thick, x + arm + 1, y + thick + 1);
  Rect(x - thick, y - arm, x + thick + 1, y + arm + 1);
  glColor3ub(17, 22, 31);
  Rect(code.left - 3, code.top - 3, code.right + 3, code.bottom + 3);
  for (int i = 0; i < fps::kFields; ++i) {
    const int tileX = code.left + (code.right - code.left) * (i % 4) / 4;
    const int tileY = code.top + (code.bottom - code.top) * (i / 4) / 2;
    const int cell = (code.right - code.left) / 32;
    const auto payload = MakePayload(TestMode::FpsView, g.fpsFields[i]);
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 8; ++col) {
        const bool white =
            row == 0 ? kSyncBits[col] != 0
                     : ((payload >> (23 - (row - 1) * 8 - col)) & 1) != 0;
        const unsigned char level = white ? 250 : 4;
        glColor3ub(level, level, level);
        Rect(tileX + col * cell, tileY + row * cell, tileX + (col + 1) * cell,
             tileY + (row + 1) * cell);
      }
  }
  glEnd();
  const bool success =
      glGetError() == GL_NO_ERROR && SwapBuffers(state.dc) != FALSE;
  return success;
}
} // namespace gpu
