#ifndef SRC_DATA_VOXELIZE_UTILS_H_
#define SRC_DATA_VOXELIZE_UTILS_H_

#include "Pointcloud.h"
#include "VoxelGrid.h"
#include "common.h"

class Config {
 public:
  float voxelSize{0.5};                                       // size of a voxel
  Eigen::Vector4f minExtent{Eigen::Vector4f(0, -20, -2, 1)};  // minimum coordinate to consider for voxelgrid creation
  Eigen::Vector4f maxExtent{Eigen::Vector4f(40, 20, 1, 1)};   // maximum coordinate to consider for voxelgrid creation

  uint32_t maxNumScans{100};
  uint32_t priorScans{0};    // number of scans before the current scan to consider.
  uint32_t pastScans{10};    // number of scans after the current scan to consider.
  float pastDistance{0.0f};  // ensure this distance of past scans to the current scan. (might imply that more then past
                             // scans are used.)
  float minRange{2.5f};      // minimum distance of laser points to consider.
  float maxRange{25.0f};     // maximum distance of laser points to consider.
  std::vector<uint32_t> filteredLabels;                    // ignored labels
  std::map<uint32_t, std::vector<uint32_t>> joinedLabels;  // labels that get joined into a specific class.

  // gen_data specific values.
  uint32_t stride_num{0};       // number of scans between generated voxel grids
  float stride_distance{0.0f};  // trajectory distance between generated voxel grids.
};

/** \brief parse a given filename and fill configuration. **/
Config parseConfiguration(const std::string& filename);

/** \brief use given anchor_pose to insert all point clouds into the given VoxelGrid
 *
 *  Iterates over all point clouds and inserts label into the label histogram of the respective voxel.
 *  However, only points inside max_range/min_range and outside the bounding box of the car are inserted.
 *
 *  \author behley
 **/
void fillVoxelGrid(const Eigen::Matrix4f& anchor_pose, const std::vector<PointcloudPtr>& points,
                   const std::vector<LabelsPtr>& labels, VoxelGrid& grid, const Config& config);

void fillVoxelGridMat(int32_t*& labels, VoxelGrid& grid);
/** \brief save given voxelgrid as .mat file.
 *
 *  \author mgarbade
 **/
void saveVoxelGrid(const VoxelGrid& grid, const std::string& filename);

#endif /* SRC_DATA_VOXELIZE_UTILS_H_ */
