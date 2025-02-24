// Copyright 2020 Tier IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "scene_walkway.hpp"

#include <behavior_velocity_planner_common/utilization/util.hpp>
#include <motion_utils/trajectory/trajectory.hpp>

#include <cmath>

namespace behavior_velocity_planner
{
namespace bg = boost::geometry;
using Point = bg::model::d2::point_xy<double>;
using Polygon = bg::model::polygon<Point>;
using Line = bg::model::linestring<Point>;
using motion_utils::calcLongitudinalOffsetPose;
using motion_utils::calcSignedArcLength;
using motion_utils::findNearestSegmentIndex;
using tier4_autoware_utils::createPoint;
using tier4_autoware_utils::getPose;

WalkwayModule::WalkwayModule(
  const int64_t module_id, const lanelet::LaneletMapPtr & lanelet_map_ptr,
  const PlannerParam & planner_param, const bool use_regulatory_element,
  const rclcpp::Logger & logger, const rclcpp::Clock::SharedPtr clock)
: SceneModuleInterface(module_id, logger, clock),
  module_id_(module_id),
  state_(State::APPROACH),
  planner_param_(planner_param),
  use_regulatory_element_(use_regulatory_element)
{
  velocity_factor_.init(VelocityFactor::SIDEWALK);

  if (use_regulatory_element_) {
    const auto reg_elem_ptr = std::dynamic_pointer_cast<const lanelet::autoware::Crosswalk>(
      lanelet_map_ptr->regulatoryElementLayer.get(module_id));
    stop_lines_ = reg_elem_ptr->stopLines();
    walkway_ = reg_elem_ptr->crosswalkLanelet();
  } else {
    const auto stop_line = getStopLineFromMap(module_id_, lanelet_map_ptr, "crosswalk_id");
    if (!!stop_line) {
      stop_lines_.push_back(stop_line.get());
    }
    walkway_ = lanelet_map_ptr->laneletLayer.get(module_id);
  }
}

boost::optional<std::pair<double, geometry_msgs::msg::Point>> WalkwayModule::getStopLine(
  const PathWithLaneId & ego_path, bool & exist_stopline_in_map) const
{
  const auto & ego_pos = planner_data_->current_odometry->pose.position;

  const auto get_stop_point =
    [&](const auto & stop_line) -> boost::optional<geometry_msgs::msg::Point> {
    const auto intersects = getLinestringIntersects(
      ego_path, lanelet::utils::to2D(stop_line).basicLineString(), ego_pos, 2);

    if (intersects.empty()) {
      return boost::none;
    }

    return createPoint(intersects.front().x(), intersects.front().y(), ego_pos.z);
  };

  for (const auto & stop_line : stop_lines_) {
    const auto p_stop_line = get_stop_point(stop_line);
    if (!p_stop_line) {
      continue;
    }

    exist_stopline_in_map = true;

    const auto dist_ego_to_stop = calcSignedArcLength(ego_path.points, ego_pos, p_stop_line.get());
    return std::make_pair(dist_ego_to_stop, p_stop_line.get());
  }

  {
    exist_stopline_in_map = false;

    if (!path_intersects_.empty()) {
      const auto p_stop_line = path_intersects_.front();
      const auto dist_ego_to_stop = calcSignedArcLength(ego_path.points, ego_pos, p_stop_line) -
                                    planner_param_.stop_line_distance;
      return std::make_pair(dist_ego_to_stop, p_stop_line);
    }
  }

  return {};
}

bool WalkwayModule::modifyPathVelocity(PathWithLaneId * path, StopReason * stop_reason)
{
  const auto & base_link2front = planner_data_->vehicle_info_.max_longitudinal_offset_m;

  debug_data_ = DebugData();
  debug_data_.base_link2front = base_link2front;
  *stop_reason = planning_utils::initializeStopReason(StopReason::WALKWAY);

  const auto input = *path;

  path_intersects_.clear();

  const auto & ego_pos = planner_data_->current_odometry->pose.position;
  const auto intersects =
    getPolygonIntersects(input, walkway_.polygon2d().basicPolygon(), ego_pos, 2);

  for (const auto & p : intersects) {
    path_intersects_.push_back(createPoint(p.x(), p.y(), ego_pos.z));
  }

  if (path_intersects_.empty()) {
    return false;
  }

  if (state_ == State::APPROACH) {
    bool exist_stopline_in_map;
    const auto p_stop_line = getStopLine(input, exist_stopline_in_map);
    if (!p_stop_line) {
      return false;
    }

    const auto & p_stop = p_stop_line.get().second;
    const auto stop_line_distance = exist_stopline_in_map ? 0.0 : planner_param_.stop_line_distance;
    const auto margin = stop_line_distance + base_link2front;
    const auto stop_pose = calcLongitudinalOffsetPose(input.points, p_stop, -margin);

    if (!stop_pose) {
      return false;
    }

    const auto inserted_pose = planning_utils::insertStopPoint(stop_pose.get().position, *path);
    if (inserted_pose) {
      debug_data_.stop_poses.push_back(inserted_pose.get());
    }

    /* get stop point and stop factor */
    StopFactor stop_factor;
    stop_factor.stop_pose = stop_pose.get();
    stop_factor.stop_factor_points.push_back(path_intersects_.front());
    planning_utils::appendStopReason(stop_factor, stop_reason);
    velocity_factor_.set(
      path->points, planner_data_->current_odometry->pose, stop_pose.get(),
      VelocityFactor::UNKNOWN);

    // use arc length to identify if ego vehicle is in front of walkway stop or not.
    const double signed_arc_dist_to_stop_point =
      calcSignedArcLength(input.points, ego_pos, stop_pose.get().position);

    const double distance_threshold = 1.0;
    debug_data_.stop_judge_range = distance_threshold;

    const auto stop_at_stop_point =
      signed_arc_dist_to_stop_point < distance_threshold &&
      planner_data_->isVehicleStopped(planner_param_.stop_duration_sec);

    if (stop_at_stop_point) {
      // If ego vehicle is after walkway stop and stopped then move to stop state
      state_ = State::STOP;
      if (signed_arc_dist_to_stop_point < -distance_threshold) {
        RCLCPP_ERROR(
          logger_, "Failed to stop near walkway but ego stopped. Change state to STOPPED");
      }
    }

    return true;
  }

  if (state_ == State::STOP) {
    if (planner_data_->isVehicleStopped()) {
      state_ = State::SURPASSED;
    }
  }

  return true;
}
}  // namespace behavior_velocity_planner
