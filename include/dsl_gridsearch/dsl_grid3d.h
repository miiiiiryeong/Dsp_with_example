#ifndef _DSL_GRID_3D_H_
#define _DSL_GRID_3D_H_

#include <ros/ros.h>
#include <geometry_msgs/Point.h>
#include <nav_msgs/Path.h>
#include <nav_msgs/Odometry.h>
#include <exploration/Frontier.h>

#include "dsl/gridsearch.h"
#include "dsl/gridcost.h"
#include "dsl/grid3d.h"
#include "dsl/grid3dconnectivity.h"
#include "dsl_gridsearch/occupancy_grid.h"


#include <geometry_msgs/Point.h>
#include <octomap_msgs/Octomap.h>
#include <octomap_msgs/conversions.h>
#include <octomap/octomap.h>

#include <memory>

namespace dsl_gridsearch
{

class DslGrid3D
{
public:
  DslGrid3D(ros::NodeHandle nh, ros::NodeHandle nh_private);

private:
  void updateGDSL(std::shared_ptr<octomap::OcTree> tree);
  void buildGDSL(std::shared_ptr<octomap::OcTree> tree);
  void saftyMarginal(Eigen::Vector3d pos, bool update);

  void handleSetStart(const geometry_msgs::PointConstPtr& msg);
  void handleSetStartOdom(const nav_msgs::Odometry msg);
  void setStart(Eigen::Vector3d wpos);
  void handleSetGoal(const geometry_msgs::PointConstPtr& msg);
  void handleSetFrontier(const exploration::FrontierConstPtr& msg);
  void setGoal(Eigen::Vector3d wpos);
  Eigen::Vector3d posRes(Eigen::Vector3d wpos);

  void handleSetOccupied(const geometry_msgs::PointConstPtr& msg);
  void handleSetUnoccupied(const geometry_msgs::PointConstPtr& msg);
  void spin(const ros::TimerEvent& e);
  void octomap_data_callback(const octomap_msgs::OctomapConstPtr& msg);

  void handleSetOccupied(Eigen::Vector3d wpos);
  void handleSetUnoccupied(Eigen::Vector3d wpos);

  void publishAllPaths();
  void publishOccupancyGrid();

  void planAllPaths();
  nav_msgs::Path dslPathToRosMsg(const dsl::GridPath<3>& dsl_path);
  nav_msgs::Path dslPathToRosMsg(const std::vector<Eigen::Vector3d>& dsl_path);
  bool isPosInBounds(const Eigen::Vector3d& pos);

  std::shared_ptr<dsl::Grid3d> grid_;
  dsl::GridCost<3> cost_;
  std::shared_ptr<dsl::Grid3dConnectivity> connectivity_;
  std::shared_ptr<dsl::GridSearch<3>> gdsl_;
  dsl::GridPath<3> path_, optpath_, splinecells_, splineoptcells_;
  std::vector<Eigen::Vector3d> splinepath_, splineoptpath_;
  //std::shared_ptr<OccupancyGrid> ogrid_;

  ros::NodeHandle nh_;
  ros::NodeHandle nh_private_;

  ros::Publisher occ_map_viz_pub_;
  ros::Publisher path_pub_;
  ros::Publisher optpath_pub_;
  ros::Publisher splinepath_pub_;
  ros::Publisher splineoptpath_pub_;

  ros::Subscriber set_start_sub_;
  ros::Subscriber set_start_odom_sub_;
  ros::Subscriber set_goal_sub_;
  ros::Subscriber set_frontier_sub;
  ros::Subscriber set_occupied_sub_;
  ros::Subscriber set_unoccupied_sub_;
  ros::Subscriber get_octomap_sub_;
  ros::Subscriber get_pos_sub_;

  ros::Timer timer;

  double cells_per_meter_;
  double spline_step_;
  bool use_gazebo_odom_;
  std::string odom_topic_;
  std::string odom_frame_id_;
  int DSL_UNKNOWN;
  int grid_length_;
  int grid_width_;
  int grid_height_;
  int length_metric;
  int width_metric;
  int height_metric;
  double res_octomap;
  std::shared_ptr<double[]> occupancy_map;
  
  Eigen::Matrix<double, 3,3> rot;
  Eigen::Vector3d first_pos;
  Eigen::Vector3d start_pos;
  Eigen::Vector3d goal_pos;
  bool start_set = false;
  bool goal_set = false;
  double length = -1.0;
  double width = -1.0;
  double height = -1.0;
  double xmin, ymin, zmin, xmax, ymax, zmax, cells_per_meter;
  Eigen::Vector3d pmin;
  Eigen::Vector3d pmax;

  int seq;
  std::string pwd = "/home/grammers/temp_log/";
};


} // namespace

#endif
