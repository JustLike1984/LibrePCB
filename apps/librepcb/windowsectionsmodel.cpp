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
#include "windowsectionsmodel.h"

#include "apptoolbox.h"
#include "guiapplication.h"
#include "project/projecteditor.h"

#include <librepcb/core/project/board/board.h>
#include <librepcb/core/project/board/boardplanefragmentsbuilder.h>
#include <librepcb/core/project/circuit/circuit.h>
#include <librepcb/core/project/project.h>
#include <librepcb/core/project/schematic/schematic.h>
#include <librepcb/core/workspace/workspace.h>
#include <librepcb/core/workspace/workspacesettings.h>
#include <librepcb/editor/3d/openglscenebuilder.h>
#include <librepcb/editor/graphics/defaultgraphicslayerprovider.h>
#include <librepcb/editor/project/boardeditor/boardgraphicsscene.h>
#include <librepcb/editor/project/schematiceditor/schematicgraphicsscene.h>
#include <librepcb/editor/widgets/openglview.h>

#include <QtCore>
#include <QtWidgets>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace editor {
namespace app {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

WindowSectionsModel::WindowSectionsModel(GuiApplication& app,
                                         QObject* parent) noexcept
  : QObject(parent),
    mApp(app),
    mLayerProvider(new DefaultGraphicsLayerProvider(
        app.getWorkspace().getSettings().themes.getActive())),
    mPlaneBuilder(),
    mCurrentSection(-1),
    mItems() {
}

WindowSectionsModel::~WindowSectionsModel() noexcept {
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void WindowSectionsModel::openSchematic(std::shared_ptr<ProjectEditor> prj,
                                        int index) noexcept {
  if (auto obj = prj->getProject().getSchematicByIndex(index)) {
    addTab(prj, ui::TabType::Schematic, *obj->getName(), index);
  }
}

void WindowSectionsModel::openBoard(std::shared_ptr<ProjectEditor> prj,
                                    int index) noexcept {
  if (auto obj = prj->getProject().getBoardByIndex(index)) {
    addTab(prj, ui::TabType::Board2d, *obj->getName(), index);
  }
}

void WindowSectionsModel::openBoard3dViewer(int section, int tab) noexcept {
  if (Section* s = getSection(section)) {
    if (Tab* t = s->getTab(tab)) {
      if (auto prj = t->project) {
        if (auto obj = prj->getProject().getBoardByIndex(t->objIndex)) {
          addTab(prj, ui::TabType::Board3d, *obj->getName(), t->objIndex);
        }
      }
    }
  }
}

void WindowSectionsModel::setCurrentTab(int section, int tab) noexcept {
  if (Section* s = getSection(section)) {
    if (Tab* t = s->getTab(tab)) {
      if (section != mCurrentSection) {
        mCurrentSection = section;
        emit currentSectionChanged(mCurrentSection);
        emit currentProjectChanged(t->project);
      }

      if (tab == s->uiData.tab_index) {
        return; // No change.
      }

      if (t->type == ui::TabType::Schematic) {
        if (auto sch =
                t->project->getProject().getSchematicByIndex(t->objIndex)) {
          s->openGlSceneBuilder.reset();
          s->openGlView.reset();
          s->scene.reset(new SchematicGraphicsScene(
              *sch, *mLayerProvider, std::make_shared<QSet<const NetSignal*>>(),
              this));
          s->uiData.overlay_color = q2s(Qt::black);
          s->uiData.frame++;
        }
      } else if (t->type == ui::TabType::Board2d) {
        if (auto brd = t->project->getProject().getBoardByIndex(t->objIndex)) {
          mPlaneBuilder.reset(new BoardPlaneFragmentsBuilder(false, this));
          connect(mPlaneBuilder.get(),
                  &BoardPlaneFragmentsBuilder::boardPlanesModified, this,
                  [this, section]() {
                    if (auto s = getSection(section)) {
                      s->uiData.frame++;
                      row_changed(section);
                    }
                  });
          mPlaneBuilder->startAsynchronously(*brd);
          s->openGlSceneBuilder.reset();
          s->openGlView.reset();
          s->scene.reset(new BoardGraphicsScene(
              *brd, *mLayerProvider, std::make_shared<QSet<const NetSignal*>>(),
              this));
          s->uiData.overlay_color = q2s(Qt::white);
          s->uiData.frame++;
        }
      } else if (t->type == ui::TabType::Board3d) {
        if (auto brd = t->project->getProject().getBoardByIndex(t->objIndex)) {
          mPlaneBuilder.reset(new BoardPlaneFragmentsBuilder(false, this));
          connect(mPlaneBuilder.get(),
                  &BoardPlaneFragmentsBuilder::boardPlanesModified, this,
                  [this, section]() {
                    if (auto s = getSection(section)) {
                      s->uiData.frame++;
                      row_changed(section);
                    }
                  });
          mPlaneBuilder->startAsynchronously(*brd);
          s->scene.reset();
          s->openGlView.reset(new OpenGlView());
          s->openGlSceneBuilder.reset(new OpenGlSceneBuilder(this));
          connect(s->openGlSceneBuilder.get(), &OpenGlSceneBuilder::objectAdded,
                  s->openGlView.get(), &OpenGlView::addObject);
          connect(
              s->openGlSceneBuilder.get(), &OpenGlSceneBuilder::objectAdded,
              this,
              [this, section]() {
                if (auto s = getSection(section)) {
                  s->uiData.frame++;
                  row_changed(section);
                }
              },
              Qt::QueuedConnection);
          auto av =
              t->project->getProject().getCircuit().getAssemblyVariants().value(
                  0);
          s->openGlSceneBuilder->start(brd->buildScene3D(
              av ? tl::make_optional(av->getUuid()) : tl::nullopt));
          s->uiData.overlay_color = q2s(Qt::black);
          s->uiData.frame++;
        }
      }

      emit currentProjectChanged(t->project);
    }

    s->uiData.tab_index = tab;
    row_changed(section);
  }
}

void WindowSectionsModel::closeTab(int section, int tab) noexcept {
  if (Section* s = getSection(section)) {
    auto tabs =
        std::dynamic_pointer_cast<slint::VectorModel<ui::Tab>>(s->uiData.tabs);
    const int tabCount = static_cast<int>(tabs->row_count());
    if (tabCount == 1) {
      if (mCurrentSection >= section) {
        --mCurrentSection;
        emit currentSectionChanged(mCurrentSection);
      }
      mItems.remove(section);
      row_removed(section, 1);
      for (int i = section; i < mItems.count(); ++i) {
        mItems[i].uiData.index--;
        row_changed(i);
      }
    } else if ((tab >= 0) && (tab < tabCount)) {
      s->tabs.remove(tab);
      tabs->erase(tab);
      int currentIndex = s->uiData.tab_index;
      if (tab < currentIndex) {
        --currentIndex;
      }
      setCurrentTab(section, std::min(currentIndex, tabCount - 2));
    }
  }
}

slint::Image WindowSectionsModel::renderScene(int section, int tab, float width,
                                              float height,
                                              int frame) noexcept {
  Q_UNUSED(frame);

  if (Section* s = getSection(section)) {
    if (Tab* t = s->getTab(tab)) {
      if (auto scene = s->scene) {
        QPixmap pixmap(width, height);
        pixmap.fill(dynamic_cast<BoardGraphicsScene*>(scene.get()) ? Qt::black
                                                                   : Qt::white);
        {
          QPainter painter(&pixmap);
          painter.setRenderHints(QPainter::Antialiasing |
                                 QPainter::SmoothPixmapTransform);
          QRectF targetRect(0, 0, width, height);
          if (t->sceneRect.isEmpty()) {
            const QRectF sceneRect = scene->itemsBoundingRect();
            t->scale = std::min(targetRect.width() / sceneRect.width(),
                                targetRect.height() / sceneRect.height());
            t->offset = sceneRect.center() - targetRect.center() / t->scale;
          }
          t->sceneRect = QRectF(0, 0, width / t->scale, height / t->scale);
          t->sceneRect.translate(t->offset);
          scene->render(&painter, targetRect, t->sceneRect);
        }
        return q2s(pixmap);
      } else if (auto view = s->openGlView) {
        view->resize(width, height);
        return q2s(view->grab());
      }
    }
  }

  return slint::Image();
}

slint::private_api::EventResult WindowSectionsModel::processScenePointerEvent(
    int section, int tab, float x, float y,
    slint::private_api::PointerEvent e) noexcept {
  if (Section* s = getSection(section)) {
    Tab* t = s->getTab(tab);

    if ((e.kind == slint::private_api::PointerEventKind::Down) &&
        (section != mCurrentSection)) {
      mCurrentSection = section;
      emit currentSectionChanged(mCurrentSection);
      emit currentProjectChanged(t ? t->project : nullptr);
    }

    if (t) {
      if (s->scene) {
        QTransform tf;
        tf.translate(t->offset.x(), t->offset.y());
        tf.scale(1 / t->scale, 1 / t->scale);
        QPointF scenePosPx = tf.map(QPointF(x, y));

        if ((e.button == slint::private_api::PointerEventButton::Middle) ||
            (e.button == slint::private_api::PointerEventButton::Right)) {
          if (e.kind == slint::private_api::PointerEventKind::Down) {
            s->startScenePos = scenePosPx;
            s->panning = true;
          } else if (e.kind == slint::private_api::PointerEventKind::Up) {
            s->panning = false;
          }
        }
        if (s->panning &&
            (e.kind == slint::private_api::PointerEventKind::Move)) {
          t->offset -= scenePosPx - s->startScenePos;
          s->uiData.frame++;
          row_changed(section);
        }
        const Point scenePos = Point::fromPx(scenePosPx);
        emit cursorCoordinatesChanged(scenePos.getX().toMm(),
                                      scenePos.getY().toMm());
      } else if (auto view = s->openGlView) {
        if (e.kind == slint::private_api::PointerEventKind::Down) {
          s->mousePressPosition = QPoint(x, y);
          s->mousePressTransform = t->transform;
          s->mousePressCenter = t->projectionCenter;
          s->buttons.insert(e.button);
        } else if (e.kind == slint::private_api::PointerEventKind::Up) {
          s->buttons.remove(e.button);
        } else if (e.kind == slint::private_api::PointerEventKind::Move) {
          const QPointF posNorm = view->toNormalizedPos(QPointF(x, y));
          const QPointF mousePressPosNorm =
              view->toNormalizedPos(s->mousePressPosition);

          if (s->buttons.contains(
                  slint::private_api::PointerEventButton::Middle) ||
              s->buttons.contains(
                  slint::private_api::PointerEventButton::Right)) {
            const QPointF cursorPosOld = view->toModelPos(mousePressPosNorm);
            const QPointF cursorPosNew = view->toModelPos(posNorm);
            t->projectionCenter =
                s->mousePressCenter + cursorPosNew - cursorPosOld;
            view->setTransform(t->transform, t->projectionFov,
                               t->projectionCenter);
            s->uiData.frame++;
            row_changed(section);
          }
          if (s->buttons.contains(
                  slint::private_api::PointerEventButton::Left)) {
            t->transform = s->mousePressTransform;
            if (e.modifiers.shift) {
              // Rotate around Z axis.
              const QPointF p1 =
                  view->toModelPos(mousePressPosNorm) - t->projectionCenter;
              const QPointF p2 =
                  view->toModelPos(posNorm) - t->projectionCenter;
              const qreal angle1 = std::atan2(p1.y(), p1.x());
              const qreal angle2 = std::atan2(p2.y(), p2.x());
              const Angle angle =
                  Angle::fromRad(angle2 - angle1).mappedTo180deg();
              const QVector3D axis = s->mousePressTransform.inverted().map(
                  QVector3D(0, 0, angle.toDeg()));
              t->transform.rotate(QQuaternion::fromAxisAndAngle(
                  axis.normalized(), angle.abs().toDeg()));
            } else {
              // Rotate around X/Y axis.
              const QVector2D delta(posNorm - mousePressPosNorm);
              const QVector3D axis = s->mousePressTransform.inverted().map(
                  QVector3D(-delta.y(), delta.x(), 0));
              t->transform.rotate(QQuaternion::fromAxisAndAngle(
                  axis.normalized(), delta.length() * 270));
            }
            view->setTransform(t->transform, t->projectionFov,
                               t->projectionCenter);
            s->uiData.frame++;
            row_changed(section);
          }
        }
      }
    }
  }
  return slint::private_api::EventResult::Accept;
}

slint::private_api::EventResult WindowSectionsModel::processSceneScrolled(
    int section, int tab, float x, float y,
    slint::private_api::PointerScrollEvent e) noexcept {
  if (Section* s = getSection(section)) {
    if (Tab* t = s->getTab(tab)) {
      qreal factor = qPow(1.3, e.delta_y / qreal(120));

      if (s->scene) {
        QTransform tf;
        tf.translate(t->offset.x(), t->offset.y());
        tf.scale(1 / t->scale, 1 / t->scale);
        QPointF scenePos0 = tf.map(QPointF(x, y));

        t->scale *= factor;

        QTransform tf2;
        tf2.translate(t->offset.x(), t->offset.y());
        tf2.scale(1 / t->scale, 1 / t->scale);
        QPointF scenePos2 = tf2.map(QPointF(x, y));

        t->offset -= scenePos2 - scenePos0;
      } else if (auto view = s->openGlView) {
        const QPointF centerNormalized = view->toNormalizedPos(QPointF(x, y));
        const QPointF modelPosOld = view->toModelPos(centerNormalized);
        t->projectionFov =
            qBound(qreal(0.01), t->projectionFov / factor, qreal(90));
        view->setTransform(t->transform, t->projectionFov, t->projectionCenter);
        const QPointF modelPosNew = view->toModelPos(centerNormalized);
        t->projectionCenter += modelPosNew - modelPosOld;
        view->setTransform(t->transform, t->projectionFov, t->projectionCenter);
      }
      s->uiData.frame++;
      row_changed(section);
    }
  }
  return slint::private_api::EventResult::Accept;
}

/*******************************************************************************
 *  Implementations
 ******************************************************************************/

std::size_t WindowSectionsModel::row_count() const {
  return mItems.size();
}

std::optional<ui::WindowSection> WindowSectionsModel::row_data(
    std::size_t i) const {
  return (i < static_cast<std::size_t>(mItems.size()))
      ? std::optional(mItems.at(i).uiData)
      : std::nullopt;
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void WindowSectionsModel::addTab(std::shared_ptr<ProjectEditor> prj,
                                 ui::TabType type, const QString& title,
                                 int objIndex) noexcept {
  // Determine section.
  int section = 0;
  if (mItems.count() < 2) {
    section = mItems.count();
    Section s;
    s.uiData = ui::WindowSection{
        section, std::make_shared<slint::VectorModel<ui::Tab>>(),
        -1,       slint::Brush(),
        0,
    };
    mItems.append(s);
    row_added(section, 1);
  } else {
    for (int i = 0; i < mItems.count(); ++i) {
      section += mItems[i].tabs.count();
    }
    section %= 2;
  }

  if (Section* s = getSection(section)) {
    Tab t;
    t.project = prj;
    t.type = type;
    t.objIndex = objIndex;
    s->tabs.append(t);
    auto tabs =
        std::dynamic_pointer_cast<slint::VectorModel<ui::Tab>>(s->uiData.tabs);
    tabs->push_back(ui::Tab{type, q2s(title)});
    setCurrentTab(section, tabs->row_count() - 1);
  }
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace app
}  // namespace editor
}  // namespace librepcb
