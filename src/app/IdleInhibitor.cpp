#include "app/IdleInhibitor.h"

#include <QDebug>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

#ifdef OMAKADE_IDLE_INHIBIT
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformwindow_p.h>

#include <wayland-client.h>

#include "idle-inhibit-unstable-v1-client-protocol.h"

#include <cstring>
#endif

IdleInhibitor::IdleInhibitor(QWindow* window, QObject* parent) : QObject(parent), m_window(window) {
  if (window != nullptr) {
    // Qt destroys the wl_surface when a window hides and creates a fresh one when it shows
    // again, so the inhibitor has to follow the surface.
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
      if (!visible) {
        release();
      } else if (m_wanted) {
        apply();
      }
    });
  }
}

IdleInhibitor::~IdleInhibitor() {
  release();
#ifdef OMAKADE_IDLE_INHIBIT
  if (m_manager != nullptr) {
    zwp_idle_inhibit_manager_v1_destroy(m_manager);
    m_manager = nullptr;
  }
  if (m_registry != nullptr) {
    wl_registry_destroy(m_registry);
    m_registry = nullptr;
  }
#endif
}

bool IdleInhibitor::isSupported() const {
  return m_manager != nullptr;
}

bool IdleInhibitor::isInhibiting() const {
  return m_inhibitor != nullptr;
}

void IdleInhibitor::setInhibited(bool inhibited) {
  if (m_wanted == inhibited) {
    return;
  }
  m_wanted = inhibited;
  if (inhibited) {
    apply();
  } else {
    release();
  }
}

#ifdef OMAKADE_IDLE_INHIBIT

namespace {

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                    uint32_t version) {
  Q_UNUSED(version);
  if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) != 0) {
    return;
  }
  auto** manager = static_cast<zwp_idle_inhibit_manager_v1**>(data);
  if (*manager == nullptr) {
    *manager = static_cast<zwp_idle_inhibit_manager_v1*>(
        wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
  }
}

void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name) {
  Q_UNUSED(data);
  Q_UNUSED(registry);
  Q_UNUSED(name);
}

const wl_registry_listener kRegistryListener = {registryGlobal, registryGlobalRemove};

wl_display* waylandDisplay() {
  auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  return wayland == nullptr ? nullptr : wayland->display();
}

}  // namespace

void IdleInhibitor::initialize() {
  if (m_initialized) {
    return;
  }
  m_initialized = true;
  wl_display* display = waylandDisplay();
  if (display == nullptr) {
    return;
  }
  m_registry = wl_display_get_registry(display);
  if (m_registry == nullptr) {
    return;
  }
  wl_registry_add_listener(m_registry, &kRegistryListener, &m_manager);
  wl_display_roundtrip(display);
  if (m_manager == nullptr) {
    qInfo() << "The compositor does not offer idle-inhibit; the screensaver will not be held"
               " while games run";
  }
}

void IdleInhibitor::apply() {
  initialize();
  if (!m_wanted || m_manager == nullptr || m_inhibitor != nullptr || m_window.isNull()) {
    return;
  }
  auto* native = m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
  wl_surface* surface = native == nullptr ? nullptr : native->surface();
  if (surface == nullptr) {
    // The window has not been mapped yet; try again once Qt has created its surface.
    QTimer::singleShot(250, this, &IdleInhibitor::apply);
    return;
  }
  m_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(m_manager, surface);
  if (wl_display* display = waylandDisplay()) {
    wl_display_flush(display);
  }
}

void IdleInhibitor::release() {
  if (m_inhibitor == nullptr) {
    return;
  }
  zwp_idle_inhibitor_v1_destroy(m_inhibitor);
  m_inhibitor = nullptr;
  if (wl_display* display = waylandDisplay()) {
    wl_display_flush(display);
  }
}

#else

void IdleInhibitor::initialize() {
  m_initialized = true;
}

void IdleInhibitor::apply() {}

void IdleInhibitor::release() {}

#endif
