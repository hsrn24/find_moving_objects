/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2018, Andreas Gustavsson.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*
* Author: Andreas Gustavsson
*********************************************************************/

/* ROS */
#include <ros/ros.h>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_ros/message_filter.h>
#include <message_filters/subscriber.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <sensor_msgs/LaserScan.h>

#ifdef NODELET
#include <pluginlib/class_list_macros.h>
#endif

/* C/C++ */
#include <iostream>
#include <cmath>
#include <pthread.h>

// #ifdef LSARRAY
// #include <omp.h>
// #endif

/* LOCAL INCLUDES */
#include <find_moving_objects/bank.h>
#include <find_moving_objects/LaserScanInterpreter.h>


#ifdef NODELET
/* TELL ROS ABOUT THIS NODELET PLUGIN */
# ifdef LSARRAY
PLUGINLIB_EXPORT_CLASS(find_moving_objects::LaserScanArrayInterpreterNodelet, nodelet::Nodelet)
# else
PLUGINLIB_EXPORT_CLASS(find_moving_objects::LaserScanInterpreterNodelet, nodelet::Nodelet)
# endif
#endif


namespace find_moving_objects
{

/*
 * Standard Units of Measure and Coordinate Conventions:   http://www.ros.org/reps/rep-0103.html
 * Coordinate Frames for Mobile Platforms:                 http://www.ros.org/reps/rep-0105.html
 */


/* CONFIDENCE CALCULATION FOR BANK */
double a_factor = -20 / 3;
double root_1=0.35, root_2=0.65; // optimized for bank coverage of 0.5s, adapted in hz calculation
double width_factor = 0.0;
double Bank::calculateConfidence(const MovingObject & mo,
                                 const BankArgument & ba,
                                 const double dt,
                                 const double mo_old_width)
{
  return /*ba.ema_alpha **/ // Using weighting decay decreases the confidence while,
         (ba.base_confidence // how much we trust the sensor itself,
          + a_factor * (dt-root_1) * (dt-root_2) // a well-adapted bank size in relation to the sensor rate and environmental context
          - width_factor * fabs(mo.seen_width - mo_old_width)); // and low difference in width between old and new object,
          // make us more confident
}



/* CONSTRUCTOR */
#ifdef NODELET
# ifdef LSARRAY
LaserScanArrayInterpreterNodelet::LaserScanArrayInterpreterNodelet()
# else
LaserScanInterpreterNodelet::LaserScanInterpreterNodelet()
# endif
#endif
#ifdef NODE
# ifdef LSARRAY
LaserScanArrayInterpreterNode::LaserScanArrayInterpreterNode()
# else
LaserScanInterpreterNode::LaserScanInterpreterNode()
# endif
#endif
: received_messages(0),
  optimize_nr_scans_in_bank(0.0)
{
#ifdef NODELET
  // Wait for time to become valid, then start bank
  ros::Time::waitForValid();
#endif
  
  tf_filter = NULL;
  tf_listener = NULL;
  tf_buffer = NULL;
  DynamicReconfigureInit();
  
#ifdef NODE
  onInit();
#endif
}



/* DESTRUCTOR */
#ifdef LSARRAY
# ifdef NODELET
LaserScanArrayInterpreterNodelet::~LaserScanArrayInterpreterNodelet()
# endif
# ifdef NODE
LaserScanArrayInterpreterNode::~LaserScanArrayInterpreterNode()
# endif
#else
# ifdef NODELET
LaserScanInterpreterNodelet::~LaserScanInterpreterNodelet()
# endif
# ifdef NODE
LaserScanInterpreterNode::~LaserScanInterpreterNode()
# endif
#endif
{
  int nr_banks = banks.size();
  for (int i=0; i<nr_banks; ++i)
  {
    delete banks[i];
  }
  banks.clear();

  if (tf_filter != NULL)   delete tf_filter;
  if (tf_buffer != NULL)   delete tf_buffer;
}



/* CALLBACK */
#ifdef LSARRAY
# ifdef NODELET
void LaserScanArrayInterpreterNodelet::laserScanArrayCallback(const find_moving_objects::LaserScanArray::ConstPtr & msg)
# endif
# ifdef NODE
void LaserScanArrayInterpreterNode::laserScanArrayCallback(const find_moving_objects::LaserScanArray::ConstPtr & msg)
# endif
#else
# ifdef NODELET
void LaserScanInterpreterNodelet::laserScanCallback(const sensor_msgs::LaserScan::ConstPtr & msg)
# endif
# ifdef NODE
void LaserScanInterpreterNode::laserScanCallback(const sensor_msgs::LaserScan::ConstPtr & msg)
# endif
#endif
{
  switch (state)
  {
    /* 
     * MAIN STATE - WHEN ALL IS INITIALIZED
     */
    case FIND_MOVING_OBJECTS:
    {
#ifdef LSARRAY
      // Consider the msgs of msg in parallel
//       #pragma omp parallel for
      for (int i=0; i<msg->msgs.size(); ++i)
      {
        // Can message be added to bank?
        if (banks[i]->addMessage(&(msg->msgs[i])) != 0)
        {
          // Adding message failed (should never happen for LaserScan), try the next one
          continue;
        }

        // If so, then find and report objects
        banks[i]->findAndReportMovingObjects();
      }
#else
      // Can message be added to bank?
      if (banks[0]->addMessage(&(*msg)) != 0) // De-reference ConstPtr object and take reference of result to get a 
                                              // pointer to a PointCloud2 object
      {
        // Adding message failed (should never happen for LaserScan)
        break;
      }

      // If so, then find and report objects
      banks[0]->findAndReportMovingObjects();
#endif
      break;
    }
      
      
    /* 
     * BEFORE MAIN STATE CAN BE SET, THIS CASE MUST HAVE BEEN EXECUTED
     */
    case INIT_BANKS:
    {
      // Debug frame to see e.g. if we are dealing with an optical frame
#ifdef NODELET
      NODELET_DEBUG_STREAM("LaserScanArray sensor is using frame: " << msg->header.frame_id);
#endif
#ifdef NODE
      ROS_DEBUG_STREAM("LaserScanArray sensor is using frame: " << msg->header.frame_id);
#endif

#ifdef LSARRAY
      // Create banks
      const int nr_msgs = msg->msgs.size();
      if (banks.size() == 0)
      {
        // If reaching this point, then this block has not been executed before
        // If it has, then it will not be executed again, and the modifications to banks and bank_arguments are hence 
        // maintained valid, assuming that the new message contains the same number of messages in the array
        bank_arguments.resize(nr_msgs);
        banks.resize(nr_msgs);
        for (int i=0; i<nr_msgs; ++i)
        {
          // Create bank and start listening to tf data
          banks[i] = new find_moving_objects::Bank(tf_buffer);
          
          // Copy first bank argument
          bank_arguments[i] = bank_arguments[0];
        }
        
        // Modify bank arguments
        for (int i=0; i<nr_msgs; ++i)
        {
          const std::string append_str = "_" + std::to_string(i);
          bank_arguments[i].topic_ema.append(append_str);
          bank_arguments[i].topic_objects_closest_point_markers.append(append_str);
          bank_arguments[i].topic_objects_velocity_arrows.append(append_str);
          bank_arguments[i].topic_objects_delta_position_lines.append(append_str);
          bank_arguments[i].topic_objects_width_lines.append(append_str);
          
          bank_arguments[i].velocity_arrow_ns.append(append_str);
          bank_arguments[i].delta_position_line_ns.append(append_str);
          bank_arguments[i].width_line_ns.append(append_str);
          
          bank_arguments[i].node_name_suffix.append(append_str);
        }
      }
      
      // Init banks
      for (int i=0; i<nr_msgs; ++i)
      {
        banks[i]->init(bank_arguments[i], &(msg->msgs[i])); // addFirstMessage always succeeds, no bool needed for init!
      }
#else
      // Create banks
      if (banks.size() == 0)
      {
        banks.resize(1);
        banks[0] = new find_moving_objects::Bank(tf_buffer);
      }
      
      // Init bank
      if (banks[0]->init(bank_arguments[0], &(*msg)) != 0)
      {
        // If init fails (should never happen for LaserScan) we do not change state, but use this one again
        break;
      }
#endif
      
      // Change state
      state = FIND_MOVING_OBJECTS;
      break;
    }
      
      
    /* 
     * CALCULATE HZ OF TOPIC AND UPDATE SIZE OF BANK
     */
    case CALCULATE_HZ:
    {
      // spin until target is reached
      received_messages++;
      const double elapsed_time = ros::Time::now().toSec() - start_time;
      
      // Are we done?
      if (max_time <= elapsed_time ||
          max_messages <= received_messages)
      {
        // Calculate HZ
        const double hz = received_messages / elapsed_time;
        
        // Set nr of messages in bank
        const double nr_scans = optimize_nr_scans_in_bank * hz;
        bank_arguments[0].nr_scans_in_bank = nr_scans - ((long) nr_scans) == 0.0 ? nr_scans + 1 : ceil(nr_scans);
    
        // Sanity check
        if (bank_arguments[0].nr_scans_in_bank < 2)
        {
          bank_arguments[0].nr_scans_in_bank = 2;
        }

        // Update confidence roots and amplitude factor
        root_1 = optimize_nr_scans_in_bank * 0.6;
        root_2 = optimize_nr_scans_in_bank * 1.4;
        a_factor = 4 * max_confidence_for_dt_match / (2*root_1*root_2 - root_1*root_1 - root_2*root_2);
    
#ifdef NODELET
        NODELET_INFO_STREAM("Topic " << subscribe_topic << " has rate " << hz << "Hz" << 
                            " (based on " << received_messages << " msgs during " << elapsed_time << " seconds)");
        NODELET_INFO_STREAM("Optimized bank size is " << bank_arguments[0].nr_scans_in_bank);
#endif
#ifdef NODE
        ROS_INFO_STREAM("Topic " << subscribe_topic << " has rate " << hz << "Hz" << 
                            " (based on " << received_messages << " msgs during " << elapsed_time << " seconds)");
        ROS_INFO_STREAM("Optimized bank size is " << bank_arguments[0].nr_scans_in_bank);
#endif
    
        // Change state since we are done
        state = INIT_BANKS;
      }
      break;
    }
      
      
    /* 
     * WHEN CALCULATING HZ OF TOPIC, WAIT FOR A MESSAGE TO ARRIVE AND SAVE THE TIME
     */
    case WAIT_FOR_FIRST_MESSAGE_HZ:
    {
      // Set start time
      start_time = ros::Time::now().toSec();
      
      // Change state
      state = CALCULATE_HZ;
      break;
    } 
      
      
    /* 
     * THERE ARE NO MORE STATES - TERMINATE, THIS IS AN ERROR
     */
    default:
    {
      ROS_ERROR("Message callback is in an unknown state");
      ROS_BREAK();
    }
  }
}



/* ENTRY POINT FOR NODELET AND INIT FOR NODE */
#ifdef NODELET
# ifdef LSARRAY
void LaserScanArrayInterpreterNodelet::onInit()
# else
void LaserScanInterpreterNodelet::onInit()
# endif
{
  // Node handles
  nh = getNodeHandle();
  nh_priv = getPrivateNodeHandle();
#endif
#ifdef NODE
# ifdef LSARRAY
void LaserScanArrayInterpreterNode::onInit()
# else
void LaserScanInterpreterNode::onInit()
# endif
{
  // Node handles
  nh = ros::NodeHandle();
  nh_priv = ros::NodeHandle("~");
#endif
  
  // Init bank_argument with parameters
  BankArgument bank_argument;
  nh_priv.param("subscribe_topic", subscribe_topic, default_subscribe_topic);
  nh_priv.param("subscribe_buffer_size", subscribe_buffer_size, default_subscribe_buffer_size);
  nh_priv.param("ema_alpha", bank_argument.ema_alpha, default_ema_alpha);
  nh_priv.param("nr_scans_in_bank", bank_argument.nr_scans_in_bank, default_nr_scans_in_bank);
  nh_priv.param("object_threshold_edge_max_delta_range", bank_argument.object_threshold_edge_max_delta_range, default_object_threshold_edge_max_delta_range);
  nh_priv.param("object_threshold_min_nr_points", bank_argument.object_threshold_min_nr_points, default_object_threshold_min_nr_points);
  nh_priv.param("object_threshold_max_distance", bank_argument.object_threshold_max_distance, default_object_threshold_max_distance);
  nh_priv.param("object_threshold_min_speed", bank_argument.object_threshold_min_speed, default_object_threshold_min_speed);
  nh_priv.param("object_threshold_max_delta_width_in_points", bank_argument.object_threshold_max_delta_width_in_points, default_object_threshold_max_delta_width_in_points);
  nh_priv.param("object_threshold_bank_tracking_max_delta_distance", bank_argument.object_threshold_bank_tracking_max_delta_distance, default_object_threshold_bank_tracking_max_delta_distance);
  nh_priv.param("object_threshold_min_confidence", bank_argument.object_threshold_min_confidence, default_object_threshold_min_confidence);
  nh_priv.param("base_confidence", bank_argument.base_confidence, default_base_confidence);
  nh_priv.param("publish_ema", bank_argument.publish_ema, default_publish_ema);
  nh_priv.param("publish_objects_closest_points_markers", bank_argument.publish_objects_closest_point_markers, default_publish_objects_closest_points_markers);
  nh_priv.param("publish_objects_velocity_arrows", bank_argument.publish_objects_velocity_arrows, default_publish_objects_velocity_arrows);
  nh_priv.param("publish_objects_delta_position_lines", bank_argument.publish_objects_delta_position_lines, default_publish_objects_delta_position_lines);
  nh_priv.param("publish_objects_width_lines", bank_argument.publish_objects_width_lines, default_publish_objects_width_lines);
  nh_priv.param("velocity_arrows_use_full_gray_scale", bank_argument.velocity_arrows_use_full_gray_scale, default_velocity_arrows_use_full_gray_scale);
  nh_priv.param("velocity_arrows_use_sensor_frame", bank_argument.velocity_arrows_use_sensor_frame, default_velocity_arrows_use_sensor_frame);
  nh_priv.param("velocity_arrows_use_base_frame", bank_argument.velocity_arrows_use_base_frame, default_velocity_arrows_use_base_frame);
  nh_priv.param("velocity_arrows_use_fixed_frame", bank_argument.velocity_arrows_use_fixed_frame, default_velocity_arrows_use_fixed_frame);
  nh_priv.param("publish_objects", bank_argument.publish_objects, default_publish_objects);
  nh_priv.param("map_frame", bank_argument.map_frame, default_map_frame);
  nh_priv.param("fixed_frame", bank_argument.fixed_frame, default_fixed_frame);
  nh_priv.param("base_frame", bank_argument.base_frame, default_base_frame);
  nh_priv.param("ns_velocity_arrows", bank_argument.velocity_arrow_ns, default_ns_velocity_arrows);
  nh_priv.param("ns_delta_position_lines", bank_argument.delta_position_line_ns, default_ns_delta_position_lines);
  nh_priv.param("ns_width_lines", bank_argument.width_line_ns, default_ns_width_lines);
  nh_priv.param("topic_ema", bank_argument.topic_ema, default_topic_ema);
  nh_priv.param("topic_objects_closest_points_markers", bank_argument.topic_objects_closest_point_markers, default_topic_objects_closest_points_markers);
  nh_priv.param("topic_objects_velocity_arrows", bank_argument.topic_objects_velocity_arrows, default_topic_objects_velocity_arrows);
  nh_priv.param("topic_objects_delta_position_lines", bank_argument.topic_objects_delta_position_lines, default_topic_objects_delta_position_lines);
  nh_priv.param("topic_objects_width_lines", bank_argument.topic_objects_width_lines, default_topic_objects_width_lines);
  nh_priv.param("topic_objects", bank_argument.topic_objects, default_topic_objects);
  nh_priv.param("publish_buffer_size", bank_argument.publish_buffer_size, default_publish_buffer_size);
  
  // Add this as the first bank_argument
  bank_arguments.push_back(bank_argument);
  
  // Optimize bank size?
  nh_priv.param("optimize_nr_scans_in_bank", optimize_nr_scans_in_bank, default_optimize_nr_scans_in_bank);
  nh_priv.param("max_confidence_for_dt_match", max_confidence_for_dt_match, default_max_confidence_for_dt_match);
  
  // If optimize_nr_scans_in_bank != 0, then yes
  if (optimize_nr_scans_in_bank != 0.0)
  {
    state = WAIT_FOR_FIRST_MESSAGE_HZ;
  }
  else
  {
    state = INIT_BANKS;
  }
  
  // Delta width confidence factor
  nh_priv.param("delta_width_confidence_decrease_factor", width_factor, default_delta_width_confidence_decrease_factor);
  
  // Set up target frames for message filter
  tf_filter_target_frames.push_back(bank_argument.map_frame);
  if (strcmp(bank_argument.map_frame.c_str(), bank_argument.fixed_frame.c_str()) != 0)
  {
    tf_filter_target_frames.push_back(bank_argument.fixed_frame);
  }
  if (strcmp(bank_argument.map_frame.c_str(), bank_argument.base_frame.c_str()) != 0 &&
      strcmp(bank_argument.fixed_frame.c_str(), bank_argument.base_frame.c_str()) != 0)
  {
    tf_filter_target_frames.push_back(bank_argument.base_frame);
  }
  
  // Create tf2 buffer, listener, subscriber and filter
  tf_buffer = new tf2_ros::Buffer;
  tf_listener = new tf2_ros::TransformListener(*tf_buffer);
#ifdef LSARRAY
  tf_subscriber = new message_filters::Subscriber<find_moving_objects::LaserScanArray>();
  tf_subscriber->subscribe(nh, subscribe_topic, subscribe_buffer_size);
  tf_filter = new tf2_ros::MessageFilter<find_moving_objects::LaserScanArray>(*tf_subscriber, *tf_buffer, "", subscribe_buffer_size, 0);
#else
  tf_subscriber = new message_filters::Subscriber<sensor_msgs::LaserScan>();
  tf_subscriber->subscribe(nh, subscribe_topic, subscribe_buffer_size);
  tf_filter = new tf2_ros::MessageFilter<sensor_msgs::LaserScan>(*tf_subscriber, *tf_buffer, "", subscribe_buffer_size, 0);
#endif
  tf_filter->setTargetFrames(tf_filter_target_frames);
  
  // Register callback in filter
  tf_filter->registerCallback( boost::bind(
#ifdef NODELET
# ifdef LSARRAY
          &LaserScanArrayInterpreterNodelet::laserScanArrayCallback,
# else
          &LaserScanInterpreterNodelet::laserScanCallback,
# endif
#endif
#ifdef NODE
# ifdef LSARRAY
          &LaserScanArrayInterpreterNode::laserScanArrayCallback,
# else
          &LaserScanInterpreterNode::laserScanCallback,
# endif
#endif
          this, _1) );
}

/* DYNAMIC RECONFIGURE */
#ifdef NODELET
# ifdef LSARRAY
void LaserScanArrayInterpreterNodelet::DynamicReconfigureInit()
# else
void LaserScanInterpreterNodelet::DynamicReconfigureInit()
# endif
#endif
#ifdef NODE
# ifdef LSARRAY
void LaserScanArrayInterpreterNode::DynamicReconfigureInit()
# else
void LaserScanInterpreterNode::DynamicReconfigureInit()
# endif
#endif
{
  dynamic_reconfigure::Server<find_moving_objects::FindMovingObjectsLaserScanConfig>::CallbackType f;
  #ifdef NODELET
  # ifdef LSARRAY
  f = boost::bind(&LaserScanArrayInterpreterNodelet::DynamicReconfigureCallback, this, _1, _2);
  # else
  f = boost::bind(&LaserScanInterpreterNodelet::DynamicReconfigureCallback, this, _1, _2);
  # endif
  #endif
  #ifdef NODE
  # ifdef LSARRAY
  f = boost::bind(&LaserScanArrayInterpreterNode::DynamicReconfigureCallback, this, _1, _2);
  # else
  f = boost::bind(&LaserScanInterpreterNode::DynamicReconfigureCallback, this, _1, _2);
  # endif
  #endif
  dynamic_reconfigure_server_.setCallback(f);
  std::cout << "Dynamic Reconfigure Initialized" << std::endl;
}

#ifdef NODELET
# ifdef LSARRAY
void LaserScanArrayInterpreterNodelet::DynamicReconfigureCallback(find_moving_objects::FindMovingObjectsLaserScanConfig &config, uint32_t level)
# else
void LaserScanInterpreterNodelet::DynamicReconfigureCallback(find_moving_objects::FindMovingObjectsLaserScanConfig &config, uint32_t level)
# endif
#endif
#ifdef NODE
# ifdef LSARRAY
void LaserScanArrayInterpreterNode::DynamicReconfigureCallback(find_moving_objects::FindMovingObjectsLaserScanConfig &config, uint32_t level)
# else
void LaserScanInterpreterNode::DynamicReconfigureCallback(find_moving_objects::FindMovingObjectsLaserScanConfig &config, uint32_t level)
# endif
#endif
{
  std::cout << "Dynamic Reconfigure Callback" << std::endl;
  for (auto& bank : banks){
    bank -> reconfigureBankParameters(config);
  }

}

} // namespace find_moving_objects



#ifdef NODE
using namespace find_moving_objects;

/* ENTRY POINT */
int main (int argc, char ** argv)
{
# ifdef LSARRAY
  // Init ROS
  ros::init(argc, argv, "laserscanarray_interpreter", ros::init_options::AnonymousName);

  // Create and init node object
  LaserScanArrayInterpreterNode ls_interpreter;
# else
  // Init ROS
  ros::init(argc, argv, "laserscan_interpreter", ros::init_options::AnonymousName);
  
  // Create and init node object
  LaserScanInterpreterNode ls_interpreter;
# endif
  
  // Enter receive loop
  ros::spin();

  return 0;
}
#endif
