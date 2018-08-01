#include "Mainframe.h"

#include <fstream>
#include <iostream>
#include <map>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtWidgets/QFileDialog>

#include "../data/label_utils.h"
#include "../data/misc.h"

#include <QtWidgets/QMessageBox>

#include "../data/voxelize_utils.h"

using namespace glow;

Mainframe::Mainframe() : mChangesSinceLastSave(false) {
  ui.setupUi(this);

  connect(ui.actionOpen, SIGNAL(triggered()), this, SLOT(open()));
  connect(ui.actionSave, SIGNAL(triggered()), this, SLOT(save()));

  /** initialize the paint button mapping **/

  connect(ui.spinPointSize, SIGNAL(valueChanged(int)), ui.mViewportXYZ, SLOT(setPointSize(int)));

  connect(ui.sldTimeline, &QSlider::valueChanged, [this](int value) { setCurrentScanIdx(value); });
  connect(ui.btnForward, &QToolButton::released, [this]() { forward(); });

  connect(ui.btnBackward, &QToolButton::released, [this]() { backward(); });

  connect(ui.chkShowPoints, &QCheckBox::toggled,
          [this](bool value) { ui.mViewportXYZ->setDrawingOption("show points", value); });

  connect(ui.chkShowVoxels, &QCheckBox::toggled,
          [this](bool value) { ui.mViewportXYZ->setDrawingOption("show voxels", value); });

  connect(this, &Mainframe::readerFinshed, this, &Mainframe::updateScans);
  connect(this, &Mainframe::readerStarted, this, &Mainframe::activateSpinner);
  connect(this, &Mainframe::buildVoxelgridFinished, this, &Mainframe::updateVoxelGrids);

  connect(ui.rdoTrainVoxels, &QRadioButton::toggled,
          [this](bool value) { ui.mViewportXYZ->setDrawingOption("show train", value); });

  connect(ui.spinVoxelSize, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          [this](double value) { updateVoxelSize(value); });

  connect(this, &Mainframe::buildVoxelgridStarted, this, &Mainframe::disableGui);
  connect(this, &Mainframe::buildVoxelgridFinished, this, &Mainframe::enableGui);

  connect(ui.spinPriorScans, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this](int32_t value) {
    reader_.setNumPriorScans(value);
    setCurrentScanIdx(ui.sldTimeline->value());
  });

  connect(ui.spinPastScans, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this](int32_t value) {
    reader_.setNumPastScans(value);
    setCurrentScanIdx(ui.sldTimeline->value());
  });

  connect(ui.spinMaxRange, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          [this](double value) {
            config.maxRange = value;
            ui.mViewportXYZ->setMaxRange(value);
            setCurrentScanIdx(ui.sldTimeline->value());
          });

  connect(ui.chkShowOccluded, &QCheckBox::toggled,
          [this](bool value) { ui.mViewportXYZ->setDrawingOption("show occluded", value); });
  connect(ui.chkShowInvalid, &QCheckBox::toggled,
          [this](bool value) { ui.mViewportXYZ->setDrawingOption("show invalid", value); });

  /** load labels and colors **/
  std::map<uint32_t, std::string> label_names;
  std::map<uint32_t, glow::GlColor> label_colors;

  getLabelNames("labels.xml", label_names);
  getLabelColors("labels.xml", label_colors);

  ui.mViewportXYZ->setLabelColors(label_colors);

  config = parseConfiguration("settings.cfg");

  ui.mViewportXYZ->setFilteredLabels(config.filteredLabels);
  ui.mViewportXYZ->setDrawingOption("highlight voxels", false);

  ui.spinPriorScans->setValue(config.priorScans);
  ui.spinPastScans->setValue(config.pastScans);

  reader_.setNumPastScans(ui.spinPastScans->value());
  reader_.setNumPriorScans(ui.spinPriorScans->value());

  //  // TODO: find reasonable voxel volume size.
  //  minExtent = Eigen::Vector4f(0, -20, -2, 1);
  //  maxExtent = Eigen::Vector4f(40, 20, 1, 1);

  //  float voxelSize = ui.spinVoxelSize->value();
  ui.spinVoxelSize->setValue(config.voxelSize);
  ui.mViewportXYZ->setMaximumScans(config.maxNumScans);
  ui.mViewportXYZ->setMaxRange(config.maxRange);
  ui.spinMaxRange->setValue(config.maxRange);
  ui.mViewportXYZ->setMinRange(config.minRange);

  priorVoxelGrid_.initialize(config.voxelSize, config.minExtent, config.maxExtent);
  pastVoxelGrid_.initialize(config.voxelSize, config.minExtent, config.maxExtent);

  ui.mViewportXYZ->setVoxelGridProperties(config.voxelSize, priorVoxelGrid_.offset());
}

Mainframe::~Mainframe() {}

void Mainframe::closeEvent(QCloseEvent* event) {
  //  if (mChangesSinceLastSave) {
  //    int ret = QMessageBox::warning(this, tr("Unsaved changes."), tr("The annotation has been modified.\n"
  //                                                                    "Do you want to save your changes?"),
  //                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
  //                                   QMessageBox::Save);
  //    if (ret == QMessageBox::Save)
  //      save();
  //    else if (ret == QMessageBox::Cancel) {
  //      event->ignore();
  //      return;
  //    }
  //  }
  statusBar()->showMessage("Writing labels...");

  event->accept();
}

void Mainframe::open() {
  //  if (mChangesSinceLastSave) {
  //    int ret = QMessageBox::warning(this, tr("Unsaved changes."), tr("The annotation has been modified.\n"
  //                                                                    "Do you want to save your changes?"),
  //                                   QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard,
  //                                   QMessageBox::Save);
  //    if (ret == QMessageBox::Save)
  //      save();
  //    else if (ret == QMessageBox::Cancel)
  //      return;
  //  }

  QString retValue =
      QFileDialog::getExistingDirectory(this, "Select scan directory", lastDirectory, QFileDialog::ShowDirsOnly);

  if (!retValue.isNull()) {
    QDir base_dir(retValue);

    if (!base_dir.exists("velodyne") || !base_dir.exists("poses.txt")) {
      std::cout << "[ERROR] velodyne or poses.txt missing." << std::endl;
      return;
    }

    reader_.initialize(retValue);

    //    ui.sldTimeline->setMaximum(reader_.count());
    ui.btnBackward->setEnabled(false);
    ui.btnForward->setEnabled(false);
    if (reader_.count() > 0) ui.btnForward->setEnabled(true);

    //    if (ui.sldTimeline->value() == 0) setCurrentScanIdx(0);
    //    ui.sldTimeline->setValue(0);

    readerFuture_ = std::async(std::launch::async, &Mainframe::readAsync, this, 0);

    ui.sldTimeline->setEnabled(false);
    ui.sldTimeline->setMaximum(reader_.count());
    ui.sldTimeline->setValue(0);

    lastDirectory = base_dir.absolutePath();

    QString title = "Voxelizer - ";
    title += QFileInfo(retValue).completeBaseName();
    setWindowTitle(title);

    mChangesSinceLastSave = false;
  }
}

void Mainframe::save() {
  saveVoxelGrid(priorVoxelGrid_, "input.mat");
  saveVoxelGrid(pastVoxelGrid_, "labels.mat");
}

void Mainframe::unsavedChanges() { mChangesSinceLastSave = true; }

void Mainframe::setCurrentScanIdx(int32_t idx) {
  std::cout << "setCurrentScanIdx(" << idx << ")" << std::endl;
  if (reader_.count() == 0) return;
  readerFuture_ = std::async(std::launch::async, &Mainframe::readAsync, this, idx);

  ui.sldTimeline->setEnabled(false);
}

void Mainframe::readAsync(uint32_t idx) {
  // TODO progress indicator.
  emit readerStarted();

  ui.sldTimeline->setEnabled(false);

  std::vector<PointcloudPtr> priorPoints;
  std::vector<LabelsPtr> priorLabels;
  std::vector<PointcloudPtr> pastPoints;
  std::vector<LabelsPtr> pastLabels;

  reader_.retrieve(idx, priorPoints, priorLabels, pastPoints, pastLabels);

  priorPoints_ = priorPoints;
  priorLabels_ = priorLabels;
  pastPoints_ = pastPoints;
  pastLabels_ = pastLabels;

  emit readerFinshed();

  buildVoxelGrids();

  ui.sldTimeline->setEnabled(true);
}

void Mainframe::disableGui() {
  ui.spinVoxelSize->setEnabled(false);
  ui.spinPastScans->setEnabled(false);
  ui.spinPriorScans->setEnabled(false);
  ui.sldTimeline->setEnabled(false);
}

void Mainframe::enableGui() {
  ui.spinVoxelSize->setEnabled(true);
  ui.spinPastScans->setEnabled(true);
  ui.spinPriorScans->setEnabled(true);
  ui.sldTimeline->setEnabled(true);
}

void Mainframe::buildVoxelGrids() {
  emit buildVoxelgridStarted();

  priorVoxelGrid_.clear();
  pastVoxelGrid_.clear();

  if (priorPoints_.size() > 0) {
    Eigen::Matrix4f anchor_pose = priorPoints_.back()->pose;

    fillVoxelGrid(anchor_pose, priorPoints_, priorLabels_, priorVoxelGrid_, config);

    fillVoxelGrid(anchor_pose, priorPoints_, priorLabels_, pastVoxelGrid_, config);
    fillVoxelGrid(anchor_pose, pastPoints_, pastLabels_, pastVoxelGrid_, config);

    // updating occlusions.
    //    std::cout << "updating occlusions." << std::endl;
    priorVoxelGrid_.updateOcclusions();
    pastVoxelGrid_.updateOcclusions();

    priorVoxelGrid_.insertOcclusionLabels();
    pastVoxelGrid_.insertOcclusionLabels();

    for (uint32_t i = 0; i < pastPoints_.size(); ++i) {
      Eigen::Vector3f endpoint = (anchor_pose.inverse() * pastPoints_[i]->pose).col(3).head(3);
      pastVoxelGrid_.updateInvalid(endpoint);
    }

    // only visualization code.
    priorVoxels_.clear();
    pastVoxels_.clear();
    // extract voxels and labels.
    extractLabeledVoxels(priorVoxelGrid_, priorVoxels_);
    extractLabeledVoxels(pastVoxelGrid_, pastVoxels_);

    //    std::cout << "end" << std::endl;
  }

  emit buildVoxelgridFinished();
}

void Mainframe::updateVoxelGrids() {
  ui.mViewportXYZ->setVoxelGridProperties(std::max<float>(0.01, ui.spinVoxelSize->value()), priorVoxelGrid_.offset());
  ui.mViewportXYZ->setVoxels(priorVoxels_, pastVoxels_);

  if (visited_.size() > 0) {
    std::vector<LabeledVoxel> voxels;
    float voxelSize = priorVoxelGrid_.resolution();
    Eigen::Vector4f offset = priorVoxelGrid_.offset();

    for (uint32_t i = 0; i < visited_.size(); ++i) {
      LabeledVoxel lv;
      Eigen::Vector4f pos = offset + Eigen::Vector4f(visited_[i].x() * voxelSize, visited_[i].y() * voxelSize,
                                                     visited_[i].z() * voxelSize, 0.0f);
      lv.position = vec3(pos.x(), pos.y(), pos.z());
      lv.label = 11;
      voxels.push_back(lv);
    }

    ui.mViewportXYZ->highlightVoxels(voxels);
  }

  updateOccludedVoxels();
  updateInvalidVoxels();
}

void Mainframe::updateOccludedVoxels() {
  VoxelGrid& grid = (ui.rdoTrainVoxels->isChecked()) ? priorVoxelGrid_ : pastVoxelGrid_;
  std::vector<LabeledVoxel> voxels;
  float voxelSize = grid.resolution();
  Eigen::Vector4f offset = grid.offset();

  for (uint32_t x = 0; x < grid.size(0); ++x) {
    for (uint32_t y = 0; y < grid.size(1); ++y) {
      for (uint32_t z = 0; z < grid.size(2); ++z) {
        if (!grid.isOccluded(x, y, z)) continue;
        LabeledVoxel lv;
        Eigen::Vector4f pos =
            offset + Eigen::Vector4f(x * voxelSize + 0.1, y * voxelSize + 0.1, z * voxelSize + 0.1, 0.0f);
        lv.position = vec3(pos.x(), pos.y(), pos.z());
        lv.label = 11;
        voxels.push_back(lv);
      }
    }
  }

  ui.mViewportXYZ->setOcclusionVoxels(voxels);
}

void Mainframe::updateInvalidVoxels() {
  if (invalidVoxels_.size() > 0) {
    std::vector<LabeledVoxel> voxels;

    float voxelSize = pastVoxelGrid_.resolution();
    Eigen::Vector4f offset = pastVoxelGrid_.offset();

    for (uint32_t x = 0; x < pastVoxelGrid_.size(0); ++x) {
      for (uint32_t y = 0; y < pastVoxelGrid_.size(1); ++y) {
        for (uint32_t z = 0; z < pastVoxelGrid_.size(2); ++z) {
          if (!pastVoxelGrid_.isInvalid(x, y, z)) continue;

          LabeledVoxel lv;
          Eigen::Vector4f pos =
              offset + Eigen::Vector4f(x * voxelSize + 0.1, y * voxelSize + 0.1, z * voxelSize + 0.1, 0.0f);
          lv.position = vec3(pos.x(), pos.y(), pos.z());
          lv.label = 11;
          voxels.push_back(lv);
        }
      }
    }

    ui.mViewportXYZ->setInvalidVoxels(voxels);
  }
}

void Mainframe::extractLabeledVoxels(const VoxelGrid& grid, std::vector<LabeledVoxel>& labeledVoxels) {
  labeledVoxels.clear();
  // fixme: iterate only over occuppied voxels.
  Eigen::Vector4f offset = grid.offset();
  float voxelSize = grid.resolution();

  for (uint32_t x = 0; x < grid.size(0); ++x) {
    for (uint32_t y = 0; y < grid.size(1); ++y) {
      for (uint32_t z = 0; z < grid.size(2); ++z) {
        const VoxelGrid::Voxel& v = grid(x, y, z);
        if (v.count > 0) {
          LabeledVoxel lv;
          Eigen::Vector4f pos = offset + Eigen::Vector4f(x * voxelSize, y * voxelSize, z * voxelSize, 0.0f);
          lv.position = vec3(pos.x(), pos.y(), pos.z());

          uint32_t maxCount = 0;
          uint32_t maxLabel = 0;

          for (auto it = v.labels.begin(); it != v.labels.end(); ++it) {
            if (it->second > maxCount) {
              maxCount = it->second;
              maxLabel = it->first;
            }
          }

          lv.label = maxLabel;
          labeledVoxels.push_back(lv);
        }
      }
    }
  }
}

void Mainframe::activateSpinner() { statusBar()->showMessage("     Reading scans..."); }

void Mainframe::updateScans() {
  statusBar()->clearMessage();
  glow::_CheckGlError(__FILE__, __LINE__);
  ui.mViewportXYZ->setPoints(priorPoints_, priorLabels_, pastPoints_, pastLabels_);
  glow::_CheckGlError(__FILE__, __LINE__);
}

void Mainframe::updateVoxelSize(float voxelSize) {
  voxelSize = std::max<float>(0.01, voxelSize);
  priorVoxelGrid_.initialize(voxelSize, config.minExtent, config.maxExtent);
  pastVoxelGrid_.initialize(voxelSize, config.minExtent, config.maxExtent);

  readerFuture_ = std::async(std::launch::async, &Mainframe::buildVoxelGrids, this);
}

void Mainframe::forward() {
  int32_t value = ui.sldTimeline->value() + 1;
  if (value < int32_t(reader_.count())) ui.sldTimeline->setValue(value);
  ui.btnBackward->setEnabled(true);
  if (value == int32_t(reader_.count()) - 1) ui.btnForward->setEnabled(false);
}

void Mainframe::backward() {
  int32_t value = ui.sldTimeline->value() - 1;
  if (value >= 0) ui.sldTimeline->setValue(value);
  ui.btnForward->setEnabled(true);
  if (value == 0) ui.btnBackward->setEnabled(false);
}
void Mainframe::readConfig() {}

void Mainframe::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_D || event->key() == Qt::Key_Right) {
    if (ui.btnForward->isEnabled() && ui.sldTimeline->isEnabled()) forward();

  } else if (event->key() == Qt::Key_A || event->key() == Qt::Key_Left) {
    if (ui.btnBackward->isEnabled() && ui.sldTimeline->isEnabled()) backward();
  }
}
