/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "logic.hpp"

#include "fame_tests.hpp"
#include "params.hpp"
#include "prospecting.hpp"
#include "protoutils.hpp"
#include "testutils.hpp"

#include "database/account.hpp"
#include "database/character.hpp"
#include "database/damagelists.hpp"
#include "database/dbtest.hpp"
#include "database/faction.hpp"
#include "database/region.hpp"
#include "hexagonal/coord.hpp"
#include "mapdata/basemap.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <json/json.h>

#include <sstream>
#include <string>
#include <vector>

namespace pxd
{

/* ************************************************************************** */

/**
 * Test fixture for testing PXLogic::UpdateState.  It sets up a test database
 * independent from SQLiteGame, so that we can more easily test custom
 * situations as needed.
 */
class PXLogicTests : public DBTestWithSchema
{

private:

  TestRandom rnd;

protected:

  ContextForTesting ctx;

  AccountsTable accounts;
  CharacterTable characters;
  RegionsTable regions;

  PXLogicTests ()
    : accounts(db), characters(db), regions(db)
  {
    InitialisePrizes (db, ctx.Params ());
  }

  /**
   * Builds a blockData JSON value from the given moves (JSON serialised
   * to a string).
   */
  Json::Value
  BuildBlockData (const std::string& movesStr)
  {
    Json::Value blockData(Json::objectValue);
    blockData["admin"] = Json::Value (Json::arrayValue);

    Json::Value meta(Json::objectValue);
    meta["height"] = 42;
    meta["timestamp"] = 1500000000;
    blockData["block"] = meta;

    std::istringstream in(movesStr);
    in >> blockData["moves"];

    return blockData;
  }

  /**
   * Creates a new character for the given name and faction.  Initialises
   * the account in the database first as needed.
   */
  CharacterTable::Handle
  CreateCharacter (const std::string& name, const Faction f)
  {
    auto a = accounts.GetByName (name);
    if (a == nullptr)
      a = accounts.CreateNew (name, f);

    CHECK (a->GetFaction () == f);
    return characters.CreateNew (name, f);
  }

  /**
   * Calls PXLogic::UpdateState with our test instances of the database,
   * params and RNG.  The given string is parsed as JSON array and used
   * as moves in the block data.
   */
  void
  UpdateState (const std::string& movesStr)
  {
    UpdateStateWithData (BuildBlockData (movesStr));
  }

  /**
   * Calls PXLogic::UpdateState with the given block data and our params, RNG
   * and stuff.  This is a more general variant of UpdateState(std::string),
   * where the block data can be modified to include extra stuff (e.g. a block
   * height of our choosing).
   */
  void
  UpdateStateWithData (const Json::Value& blockData)
  {
    PXLogic::UpdateState (db, rnd, ctx.Chain (), ctx.Map (), blockData);
  }

  /**
   * Calls PXLogic::UpdateState with the given moves and a provided (mocked)
   * FameUpdater instance.
   */
  void
  UpdateStateWithFame (FameUpdater& fame, const std::string& moveStr)
  {
    const auto blockData = BuildBlockData (moveStr);
    PXLogic::UpdateState (db, fame, rnd, ctx, blockData);
  }

  /**
   * Calls game-state validation.
   */
  void
  ValidateState ()
  {
    PXLogic::ValidateStateSlow (db);
  }

};

namespace
{

/**
 * Adds an attack to the character that does always exactly one damage and
 * has the given range.
 */
void
AddUnityAttack (Character& c, const HexCoord::IntT range)
{
  auto* attack = c.MutableProto ().mutable_combat_data ()->add_attacks ();
  attack->set_range (range);
  attack->set_min_damage (1);
  attack->set_max_damage (1);
}

/* ************************************************************************** */

TEST_F (PXLogicTests, WaypointsBeforeMovement)
{
  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->MutableVolatileMv ().set_partial_step (1000);
  auto& pb = c->MutableProto ();
  pb.set_speed (750);
  pb.mutable_combat_data ();
  pb.mutable_movement ()->add_waypoints ()->set_x (5);
  c.reset ();

  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": [{"x": -1, "y": 0}]}}}
    }
  ])");

  EXPECT_EQ (characters.GetById (1)->GetPosition (), HexCoord (0, 0));
  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (1)->GetPosition (), HexCoord (-1, 0));
}

TEST_F (PXLogicTests, MovementBeforeTargeting)
{
  auto c = CreateCharacter ("domob", Faction::RED);
  const auto id1 = c->GetId ();
  AddUnityAttack (*c, 10);
  c.reset ();

  c = CreateCharacter ("andy", Faction::GREEN);
  const auto id2 = c->GetId ();
  c->SetPosition (HexCoord (11, 0));
  auto& pb = c->MutableProto ();
  pb.set_speed (750);
  pb.mutable_combat_data ();
  c.reset ();

  UpdateState ("[]");

  ASSERT_EQ (characters.GetById (id2)->GetPosition (), HexCoord (11, 0));
  ASSERT_FALSE (characters.GetById (id1)->GetProto ().has_target ());

  c = characters.GetById (id2);
  auto* wp = c->MutableProto ().mutable_movement ()->mutable_waypoints ();
  c->MutableVolatileMv ().set_partial_step (500);
  *wp->Add () = CoordToProto (HexCoord (0, 0));
  c.reset ();

  UpdateState ("[]");

  ASSERT_EQ (characters.GetById (id2)->GetPosition (), HexCoord (10, 0));
  c = characters.GetById (id1);
  ASSERT_TRUE (c->GetProto ().has_target ());
  const auto& t = c->GetProto ().target ();
  EXPECT_EQ (t.type (), proto::TargetId::TYPE_CHARACTER);
  EXPECT_EQ (t.id (), id2);
}

TEST_F (PXLogicTests, KilledVehicleNoLongerBlocks)
{
  auto c = CreateCharacter ("attacker", Faction::GREEN);
  const auto idAttacker = c->GetId ();
  c->SetPosition (HexCoord (11, 0));
  AddUnityAttack (*c, 1);
  c.reset ();

  c = CreateCharacter ("obstacle", Faction::RED);
  const auto idObstacle = c->GetId ();
  c->SetPosition (HexCoord (10, 0));
  c->MutableHP ().set_armour (1);
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  c = CreateCharacter ("moving", Faction::RED);
  const auto idMoving = c->GetId ();
  c->SetPosition (HexCoord (9, 0));
  auto& pb = c->MutableProto ();
  pb.set_speed (1000);
  pb.mutable_combat_data ();
  c.reset ();

  /* Process one block to allow targeting.  */
  UpdateState ("[]");
  ASSERT_NE (characters.GetById (idObstacle), nullptr);
  ASSERT_EQ (characters.GetById (idAttacker)->GetProto ().target ().id (),
             idObstacle);

  /* Next block, the obstacle should be killed and the moving vehicle
     can be moved into its spot.  */
  ASSERT_EQ (idMoving, 3);
  UpdateState (R"([
    {
      "name": "moving",
      "move": {"c": {"3": {"wp": [{"x": 10, "y": 0}]}}}
    }
  ])");

  ASSERT_EQ (characters.GetById (idObstacle), nullptr);
  EXPECT_EQ (characters.GetById (idMoving)->GetPosition (), HexCoord (10, 0));
}

TEST_F (PXLogicTests, DamageInNextRound)
{
  auto c = CreateCharacter ("domob", Faction::RED);
  AddUnityAttack (*c, 1);
  c.reset ();

  c = CreateCharacter ("andy", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableHP ().set_armour (100);
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 100);
  UpdateState ("[]");
  EXPECT_EQ (characters.GetById (idTarget)->GetHP ().armour (), 99);
}

TEST_F (PXLogicTests, DamageKillsRegeneration)
{
  auto c = CreateCharacter ("domob", Faction::RED);
  AddUnityAttack (*c, 1);
  c.reset ();

  c = CreateCharacter ("andy", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableProto ().mutable_combat_data ();
  c.reset ();

  /* Progress one round forward to target.  */
  UpdateState ("[]");

  /* Update the target character so that it will be killed with the attack,
     but would regenerate HP if that were done before applying damage.  */
  c = characters.GetById (idTarget);
  ASSERT_TRUE (c != nullptr);
  auto& regen = c->MutableRegenData ();
  regen.set_shield_regeneration_mhp (2000);
  regen.mutable_max_hp ()->set_shield (100);
  c->MutableHP ().set_shield (1);
  c->MutableHP ().set_armour (0);
  c.reset ();

  /* Now the attack should kill the target before it can regenerate.  */
  UpdateState ("[]");
  EXPECT_TRUE (characters.GetById (idTarget) == nullptr);
}

TEST_F (PXLogicTests, PickUpDeadDrop)
{
  const HexCoord pos(10, 20);

  auto c = CreateCharacter ("attacker", Faction::RED);
  const auto idAttacker = c->GetId ();
  ASSERT_EQ (idAttacker, 1);
  c->SetPosition (pos);
  c->MutableProto ().set_cargo_space (1000);
  AddUnityAttack (*c, 1);
  c.reset ();

  c = CreateCharacter ("target", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->SetPosition (pos);
  c->MutableProto ().set_cargo_space (1000);
  c->MutableProto ().mutable_combat_data ();
  c->GetInventory ().SetFungibleCount ("foo", 10);
  c.reset ();

  /* Progress one round forward to target.  */
  UpdateState ("[]");

  /* Update the target character so that it will be killed with the attack.  */
  c = characters.GetById (idTarget);
  ASSERT_TRUE (c != nullptr);
  c->MutableHP ().set_shield (1);
  c->MutableHP ().set_armour (0);
  c.reset ();

  /* Now the attack should kill the target.  The attacker should be able to
     pick up the dropped loot right at the same time, because kills are
     processed at the beginning of a block, before handling moves.  */
  UpdateState (R"([
    {
      "name": "attacker",
      "move": {"c": {"1": {"pu": {"f": {"foo": 3}}}}}
    }
  ])");

  EXPECT_TRUE (characters.GetById (idTarget) == nullptr);
  c = characters.GetById (idAttacker);
  ASSERT_TRUE (c != nullptr);
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 3);
}

TEST_F (PXLogicTests, DamageLists)
{
  DamageLists dl(db, 0);

  auto c = CreateCharacter ("domob", Faction::RED);
  const auto idAttacker = c->GetId ();
  AddUnityAttack (*c, 1);
  c.reset ();

  c = CreateCharacter ("andy", Faction::GREEN);
  const auto idTarget = c->GetId ();
  c->MutableProto ().mutable_combat_data ();
  auto& regen = c->MutableRegenData ();
  regen.mutable_max_hp ()->set_shield (100);
  c->MutableHP ().set_shield (100);
  c.reset ();

  /* Progress one round forward to target.  */
  UpdateState ("[]");

  /* Deal damage, which should be recorded in the damage list.  */
  Json::Value blockData = BuildBlockData ("[]");
  blockData["block"]["height"] = 100;
  UpdateStateWithData (blockData);
  EXPECT_EQ (dl.GetAttackers (idTarget),
             DamageLists::Attackers ({idAttacker}));

  /* Remove the attacks, so the damage list entry is not refreshed.  */
  c = characters.GetById (idAttacker);
  c->MutableProto ().mutable_combat_data ()->clear_attacks ();
  c.reset ();

  /* The damage list entry should still be present 99 blocks after.  */
  blockData = BuildBlockData ("[]");
  blockData["block"]["height"] = 199;
  UpdateStateWithData (blockData);
  EXPECT_EQ (dl.GetAttackers (idTarget),
             DamageLists::Attackers ({idAttacker}));

  /* The entry should be removed at block 200.  */
  blockData = BuildBlockData ("[]");
  blockData["block"]["height"] = 200;
  UpdateStateWithData (blockData);
  EXPECT_EQ (dl.GetAttackers (idTarget), DamageLists::Attackers ({}));
}

TEST_F (PXLogicTests, FameUpdate)
{
  /* Set up two characters that will kill each other in the same turn.  */
  std::vector<Database::IdT> ids;
  for (const auto f : {Faction::RED, Faction::GREEN})
    {
      auto c = CreateCharacter (FactionToString (f), f);
      ids.push_back (c->GetId ());
      AddUnityAttack (*c, 1);
      auto& regen = c->MutableRegenData ();
      regen.mutable_max_hp ()->set_shield (1);
      c->MutableHP ().set_shield (1);
    }

  MockFameUpdater fame(db, ctx);

  EXPECT_CALL (fame, UpdateForKill (ids[0], DamageLists::Attackers ({ids[1]})));
  EXPECT_CALL (fame, UpdateForKill (ids[1], DamageLists::Attackers ({ids[0]})));

  /* Do two updates, first for targeting and the second for the actual kill.  */
  UpdateStateWithFame (fame, "[]");
  ASSERT_NE (characters.GetById (ids[0]), nullptr);
  ASSERT_NE (characters.GetById (ids[1]), nullptr);
  UpdateStateWithFame (fame, "[]");
  ASSERT_EQ (characters.GetById (ids[0]), nullptr);
  ASSERT_EQ (characters.GetById (ids[1]), nullptr);
}

TEST_F (PXLogicTests, ProspectingBeforeMovement)
{
  /* This should test that prospecting is started before processing
     movement.  In other words, if a character is about to move to the
     next region when a "prospect" command hits, then prospecting should
     be started at the "old" region.  For this, we need two coordinates
     next to each other but in different regions.  */
  HexCoord pos1, pos2;
  RegionMap::IdT region1, region2;
  for (HexCoord::IntT x = 0; ; ++x)
    {
      pos1 = HexCoord (x, 0);
      region1 = ctx.Map ().Regions ().GetRegionId (pos1);
      pos2 = HexCoord (x + 1, 0);
      region2 = ctx.Map ().Regions ().GetRegionId (pos2);
      if (region1 != region2)
        break;
    }
  CHECK_NE (region1, region2);
  CHECK_EQ (HexCoord::DistanceL1 (pos1, pos2), 1);
  LOG (INFO)
      << "Neighbouring coordinates " << pos1 << " and " << pos2
      << " are in differing regions " << region1 << " and " << region2;

  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos1);
  c->MutableVolatileMv ().set_partial_step (1000);
  auto& pb = c->MutableProto ();
  pb.mutable_combat_data ();
  *pb.mutable_movement ()->add_waypoints () = CoordToProto (pos2);
  c.reset ();

  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");

  c = characters.GetById (1);
  EXPECT_EQ (c->GetPosition (), pos1);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_TRUE (c->GetProto ().has_prospection ());

  auto r = regions.GetById (region1);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  r = regions.GetById (region2);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
}

TEST_F (PXLogicTests, ProspectingUserKilled)
{
  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  /* Set up characters such that one is killing the other on the next round.  */
  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  AddUnityAttack (*c, 1);
  c.reset ();

  c = CreateCharacter ("andy", Faction::GREEN);
  ASSERT_EQ (c->GetId (), 2);
  c->SetPosition (pos);
  c->MutableProto ().mutable_combat_data ();
  auto& regen = c->MutableRegenData ();
  regen.mutable_max_hp ()->set_shield (100);
  c->MutableHP ().set_shield (1);
  c->MutableHP ().set_armour (0);
  c.reset ();

  /* Progress one round forward to target and also start prospecting
     with the character that will be killed.  */
  UpdateState (R"([
    {
      "name": "andy",
      "move": {"c": {"2": {"prospect": {}}}}
    }
  ])");

  c = characters.GetById (2);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_TRUE (c->GetProto ().has_prospection ());

  /* Make sure that the prospecting operation would be finished on the next
     step (but it won't be as the character is killed).  */
  c->SetBusy (1);
  c.reset ();

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 2);

  /* Process another round, where the prospecting character is killed.  Thus
     the other is able to start prospecting at the same spot.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");

  EXPECT_TRUE (characters.GetById (2) == nullptr);

  c = characters.GetById (1);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_TRUE (c->GetProto ().has_prospection ());

  r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());
}

TEST_F (PXLogicTests, FinishingProspecting)
{
  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  c->MutableProto ().mutable_combat_data ();
  c->MutableProto ().set_speed (1000);
  c.reset ();

  /* Start prospecting with that character.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");
  EXPECT_EQ (characters.GetById (1)->GetBusy (), 10);

  /* Process blocks until the operation is nearly done.  */
  for (unsigned i = 0; i < 9; ++i)
    UpdateState ("[]");
  EXPECT_EQ (characters.GetById (1)->GetBusy (), 1);

  auto r = regions.GetById (region);
  EXPECT_EQ (r->GetProto ().prospecting_character (), 1);
  EXPECT_FALSE (r->GetProto ().has_prospection ());

  /* Process the last block which finishes prospecting.  We should be able
     to do a movement command right away as well, since the busy state is
     processed before the moves.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"wp": [{"x": 0, "y": 0}]}}}
    }
  ])");

  c = characters.GetById (1);
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_FALSE (c->GetProto ().has_prospection ());
  EXPECT_TRUE (c->GetProto ().has_movement ());

  r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
}

TEST_F (PXLogicTests, MiningRightAfterProspecting)
{
  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  c->MutableProto ().mutable_combat_data ();
  c->MutableProto ().mutable_mining ()->mutable_rate ()->set_min (1);
  c->MutableProto ().mutable_mining ()->mutable_rate ()->set_max (1);
  c->MutableProto ().set_cargo_space (100);
  c.reset ();

  /* Prospect the region with the character.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");
  for (unsigned i = 0; i < 9; ++i)
    UpdateState ("[]");
  EXPECT_EQ (characters.GetById (1)->GetBusy (), 1);

  /* In the next block, prospecting will be finished.  We can already start
     mining the now-prospected region immediately.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"mine": {}}}}
    }
  ])");

  auto r = regions.GetById (region);
  const std::string type = r->GetProto ().prospection ().resource ();
  LOG (INFO) << "Resource found: " << type;

  c = characters.GetById (1);
  EXPECT_EQ (c->GetBusy (), 0);
  EXPECT_TRUE (c->GetProto ().mining ().active ());
  EXPECT_EQ (c->GetInventory ().GetFungibleCount (type), 1);
}

TEST_F (PXLogicTests, MiningAndDropping)
{
  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  c->MutableProto ().mutable_combat_data ();
  c->MutableProto ().mutable_mining ()->mutable_rate ()->set_min (10);
  c->MutableProto ().mutable_mining ()->mutable_rate ()->set_max (10);
  c->MutableProto ().mutable_mining ()->set_active (true);
  c->MutableProto ().set_cargo_space (1000);
  c->GetInventory ().SetFungibleCount ("foo", 95);
  c.reset ();

  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_resource ("foo");
  r->SetResourceLeft (1000);
  r.reset ();

  /* Processing one block will mine some more, filling up the inventory.  */
  UpdateState ("[]");
  c = characters.GetById (1);
  EXPECT_TRUE (c->GetProto ().mining ().active ());
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 100);
  c.reset ();
  EXPECT_EQ (regions.GetById (region)->GetResourceLeft (), 995);

  /* In the next block, drop loot.  This should take effect before mining,
     so that we will be able to mine some more afterwards.  */
  UpdateState (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"drop": {"f": {"foo": 3}}}}}
    }
  ])");
  c = characters.GetById (1);
  EXPECT_TRUE (c->GetProto ().mining ().active ());
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 100);
  c.reset ();
  EXPECT_EQ (regions.GetById (region)->GetResourceLeft (), 992);

  /* One more block where we won't pick up anything, so we will stop mining.  */
  UpdateState ("[]");
  c = characters.GetById (1);
  EXPECT_FALSE (c->GetProto ().mining ().active ());
  EXPECT_EQ (c->GetInventory ().GetFungibleCount ("foo"), 100);
  c.reset ();
  EXPECT_EQ (regions.GetById (region)->GetResourceLeft (), 992);
}

TEST_F (PXLogicTests, MiningWhenReprospected)
{
  ctx.SetChain (xaya::Chain::REGTEST);

  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = CreateCharacter ("domob", Faction::RED);
  ASSERT_EQ (c->GetId (), 1);
  c->SetPosition (pos);
  c->MutableProto ().mutable_combat_data ();
  c->MutableProto ().mutable_mining ()->mutable_rate ()->set_min (1);
  c->MutableProto ().mutable_mining ()->mutable_rate ()->set_max (1);
  c->MutableProto ().mutable_mining ()->set_active (true);
  c->MutableProto ().set_cargo_space (1000);
  c.reset ();

  auto r = regions.GetById (region);
  r->MutableProto ().mutable_prospection ()->set_height (1);
  r->MutableProto ().mutable_prospection ()->set_resource ("foo");
  r->SetResourceLeft (1);
  r.reset ();

  /* When we reprospect the region while still mining, this should just stop
     mining gracefully.  We can only reprospect after using up the resources,
     which means that we need to mine for one turn before.  */
  UpdateState ("[]");
  auto data = BuildBlockData (R"([
    {
      "name": "domob",
      "move": {"c": {"1": {"prospect": {}}}}
    }
  ])");
  data["block"]["height"] = 200;
  UpdateStateWithData (data);

  c = characters.GetById (1);
  EXPECT_EQ (c->GetBusy (), 10);
  EXPECT_FALSE (c->GetProto ().mining ().active ());
  c.reset ();
  EXPECT_FALSE (regions.GetById (region)->GetProto ().has_prospection ());
}

/* ************************************************************************** */

using ValidateStateTests = PXLogicTests;

TEST_F (ValidateStateTests, CharacterFactions)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto id = c->GetId ();
  c.reset ();
  EXPECT_DEATH (ValidateState (), "owned by uninitialised account");

  accounts.CreateNew ("domob", Faction::GREEN);
  EXPECT_DEATH (ValidateState (), "Faction mismatch");

  accounts.CreateNew ("andy", Faction::RED);
  characters.GetById (id)->SetOwner ("andy");
  ValidateState ();
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
