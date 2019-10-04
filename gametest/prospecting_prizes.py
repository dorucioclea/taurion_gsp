#!/usr/bin/env python

#   GSP for the Taurion blockchain game
#   Copyright (C) 2019  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Tests distribution of prizes for prospecting.
"""

from pxtest import PXTest

# Timestamps when the competition is still active and when it is
# already over.  Note that for some reason we cannot be exact to the
# second here, since the mined block timestamps not always match the
# mocktime exactly.  We verify the correct behaviour with respect to the
# timestamp in unit tests, though.
COMPETITION_RUNNING = 1500000000
COMPETITION_OVER = 1600000000


class ProspectingPrizesTest (PXTest):

  def run (self):
    # Mine a couple of blocks to get a meaningful median time
    # that is before the times we'll use in testing.
    self.rpc.xaya.setmocktime (COMPETITION_RUNNING)
    self.generate (10)

    self.collectPremine ()

    # First test:  Try and retry (with a reorg) prospecting in the
    # same region to get both a silver tier and not silver.
    self.mainLogger.info ("Testing randomisation of prizes...")

    self.initAccount ("prize trier", "r")
    c = self.createCharacters ("prize trier")
    self.generate (1)
    pos = {"x": -1000, "y": 1000}
    self.moveCharactersTo ({"prize trier": pos})
    stillNeedNoSilver = True
    stillNeedSilver = True
    blk = None
    self.getCharacters ()["prize trier"].sendMove ({"prospect": {}})
    self.generate (9)
    while stillNeedNoSilver or stillNeedSilver:
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()
      self.generate (1)

      prosp = self.getRegionAt (pos).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")
      if (not "prize" in prosp) or prosp["prize"] != "silver":
        stillNeedNoSilver = False
      elif prosp["prize"] == "silver":
        stillNeedSilver = False

      self.rpc.xaya.invalidateblock (blk)

    assert blk is not None
    blkOldTime = blk

    # Test the impact of the block time onto received prizes.  After
    # the competition is over, no prizes should be found anymore.  It
    # is possible to have a "beyond" block and then an earlier block with
    # prizes, though.  Thus we mine the block before prospecting ends
    # always after the competition.
    self.mainLogger.info ("Testing time and prizes...")

    self.rpc.xaya.setmocktime (COMPETITION_OVER)
    self.generate (1)

    # There's a 12% chance that we will simply not find a silver prize
    # (with 10% chance) in 20 trials even if we could, but we are fine
    # with that.
    for _ in range (20):
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()

      prosp = self.getRegionAt (pos).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")
      assert "prize" not in prosp

      self.rpc.xaya.invalidateblock (blk)

    self.rpc.xaya.setmocktime (COMPETITION_RUNNING)
    stillNeedPrize = True
    while stillNeedPrize:
      self.generate (1)
      blk = self.rpc.xaya.getbestblockhash ()

      prosp = self.getRegionAt (pos).data["prospection"]
      self.assertEqual (prosp["name"], "prize trier")
      if "prize" in prosp:
        stillNeedPrize = False

      self.rpc.xaya.invalidateblock (blk)

    # Restore the last randomised attempt.  Else we might end up with
    # a long invalid chain, which can confuse the reorg test.
    self.rpc.xaya.reconsiderblock (blkOldTime)

    # Prospect in some regions and verify some basic expectations
    # on the number of prizes found.
    self.mainLogger.info ("Testing prize numbers...")

    sendTo = {}
    regionIds = set ()
    nextInd = 2
    for i in range (2):
      for j in range (10):
        pos = {"x": 20 * i, "y": 20 * j}
        region = self.getRegionAt (pos)
        assert "prospection" not in region.data
        assert region.getId () not in regionIds
        regionIds.add (region.getId ())

        self.createCharacters ("prize trier")
        nm = "prize trier %d" % nextInd
        nextInd += 1
        sendTo[nm] = pos
    self.generate (1)
    self.moveCharactersTo (sendTo)

    chars = self.getCharacters ()
    for nm in sendTo:
      chars[nm].sendMove ({"prospect": {}})
    self.generate (11)

    prizesInRegions = {
      "gold": 0,
      "silver": 0,
      "bronze": 0,
    }
    for r in self.getRpc ("getregions"):
      if ("prospection" in r) and "prize" in r["prospection"]:
        prizesInRegions[r["prospection"]["prize"]] += 1
    for nm, val in self.getRpc ("getprizestats").iteritems ():
      self.assertEqual (prizesInRegions[nm], val["found"])
    self.log.info ("Found prizes:\n%s" % prizesInRegions)
    self.assertEqual (prizesInRegions["bronze"], 1)
    assert prizesInRegions["silver"] > 0


if __name__ == "__main__":
  ProspectingPrizesTest ().main ()
