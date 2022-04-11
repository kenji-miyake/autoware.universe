# Copyright 2022 TIER IV, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.frontend.parser import parse_substitution
from launch.launch_description_sources import FrontendLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import SetParameter
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities.evaluate_parameters import evaluate_parameter_dict
from launch_ros.utilities.normalize_parameters import normalize_parameter_dict


def launch_setup(context, *args, **kwargs):
    param_dict = normalize_parameter_dict({"diag_names": LaunchConfiguration("diag_names")})
    diag_names = evaluate_parameter_dict(context, param_dict)["diag_names"]

    for diag_name in diag_names:
        print("diag_name:", diag_name)

    return [
        IncludeLaunchDescription(
            FrontendLaunchDescriptionSource(
                [
                    FindPackageShare("dummy_diag_publisher"),
                    "/launch/dummy_diag_publisher_node.launch.xml",
                ]
            ),
            launch_arguments={
                "diag_name": diag_name,
            }.items(),
        )
        for diag_name in diag_names
    ]


def generate_launch_description():
    return LaunchDescription(
        [
            DeclareLaunchArgument("diag_names"),
            OpaqueFunction(function=launch_setup),
        ]
    )
