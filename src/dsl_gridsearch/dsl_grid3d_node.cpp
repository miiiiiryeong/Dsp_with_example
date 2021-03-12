#include "dsl_gridsearch/dsl_grid3d.h"


int main (int argc, char **argv)
{
  ros::init (argc, argv, "dsl_grid3d");
  ros::NodeHandle nh;
  ros::NodeHandle nh_private("~");

  dsl_gridsearch::DslGrid3D dslg3d(nh, nh_private);

    //ros::MultiThreadedSpinner spinner(0);
    //spinner.spin();
    //ros::AsyncSpinner spinner(0);
    //spinner.start();
    //ros::waitForShutdown();
  ros::spin();


  return 0;
}


