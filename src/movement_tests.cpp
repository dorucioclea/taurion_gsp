#include "movement.hpp"

#include "params.hpp"
#include "protoutils.hpp"

#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "hexagonal/coord.hpp"
#include "hexagonal/pathfinder.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <google/protobuf/repeated_field.h>

#include <utility>
#include <vector>

namespace pxd
{
namespace
{

/* ************************************************************************** */

/**
 * Returns an edge-weight function that has the given distance between
 * tiles and no obstacles.
 */
PathFinder::EdgeWeightFcn
EdgeWeights (const PathFinder::DistanceT dist)
{
  return [dist] (const HexCoord& from, const HexCoord& to)
    {
      return dist;
    };
}

/**
 * Returns an edge-weight function that has the given distance between
 * neighbouring tiles but also marks all tiles with x=-1 as obstacle.
 */
PathFinder::EdgeWeightFcn
EdgesWithObstacle (const PathFinder::DistanceT dist)
{
  return [dist] (const HexCoord& from, const HexCoord& to)
    {
      if (from.GetX () == -1 || to.GetX () == -1)
        return PathFinder::NO_CONNECTION;
      return dist;
    };
}

/* ************************************************************************** */

class MovementEdgeWeightTests : public DBTestWithSchema
{

protected:

  /** Test instance for dynamic obstacles.  */
  DynObstacles dyn;

  MovementEdgeWeightTests ()
    : dyn(db)
  {}

};

TEST_F (MovementEdgeWeightTests, BaseEdgesPassedThrough)
{
  const auto baseEdges = EdgesWithObstacle (42);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (0, 0), HexCoord (1, 0),
                                 baseEdges, dyn, Faction::RED),
             42);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (0, 0), HexCoord (-1, 0),
                                 baseEdges, dyn, Faction::RED),
             PathFinder::NO_CONNECTION);
}

TEST_F (MovementEdgeWeightTests, DynamicObstacle)
{
  const auto baseEdges = EdgeWeights (42);
  dyn.AddVehicle (HexCoord (0, 0), Faction::RED);

  EXPECT_EQ (MovementEdgeWeight (HexCoord (0, 0), HexCoord (1, 0),
                                 baseEdges, dyn, Faction::RED),
             PathFinder::NO_CONNECTION);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (1, 0), HexCoord (0, 0),
                                 baseEdges, dyn, Faction::RED),
             PathFinder::NO_CONNECTION);

  EXPECT_EQ (MovementEdgeWeight (HexCoord (0, 0), HexCoord (1, 0),
                                 baseEdges, dyn, Faction::GREEN),
             42);
  EXPECT_EQ (MovementEdgeWeight (HexCoord (1, 0), HexCoord (0, 0),
                                 baseEdges, dyn, Faction::GREEN),
             42);
}

/* ************************************************************************** */

/**
 * Test fixture for the character movement.  It automatically sets up a test
 * character and has convenient functions for setting up its movement data
 * in the database and retrieving the updated data.
 */
class MovementTests : public DBTestWithSchema
{

protected:

  /** Params instance, set to mainnet.  */
  const Params params;

  /** Character table used for interacting with the test database.  */
  CharacterTable tbl;

  MovementTests ()
    : params(xaya::Chain::MAIN), tbl(db)
  {
    const auto h = tbl.CreateNew ("domob", Faction::RED);
    CHECK_EQ (h->GetId (), 1);
  }

  /**
   * Returns a handle to the test character (for inspection and update).
   */
  CharacterTable::Handle
  GetTest ()
  {
    return tbl.GetById (1);
  }

  /**
   * Returns whether or not the test character is still moving.
   */
  bool
  IsMoving ()
  {
    return GetTest ()->GetProto ().has_movement ();
  }

  /**
   * Sets the test character's waypoints from the given vector.
   */
  void
  SetWaypoints (const std::vector<HexCoord>& coords)
  {
    const auto h = GetTest ();
    auto* mv = h->MutableProto ().mutable_movement ();
    SetRepeatedCoords (coords, *mv->mutable_waypoints ());
  }

  /**
   * Processes n movement steps for the test character.
   */
  void
  StepCharacter (const PathFinder::DistanceT speed,
                 const PathFinder::EdgeWeightFcn& edges,
                 const unsigned n)
  {
    GetTest ()->MutableProto ().set_speed (speed);
    for (unsigned i = 0; i < n; ++i)
      {
        ASSERT_TRUE (IsMoving ());
        ProcessCharacterMovement (*GetTest (), params, edges);
      }
  }

  /**
   * Steps the character multiple times and expects that we reach certain
   * points through that.  We expect it to have stopped after the last milestone
   * is reached.
   */
  void
  ExpectSteps (const PathFinder::DistanceT speed,
               const PathFinder::EdgeWeightFcn& edges,
               const std::vector<std::pair<unsigned, HexCoord>>& milestones)
  {
    for (const auto& m : milestones)
      {
        EXPECT_TRUE (IsMoving ());
        StepCharacter (speed, edges, m.first);
        EXPECT_EQ (GetTest ()->GetPosition (), m.second);
      }
    EXPECT_FALSE (IsMoving ());
  }

};

TEST_F (MovementTests, Basic)
{
  SetWaypoints ({HexCoord (10, 2), HexCoord (10, 5)});
  ExpectSteps (1, EdgeWeights (1),
    {
      {12, HexCoord (10, 2)},
      {3, HexCoord (10, 5)},
    });
}

TEST_F (MovementTests, SlowSpeed)
{
  SetWaypoints ({HexCoord (3, 0)});
  ExpectSteps (2, EdgeWeights (3),
    {
      {4, HexCoord (2, 0)},
      {1, HexCoord (3, 0)},
    });
}

TEST_F (MovementTests, FastSpeed)
{
  SetWaypoints ({HexCoord (3, 0), HexCoord (-3, 0)});
  ExpectSteps (7, EdgeWeights (1),
    {
      {1, HexCoord (-1, 0)},
      {1, HexCoord (-3, 0)},
    });
}

TEST_F (MovementTests, DuplicateWaypoints)
{
  SetWaypoints (
    {
      HexCoord (0, 0),
      HexCoord (1, 0), HexCoord (1, 0),
      HexCoord (2, 0), HexCoord (2, 0),
    });
  ExpectSteps (1, EdgeWeights (1),
    {
      {1, HexCoord (1, 0)},
      {1, HexCoord (2, 0)},
    });
}

TEST_F (MovementTests, WaypointsTooFar)
{
  SetWaypoints ({HexCoord (100, 0), HexCoord (201, 0)});
  ExpectSteps (1, EdgeWeights (10),
    {
      {1000, HexCoord (100, 0)},
    });
}

TEST_F (MovementTests, ObstacleInWaypoints)
{
  SetWaypoints ({HexCoord (0, 5), HexCoord (-2, 5)});
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      {5, HexCoord (0, 5)},
    });
}

TEST_F (MovementTests, ObstacleInSteps)
{
  SetWaypoints ({HexCoord (5, 0), HexCoord (-10, 0)});

  /* Step first without the obstacle, so that the final steps are already
     planned through where it will be later on.  */
  StepCharacter (1, EdgeWeights (1), 7);
  EXPECT_TRUE (IsMoving ());
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (3, 0));
  const auto mv = GetTest ()->GetProto ().movement ();
  EXPECT_EQ (mv.waypoints_size (), 1);
  EXPECT_GT (mv.steps_size (), 0);

  /* Now let the obstacle appear.  This should move the character right up to
     it and then stop.  */
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      {3, HexCoord (0, 0)},
    });
}

TEST_F (MovementTests, CharacterInObstacle)
{
  /* This is a situation that should not actually appear in practice.  But it
     is good to ensure it behaves as expected anyway.  */
  GetTest ()->SetPosition (HexCoord (-1, 0));
  SetWaypoints ({HexCoord (10, 0)});
  ExpectSteps (1, EdgesWithObstacle (1),
    {
      {1, HexCoord (-1, 0)},
    });
}

/* ************************************************************************** */

class AllMovementTests : public MovementTests
{

protected:

  /**
   * Steps all characters for one block.  This constructs a fresh dynamic
   * obstacle map from the database (as is done in the real game logic).
   */
  void
  StepAll (const PathFinder::EdgeWeightFcn& baseEdges)
  {
    DynObstacles dyn(db);
    ProcessAllMovement (db, dyn, params, baseEdges);
  }

};

TEST_F (AllMovementTests, LongSteps)
{
  /* This test verifies that we are able to perform many steps in a single
     block.  In particular, this only works if updating the dynamic obstacle
     map for the vehicle being moved works correctly.  */

  GetTest ()->SetPartialStep (1000);
  SetWaypoints ({
    HexCoord (5, 0),
    HexCoord (5, 0),
    HexCoord (0, 0),
    HexCoord (0, 0),
    HexCoord (2, 0),
    HexCoord (10, 0),
    HexCoord (-10, 0),
    HexCoord (-10, 0),
  });
  StepAll (EdgeWeights (1));

  EXPECT_FALSE (IsMoving ());
  EXPECT_EQ (GetTest ()->GetPosition (), HexCoord (-10, 0));
}

TEST_F (AllMovementTests, OtherVehicles)
{
  /* Movement is processed ordered by the character ID.  Thus when multiple
     vehicles move onto the same tile through their steps, then the one with
     lowest ID takes precedence.  */

  /* Move the test character from the fixture out of the way.  */
  GetTest ()->SetPosition (HexCoord (100, 0));

  /* Helper function to create one of our characters set up to move to
     the origin in the next step.  */
  const auto setupChar = [this] (const Faction f, const HexCoord& pos)
    {
      auto c = tbl.CreateNew ("domob", f);

      c->MutableProto ().set_speed (1);
      c->SetPosition (pos);

      auto* mv = c->MutableProto ().mutable_movement ();
      *mv->add_waypoints () = CoordToProto (HexCoord (0, 0));
      *mv->add_steps () = CoordToProto (c->GetPosition ());

      return c->GetId ();
    };

  const auto id1 = setupChar (Faction::RED, HexCoord (1, 0));
  const auto id2 = setupChar (Faction::RED, HexCoord (-1, 0));
  ASSERT_GT (id2, id1);
  const auto id3 = setupChar (Faction::GREEN, HexCoord (0, 1));
  ASSERT_GT (id3, id2);

  StepAll (EdgeWeights (1));

  EXPECT_EQ (tbl.GetById (id1)->GetPosition (), HexCoord (0, 0));
  EXPECT_EQ (tbl.GetById (id2)->GetPosition (), HexCoord (-1, 0));
  EXPECT_EQ (tbl.GetById (id3)->GetPosition (), HexCoord (0, 0));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
