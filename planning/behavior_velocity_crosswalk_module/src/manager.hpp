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

#ifndef MANAGER_HPP_
#define MANAGER_HPP_

#include "scene_crosswalk.hpp"
#include "scene_walkway.hpp"

#include <behavior_velocity_planner_common/plugin_interface.hpp>
#include <behavior_velocity_planner_common/plugin_wrapper.hpp>
#include <behavior_velocity_planner_common/scene_module_interface.hpp>
#include <lanelet2_extension/regulatory_elements/crosswalk.hpp>
#include <rclcpp/rclcpp.hpp>

#include <autoware_auto_planning_msgs/msg/path_with_lane_id.hpp>
#include <tier4_api_msgs/msg/crosswalk_status.hpp>

#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace behavior_velocity_planner
{

using autoware_auto_planning_msgs::msg::PathWithLaneId;

class CrosswalkModuleManager : public SceneModuleManagerInterfaceWithRTC
{
public:
  explicit CrosswalkModuleManager(rclcpp::Node & node);

  const char * getModuleName() override { return "crosswalk"; }

private:
  CrosswalkModule::PlannerParam crosswalk_planner_param_{};

  void launchNewModules(const PathWithLaneId & path) override;

  std::function<bool(const std::shared_ptr<SceneModuleInterface> &)> getModuleExpiredFunction(
    const PathWithLaneId & path) override;

  boost::optional<bool> opt_use_regulatory_element_{boost::none};
};

class WalkwayModuleManager : public SceneModuleManagerInterface
{
public:
  explicit WalkwayModuleManager(rclcpp::Node & node);

  const char * getModuleName() override { return "walkway"; }

private:
  WalkwayModule::PlannerParam walkway_planner_param_{};

  void launchNewModules(const PathWithLaneId & path) override;

  std::function<bool(const std::shared_ptr<SceneModuleInterface> &)> getModuleExpiredFunction(
    const PathWithLaneId & path) override;

  boost::optional<bool> opt_use_regulatory_element_{boost::none};
};

class CrosswalkModulePlugin : public PluginWrapper<CrosswalkModuleManager>
{
};

class WalkwayModulePlugin : public PluginWrapper<WalkwayModuleManager>
{
};

}  // namespace behavior_velocity_planner

#endif  // MANAGER_HPP_
