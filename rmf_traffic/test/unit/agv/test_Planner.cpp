/*
 * Copyright (C) 2019 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <rmf_traffic/agv/Planner.hpp>
#include <rmf_traffic/schedule/Database.hpp>

#include <rmf_traffic/Conflict.hpp>

#include <rmf_utils/catch.hpp>

#include "../utils_Trajectory.hpp"

#include <iostream>

void test_with_obstacle(
    const std::string& parent,
    rmf_traffic::schedule::Database& database,
    const rmf_traffic::Trajectory& obstacle,
    const rmf_traffic::agv::Planner::Options& options,
    const rmf_traffic::Trajectory& original_trajectory,
    const rmf_traffic::Time time,
    const bool test_performance,
    const std::size_t N)
{
  database.insert(obstacle);

  const auto start_time = std::chrono::steady_clock::now();
  std::vector<rmf_traffic::Trajectory> solution;
  for(std::size_t i=0; i < N; ++i)
    CHECK(rmf_traffic::agv::Planner::solve(time, 2, 0.0, 12, nullptr, options, solution));

  const auto end_time = std::chrono::steady_clock::now();
  if(test_performance)
  {
    const double sec = rmf_traffic::time::to_seconds(end_time - start_time);
    std::cout << "\n" << parent << " w/ obstacle" << std::endl;
    std::cout << "Total: " << sec << std::endl;
    std::cout << "Per run: " << sec/N << std::endl;
  }

  REQUIRE(solution.size() == 1);
  const auto t_obs = solution.front();
  CHECK( (t_obs.front().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(5, -5)).norm() == Approx(0.0) );
  CHECK( (t_obs.back().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(12, 12)).norm() == Approx(0.0) );
  CHECK( original_trajectory.duration() < t_obs.duration() );

  // Confirm that the trajectory does not conflict with anything in the
  // schedule
  for(const auto& entry : database.query(rmf_traffic::schedule::query_everything()))
    CHECK(rmf_traffic::DetectConflict::between(t_obs, entry).empty());

  // Confirm that the vehicle pulled into holding point 4 in order to avoid
  // the conflict
  auto hold_it = t_obs.end();
  for(auto it = t_obs.begin(); it != t_obs.end(); ++it)
  {
    if((it->get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(-5, 0)).norm() < 1e-8)
    {
      hold_it = it;
      break;
    }
  }

  CHECK(hold_it != t_obs.end());
}

SCENARIO("Test planning")
{
  using namespace std::chrono_literals;

  const std::string test_map_name = "test_map";
  rmf_traffic::agv::Graph graph;
  graph.add_waypoint(test_map_name, {-5, -5}); // 0
  graph.add_waypoint(test_map_name, { 0, -5}); // 1
  graph.add_waypoint(test_map_name, { 5, -5}); // 2
  graph.add_waypoint(test_map_name, {10, -5}); // 3
  graph.add_waypoint(test_map_name, {-5,  0}, true); // 4
  graph.add_waypoint(test_map_name, { 0,  0}, true); // 5
  graph.add_waypoint(test_map_name, { 5,  0}, true); // 6
  graph.add_waypoint(test_map_name, {10,  0}); // 7
  graph.add_waypoint(test_map_name, {10,  4}); // 8
  graph.add_waypoint(test_map_name, { 0,  8}); // 9
  graph.add_waypoint(test_map_name, { 5,  8}); // 10
  graph.add_waypoint(test_map_name, {10, 12}); // 11
  graph.add_waypoint(test_map_name, {12, 12}); // 12
  REQUIRE(graph.num_waypoints()==13);

  auto add_bidir_lane = [&](const std::size_t w0, const std::size_t w1)
  {
    graph.add_lane(w0, w1);
    graph.add_lane(w1, w0);
  };

  add_bidir_lane(0, 1);
  add_bidir_lane(1, 2);
  add_bidir_lane(2, 3);
  add_bidir_lane(1, 5);
  add_bidir_lane(3, 7);
  add_bidir_lane(4, 5);
  add_bidir_lane(5, 9);
  add_bidir_lane(6, 10);
  add_bidir_lane(7, 8);
  add_bidir_lane(9, 10);
  add_bidir_lane(10, 11);

  WHEN("initial conditions satisfy the goals")
  {
    const rmf_traffic::Time time = std::chrono::steady_clock::now();
    const rmf_traffic::agv::VehicleTraits traits(
        {0.7, 0.3}, {1.0, 0.45}, make_test_profile(UnitCircle));

    rmf_traffic::schedule::Database database;

    rmf_traffic::agv::Planner::Options options(traits, graph, database);
    std::vector<rmf_traffic::Trajectory> solution;
    double goal_orientation=0.0;
    
    bool solved=rmf_traffic::agv::Planner::solve(time,3,0.0,3,&goal_orientation,options,solution);
    CHECK(solved);
    CHECK(solution.size()==1);
    CHECK(solution.front().size()==0); 
    
  }


  WHEN("initial and goal waypoints are same but goal_orientation is different")
  {
    const rmf_traffic::Time time = std::chrono::steady_clock::now();
    const rmf_traffic::agv::VehicleTraits traits(
        {0.7, 0.3}, {1.0, 0.45}, make_test_profile(UnitCircle));

    rmf_traffic::schedule::Database database;

    rmf_traffic::agv::Planner::Options options(traits, graph, database);
    std::vector<rmf_traffic::Trajectory> solution;
    double goal_orientation=M_PI_2;
    
    bool solved=rmf_traffic::agv::Planner::solve(time,3,0.0,3,&goal_orientation,options,solution);
    CHECK(solved);
    CHECK(solution.size()==1);
    auto t=solution.front();
    CHECK(t.back().get_finish_position()[2]-goal_orientation==Approx(0));
    //std::cout<<"T final orientation: "<<t.back().get_finish_position()[2]<<std::endl;
    
  }

  //TEST with database with multiple trajectories 

  GIVEN("Goal from 2->12 and obstacle from 9->1")
  {
    const rmf_traffic::Time time = std::chrono::steady_clock::now();
    const rmf_traffic::agv::VehicleTraits traits(
        {0.7, 0.3}, {1.0, 0.45}, make_test_profile(UnitCircle));

    rmf_traffic::schedule::Database database;

    rmf_traffic::agv::Planner::Options options(traits, graph, database);

    std::vector<rmf_traffic::Trajectory> solution;

    // TODO(MXG): Move this content into a performance test folder
    const bool test_performance = false;
  //  const bool test_performance = true;
    const std::size_t N = test_performance? 10 : 1;

    rmf_traffic::Trajectory obstacle{test_map_name};
    obstacle.insert(
          time + 24s,
          make_test_profile(UnitCircle),
          {0.0, 8.0, 0.0},
          {0.0, 0.0, 0.0});
    obstacle.insert(
          time + 50s,
          make_test_profile(UnitCircle),
          {0.0, 0.0, 0.0},
          {0.0, 0.0, 0.0});
    obstacle.insert(
          time + 70s,
          make_test_profile(UnitCircle),
          {0.0, -5.0, 0.0},
          {0.0, 0.0, 0.0});

    WHEN("Docking is not constrained")
    {
      using namespace rmf_traffic::agv;
      add_bidir_lane(11, 12);

      options.set_graph(graph);

      const auto start_time = std::chrono::steady_clock::now();

      for(std::size_t i=0; i < N; ++i)
        CHECK(rmf_traffic::agv::Planner::solve(time, 2, 0.0, 12, nullptr, options, solution));

      const auto end_time = std::chrono::steady_clock::now();
      if(test_performance)
      {
        const double sec = rmf_traffic::time::to_seconds(end_time - start_time);
        std::cout << "\nUnconstrained" << std::endl;
        std::cout << "Total: " << sec << std::endl;
        std::cout << "Per run: " << sec/N << std::endl;
      }

      REQUIRE(solution.size() == 1);
      const auto t = solution.front();
      CHECK( (t.front().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(5, -5)).norm() == Approx(0.0) );
      CHECK( (t.back().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(12, 12)).norm() == Approx(0.0) );

      WHEN("An obstacle is introduced")
      {
        test_with_obstacle(
              "Unconstrained", database, obstacle,
              options, t, time, test_performance, N);
      }
    }

    WHEN("Docking must be at 0-degrees")
    {
      using namespace rmf_traffic::agv;
      graph.add_lane(11, {12, Graph::OrientationConstraint::make({0.0})});
      graph.add_lane({12, Graph::OrientationConstraint::make({0.0})}, 11);

      options.set_graph(graph);

      const auto start_time = std::chrono::steady_clock::now();
      for(std::size_t i=0; i < N; ++i)
        CHECK(rmf_traffic::agv::Planner::solve(time, 2, 0.0, 12, nullptr, options, solution));

      const auto end_time = std::chrono::steady_clock::now();
      if(test_performance)
      {
        const double sec = rmf_traffic::time::to_seconds(end_time - start_time);
        std::cout << "\nConstrained to 0.0" << std::endl;
        std::cout << "Total: " << sec << std::endl;
        std::cout << "Per run: " << sec/N << std::endl;
      }

      REQUIRE(solution.size() == 1);
      const auto& t = solution.front();
      CHECK( (t.front().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(5, -5)).norm() == Approx(0.0) );
      CHECK( (t.back().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(12, 12)).norm() == Approx(0.0) );
      CHECK( t.back().get_finish_position()[2] == Approx(0.0) );

      WHEN("An obstacle is introduced")
      {
        test_with_obstacle(
              "Constrained to 0.0", database, obstacle,
              options, t, time, test_performance, N);
      }
    }

    WHEN("Docking must be at 180-degrees")
    {
      using namespace rmf_traffic::agv;
      graph.add_lane(11, {12, Graph::OrientationConstraint::make({M_PI})});
      graph.add_lane({12, Graph::OrientationConstraint::make({M_PI})}, 11);

      options.set_graph(graph);

      const auto start_time = std::chrono::steady_clock::now();
      for(std::size_t i=0; i < N; ++i)
        rmf_traffic::agv::Planner::solve(time, 2, 0.0, 12, nullptr, options, solution);

      const auto end_time = std::chrono::steady_clock::now();
      if(test_performance)
      {
        const double sec = rmf_traffic::time::to_seconds(end_time - start_time);
        std::cout << "\nConstrained to 180.0" << std::endl;
        std::cout << "Total: " << sec << std::endl;
        std::cout << "Per run: " << sec/N << std::endl;
      }

      REQUIRE(solution.size() == 1);
      const auto& t = solution.front();
      CHECK( (t.front().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(5, -5)).norm() == Approx(0.0) );
      CHECK( (t.back().get_finish_position().block<2,1>(0,0) - Eigen::Vector2d(12, 12)).norm() == Approx(0.0) );
      CHECK( t.back().get_finish_position()[2] == Approx(M_PI) );

      WHEN("An obstacle is introduced")
      {
        test_with_obstacle(
              "Constrained to 180.0", database, obstacle,
              options, t, time, test_performance, N);
      }
    }
  }//end of GIVEN


  





}
