/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * https://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "guiapplication.h"

#include "apptoolbox.h"
#include "library/librariesmodel.h"
#include "mainwindow.h"
#include "project/projecteditor.h"
#include "project/projectsmodel.h"
#include "workspace/favoriteprojectsmodel.h"
#include "workspace/filesystemmodel.h"
#include "workspace/recentprojectsmodel.h"

#include <librepcb/core/application.h>
#include <librepcb/core/types/lengthunit.h>
#include <librepcb/core/workspace/workspace.h>
#include <librepcb/core/workspace/workspacelibrarydb.h>
#include <slint-platform.h>

#include <QtCore>

QT_BEGIN_NAMESPACE

class QOpenGLContext;
class QWindow;
class QPlatformWindow;
class QBackingStore;

class Q_GUI_EXPORT QPlatformNativeInterface : public QObject {
public:
  virtual void* nativeResourceForIntegration(const QByteArray& resource);
  virtual void* nativeResourceForContext(const QByteArray& resource,
                                         QOpenGLContext* context);
  virtual void* nativeResourceForWindow(const QByteArray& resource,
                                        QWindow* window);
  virtual void* nativeResourceForBackingStore(const QByteArray& resource,
                                              QBackingStore* backingStore);

  typedef void* (*NativeResourceForIntegrationFunction)();
  typedef void* (*NativeResourceForContextFunction)(QOpenGLContext* context);
  typedef void* (*NativeResourceForWindowFunction)(QWindow* window);
  typedef void* (*NativeResourceForBackingStoreFunction)(
      QBackingStore* backingStore);
  virtual NativeResourceForIntegrationFunction
      nativeResourceFunctionForIntegration(const QByteArray& resource);
  virtual NativeResourceForContextFunction nativeResourceFunctionForContext(
      const QByteArray& resource);
  virtual NativeResourceForWindowFunction nativeResourceFunctionForWindow(
      const QByteArray& resource);
  virtual NativeResourceForBackingStoreFunction
      nativeResourceFunctionForBackingStore(const QByteArray& resource);

  virtual QVariantMap windowProperties(QPlatformWindow* window) const;
  virtual QVariant windowProperty(QPlatformWindow* window,
                                  const QString& name) const;
  virtual QVariant windowProperty(QPlatformWindow* window, const QString& name,
                                  const QVariant& defaultValue) const;
  virtual void setWindowProperty(QPlatformWindow* window, const QString& name,
                                 const QVariant& value);

Q_SIGNALS:
  void windowPropertyChanged(QPlatformWindow* window,
                             const QString& propertyName);
};

QT_END_NAMESPACE

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace editor {
namespace app {

static void update_timer() {
  static QTimer timer;
  static auto init = [] {
    timer.callOnTimeout([] {
      slint::platform::update_timers_and_animations();
      update_timer();
    });
    return true;
  }();
  if (auto timeout = slint::platform::duration_until_next_timer_update()) {
    timer.start(*timeout);
  } else {
    timer.stop();
  }
}

slint::PointerEventButton convert_button(Qt::MouseButtons b) {
  switch (b) {
    case Qt::LeftButton:
      return slint::PointerEventButton::Left;
    case Qt::RightButton:
      return slint::PointerEventButton::Right;
    case Qt::MiddleButton:
      return slint::PointerEventButton::Middle;
    default:
      return slint::PointerEventButton::Other;
  }
}

static slint::platform::NativeWindowHandle window_handle_for_qt_window(
    QWindow* window) {
  // Ensure that the native window surface exists
  window->create();
#ifdef __APPLE__
  QPlatformNativeInterface* native = qApp->platformNativeInterface();
  NSView* nsview = reinterpret_cast<NSView*>(
      native->nativeResourceForWindow(QByteArray("nsview"), window));
  NSWindow* nswindow = reinterpret_cast<NSWindow*>(
      native->nativeResourceForWindow(QByteArray("nswindow"), window));
  return slint::platform::NativeWindowHandle::from_appkit(nsview, nswindow);
#elif defined Q_OS_WIN
  auto wid = Qt::HANDLE(window->winId());
  return slint::platform::NativeWindowHandle::from_win32(
      wid, GetModuleHandle(nullptr));
#else
  // Try Wayland first, then XLib, then Xcb
  auto wid = window->winId();
  auto visual_id = 0;  // FIXME
  QPlatformNativeInterface* native = qApp->platformNativeInterface();
  auto screen = quintptr(
      native->nativeResourceForWindow(QByteArray("x11screen"), window));
  if (auto* wayland_display = reinterpret_cast<wl_display*>(
          native->nativeResourceForIntegration(QByteArray("wl_display")))) {
    auto* wayland_surface = reinterpret_cast<wl_surface*>(
        native->nativeResourceForWindow(QByteArray("surface"), window));
    return slint::platform::NativeWindowHandle::from_wayland(wayland_surface,
                                                             wayland_display);
  } else if (auto* x11_display = native->nativeResourceForWindow(
                 QByteArray("display"), window)) {
    return slint::platform::NativeWindowHandle::from_x11_xlib(
        wid, wid, x11_display, screen);
  } else if (auto* xcb_connection = reinterpret_cast<xcb_connection_t*>(
                 native->nativeResourceForWindow(QByteArray("connection"),
                                                 window))) {
    return slint::platform::NativeWindowHandle::from_x11_xcb(
        wid, wid, xcb_connection, screen);
  } else {
    throw "Unsupported windowing system (tried wayland, xlib, and xcb)";
  }
#endif
}

static slint::SharedString key_event_text(QKeyEvent* e) {
  switch (e->key()) {
    case Qt::Key::Key_Backspace:
      return slint::platform::key_codes::Backspace;
    case Qt::Key::Key_Tab:
      return slint::platform::key_codes::Tab;
    case Qt::Key::Key_Enter:
    case Qt::Key::Key_Return:
      return slint::platform::key_codes::Return;
    case Qt::Key::Key_Escape:
      return slint::platform::key_codes::Escape;
    case Qt::Key::Key_Backtab:
      return slint::platform::key_codes::Backtab;
    case Qt::Key::Key_Delete:
      return slint::platform::key_codes::Delete;
    case Qt::Key::Key_Shift:
      return slint::platform::key_codes::Shift;
    case Qt::Key::Key_Control:
      return slint::platform::key_codes::Control;
    case Qt::Key::Key_Alt:
      return slint::platform::key_codes::Alt;
    case Qt::Key::Key_AltGr:
      return slint::platform::key_codes::AltGr;
    case Qt::Key::Key_CapsLock:
      return slint::platform::key_codes::CapsLock;
    case Qt::Key::Key_Meta:
      return slint::platform::key_codes::Meta;
    case Qt::Key::Key_Up:
      return slint::platform::key_codes::UpArrow;
    case Qt::Key::Key_Down:
      return slint::platform::key_codes::DownArrow;
    case Qt::Key::Key_Left:
      return slint::platform::key_codes::LeftArrow;
    case Qt::Key::Key_Right:
      return slint::platform::key_codes::RightArrow;
    case Qt::Key::Key_F1:
      return slint::platform::key_codes::F1;
    case Qt::Key::Key_F2:
      return slint::platform::key_codes::F2;
    case Qt::Key::Key_F3:
      return slint::platform::key_codes::F3;
    case Qt::Key::Key_F4:
      return slint::platform::key_codes::F4;
    case Qt::Key::Key_F5:
      return slint::platform::key_codes::F5;
    case Qt::Key::Key_F6:
      return slint::platform::key_codes::F6;
    case Qt::Key::Key_F7:
      return slint::platform::key_codes::F7;
    case Qt::Key::Key_F8:
      return slint::platform::key_codes::F8;
    case Qt::Key::Key_F9:
      return slint::platform::key_codes::F9;
    case Qt::Key::Key_F10:
      return slint::platform::key_codes::F10;
    case Qt::Key::Key_F11:
      return slint::platform::key_codes::F11;
    case Qt::Key::Key_F12:
      return slint::platform::key_codes::F12;
    case Qt::Key::Key_F13:
      return slint::platform::key_codes::F13;
    case Qt::Key::Key_F14:
      return slint::platform::key_codes::F14;
    case Qt::Key::Key_F15:
      return slint::platform::key_codes::F15;
    case Qt::Key::Key_F16:
      return slint::platform::key_codes::F16;
    case Qt::Key::Key_F17:
      return slint::platform::key_codes::F17;
    case Qt::Key::Key_F18:
      return slint::platform::key_codes::F18;
    case Qt::Key::Key_F19:
      return slint::platform::key_codes::F19;
    case Qt::Key::Key_F20:
      return slint::platform::key_codes::F20;
    case Qt::Key::Key_F21:
      return slint::platform::key_codes::F21;
    case Qt::Key::Key_F22:
      return slint::platform::key_codes::F22;
    case Qt::Key::Key_F23:
      return slint::platform::key_codes::F23;
    case Qt::Key::Key_F24:
      return slint::platform::key_codes::F24;
    case Qt::Key::Key_Insert:
      return slint::platform::key_codes::Insert;
    case Qt::Key::Key_Home:
      return slint::platform::key_codes::Home;
    case Qt::Key::Key_End:
      return slint::platform::key_codes::End;
    case Qt::Key::Key_PageUp:
      return slint::platform::key_codes::PageUp;
    case Qt::Key::Key_PageDown:
      return slint::platform::key_codes::PageDown;
    case Qt::Key::Key_ScrollLock:
      return slint::platform::key_codes::ScrollLock;
    case Qt::Key::Key_Pause:
      return slint::platform::key_codes::Pause;
    case Qt::Key::Key_SysReq:
      return slint::platform::key_codes::SysReq;
    case Qt::Key::Key_Stop:
      return slint::platform::key_codes::Stop;
    case Qt::Key::Key_Menu:
      return slint::platform::key_codes::Menu;
    default:
      if (e->modifiers() & Qt::ControlModifier) {
        // e->text() is not the key when Ctrl is pressed
        return QKeySequence(e->key()).toString().toLower().toUtf8().data();
      }
      return e->text().toUtf8().data();
  }
}

class MyWindow : public QWindow, public slint::platform::WindowAdapter {
  std::optional<slint::platform::SkiaRenderer> m_renderer;
  bool m_visible = false;

public:
  MyWindow(QWindow* parentWindow = nullptr) : QWindow(parentWindow) {
    resize(640, 480);
    m_renderer.emplace(window_handle_for_qt_window(this), size());
  }

  slint::platform::AbstractRenderer& renderer() override {
    return m_renderer.value();
  }

  void paintEvent(QPaintEvent* ev) override {
    slint::platform::update_timers_and_animations();

    m_renderer->render();

    if (window().has_active_animations()) {
      requestUpdate();
    }
    update_timer();
  }

  void closeEvent(QCloseEvent* event) override {
    if (m_visible) {
      event->ignore();
      window().dispatch_close_requested_event();
    }
  }

  bool event(QEvent* e) override {
    if (e->type() == QEvent::UpdateRequest) {
      paintEvent(static_cast<QPaintEvent*>(e));
      return true;
    } else if (e->type() == QEvent::KeyPress) {
      auto ke = static_cast<QKeyEvent*>(e);
      if (ke->isAutoRepeat())
        window().dispatch_key_press_repeat_event(key_event_text(ke));
      else
        window().dispatch_key_press_event(key_event_text(ke));
      return true;
    } else if (e->type() == QEvent::KeyRelease) {
      window().dispatch_key_release_event(
          key_event_text(static_cast<QKeyEvent*>(e)));
      return true;
    } else if (e->type() == QEvent::WindowActivate) {
      window().dispatch_window_active_changed_event(true);
      return true;
    } else if (e->type() == QEvent::WindowDeactivate) {
      window().dispatch_window_active_changed_event(false);
      return true;
    } else {
      return QWindow::event(e);
    }
  }

  void set_visible(bool visible) override {
    m_visible = visible;
    if (visible) {
      window().dispatch_scale_factor_change_event(devicePixelRatio());
      QWindow::show();
    } else {
      QWindow::close();
    }
  }

  void set_size(slint::PhysicalSize size) override {
    float scale_factor = devicePixelRatio();
    resize(size.width / scale_factor, size.height / scale_factor);
  }

  slint::PhysicalSize size() override {
    auto windowSize = slint::LogicalSize({float(width()), float(height())});
    float scale_factor = devicePixelRatio();
    return slint::PhysicalSize({uint32_t(windowSize.width * scale_factor),
                                uint32_t(windowSize.height * scale_factor)});
  }

  void set_position(slint::PhysicalPosition position) override {
    float scale_factor = devicePixelRatio();
    setFramePosition(
        QPointF(position.x / scale_factor, position.y / scale_factor)
            .toPoint());
  }

  std::optional<slint::PhysicalPosition> position() override {
    auto pos = framePosition();
    float scale_factor = devicePixelRatio();
    return {slint::PhysicalPosition(
        {int32_t(pos.x() * scale_factor), int32_t(pos.y() * scale_factor)})};
  }

  void request_redraw() override { requestUpdate(); }

  void update_window_properties(const WindowProperties& props) override {
    QWindow::setTitle(QString::fromUtf8(props.title().data()));
    auto c = props.layout_constraints();
    QWindow::setMaximumSize(c.max ? QSize(c.max->width, c.max->height)
                                  : QSize(1 << 15, 1 << 15));
    QWindow::setMinimumSize(c.min ? QSize(c.min->width, c.min->height)
                                  : QSize());

    Qt::WindowStates states = windowState() & Qt::WindowActive;
    if (props.is_fullscreen()) states |= Qt::WindowFullScreen;
    if (props.is_minimized()) states |= Qt::WindowMinimized;
    if (props.is_maximized()) states |= Qt::WindowMaximized;
    setWindowStates(states);
  }

  void resizeEvent(QResizeEvent* ev) override {
    auto logicalSize = ev->size();
    window().dispatch_resize_event(slint::LogicalSize(
        {float(logicalSize.width()), float(logicalSize.height())}));
  }

  void mousePressEvent(QMouseEvent* event) override {
    slint::platform::update_timers_and_animations();
    window().dispatch_pointer_press_event(
        slint::LogicalPosition(
            {float(event->pos().x()), float(event->pos().y())}),
        convert_button(event->button()));
    update_timer();
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    slint::platform::update_timers_and_animations();
    window().dispatch_pointer_release_event(
        slint::LogicalPosition(
            {float(event->pos().x()), float(event->pos().y())}),
        convert_button(event->button()));
    update_timer();
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    slint::platform::update_timers_and_animations();
    window().dispatch_pointer_move_event(slint::LogicalPosition(
        {float(event->pos().x()), float(event->pos().y())}));
    update_timer();
  }
};

struct MyPlatform : public slint::platform::Platform {
  std::unique_ptr<QWindow> parentWindow;

  std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter()
      override {
    return std::make_unique<MyWindow>(parentWindow.get());
  }

  void set_clipboard_text(
      const slint::SharedString& str,
      slint::platform::Platform::Clipboard clipboard) override {
    switch (clipboard) {
      case slint::platform::Platform::Clipboard::DefaultClipboard:
        qApp->clipboard()->setText(QString::fromUtf8(str.data()),
                                   QClipboard::Clipboard);
        break;
      case slint::platform::Platform::Clipboard::SelectionClipboard:
        qApp->clipboard()->setText(QString::fromUtf8(str.data()),
                                   QClipboard::Selection);
        break;
    }
  }

  std::optional<slint::SharedString> clipboard_text(
      Clipboard clipboard) override {
    QString text;
    switch (clipboard) {
      case slint::platform::Platform::Clipboard::DefaultClipboard:
        text = qApp->clipboard()->text(QClipboard::Clipboard);
        break;
      case slint::platform::Platform::Clipboard::SelectionClipboard:
        text = qApp->clipboard()->text(QClipboard::Selection);
        break;
      default:
        return {};
    }
    if (text.isNull()) {
      return {};
    } else {
      return slint::SharedString(text.toUtf8().data());
    }
  }
};

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

GuiApplication::GuiApplication(Workspace& ws, QObject* parent) noexcept
  : QObject(parent),
    mWorkspace(ws),
    mRecentProjects(new RecentProjectsModel(ws, this)),
    mFavoriteProjects(new FavoriteProjectsModel(ws, this)),
    mLibraries(new LibrariesModel(ws, this)),
    mProjects(new ProjectsModel(this)) {
  mWorkspace.getLibraryDb().startLibraryRescan();
  createNewWindow();
}

GuiApplication::~GuiApplication() noexcept {
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void GuiApplication::exec() {
  slint::run_event_loop();
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void GuiApplication::createNewWindow() noexcept {
  static MyPlatform* platform = [] {
    auto platform = std::make_unique<MyPlatform>();
    auto p2 = platform.get();
    slint::platform::set_platform(std::move(platform));
    return p2;
  }();

  slint::platform::update_timers_and_animations();

  // Create Slint window.
  auto win = ui::AppWindow::create();
  win->set_window_title(
      QString("LibrePCB %1").arg(Application::getVersion()).toUtf8().data());
  win->set_workspace_path(mWorkspace.getPath().toNative().toUtf8().data());
  win->on_close([&] { slint::quit_event_loop(); });

  // Set static data.
  const ui::Globals& globals = win->global<ui::Globals>();
  globals.set_preview_mode(false);

  // Register global callbacks.
  globals.on_menu_item_triggered(
      [this](ui::MenuItemId id) { menuItemTriggered(id); });
  globals.on_parse_length_input(
      [](slint::SharedString text, slint::SharedString unit) {
        ui::EditParseResult res{false, text, unit};
        try {
          QString value = text.begin();
          foreach (const LengthUnit& unit, LengthUnit::getAllUnits()) {
            foreach (const QString& suffix, unit.getUserInputSuffixes()) {
              if (value.endsWith(suffix)) {
                value.chop(suffix.length());
                res.evaluated_unit = unit.toShortStringTr().toStdString();
              }
            }
          }
          Length l = Length::fromMm(value);
          value = l.toMmString();
          if (value.endsWith(".0")) {
            value.chop(2);
          }
          res.evaluated_value = value.toStdString();
          res.valid = true;
        } catch (const Exception& e) {
        }
        return res;
      });
  globals.on_ensure_libraries_populated(
      std::bind(&LibrariesModel::ensurePopulated, mLibraries.get()));
  globals.on_install_checked_libraries(
      std::bind(&LibrariesModel::installCheckedLibraries, mLibraries.get()));
  globals.on_uninstall_library(std::bind(&LibrariesModel::uninstallLibrary,
                                         mLibraries.get(),
                                         std::placeholders::_1));

  // Set models.
  globals.set_workspace_folder(std::make_shared<FileSystemModel>(
      mWorkspace, mWorkspace.getProjectsPath(), this));
  globals.set_recent_projects(mRecentProjects);
  globals.set_favorite_projects(mFavoriteProjects);
  globals.set_libraries(mLibraries);
  globals.set_open_projects(mProjects);

  // Bind global properties.
  bind(this, globals, &ui::Globals::set_status_bar_progress,
       &mWorkspace.getLibraryDb(), &WorkspaceLibraryDb::scanProgressUpdate, 0);
  bind(this, globals, &ui::Globals::set_outdated_libraries, mLibraries.get(),
       &LibrariesModel::outdatedLibrariesChanged,
       mLibraries->getOutdatedLibraries());
  bind(this, globals, &ui::Globals::set_checked_libraries, mLibraries.get(),
       &LibrariesModel::checkedLibrariesChanged,
       mLibraries->getCheckedLibraries());
  bind(this, globals, &ui::Globals::set_refreshing_available_libraries,
       mLibraries.get(), &LibrariesModel::fetchingRemoteLibrariesChanged,
       mLibraries->isFetchingRemoteLibraries());

  // Build wrapper.
  mWindows.append(
      std::make_shared<MainWindow>(*this, win, mWindows.count(), this));
}

void GuiApplication::menuItemTriggered(ui::MenuItemId id) noexcept {
  switch (id) {
    case ui::MenuItemId::NewWindow:
      createNewWindow();
      break;
    default:
      qWarning() << "Unknown menu item triggered:" << static_cast<int>(id);
      break;
  }
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace app
}  // namespace editor
}  // namespace librepcb
