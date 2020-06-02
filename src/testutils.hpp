/*
    GSP for the Taurion blockchain game
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#ifndef PXD_TESTUTILS_HPP
#define PXD_TESTUTILS_HPP

#include "context.hpp"

#include "mapdata/basemap.hpp"

#include <xayagame/gamelogic.hpp>
#include <xayautil/random.hpp>

#include <json/json.h>

namespace pxd
{

/**
 * Random instance that seeds itself on construction from a fixed test seed.
 */
class TestRandom : public xaya::Random
{

public:

  TestRandom ();

};

/**
 * Context instance that can modify certain fields (like the block height).
 */
class ContextForTesting : public Context
{

private:

  /** The BaseMap instance used.  */
  BaseMap map;

public:

  ContextForTesting ()
    : Context(xaya::Chain::REGTEST, map, 0, NO_TIMESTAMP)
  {
    /* Note that map will be constructed only after the superclass
       constructor, which stores a reference to it.  That is fine, though,
       as it won't be accessed then.  */
  }

  void SetChain (xaya::Chain c);
  void SetHeight (const unsigned h);
  void SetTimestamp (const int64_t ts);

};

/**
 * Parses a string into JSON.
 */
Json::Value ParseJson (const std::string& str);

/**
 * Checks for "partial equality" of the given JSON values.  This means that
 * keys not present in the expected value (if it is an object) are not checked
 * in the actual value at all.  If keys have a value of null in expected,
 * then they must not be there in actual at all.
 */
bool PartialJsonEqual (const Json::Value& actual, const Json::Value& expected);

} // namespace pxd

#endif // PXD_TESTUTILS_HPP
