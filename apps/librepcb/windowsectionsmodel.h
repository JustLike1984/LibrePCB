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

#ifndef LIBREPCB_WINDOWSECTIONSMODEL_H
#define LIBREPCB_WINDOWSECTIONSMODEL_H

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "appwindow.h"

#include <QtCore>
#include <QtGui>

#include <memory>

/*******************************************************************************
 *  Namespace / Forward Declarations
 ******************************************************************************/
namespace librepcb {

class BoardPlaneFragmentsBuilder;

namespace editor {

class GraphicsScene;
class IF_GraphicsLayerProvider;
class OpenGlSceneBuilder;
class OpenGlView;

namespace app {

class GuiApplication;
class ProjectEditor;

/*******************************************************************************
 *  Class WindowSectionsModel
 ******************************************************************************/

/**
 * @brief The WindowSectionsModel class
 */
class WindowSectionsModel : public QObject,
                            public slint::Model<ui::WindowSection> {
  Q_OBJECT

public:
  // Types
  struct Tab {
    std::shared_ptr<ProjectEditor> project;
    ui::TabType type;
    int objIndex = -1;
    QPointF offset;
    qreal scale = 1;
    QRectF sceneRect;

    qreal projectionFov = 15;
    QPointF projectionCenter;
    QMatrix4x4 transform;
  };
  struct Section {
    ui::WindowSection uiData;

    QList<Tab> tabs;
    std::shared_ptr<GraphicsScene> scene;
    std::shared_ptr<OpenGlView> openGlView;
    std::shared_ptr<OpenGlSceneBuilder> openGlSceneBuilder;
    bool panning = false;
    QPointF startScenePos;

    QPointF mousePressPosition;
    QMatrix4x4 mousePressTransform;
    QPointF mousePressCenter;
    QSet<slint::private_api::PointerEventButton> buttons;

    Tab* getTab(int index) noexcept {
      return ((index >= 0) && (index < tabs.count())) ? &tabs[index] : nullptr;
    }
  };

  // Constructors / Destructor
  WindowSectionsModel() = delete;
  WindowSectionsModel(const WindowSectionsModel& other) = delete;
  explicit WindowSectionsModel(GuiApplication& app,
                               QObject* parent = nullptr) noexcept;
  virtual ~WindowSectionsModel() noexcept;

  // General Methods
  void openSchematic(std::shared_ptr<ProjectEditor> prj, int index) noexcept;
  void openBoard(std::shared_ptr<ProjectEditor> prj, int index) noexcept;
  void openBoard3dViewer(int section, int tab) noexcept;
  void setCurrentTab(int section, int tab) noexcept;
  void closeTab(int section, int tab) noexcept;
  slint::Image renderScene(int section, int tab, float width, float height,
                           int frame) noexcept;
  slint::private_api::EventResult processScenePointerEvent(
      int section, int tab, float x, float y,
      slint::private_api::PointerEvent e) noexcept;
  slint::private_api::EventResult processSceneScrolled(
      int section, int tab, float x, float y,
      slint::private_api::PointerScrollEvent e) noexcept;

  // Implementations
  std::size_t row_count() const override;
  std::optional<ui::WindowSection> row_data(std::size_t i) const override;

  // Operator Overloadings
  WindowSectionsModel& operator=(const WindowSectionsModel& rhs) = delete;

signals:
  void currentSectionChanged(int section);
  void currentProjectChanged(std::shared_ptr<ProjectEditor> prj);
  void cursorCoordinatesChanged(qreal x, qreal y);

private:
  void addTab(std::shared_ptr<ProjectEditor> prj, ui::TabType type,
              const QString& title, int objIndex) noexcept;
  Section* getSection(int index) noexcept {
    return ((index >= 0) && (index < mItems.count())) ? &mItems[index]
                                                      : nullptr;
  }

  GuiApplication& mApp;
  std::unique_ptr<IF_GraphicsLayerProvider> mLayerProvider;
  std::unique_ptr<BoardPlaneFragmentsBuilder> mPlaneBuilder;
  int mCurrentSection;
  QList<Section> mItems;
};

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace app
}  // namespace editor
}  // namespace librepcb

#endif
