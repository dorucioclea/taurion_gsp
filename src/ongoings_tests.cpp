/*
    GSP for the Taurion blockchain game
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "ongoings.hpp"

#include "testutils.hpp"

#include "database/building.hpp"
#include "database/character.hpp"
#include "database/dbtest.hpp"
#include "database/inventory.hpp"
#include "database/ongoing.hpp"
#include "database/region.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace pxd
{
namespace
{

class OngoingsTests : public DBTestWithSchema
{

protected:

  BuildingsTable buildings;
  BuildingInventoriesTable buildingInv;
  CharacterTable characters;
  OngoingsTable ongoings;

  TestRandom rnd;
  ContextForTesting ctx;

  OngoingsTests ()
    : buildings(db), buildingInv(db), characters(db), ongoings(db)
  {}

  /**
   * Inserts an ongoing operation into the table, associated to the
   * given character.  Returns the handle for further changes.
   */
  OngoingsTable::Handle
  AddOp (Character& c)
  {
    auto op = ongoings.CreateNew ();
    op->SetCharacterId (c.GetId ());
    c.MutableProto ().set_ongoing (op->GetId ());
    return op;
  }

  /**
   * Inserts an ongoing operation into the table, associated to the given
   * building.  Returns the handle.
   */
  OngoingsTable::Handle
  AddOp (Building& b)
  {
    auto op = ongoings.CreateNew ();
    op->SetBuildingId (b.GetId ());
    return op;
  }

  /**
   * Returns the number of ongoing operations.
   */
  unsigned
  GetNumOngoing ()
  {
    auto res = ongoings.QueryAll ();
    unsigned cnt = 0;
    while (res.Step ())
      ++cnt;
    return cnt;
  }

};

TEST_F (OngoingsTests, ProcessedByHeight)
{
  proto::BlueprintCopy cpTemplate;
  cpTemplate.set_account ("domob");
  cpTemplate.set_original_type ("bow bpo");
  cpTemplate.set_copy_type ("bow bpc");

  auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  const auto bId = b->GetId ();

  auto op = AddOp (*b);
  op->SetHeight (10);
  cpTemplate.set_num_copies (1);
  *op->MutableProto ().mutable_blueprint_copy () = cpTemplate;
  op.reset ();

  op = AddOp (*b);
  op->SetHeight (15);
  cpTemplate.set_num_copies (10);
  *op->MutableProto ().mutable_blueprint_copy () = cpTemplate;
  op.reset ();

  b.reset ();

  ctx.SetHeight (9);
  ProcessAllOngoings (db, rnd, ctx);
  auto inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 0);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 0);
  EXPECT_EQ (GetNumOngoing (), 2);

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 1);
  EXPECT_EQ (GetNumOngoing (), 1);

  ctx.SetHeight (14);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 1);
  EXPECT_EQ (GetNumOngoing (), 1);

  ctx.SetHeight (15);
  ProcessAllOngoings (db, rnd, ctx);
  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 2);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 11);
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, ArmourRepair)
{
  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto cId = c->GetId ();
  c->MutableRegenData ().mutable_max_hp ()->set_armour (1'000);
  c->MutableHP ().set_armour (850);

  auto op = AddOp (*c);
  const auto opId = op->GetId ();
  op->SetHeight (10);
  op->MutableProto ().mutable_armour_repair ();

  op.reset ();
  c.reset ();

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  c = characters.GetById (cId);
  EXPECT_FALSE (c->IsBusy ());
  EXPECT_EQ (c->GetHP ().armour (), 1'000);
  EXPECT_EQ (ongoings.GetById (opId), nullptr);
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, Prospection)
{
  const HexCoord pos(5, 5);
  const auto region = ctx.Map ().Regions ().GetRegionId (pos);

  auto c = characters.CreateNew ("domob", Faction::RED);
  const auto cId = c->GetId ();
  c->SetPosition (pos);

  auto op = AddOp (*c);
  op->SetHeight (10);
  op->MutableProto ().mutable_prospection ();

  op.reset ();
  c.reset ();

  RegionsTable regions(db, 5);
  regions.GetById (region)->MutableProto ().set_prospecting_character (cId);

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  c = characters.GetById (cId);
  EXPECT_FALSE (c->IsBusy ());
  auto r = regions.GetById (region);
  EXPECT_FALSE (r->GetProto ().has_prospecting_character ());
  EXPECT_EQ (r->GetProto ().prospection ().name (), "domob");
  EXPECT_EQ (GetNumOngoing (), 0);
}

TEST_F (OngoingsTests, BlueprintCopy)
{
  auto b = buildings.CreateNew ("ancient1", "", Faction::ANCIENT);
  const auto bId = b->GetId ();
  auto op = AddOp (*b);
  op->SetHeight (10);
  auto& cp = *op->MutableProto ().mutable_blueprint_copy ();
  cp.set_account ("domob");
  cp.set_original_type ("bow bpo");
  cp.set_copy_type ("bow bpc");
  cp.set_num_copies (20);
  op.reset ();
  b.reset ();

  auto inv = buildingInv.Get (bId, "domob");
  inv->GetInventory ().AddFungibleCount ("bow bpc", 10);
  inv.reset ();

  ctx.SetHeight (10);
  ProcessAllOngoings (db, rnd, ctx);

  inv = buildingInv.Get (bId, "domob");
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpo"), 1);
  EXPECT_EQ (inv->GetInventory ().GetFungibleCount ("bow bpc"), 30);
  EXPECT_EQ (GetNumOngoing (), 0);
}

} // anonymous namespace
} // namespace pxd