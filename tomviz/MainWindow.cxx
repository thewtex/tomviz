/******************************************************************************

  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <pqMacroReaction.h>
#include <pqObjectBuilder.h>
#include <pqPythonShellReaction.h>
#include <pqSaveAnimationReaction.h>
#include <pqSaveStateReaction.h>
#include <pqSettings.h>
#include <pqView.h>
#include <vtkPVPlugin.h>
#include <vtkPVRenderView.h>
#include <vtkSMSettings.h>
#include <vtkSMViewProxy.h>

#include "AboutDialog.h"
#include "AcquisitionWidget.h"
#include "ActiveObjects.h"
#include "AddAlignReaction.h"
#include "AddPythonTransformReaction.h"
#include "AddRotateAlignReaction.h"
#include "AddRotateAlignReaction.h"
#include "Behaviors.h"
#include "DataPropertiesPanel.h"
#include "DataTransformMenu.h"
#include "LoadDataReaction.h"
#include "LoadPaletteReaction.h"
#include "ModuleManager.h"
#include "ModuleMenu.h"
#include "ModulePropertiesPanel.h"
#include "ProgressDialogManager.h"
#include "PythonGeneratedDatasetReaction.h"
#include "RecentFilesMenu.h"
#include "ReconstructionReaction.h"
#include "ResetReaction.h"
#include "SaveDataReaction.h"
#include "SaveLoadStateReaction.h"
#include "SaveScreenshotReaction.h"
#include "SaveWebReaction.h"
#include "ScaleLegend.h"
#include "SetTiltAnglesOperator.h"
#include "SetTiltAnglesReaction.h"
#include "ToggleDataTypeReaction.h"
#include "Utilities.h"
#include "ViewMenuManager.h"
#include "WelcomeDialog.h"
#include "tomvizConfig.h"

#include "PipelineModel.h"

#include <QAction>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

// undef ERROR here as its used in pqOutputWidget!
#undef ERROR
#include <pqOutputWidget.h>

// we are building with dax, so we have plugins to import
#ifdef DAX_DEVICE_ADAPTER
// Adds required forward declarations.
PV_PLUGIN_IMPORT_INIT(tomvizThreshold);
PV_PLUGIN_IMPORT_INIT(tomvizStreaming);
#endif

namespace {
QString getAutosaveFile()
{
  // workaround to get user config location
  QString dataPath;
  dataPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
  QDir dataDir(dataPath);
  if (!dataDir.exists()) {
    dataDir.mkpath(dataPath);
  }
  return dataDir.absoluteFilePath(".tomviz_autosave.tvsm");
}
}

namespace tomviz {

MainWindow::MainWindow(QWidget* parent, Qt::WindowFlags flags)
  : QMainWindow(parent, flags), m_ui(new Ui::MainWindow)
{

  // Override the default setting for showing full messages. This needs to be
  // done prior to calling m_ui->setupUi(this) which sets the default to false.
  pqSettings* qtSettings = pqApplicationCore::instance()->settings();
  if (!qtSettings->contains("pqOutputWidget.ShowFullMessages")) {
    qtSettings->setValue("pqOutputWidget.ShowFullMessages", true);
  }

  // checkOpenGL();
  m_ui->setupUi(this);
  m_timer = new QTimer(this);
  connect(m_timer, SIGNAL(timeout()), SLOT(autosave()));
  m_timer->start(5 /*minutes*/ * 60 /*seconds per minute*/ *
                 1000 /*msec per second*/);

  QString version(TOMVIZ_VERSION);
  if (QString(TOMVIZ_VERSION_EXTRA).size() > 0)
    version.append("-").append(TOMVIZ_VERSION_EXTRA);
  setWindowTitle("tomviz " + version);

  QIcon icon(":/icons/tomviz.png");
  setWindowIcon(icon);

  // tabify output messages widget.
  tabifyDockWidget(m_ui->dockWidget_3, m_ui->dockWidgetMessages);
  m_ui->dockWidgetMessages->hide();

  // Tweak the initial sizes of the dock widgets.
  QList<QDockWidget*> docks;
  docks << m_ui->dockWidget << m_ui->dockWidget_5 << m_ui->dockWidgetMessages;
  QList<int> dockSizes;
  dockSizes << 250 << 250 << 250;
  resizeDocks(docks, dockSizes, Qt::Horizontal);
  docks.clear();
  dockSizes.clear();
  docks << m_ui->dockWidget_3;
  dockSizes << 200;
  resizeDocks(docks, dockSizes, Qt::Vertical);

  // raise dockWidgetMessages on error.
  connect(m_ui->outputWidget, SIGNAL(messageDisplayed(const QString&, int)),
          SLOT(handleMessage(const QString&, int)));

  // Link the histogram in the central widget to the active data source.
  m_ui->centralWidget->connect(&ActiveObjects::instance(),
                               SIGNAL(dataSourceActivated(DataSource*)),
                               SLOT(setActiveColorMapDataSource(DataSource*)));
  m_ui->centralWidget->connect(&ActiveObjects::instance(),
                               SIGNAL(moduleActivated(Module*)),
                               SLOT(setActiveModule(Module*)));
  m_ui->centralWidget->connect(&ActiveObjects::instance(),
                               SIGNAL(colorMapChanged(DataSource*)),
                               SLOT(setActiveColorMapDataSource(DataSource*)));
  m_ui->centralWidget->connect(m_ui->dataPropertiesPanel,
                               SIGNAL(colorMapUpdated()),
                               SLOT(onColorMapUpdated()));

  m_ui->treeWidget->setModel(new PipelineModel(this));
  m_ui->treeWidget->header()->setStretchLastSection(false);
  m_ui->treeWidget->header()->setVisible(false);
  m_ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_ui->treeWidget->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_ui->treeWidget->header()->resizeSection(1, 30);
  // Ensure that items are expanded by default, can be collapsed at will.
  connect(m_ui->treeWidget->model(),
          SIGNAL(rowsInserted(QModelIndex, int, int)), m_ui->treeWidget,
          SLOT(expandAll()));
  connect(m_ui->treeWidget->model(), SIGNAL(modelReset()), m_ui->treeWidget,
          SLOT(expandAll()));

  // connect quit.
  connect(m_ui->actionExit, SIGNAL(triggered()), SLOT(close()));

  // Connect up the module/data changed to the appropriate slots.
  connect(&ActiveObjects::instance(), SIGNAL(dataSourceActivated(DataSource*)),
          SLOT(dataSourceChanged(DataSource*)));
  connect(&ActiveObjects::instance(), SIGNAL(moduleActivated(Module*)),
          SLOT(moduleChanged(Module*)));
  connect(&ActiveObjects::instance(), SIGNAL(operatorActivated(Operator*)),
          SLOT(operatorChanged(Operator*)));

  // Connect the about dialog up too.
  connect(m_ui->actionAbout, SIGNAL(triggered()), SLOT(showAbout()));

  new pqPythonShellReaction(m_ui->actionPythonConsole);
  new pqMacroReaction(m_ui->actionMacros);

  // Instantiate tomviz application behavior.
  new Behaviors(this);

  new LoadDataReaction(m_ui->actionOpen);

  // Build Data Transforms menu
  new DataTransformMenu(this, m_ui->menuData, m_ui->menuSegmentation);

  // Build Tomography menu
  // ################################################################
  QAction* toggleDataTypeAction =
    m_ui->menuTomography->addAction("Toggle Data Type");
  m_ui->menuTomography->addSeparator();

  QAction* setTiltAnglesAction =
    m_ui->menuTomography->addAction("Set Tilt Angles");
  m_ui->menuTomography->addSeparator();

  QAction* dataProcessingLabel =
    m_ui->menuTomography->addAction("Pre-processing:");
  dataProcessingLabel->setEnabled(false);
  QAction* downsampleByTwoAction =
    m_ui->menuTomography->addAction("Bin Tilt Images x2");
  QAction* removeBadPixelsAction =
    m_ui->menuTomography->addAction("Remove Bad Pixels");
  QAction* gaussianFilterAction =
    m_ui->menuTomography->addAction("Gaussian Filter");
  QAction* autoSubtractBackgroundAction =
    m_ui->menuTomography->addAction("Background Subtraction (Auto)");
  QAction* subtractBackgroundAction =
    m_ui->menuTomography->addAction("Background Subtraction (Manual)");
  QAction* normalizationAction =
    m_ui->menuTomography->addAction("Normalize Average Image Intensity");
  QAction* gradientMagnitude2DSobelAction =
    m_ui->menuTomography->addAction("2D Gradient Magnitude");

  m_ui->menuTomography->addSeparator();
  QAction* alignmentLabel = m_ui->menuTomography->addAction("Alignment:");
  alignmentLabel->setEnabled(false);
  QAction* autoAlignCCAction = m_ui->menuTomography->addAction(
    "Image Alignment (Auto: Cross Correlation)");
  QAction* autoAlignCOMAction =
    m_ui->menuTomography->addAction("Image Alignment (Auto: Center of Mass)");
  QAction* alignAction =
    m_ui->menuTomography->addAction("Image Alignment (Manual)");
  QAction* autoRotateAlignAction =
    m_ui->menuTomography->addAction("Tilt Axis Rotation Alignment (Auto)");
  QAction* autoRotateAlignShiftAction =
    m_ui->menuTomography->addAction("Tilt Axis Shift Alignment (Auto)");
  QAction* rotateAlignAction =
    m_ui->menuTomography->addAction("Tilt Axis Alignment (Manual)");
  m_ui->menuTomography->addSeparator();

  QAction* reconLabel = m_ui->menuTomography->addAction("Reconstruction:");
  reconLabel->setEnabled(false);
  QAction* reconDFMAction =
    m_ui->menuTomography->addAction("Direct Fourier Method");
  QAction* reconWBPAction =
    m_ui->menuTomography->addAction("Weighted Back Projection");
  QAction* reconWBP_CAction =
    m_ui->menuTomography->addAction("Simple Back Projection (C++)");
  QAction* reconARTAction =
    m_ui->menuTomography->addAction("Algebraic Reconstruction Technique (ART)");
  QAction* reconSIRTAction = m_ui->menuTomography->addAction(
    "Simultaneous Iterative Recon. Technique (SIRT)");
  QAction* reconDFMConstraintAction =
    m_ui->menuTomography->addAction("Constraint-based Direct Fourier Method");
  QAction* reconTVMinimizationAction =
    m_ui->menuTomography->addAction("TV Minimization Method");
  m_ui->menuTomography->addSeparator();

  QAction* simulationLabel = m_ui->menuTomography->addAction("Simulation:");
  simulationLabel->setEnabled(false);
  QAction* generateTiltSeriesAction =
    m_ui->menuTomography->addAction("Project Tilt Series from Volume");

  QAction* randomShiftsAction =
    m_ui->menuTomography->addAction("Shift Tilt Series Randomly");
  QAction* addPoissonNoiseAction =
    m_ui->menuTomography->addAction("Add Poisson Noise");

  // Set up reactions for Tomography Menu
  //#################################################################
  new ToggleDataTypeReaction(toggleDataTypeAction, this);
  new SetTiltAnglesReaction(setTiltAnglesAction, this);

  new AddPythonTransformReaction(
    generateTiltSeriesAction, "Generate Tilt Series",
    readInPythonScript("GenerateTiltSeries"), false, true,
    readInJSONDescription("GenerateTiltSeries"));

  new AddAlignReaction(alignAction);
  new AddPythonTransformReaction(downsampleByTwoAction, "Bin Tilt Image x2",
                                 readInPythonScript("BinTiltSeriesByTwo"),
                                 true);
  new AddPythonTransformReaction(
    removeBadPixelsAction, "Remove Bad Pixels",
    readInPythonScript("RemoveBadPixelsTiltSeries"), true, false);
  new AddPythonTransformReaction(
    gaussianFilterAction, "Gaussian Filter Tilt Series",
    readInPythonScript("GaussianFilterTiltSeries"), true, false,
    readInJSONDescription("GaussianFilterTiltSeries"));
  new AddPythonTransformReaction(
    autoSubtractBackgroundAction, "Background Subtraction (Auto)",
    readInPythonScript("Subtract_TiltSer_Background_Auto"), true);
  new AddPythonTransformReaction(
    subtractBackgroundAction, "Background Subtraction (Manual)",
    readInPythonScript("Subtract_TiltSer_Background"), true);
  new AddPythonTransformReaction(normalizationAction, "Normalize Tilt Series",
                                 readInPythonScript("NormalizeTiltSeries"),
                                 true);
  new AddPythonTransformReaction(
    gradientMagnitude2DSobelAction, "Gradient Magnitude 2D",
    readInPythonScript("GradientMagnitude2D_Sobel"), true);
  new AddRotateAlignReaction(rotateAlignAction);
  new AddPythonTransformReaction(
    autoRotateAlignAction, "Auto Tilt Axis Align",
    readInPythonScript("AutoTiltAxisRotationAlignment"), true);
  new AddPythonTransformReaction(
    autoRotateAlignShiftAction, "Auto Tilt Axis Shift Align",
    readInPythonScript("AutoTiltAxisShiftAlignment"), true);

  new AddPythonTransformReaction(
    autoAlignCCAction, "Auto Tilt Image Align (XCORR)",
    readInPythonScript("AutoCrossCorrelationTiltImageAlignment"), true);
  new AddPythonTransformReaction(
    autoAlignCOMAction, "Auto Tilt Image Align (CoM)",
    readInPythonScript("AutoCenterOfMassTiltImageAlignment"), true);
  new AddPythonTransformReaction(reconDFMAction, "Reconstruct (Direct Fourier)",
                                 readInPythonScript("Recon_DFT"), true, false,
                                 readInJSONDescription("Recon_DFT"));
  new AddPythonTransformReaction(reconWBPAction,
                                 "Reconstruct (Back Projection)",
                                 readInPythonScript("Recon_WBP"), true, false,
                                 readInJSONDescription("Recon_WBP"));
  new AddPythonTransformReaction(reconARTAction, "Reconstruct (ART)",
                                 readInPythonScript("Recon_ART"), true, false,
                                 readInJSONDescription("Recon_ART"));
  new AddPythonTransformReaction(reconSIRTAction, "Reconstruct (SIRT)",
                                 readInPythonScript("Recon_SIRT"), true, false,
                                 readInJSONDescription("Recon_SIRT"));
  new AddPythonTransformReaction(
    reconDFMConstraintAction, "Reconstruct (Constraint-based Direct Fourier)",
    readInPythonScript("Recon_DFT_constraint"), true);
  new AddPythonTransformReaction(
    reconTVMinimizationAction, "Reconstruct (TV Minimization)",
    readInPythonScript("Recon_TV_minimization"), true, false,
    readInJSONDescription("Recon_TV_minimization"));

  new ReconstructionReaction(reconWBP_CAction);

  new AddPythonTransformReaction(
    randomShiftsAction, "Shift Tilt Series Randomly",
    readInPythonScript("ShiftTiltSeriesRandomly"), true, false,
    readInJSONDescription("ShiftTiltSeriesRandomly"));
  new AddPythonTransformReaction(addPoissonNoiseAction, "Add Poisson Noise",
                                 readInPythonScript("AddPoissonNoise"), true,
                                 false,
                                 readInJSONDescription("AddPoissonNoise"));

  //#################################################################
  new ModuleMenu(m_ui->modulesToolbar, m_ui->menuModules, this);
  new RecentFilesMenu(*m_ui->menuRecentlyOpened, m_ui->menuRecentlyOpened);
  new pqSaveStateReaction(m_ui->actionSaveDebuggingState);

  new SaveDataReaction(m_ui->actionSaveData);
  new SaveScreenshotReaction(m_ui->actionSaveScreenshot, this);
  new pqSaveAnimationReaction(m_ui->actionSaveMovie);
  new SaveWebReaction(m_ui->actionSaveWeb);

  new SaveLoadStateReaction(m_ui->actionSaveState);
  new SaveLoadStateReaction(m_ui->actionLoadState, /*load*/ true);

  auto reaction = new ResetReaction(m_ui->actionReset);
  connect(m_ui->menu_File, &QMenu::aboutToShow, reaction,
          &ResetReaction::updateEnableState);

  ViewMenuManager* viewMenuManager = new ViewMenuManager(this, m_ui->menuView);

  QMenu* sampleDataMenu = new QMenu("Sample Data", this);
  m_ui->menubar->insertMenu(m_ui->menuHelp->menuAction(), sampleDataMenu);
  QAction* userGuideAction = m_ui->menuHelp->addAction("User Guide");
  connect(userGuideAction, SIGNAL(triggered()), SLOT(openUserGuide()));
#ifdef TOMVIZ_DATA
  QAction* reconAction =
    sampleDataMenu->addAction("Star Nanoparticle (Reconstruction)");
  QAction* tiltAction =
    sampleDataMenu->addAction("Star Nanoparticle (Tilt Series)");
  connect(reconAction, SIGNAL(triggered()), SLOT(openRecon()));
  connect(tiltAction, SIGNAL(triggered()), SLOT(openTilt()));
  sampleDataMenu->addSeparator();
#endif
  QAction* constantDataAction =
    sampleDataMenu->addAction("Generate Constant Dataset");
  new PythonGeneratedDatasetReaction(constantDataAction, "Constant Dataset",
                                     readInPythonScript("ConstantDataset"));
  QAction* randomParticlesAction =
    sampleDataMenu->addAction("Generate Random Particles");
  new PythonGeneratedDatasetReaction(randomParticlesAction, "Random Particles",
                                     readInPythonScript("RandomParticles"));
  QAction* probeShapeAction =
    sampleDataMenu->addAction("Generate Electron Beam Shape");
  new PythonGeneratedDatasetReaction(probeShapeAction, "Electron Beam Shape",
                                     readInPythonScript("STEM_probe"));
  sampleDataMenu->addSeparator();
  QAction* sampleDataLinkAction =
    sampleDataMenu->addAction("Download More Datasets");
  connect(sampleDataLinkAction, SIGNAL(triggered()), SLOT(openDataLink()));

  QAction* moveObjects = m_ui->utilitiesToolbar->addAction(
    QIcon(":/icons/move_objects.png"), "MoveObjects");
  moveObjects->setToolTip(
    "Enable to allow moving of the selected dataset in the scene");
  moveObjects->setCheckable(true);

  connect(moveObjects, SIGNAL(triggered(bool)), &ActiveObjects::instance(),
          SLOT(setMoveObjectsMode(bool)));

  QAction* loadPaletteAction = m_ui->utilitiesToolbar->addAction(
    QIcon(":/icons/pqPalette.png"), "LoadPalette");
  new LoadPaletteReaction(loadPaletteAction);

  QToolButton* tb = qobject_cast<QToolButton*>(
    m_ui->utilitiesToolbar->widgetForAction(loadPaletteAction));
  if (tb) {
    tb->setPopupMode(QToolButton::InstantPopup);
  }

// now init the optional dax plugins
#ifdef DAX_DEVICE_ADAPTER
  PV_PLUGIN_IMPORT(tomvizThreshold);
  PV_PLUGIN_IMPORT(tomvizStreaming);
#endif

  ResetReaction::reset();
  // Initialize worker manager
  new ProgressDialogManager(this);

  // Initialize scale legend
  ScaleLegend* scaleLegend = new ScaleLegend(this);

  connect(viewMenuManager, SIGNAL(setScaleLegendStyle(ScaleLegendStyle)),
          scaleLegend, SLOT(setStyle(ScaleLegendStyle)));
  connect(viewMenuManager, SIGNAL(setScaleLegendVisibility(bool)), scaleLegend,
          SLOT(setVisibility(bool)));

  // Add the acquisition client experimentally.
  auto acquisitionWidget = new AcquisitionWidget(this);
  auto acquisitionAction = m_ui->menuTools->addAction("Acquisition");
  connect(acquisitionAction, SIGNAL(triggered(bool)), acquisitionWidget,
          SLOT(show()));
}

MainWindow::~MainWindow()
{
  ModuleManager::instance().reset();
  QString autosaveFile = getAutosaveFile();
  if (QFile::exists(autosaveFile) && !QFile::remove(autosaveFile)) {
    std::cerr << "Failed to remove autosave file." << std::endl;
  }
}

void MainWindow::showAbout()
{
  if (!m_aboutDialog) {
    m_aboutDialog = new AboutDialog(this);
  }
  m_aboutDialog->show();
}

void MainWindow::openTilt()
{
  QString path = QApplication::applicationDirPath() + "/../share/tomviz/Data";
  path += "/TiltSeries_NanoParticle_doi_10.1021-nl103400a.tif";
  QFileInfo info(path);
  if (info.exists()) {
    DataSource* source = LoadDataReaction::loadData(info.canonicalFilePath());
    auto op = new SetTiltAnglesOperator;
    int extents[6];
    source->getExtent(extents);
    auto numTilts = extents[5] - extents[4] + 1;
    QMap<size_t, double> tiltAngles;
    for (int i = 0; i < numTilts; ++i) {
      tiltAngles[i] = -73 + 2 * i;
    }
    op->setTiltAngles(tiltAngles);
    source->addOperator(op);
  }
}

void MainWindow::openRecon()
{
  QString path = QApplication::applicationDirPath() + "/../share/tomviz/Data";
  path += "/Recon_NanoParticle_doi_10.1021-nl103400a.tif";
  QFileInfo info(path);
  if (info.exists()) {
    LoadDataReaction::loadData(info.canonicalFilePath());
  } else {
    QMessageBox::warning(
      this, "Sample Data not found",
      QString("The data file \"%1\" was not found.").arg(path));
  }
}

void MainWindow::openDataLink()
{
  QString link = "http://www.nature.com/articles/sdata201641";
  QDesktopServices::openUrl(QUrl(link));
}

void MainWindow::openUserGuide()
{
  QString path = QApplication::applicationDirPath() +
                 "/../share/tomviz/docs/TomvizBasicUserGuide.pdf";
  QFileInfo info(path);
  if (info.exists()) {
    QUrl userGuideUrl = QUrl::fromLocalFile(path);
    QDesktopServices::openUrl(userGuideUrl);
  } else {
    QMessageBox::warning(
      this, "User Guide not found",
      QString("The user guide \"%1\" was not found.").arg(path));
  }
}

void MainWindow::dataSourceChanged(DataSource*)
{
  m_ui->propertiesPanelStackedWidget->setCurrentWidget(
    m_ui->dataPropertiesScrollArea);
}

void MainWindow::moduleChanged(Module*)
{
  m_ui->propertiesPanelStackedWidget->setCurrentWidget(
    m_ui->modulePropertiesScrollArea);
}

void MainWindow::operatorChanged(Operator*)
{
  m_ui->propertiesPanelStackedWidget->setCurrentWidget(
    m_ui->operatorPropertiesScrollArea);
}

void MainWindow::showEvent(QShowEvent* e)
{
  QMainWindow::showEvent(e);
  if (m_isFirstShow) {
    m_isFirstShow = false;
    QTimer::singleShot(1, this, SLOT(onFirstWindowShow()));
  }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
  if (ModuleManager::instance().hasRunningOperators()) {
    QMessageBox::StandardButton response = QMessageBox::question(
      this, "Close tomviz?", "You have transforms that are not completed "
                             "running in the background. These may not exit "
                             "cleanly. Are "
                             "you sure you want to try exiting anyway?");
    if (response == QMessageBox::No) {
      e->ignore();
      return;
    }
  }
  ModuleManager::instance().removeAllModules();
  ModuleManager::instance().removeAllDataSources();
  e->accept();
}

bool MainWindow::checkOpenGL()
{
  // Check for required OpenGL version, and pop up a warning dialog if needed.
  QSurfaceFormat format;
  format.setVersion(3, 2);
  QOffscreenSurface offScreen;
  offScreen.setFormat(format);
  offScreen.create();
  QOpenGLContext context;
  context.setFormat(format);
  context.create();
  context.makeCurrent(&offScreen);
  if (!context.isValid() || context.format().version() < qMakePair(3, 2)) {
    QMessageBox::critical(this, "Tomviz",
                          "This system does not support OpenGL 3.2 or higher. "
                          "The application is unlikely to function correctly.");
    return false;
  }
  return true;
}

void MainWindow::onFirstWindowShow()
{
  QFile file(getAutosaveFile());
  if (!file.exists()) {
    QSettings* settings = pqApplicationCore::instance()->settings();
    int showWelcome =
      settings->value("GeneralSettings.ShowWelcomeDialog", 1).toInt();
    if (showWelcome) {
      QString path =
        QApplication::applicationDirPath() + "/../share/tomviz/Data";
      path += "/Recon_NanoParticle_doi_10.1021-nl103400a.tif";
      QFileInfo info(path);
      if (info.exists()) {
        WelcomeDialog welcomeDialog(this);
        welcomeDialog.setModal(true);
        welcomeDialog.exec();
      }
    }
    return;
  }
  QMessageBox::StandardButton response =
    QMessageBox::question(this, "Load autosave?",
                          "There is a tomviz autosave file present.  Load it?",
                          QMessageBox::Yes | QMessageBox::No);
  if (response == QMessageBox::Yes) {
    SaveLoadStateReaction::loadState(getAutosaveFile());
  }
}

void MainWindow::autosave()
{
  SaveLoadStateReaction::saveState(getAutosaveFile(), false);
}

void MainWindow::handleMessage(const QString&, int type)
{
  QDockWidget* dock = m_ui->dockWidgetMessages;
  if (!dock->isVisible() &&
      (type == pqOutputWidget::ERROR || type == pqOutputWidget::WARNING)) {
    // if dock is not visible, we always pop it up as a floating dialog. This
    // avoids causing re-renders which may cause more errors and more confusion.
    QRect rectApp = geometry();

    QRect rectDock(QPoint(0, 0), QSize(static_cast<int>(rectApp.width() * 0.4),
                                       dock->sizeHint().height()));
    rectDock.moveCenter(QPoint(
      rectApp.center().x(), rectApp.bottom() - dock->sizeHint().height() / 2));
    dock->setFloating(true);
    dock->setGeometry(rectDock);
    dock->show();
  }
  if (dock->isVisible()) {
    dock->raise();
  }
}

}
