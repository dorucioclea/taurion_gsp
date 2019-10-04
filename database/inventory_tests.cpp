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

#include "inventory.hpp"

#include "dbtest.hpp"

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

#include <map>

namespace pxd
{
namespace
{

using google::protobuf::TextFormat;

/* ************************************************************************** */

class InventoryTests : public testing::Test
{

protected:

  Inventory inv;

  /**
   * Expects that the elements in the fungible "map" match the given
   * set of expected elements.
   */
  void
  ExpectFungibleElements (const std::map<std::string, uint64_t>& expected)
  {
    const auto& fungible = inv.GetFungible ();
    ASSERT_EQ (expected.size (), fungible.size ());
    for (const auto& entry : expected)
      {
        const auto mit = fungible.find (entry.first);
        ASSERT_TRUE (mit != fungible.end ())
            << "Entry " << entry.first << " not found in actual data";
        ASSERT_EQ (mit->second, entry.second);
      }
  }

};

TEST_F (InventoryTests, DefaultData)
{
  ExpectFungibleElements ({});
  EXPECT_EQ (inv.GetFungibleCount ("foo"), 0);
  EXPECT_FALSE (inv.IsDirty ());
  EXPECT_EQ (inv.GetProtoForBinding ().GetSerialised (), "");
}

TEST_F (InventoryTests, FromProto)
{
  LazyProto<proto::Inventory> pb;
  pb.SetToDefault ();
  CHECK (TextFormat::ParseFromString (R"(
    fungible: { key: "foo" value: 10 }
    fungible: { key: "bar" value: 5 }
  )", &pb.Mutable ()));

  inv = std::move (pb);

  ExpectFungibleElements ({{"foo", 10}, {"bar", 5}});
  EXPECT_FALSE (inv.IsEmpty ());
}

TEST_F (InventoryTests, Modification)
{
  inv.SetFungibleCount ("foo", 10);
  inv.SetFungibleCount ("bar", 5);

  ExpectFungibleElements ({{"foo", 10}, {"bar", 5}});
  EXPECT_EQ (inv.GetFungibleCount ("foo"), 10);
  EXPECT_EQ (inv.GetFungibleCount ("bar"), 5);
  EXPECT_EQ (inv.GetFungibleCount ("baz"), 0);
  EXPECT_FALSE (inv.IsEmpty ());
  EXPECT_TRUE (inv.IsDirty ());

  inv.AddFungibleCount ("bar", 3);
  ExpectFungibleElements ({{"foo", 10}, {"bar", 8}});

  inv.SetFungibleCount ("foo", 0);
  ExpectFungibleElements ({{"bar", 8}});
  EXPECT_FALSE (inv.IsEmpty ());

  inv.AddFungibleCount ("bar", -8);
  EXPECT_TRUE (inv.IsEmpty ());
  EXPECT_TRUE (inv.IsDirty ());
}

TEST_F (InventoryTests, DualProduct)
{
  const auto val = Inventory::Product (MAX_ITEM_QUANTITY, -MAX_ITEM_DUAL);
  EXPECT_EQ (val % MAX_ITEM_DUAL, 0);
  EXPECT_EQ (val / MAX_ITEM_QUANTITY, -MAX_ITEM_DUAL);
}

/* ************************************************************************** */

struct CountResult : public Database::ResultType
{
  RESULT_COLUMN (int64_t, cnt, 1);
};

class GroundLootTests : public DBTestWithSchema
{

protected:

  GroundLootTable tbl;

  GroundLootTests ()
    : tbl(db)
  {}

  /**
   * Returns the number of entries in the ground-loot table.
   */
  unsigned
  CountEntries ()
  {
    auto stmt = db.Prepare ("SELECT COUNT(*) AS `cnt` FROM `ground_loot`");
    auto res = stmt.Query<CountResult> ();
    CHECK (res.Step ());
    const unsigned cnt = res.Get<CountResult::cnt> ();
    CHECK (!res.Step ());
    return cnt;
  }

};

TEST_F (GroundLootTests, DefaultData)
{
  auto h = tbl.GetByCoord (HexCoord (1, 2));
  EXPECT_EQ (h->GetPosition (), HexCoord (1, 2));
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
  h.reset ();

  EXPECT_EQ (CountEntries (), 0);
}

TEST_F (GroundLootTests, Update)
{
  const HexCoord c1(1, 2);
  tbl.GetByCoord (c1)->GetInventory ().SetFungibleCount ("foo", 5);

  const HexCoord c2(1, 3);
  tbl.GetByCoord (c2)->GetInventory ().SetFungibleCount ("bar", 42);

  auto h = tbl.GetByCoord (c1);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 5);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 0);

  h = tbl.GetByCoord (c2);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("bar"), 42);

  EXPECT_EQ (CountEntries (), 2);
}

TEST_F (GroundLootTests, Removal)
{
  const HexCoord c(1, 2);
  tbl.GetByCoord (c)->GetInventory ().SetFungibleCount ("foo", 5);
  EXPECT_EQ (CountEntries (), 1);

  auto h = tbl.GetByCoord (c);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 5);
  h->GetInventory ().SetFungibleCount ("foo", 0);
  h.reset ();
  EXPECT_EQ (CountEntries (), 0);

  h = tbl.GetByCoord (c);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 0);
  EXPECT_TRUE (h->GetInventory ().IsEmpty ());
}

using GroundLootTableTests = GroundLootTests;

TEST_F (GroundLootTableTests, QueryNonEmpty)
{
  const HexCoord c1(1, 2);
  const HexCoord c2(1, 3);
  const HexCoord c3(2, 2);

  tbl.GetByCoord (c1)->GetInventory ().SetFungibleCount ("foo", 1);
  tbl.GetByCoord (c2)->GetInventory ().SetFungibleCount ("foo", 2);
  tbl.GetByCoord (c3)->GetInventory ().SetFungibleCount ("foo", 3);

  auto res = tbl.QueryNonEmpty ();

  ASSERT_TRUE (res.Step ());
  auto h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetPosition (), c1);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 1);

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetPosition (), c2);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 2);

  ASSERT_TRUE (res.Step ());
  h = tbl.GetFromResult (res);
  EXPECT_EQ (h->GetPosition (), c3);
  EXPECT_EQ (h->GetInventory ().GetFungibleCount ("foo"), 3);

  ASSERT_FALSE (res.Step ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace pxd
