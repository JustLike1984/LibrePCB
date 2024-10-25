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
#include "mainwindow.h"

#include "apptoolbox.h"
#include "guiapplication.h"
#include "project/projecteditor.h"
#include "project/projectsmodel.h"
#include "windowsectionsmodel.h"

#include <librepcb/core/fileio/filepath.h>
#include <librepcb/core/project/board/board.h>
#include <librepcb/core/project/project.h>
#include <librepcb/core/project/schematic/schematic.h>
#include <librepcb/core/workspace/workspace.h>
#include <librepcb/core/workspace/workspacesettings.h>
#include <librepcb/editor/workspace/desktopservices.h>

#include <QtCore>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace editor {
namespace app {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

MainWindow::MainWindow(GuiApplication& app,
                       slint::ComponentHandle<ui::AppWindow> win, int index,
                       QObject* parent) noexcept
  : QObject(parent),
    mApp(app),
    mWindow(win),
    mGlobals(mWindow->global<ui::Globals>()),
    mIndex(index),
    mSections(new WindowSectionsModel(app, win, this)) {
  // Set initial data.
  mGlobals.set_current_project(ui::ProjectData{});
  mWindow->set_cursor_coordinate(slint::SharedString());

  // Register global callbacks.
  mGlobals.on_project_item_doubleclicked(std::bind(
      &MainWindow::projectItemDoubleClicked, this, std::placeholders::_1));
  mGlobals.on_schematic_clicked(std::bind(&MainWindow::schematicItemClicked,
                                          this, std::placeholders::_1));
  mGlobals.on_board_clicked(
      std::bind(&MainWindow::boardItemClicked, this, std::placeholders::_1));
  mGlobals.on_board_3d_clicked(std::bind(&MainWindow::board3dItemClicked, this,
                                         std::placeholders::_1,
                                         std::placeholders::_2));
  mGlobals.on_tab_clicked(std::bind(&MainWindow::tabClicked, this,
                                    std::placeholders::_1,
                                    std::placeholders::_2));
  mGlobals.on_tab_close_clicked(std::bind(&MainWindow::tabCloseClicked, this,
                                          std::placeholders::_1,
                                          std::placeholders::_2));
  mGlobals.on_render_scene(
      std::bind(&MainWindow::renderScene, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5));
  mGlobals.on_scene_pointer_event(
      std::bind(&MainWindow::onScenePointerEvent, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5));
  mGlobals.on_scene_scrolled(
      std::bind(&MainWindow::onSceneScrolled, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5));

  // Set models.
  mGlobals.set_sections(mSections);

  // Show window.
  mWindow->show();
}

MainWindow::~MainWindow() noexcept {
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void MainWindow::projectItemDoubleClicked(
    const slint::SharedString& path) noexcept {
  const FilePath fp(s2q(path));
  if (!fp.isValid()) {
    qWarning() << "Invalid file path:" << path.data();
    return;
  }
  if ((fp.getSuffix() == "lpp") || (fp.getSuffix() == "lppz")) {
    mProject = mApp.getProjects().openProject(fp);
    auto schematics =
        std::make_shared<slint::VectorModel<slint::SharedString>>();
    auto boards = std::make_shared<slint::VectorModel<slint::SharedString>>();

    for (auto sch : mProject->getProject().getSchematics()) {
      schematics->push_back(q2s(*sch->getName()));
    }

    for (auto brd : mProject->getProject().getBoards()) {
      boards->push_back(q2s(*brd->getName()));
    }

    mGlobals.set_current_project(ui::ProjectData{
        true,
        q2s(*mProject->getProject().getName()),
        schematics,
        boards,
    });
    mWindow->set_page(ui::MainPage::Project);
  } else {
    DesktopServices ds(mApp.getWorkspace().getSettings(), nullptr);
    ds.openLocalPath(fp);
  }
}

void MainWindow::schematicItemClicked(int index) noexcept {
  if (!mProject) return;
  if (auto obj = mProject->getProject().getSchematicByIndex(index)) {
    addTab(ui::TabType::Schematic, *obj->getName(), index);
  }
}

void MainWindow::boardItemClicked(int index) noexcept {
  if (!mProject) return;
  if (auto obj = mProject->getProject().getBoardByIndex(index)) {
    addTab(ui::TabType::Board2d, *obj->getName(), index);
  }
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace app
}  // namespace editor
}  // namespace librepcb
