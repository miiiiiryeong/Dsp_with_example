#include "dsp/dsp.h"


namespace dsp
{

Dsp::Dsp(ros::NodeHandle nh, ros::NodeHandle nh_private) :
  nh_(nh),
  nh_private_(nh_private)
{
    ROS_INFO("Dsp::Dsp(ros::NodeHandle nh, ros::NodeHandle nh_private)");        

    if (!nh_private_.getParam ("map_topic", map_topic_))
        map_topic_ = "octomap_full";
    if (!nh_private_.getParam ("spline_step_", spline_step_))
        spline_step_ = 0.01;
    if (!nh_private_.getParam ("lower_thresh", lower_thresh_))
        lower_thresh_ = 59;
    if (!nh_private_.getParam ("upper_thresh", upper_thresh_))
        upper_thresh_ = 60;
    if (!nh_private_.getParam ("risk", risk_))
        risk_ = 2;

    if (!nh_private_.getParam ("use_gazebo_odom", use_gazebo_odom_))
        use_gazebo_odom_ = false;
    if (!nh_private_.getParam ("use_3d", use_3d_))
        use_3d_ = true;
    if (!nh_private_.getParam ("odom_topic", odom_topic_))
        // original
        // odom_topic_ = "pixy/truth/NWU";
        // edited
        // need to set odom topic name
        odom_topic_ = "odometry/imu";
    if (!nh_private_.getParam ("odom_frame_id", odom_frame_id_))
        odom_frame_id_ = "odom";
    if (!nh_private_.getParam ("base_link_frame", base_link_frame_))
        base_link_frame_ = "velodyne";
    if (!nh_private_.getParam ("unknown_value", DSP_UNKNOWN))
        DSP_UNKNOWN = 10000;
    if (!nh_private_.getParam ("update_rate", RATE))
        RATE = 1;

    occ_map_viz_pub_ = nh_.advertise<visualization_msgs::Marker>( "dsp/occupancy_map",  0);
    path_pub_ = nh_.advertise<nav_msgs::Path>( "dsp/path",  0);
    splinepath_pub_ = nh_.advertise<nav_msgs::Path>( "dsp/splinepath",  0);

    if(use_gazebo_odom_)
    {
        ROS_INFO("Using odom ass start");
        set_start_odom_sub_ = nh_.subscribe<nav_msgs::Odometry>(odom_topic_, 1, 
          &Dsp::handleSetStartOdom, this);
    }
    else
    {
        ROS_INFO("Manualy set start");
        set_start_sub_ = nh_.subscribe<geometry_msgs::Point>("dsp/set_start", 1, 
          &Dsp::handleSetStart, this);
    }

    if(use_3d_){
        get_octomap_sub_ = nh_.subscribe<octomap_msgs::Octomap>(map_topic_, 1,
            &Dsp::octomap_data_callback, this);
    }
    else {
        get_octomap_sub_ = nh_.subscribe<nav_msgs::OccupancyGrid>(map_topic_, 1,
            &Dsp::occupancy_grid_callback, this);
        height_voxel = 1;
    }

    set_goal_sub_ = nh_.subscribe<geometry_msgs::Point>("dsp/set_goal", 1,
        &Dsp::handleSetGoal, this);

    if(RATE > 0.0)
        path_update_timer = nh_.createTimer(ros::Duration(RATE), &Dsp::pathUpdateCallback, this);


    tran = &listener;

    cost_srv_ = nh_.advertiseService("dsp/path_cost", &Dsp::request_cost, this);

    start_pos << 0,0,0; 
    goal_pos << 0,0,0; 
}

// 2D occupancyGrid callback 
void Dsp::occupancy_grid_callback(const nav_msgs::OccupancyGridConstPtr& msg){

    pmin(0) = msg->info.origin.position.x;
    pmin(1) = msg->info.origin.position.y;
    pmin(2) = 0;

    if (length * width == msg->info.width * msg->info.height){
        int size = length_voxel * width_voxel;
        for (int i = 0; i < size; i++){
            Eigen::Vector3d pos(i % length_voxel, i / length_voxel, 0);
            if(msg->data[i] > upper_thresh_ && occupancy_map[i] != DSP_OCCUPIED){
                saftyMarginal(pos, true);
                occupancy_map[i] = DSP_OCCUPIED;
                gdsl_->SetCost(pos, DSP_OCCUPIED);
            }
            else if (msg->data[i] < lower_thresh_ && msg->data[i] != -1 && occupancy_map[i] >= DSP_UNKNOWN){
                if (occupancy_map[i] == DSP_OCCUPIED){
                    gdsl_->SetCost(pos, 1);
                    occupancy_map[i] = 1;
                    saftyMarginalLoop(pos);
                } else {
                    gdsl_->SetCost(pos, occupancy_map[i] - DSP_UNKNOWN + 1);
                    occupancy_map[i] = occupancy_map[i] - DSP_UNKNOWN + 1;
                }
            }
            else if (msg->data[i] == -1){
                if (occupancy_map[i] < DSP_UNKNOWN){
                    gdsl_->SetCost(pos, occupancy_map[i] + DSP_UNKNOWN);
                    occupancy_map[i] = occupancy_map[i] + DSP_UNKNOWN;
                }
                else if (occupancy_map[i] == DSP_OCCUPIED){
                    gdsl_->SetCost(pos, DSP_UNKNOWN);
                    occupancy_map[i] = DSP_UNKNOWN;
                    saftyMarginalLoop(pos);
                }
            }
        }
    }
    else {
    
        width = msg->info.height;
        length = msg->info.width;
        res_octomap = msg->info.resolution;
        grid_built = true;

        length_voxel = length; 
        width_voxel = width; 
        int size = length_voxel * width_voxel;
        occupancy_map.reset(new double[size]);

        for(int i = 0; i < size; i++){
            occupancy_map[i] = DSP_UNKNOWN; 
        }

        for(int i = 0; i < size; i++){
            if(msg->data[i] > upper_thresh_){
                Eigen::Vector3d pos(i % length_voxel, i / length_voxel, 0);
                saftyMarginal(pos, false);
                occupancy_map[i] = DSP_OCCUPIED;
            }
            else if (msg->data[i] < lower_thresh_ && msg->data[i] != -1 && occupancy_map[i] >= DSP_UNKNOWN){
                occupancy_map[i] = occupancy_map[i] - DSP_UNKNOWN + 1;
            }
                
        }
        buildGraph();
    }
    //publishOccupancyGrid();
    //setAndPublishPath();
    return;
}

// octomap 3D callback
void Dsp::octomap_data_callback(const octomap_msgs::OctomapConstPtr& msg) {
    //std::cout<<"1"<<std::endl;
    std::shared_ptr<octomap::OcTree> tree(dynamic_cast<octomap::OcTree*>(octomap_msgs::msgToMap(*msg)));
    res_octomap = tree->getResolution();
    double length_test, width_test, height_test;
    tree->getMetricSize(length_test, width_test, height_test);
    tree->getMetricMin(pmin(0), pmin(1), pmin(2));

    if (length_test * width_test * height_test == length * width * height)
    {
        updateGDSP(tree);
    }
    else
    {

        length = length_test;
        width = width_test;
        height = height_test;
        buildGDSP(tree);
        grid_built = true;
    }
    //publishOccupancyGrid();
    //setAndPublishPath();
}


void Dsp::buildGDSP(std::shared_ptr<octomap::OcTree> tree)
{
    //std::cout<<"2"<<std::endl;
    length_voxel = round(length/res_octomap);
    width_voxel = round(width/res_octomap);
    height_voxel = round(height/res_octomap);

    int size = length_voxel * width_voxel * height_voxel;
    occupancy_map.reset(new double[size]);

   for(int i = 0; i < size; i++)
   {
     occupancy_map[i] = DSP_UNKNOWN; 
   }

   for(octomap::OcTree::leaf_iterator it = tree->begin_leafs(), end = tree->end_leafs(); it != end; ++it)
    {
        Eigen::Vector3d pos(it.getX(), it.getY(), it.getZ());
        pos = posRes(pos);
        int n = it.getSize() / res_octomap;
        
        if (n != 1){
            for (int i = 0; i < 3; i++){
                pos(i) = pos(i) + 0.5;
            }
        }
        int i = -n/2;
	//it->getOccupancy();
        // octomap may have lage nodes. loops will add voxels for entire large node
        do{
            int j = -n/2;
            do{
                int k = -n/2;
                do{
	            int idx = ((int) pos(0) + i) + (((int) pos(1) + j) * length_voxel) + (((int) pos(2) + k) * length_voxel * width_voxel);
	            if(tree->isNodeOccupied(*it)) {
                        Eigen::Vector3d p(pos(0) + i, pos(1) + j, pos(2) + k);
                        saftyMarginal(p, false);
		        occupancy_map[idx] = DSP_OCCUPIED;
	            }
                    else {//if (occupancy_map[idx] >= DSP_UNKNOWN){
                        occupancy_map[idx] = occupancy_map[idx] - DSP_UNKNOWN + 1;
                    }
                    k++;
                } while (k < n/2);
                j++;
            } while (j < n/2); 
            i++;
        } while ( i < n/2);
    }


    //Bound occupancy_map from top and bottom
/*      for(int x = 0; x < ogrid_->getLength(); x++)
    {  
        for(int y = 0; y < width_voxel; y++)  
        {
            //for(int z = 0; z < height_voxel; z++)  
            //{
                Eigen::Vector3i gpMin(x, y, zmin);
                Eigen::Vector3d wpMin = ogrid_->gridToPosition(gpMin);
                ogrid_->setOccupied(wpMin, true);

                Eigen::Vector3i gpMax(x, y, height_voxel-1);
                Eigen::Vector3d wpMax = ogrid_->gridToPosition(gpMax);
                ogrid_->setOccupied(wpMax, true);
            //}
        }
    }
*/
  buildGraph();
}

void Dsp::buildGraph(){
    //std::cout<<"3"<<std::endl;
    ROS_INFO("Building search graph...");
    int size = length_voxel * width_voxel * height_voxel;
    grid_.reset(new dsl::Grid3d(length_voxel, width_voxel, height_voxel, 
        occupancy_map.get(),
        1, 1, 1, 1, DSP_OCCUPIED + 1));
    connectivity_.reset(new dsl::Grid3dConnectivity(*grid_));
    gdsl_.reset(new dsl::GridSearch<3>(*grid_, *connectivity_, cost_, true));
    ROS_INFO("Graph built");
}

void Dsp::updateGDSP(std::shared_ptr<octomap::OcTree> tree)
{
    //std::cout<<"4"<<std::endl;
    for(octomap::OcTree::leaf_iterator it = tree->begin_leafs(), end = tree->end_leafs(); it != end; ++it)
    {
        Eigen::Vector3d pos(it.getX(), it.getY(), it.getZ());
        pos = posRes(pos);

            
        int n = it.getSize() / res_octomap;
        if (n != 1){
            for (int i = 0; i < 3; i++){
                pos(i) = pos(i) + 0.5;
            }
        }
        int i = -n/2;
	//it->getOccupancy();
        // octomap may have lage nodes. loops will add voxels for entire large node
        do{
            int j = -n/2;
            do{
                int k = -n/2;
                do{
	            int idx = ((int) pos(0) + i) + ((int) pos(1) + j) *length_voxel + ((int) pos(2) + k) *length_voxel*width_voxel;
                    Eigen::Vector3d p(pos(0) + i, pos(1) + j, pos(2) + k);

                    if(tree->isNodeOccupied(*it) and occupancy_map[idx] != DSP_OCCUPIED)
                    {
                        gdsl_->SetCost(p, DSP_OCCUPIED);
                        occupancy_map[idx] = DSP_OCCUPIED;
                        saftyMarginal(p, true);
                    }			
                    else if(!tree->isNodeOccupied(*it)  and occupancy_map[idx] >= DSP_UNKNOWN)
                    {
                        if(occupancy_map[idx] < DSP_OCCUPIED)
                        {
                            gdsl_->SetCost(p, occupancy_map[idx] - DSP_UNKNOWN + 1);
                            occupancy_map[idx] = occupancy_map[idx] - DSP_UNKNOWN + 1;
                        }
                        else
                        {
                            gdsl_->SetCost(p, 1);
                            occupancy_map[idx] = 1;
                            saftyMarginalLoop(pos);
                        }
                    }
                    k++;
                } while (k < n/2);
                j++;
            } while (j < n/2); 
            i++;
        } while ( i < n/2);
    }
    return;
}

// adding of safty margianl to occupied space inprove safty
void Dsp::saftyMarginal(Eigen::Vector3d pos, bool update)
{
    //std::cout<<"5"<<std::endl;
    Eigen::Vector3d local_pose;
    for(int i = -risk_; i <= risk_; i++){
        for(int j = -risk_; j <= risk_; j++){
            for(int k = -risk_; k <= risk_; k++){
                int x = pos(0) + i;
                int y = pos(1) + j;
                int z = pos(2) + k;
                if(x >= 0 && x < length_voxel && y >= 0 && y < width_voxel && z >= 0 && z < height_voxel){
	            int idx = x + y*length_voxel + z*length_voxel*width_voxel;
                    local_pose << pos(0) + i, pos(1) + j, pos(2) + k;
                    int sum = i * i + j * j + k * k;
                    if (!sum){ // sum = 0 > current ocupied space
                        continue;
                    }
                    int cost = DSP_UNKNOWN / (sum + 2);
                    if(occupancy_map[idx] < cost){
                        occupancy_map[idx] = cost;
                        if(update){
                            gdsl_->SetCost(local_pose, cost);
                        }
                    }
                    else if (DSP_UNKNOWN == occupancy_map[idx] or (occupancy_map[idx] >= DSP_UNKNOWN and occupancy_map[idx] < DSP_UNKNOWN + cost)){
                        cost += DSP_UNKNOWN;
                        occupancy_map[idx] = cost;
                        if(update){
                            gdsl_->SetCost(local_pose, cost);
                        }
                    }
                }
            }
        }
    }
}

// adding safty marginal to cells descoverd free
void Dsp::saftyMarginalFree(Eigen::Vector3d pos)
{
    //std::cout<<"6"<<std::endl;
    int sum = risk_ * risk_ * 3 + 1;
    int index;
    Eigen::Vector3d local_pose;
    for(int i = -risk_; i <= risk_; i++){
        for(int j = -risk_; j <= risk_; j++){
            for(int k = -risk_; k <= risk_; k++){
                int x = (int)round(pos(0)) + i;
                int y = (int)round(pos(1)) + j;
                int z = (int)round(pos(2)) + k;
                if(x >= 0 && x < length_voxel && y >= 0 && y < width_voxel && z >= 0 && z < height_voxel){
	            int idx = x + y*length_voxel + z*length_voxel*width_voxel;
                    if (occupancy_map[idx] == DSP_OCCUPIED){
                        if (i * i + j * j + k * k < sum){
                            sum = i * i + j * j + k * k;
                            local_pose << pos(0) + i, pos(1) + j, pos(2) + k;
                            index = idx;
                        }
                    }
                }
            }
        }
    }
    int idx = pos(0) + pos(1)*length_voxel + pos(2)*length_voxel*width_voxel;
    if (idx < 0 or idx > (length_voxel * width_voxel * height_voxel)){
	//return;
    }
    if (sum <= risk_ * risk_ * 3){
        int cost = DSP_UNKNOWN / (sum + 2);
        if (occupancy_map[index] < DSP_UNKNOWN){
            if(occupancy_map[idx] < cost){
                occupancy_map[idx] = cost;
                gdsl_->SetCost(pos, cost);
            }
        }
        else if (occupancy_map[idx] >= DSP_UNKNOWN && occupancy_map[index] != DSP_OCCUPIED){
            cost = cost + DSP_UNKNOWN;
            if (cost > occupancy_map[idx]){
                occupancy_map[idx] = cost;
                gdsl_->SetCost(pos, cost);
            }
        }

    } else {
        if (occupancy_map[idx] < DSP_UNKNOWN){
            gdsl_->SetCost(pos, 1);
            occupancy_map[idx] = 1;
        } else {
            gdsl_->SetCost(pos, DSP_UNKNOWN);
            occupancy_map[idx] = DSP_UNKNOWN;
        }
    }
}

void Dsp::saftyMarginalLoop(Eigen::Vector3d pos){
    
    //std::cout<<"7"<<std::endl;
    Eigen::Vector3d local_pose;
    for(int i = -risk_; i <= risk_; i++){
        for(int j = -risk_; j <= risk_; j++){
            for(int k = -risk_; k <= risk_; k++){
                int x = (int)round(pos(0)) + i;
                int y = (int)round(pos(1)) + j;
                int z = (int)round(pos(2)) + k;
                if(x >= 0 && x < length_voxel && y >= 0 && y < width_voxel && z >= 0 && z < height_voxel){
                    local_pose << x, y, z;
                    saftyMarginalFree(local_pose);
                }
            }
        }
    }
    
}

void Dsp::handleSetStartOdom(const nav_msgs::Odometry msg)
{
    //std::cout<<"8"<<std::endl;
    Eigen::Vector3d wpos(msg.pose.pose.position.x , msg.pose.pose.position.y, msg.pose.pose.position.z);
    //setStart(wpos);
    //setAndPublishPath();

    
}

bool Dsp::request_cost(dsp::pathCost::Request &req, dsp::pathCost::Response &res){
    //std::cout<<"9"<<std::endl;

    Eigen::Vector3d start_req(req.start.x, req.start.y, req.start.z);
    Eigen::Vector3d goal_req(req.stop.x, req.stop.y, req.stop.z);


    if(!setSG(start_req, goal_req)){
        return false;
    }

    dsl::GridPath<3> path;
    gdsl_->Plan(path);
    res.cost = path.cost;
    nav_msgs::Path ros_path = dspPathToRosMsg(path,false);
    res.path = ros_path;

    return true;

}

void Dsp::handleSetStart(const geometry_msgs::PointConstPtr& msg)
{
    //std::cout<<"10"<<std::endl;
    Eigen::Vector3d wpos(msg->x, msg->y, msg->z);
    setStart(wpos);
}

void Dsp::setTfStart(){
    //std::cout<<"11"<<std::endl;
    // original
    tf::StampedTransform transform;
    try {
        tran->lookupTransform(odom_frame_id_, base_link_frame_, ros::Time(0.0), transform);
    } catch (tf::TransformException ex){
        ROS_ERROR("%s", ex.what());
        return;
    }
    Eigen::Vector3d wpos(transform.getOrigin().x(),
            transform.getOrigin().y(),
            transform.getOrigin().z());
    if(!use_3d_)
        wpos[2] = 0.0;
    setStart(wpos);

    // // world <-> base_link!
    // static tf::TransformBroadcaster br;
    // tf::Transform transform_2;

    // // set transform info.
    // transform_2.setOrigin(tf::Vector3(transform.getOrigin().x(),
    //     transform.getOrigin().y(),
    //     transform.getOrigin().z()));

    // // send transform
    // ros::Time now = ros::Time::now();
    // br.sendTransform(tf::StampedTransform(transform_2, now, "world", "base_link"));

}

void Dsp::setStart(Eigen::Vector3d wpos)
{
    //std::cout<<"12"<<std::endl;
    if(!use_3d_){
        wpos[2] = 0;
    }
    start_pos = wpos; 
    //setAndPublishPath();
}

void Dsp::handleSetGoal(const geometry_msgs::PointConstPtr& msg)
{
    //std::cout<<"13"<<std::endl;
    Eigen::Vector3d wpos(msg->x, msg->y, msg->z);
    setGoal(wpos);
}

void Dsp::setGoal(Eigen::Vector3d wpos)
{
    //std::cout<<"14"<<std::endl;
    if(!use_3d_){
        wpos[2] = 0;
    }
    goal_pos = wpos; 
    setAndPublishPath();
}

void Dsp::setAndPublishPath(){
    //std::cout<<"15"<<std::endl;
    if(!grid_built){
        return;
    }
    setTfStart();
    //Eigen::Vector3d grid_start = posRes(start_pos);
    //Eigen::Vector3d grid_goal = posRes(goal_pos);

    if(!setSG(start_pos, goal_pos)){
        return;
    }
    /*
    if((int) grid_start(0) == (int) grid_goal(0)
        and (int) grid_start(1) == (int) grid_goal(1)
        and (int) grid_start(2) == (int) grid_goal(2))
    {
        ROS_WARN("Start and goal poses are the some");
        return;
    }
    if (!gdsl_->SetStart(grid_start))
    {
        ROS_WARN("SetStart faild");
        return;
    }
    if (!gdsl_->SetGoal(grid_goal))
    {
        ROS_WARN("SetGoal faild");
        return;
    }
    */

    planAllPaths();
    publishAllPaths();

}

bool Dsp::setSG(Eigen::Vector3d start, Eigen::Vector3d goal){
    //std::cout<<"16"<<std::endl;

    Eigen::Vector3d grid_start = posRes(start);
    Eigen::Vector3d grid_goal = posRes(goal);

    if((int) grid_start(0) == (int) grid_goal(0)
        and (int) grid_start(1) == (int) grid_goal(1)
        and (int) grid_start(2) == (int) grid_goal(2))
    {
        ROS_WARN("Start and goal poses are the same");
        return false;
    }
    if (!gdsl_->SetStart(grid_start))
    {
        ROS_WARN("SetStart faild");
        return false;
    }
    if (!gdsl_->SetGoal(grid_goal))
    {
        ROS_WARN("SetGoal faild");
        return false;
    }
    return true;
}
// transfomre a pose betewn IRL cordinates and map cordinate 
Eigen::Vector3d Dsp::posRes(Eigen::Vector3d wpos)
{
    //std::cout<<"17"<<std::endl;
    for (int i = 0; i < 3; i++)
    {
        wpos(i) = (wpos(i) - pmin(i)) / res_octomap;
    }
    return wpos;
}

void Dsp::pathUpdateCallback(const ros::TimerEvent& event){
    setAndPublishPath();
}

void Dsp::planAllPaths()
{
    //std::cout<<"18"<<std::endl;
    gdsl_->Plan(path_);
    //gdsl_->SplinePath(path_, splinepath_, spline_step_);
    return;
}

void Dsp::publishAllPaths()
{
    //std::cout<<"19"<<std::endl;
  path_pub_.publish(dspPathToRosMsg(path_, false));
  //if (path_.cells.size() <= 3){
  //  splinepath_pub_.publish(dspPathToRosMsg(path_, false)); 
  //} else {
  //  splinepath_pub_.publish(dspPathToRosMsg(splinepath_, true)); 
  //}
}

// transfom paht to ros paht
nav_msgs::Path Dsp::dspPathToRosMsg(const dsl::GridPath<3> &dsp_path, bool isSplined)
{
    //std::cout<<"20"<<std::endl;
  std::vector<Eigen::Vector3d> path;
  for(int i = 0; i < dsp_path.cells.size(); i++)
  {
    path.push_back(dsp_path.cells[i].c * res_octomap);
  }
  return dspPathToRosMsg(path, isSplined);
}

nav_msgs::Path Dsp::dspPathToRosMsg(const std::vector<Eigen::Vector3d> &dsp_path, bool isSplined)
{
    //std::cout<<"21"<<std::endl;
  nav_msgs::Path msg;  
  
  msg.header.frame_id = odom_frame_id_; 
  msg.poses.resize(dsp_path.size());
  for(int i = 0; i < dsp_path.size(); i++)
  {
    if(isSplined){
        msg.poses[i].pose.position.x = dsp_path[i][0] * res_octomap + pmin(0);
        msg.poses[i].pose.position.y = dsp_path[i][1] * res_octomap + pmin(1);
        msg.poses[i].pose.position.z = dsp_path[i][2] * res_octomap + pmin(2);
    }
    else{
        msg.poses[i].pose.position.x = dsp_path[i][0] + pmin(0);
        msg.poses[i].pose.position.y = dsp_path[i][1] + pmin(1);
        msg.poses[i].pose.position.z = dsp_path[i][2] + pmin(2);
    }
  }

  return msg; 
}


// publishing map usedfule when debuging
void Dsp::publishOccupancyGrid()
{
    //std::cout<<"22"<<std::endl;
    visualization_msgs::Marker occmap_viz;

    std::vector<geometry_msgs::Point> marker_pos;
    std::vector<geometry_msgs::Point> risk_pos;
     
    for(double x = 0; x < length_voxel; x++)
    {

        for(double y = 0; y < width_voxel; y++)
        {
            for(double z = 0; z < height_voxel; z++)
            {
                Eigen::Vector3d pos(x, y, z);
                int idx = x + y*length + z*length*width;

                // Different parts to vizulize
                if(gdsl_->GetCost(pos) == DSP_OCCUPIED)
                //if(gdsl_->GetCost(pos) == DSP_UNKNOWN)
                //if(gdsl_->GetCost(pos) < 10)
                //if(gdsl_->GetCost(pos) == 1)
                //if(gdsl_->GetCost(pos) > 1 and gdsl_->GetCost(pos) < DSP_UNKNOWN)
                //if(gdsl_->GetCost(pos) < DSP_OCCUPIED and gdsl_->GetCost(pos) > DSP_UNKNOWN)
                //if(gdsl_->GetCost(pos) >= DSP_UNKNOWN / 5 and gdsl_->GetCost(pos) < DSP_UNKNOWN)
                {
                    //std::cout<<gdsl_->GetCost(pos)<<std::endl;
                    geometry_msgs::Point pt;
                    //pt.x = (x + pmin(0)) * res_octomap;
                    //pt.y = (y + pmin(1)) * res_octomap;
                    //pt.z = (z + pmin(2)) * res_octomap;
                    pt.x = (x * res_octomap) + pmin(0);
                    pt.y = (y * res_octomap) + pmin(1);
                    pt.z = (z * res_octomap) + pmin(2);
                    marker_pos.push_back(pt);
                }  
            }  
        }
    }

    occmap_viz.header.frame_id = odom_frame_id_; ///world /pixy/velodyne
    occmap_viz.header.stamp = ros::Time();
    occmap_viz.ns = "dsp";
    occmap_viz.id = 1;
    occmap_viz.type = visualization_msgs::Marker::CUBE_LIST;
    occmap_viz.action = visualization_msgs::Marker::ADD;
    occmap_viz.pose.position.x = 0.5 * res_octomap;// + pmin(0) * res_octomap; 
    occmap_viz.pose.position.y = 0.5 * res_octomap;// + pmin(1) * res_octomap;
    occmap_viz.pose.position.z = 0.5 * res_octomap;// + pmin(2) * res_octomap;
    occmap_viz.pose.orientation.x = 0.0;
    occmap_viz.pose.orientation.y = 0.0;
    occmap_viz.pose.orientation.z = 0.0;
    occmap_viz.pose.orientation.w = 1.0;
    occmap_viz.scale.x = 1.0 * res_octomap;
    occmap_viz.scale.y = 1.0 * res_octomap;
    occmap_viz.scale.z = 1.0 * res_octomap;
    occmap_viz.color.a = 0.5;
    occmap_viz.color.r = 1.0;
    occmap_viz.color.g = 0.0;
    occmap_viz.color.b = 0.0;
    occmap_viz.points = marker_pos;
    occ_map_viz_pub_.publish(occmap_viz);

}

} // namespace
