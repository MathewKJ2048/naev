--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="The Search for Cynthia">
 <flags>
   <unique />
 </flags>
 <avail>
  <priority>4</priority>
  <done>The Runaway</done>
  <chance>11</chance>
  <location>Bar</location>
  <system>Goddard</system>
 </avail>
</mission>
--]]
--[[
   This is the second half of "The Runaway"
   Here, Cynthia's father pays you to track down his daughter.
   It is alluded to that Cynthia ran away due to her abusive mother.
   The father has been named after me, Old T. Man.
   I'm joking about the last line a little. If you want to name him, feel free.

   [JEB] split mission into stages as follows:
      1. Search "targetworlds" for clues. (Unchecked locations are kept in mem.search_pnts.)
      2. Follow a clear lead to "catchworld".
      3. Return to "homeworld".
--]]

local fmt = require "format"
local neu = require "common.neutral"

local releasereward = 25e3
local reward = 300e3

-- Mission constants
local cargoname = N_("Cynthia")
local cargodesc = N_("A young teenager.")
local targetworlds = {
  spob.get("Niflheim"),
  spob.get("Nova Shakar"),
  spob.get("Selphod"),
  spob.get("Emperor's Fist")
}
local catchworld = spob.get("Torloth")
local homeworld = spob.get("Zhiru")

function create ()
   misn.setNPC( _("Old Man"), "neutral/unique/cynthia_father.webp", _("An old man sits at a table with some missing person papers.") )
end

local function init_search()
   if mem.search_pnts ~= nil then
      return
   end
   -- Migrate an old mission in progress
   if mem.runawayMarker ~= nil then
      misn.markerRm( mem.runawayMarker )
      mem.runawayMarker = nil
   end

   mem.stage = 1
   mem.search_pnts = {}
   mem.search_markers = {}
   mem.search_osds = {}
   for i,pnt in ipairs(targetworlds) do
      mem.search_pnts[i] = pnt
      mem.search_osds[i] = fmt.f( _("Search for Cynthia on {pnt} in {sys}"), {pnt=pnt, sys=pnt:system()} )
      mem.search_markers[i] = misn.markerAdd( pnt, "low" )
   end
   misn.osdCreate( _("The Search for Cynthia"), mem.search_osds )
end

local function tbl_index( tbl, elm )
   for k,v in ipairs(tbl) do
      if v==elm then
         return k
      end
   end
   return nil
end

function accept ()
   --This mission does not make any system claims
   if not tk.yesno( _("The Search for Cynthia"), fmt.f(_([[Approaching him, he hands you a paper. It offers a {credits} reward for the finding of a "Cynthia" person.
    "That's my girl. She disappeared quite a few decaperiods ago. We managed to track her down to here, but where she went afterwards remains a mystery. We know she was kidnapped, but if you know anything..." The man begins to cry. "Have you seen any trace of her?"]]),{credits=fmt.credits(reward)})) then
      misn.finish()
   end

   if not misn.accept() then
      return
   end

   misn.setTitle( _("The Search for Cynthia") )
   misn.setReward( fmt.f( _("{credits} on delivery."), {credits=fmt.credits(reward)} ) )
   misn.setDesc( _("Search for Cynthia.") )

   init_search()

   tk.msg( _("The Search for Cynthia"), _([[Looking at the picture, you see that the locket matches the one that Cynthia wore, so you hand it to her father. "I believe that this was hers." Stunned, the man hands you a list of planets that they wanted to look for her on.]]) )

   hook.land("land")
end

-- luacheck: globals land (Hook functions passed by name)
function land ()
   init_search() -- to rescue naev-0.9.1 games, which weren't saving the progress

   --If we land on Nova Shakar, display message, reset target and carry on.
   if mem.stage == 1 and spob.cur() == targetworlds[2] then
      mem.stage = 2
      tk.msg(_("The Search for Cynthia"), _("At last! You find her, but she ducks into a tour bus when she sees you. The schedule says it's destined for Torloth. You begin to wonder if she'll want to be found."))

      --Set up the *secret* OSD text
      misn.osdCreate( _("The Search for Cynthia"), {
         _("Catch Cynthia on Torloth in Cygnus"),
         _("Return Cynthia to her father on Zhiru in the Goddard system"),
      } )

      for i,marker in ipairs(mem.search_markers) do
         misn.markerRm( marker )
      end
      mem.search_markers = { misn.markerAdd( catchworld ) }

   --If we land on Niflheim, display message, reset target and carry on.
   elseif mem.stage == 1 then
      local i = tbl_index( mem.search_pnts, spob.cur() )
      if i == nil then
         return
      end
      tk.msg(_("The Search for Cynthia"), _("After thoroughly searching the spaceport, you decide that she wasn't there."))
      misn.markerRm( mem.search_markers[i] )
      table.remove( mem.search_pnts, i )
      table.remove( mem.search_markers, i )
      table.remove( mem.search_osds, i )
      misn.osdCreate( _("The Search for Cynthia"), mem.search_osds )

   --If we land on Torloth, change OSD, display message, reset target and carry on.
   elseif mem.stage == 2 and spob.cur() == catchworld then
      mem.stage = 3

      --If you decide to release her, speak appropriately, otherwise carry on
      if not tk.yesno(_("The Search for Cynthia"), _([[After chasing Cynthia through most of the station, you find her curled up at the end of a hall, crying. As you approach, she screams, "Why can't you leave me alone? I don't want to go back to my terrible parents!" Will you take her anyway?]])) then
         misn.osdCreate( _("The Search for Cynthia"), {
            _("Catch Cynthia on Torloth in Cygnus"),
            _("Go to Zhiru in Goddard to lie to Cynthia's father"),
         } )
         tk.msg(_("The Search for Cynthia"), _([["Please, please, please don't ever come looking for me again, I beg of you!"]]))
      else
         tk.msg(_("The Search for Cynthia"), _([[Cynthia stops crying and proceeds to hide in the farthest corner of your ship. Attempts to talk to her end up fruitless.]]))
         local c = commodity.new( cargoname, cargodesc )
         mem.cargoID = misn.cargoAdd( c, 0 )
      end

      misn.osdActive( 2 )
      misn.markerMove( mem.search_markers[1], homeworld )

   --If we land on Zhiru to finish the mission, clean up, reward, and leave.
   elseif mem.stage == 3 and spob.cur() == homeworld then
      --Talk to the father and get the reward
      if misn.osdGetActive() == _("Return Cynthia to her father on Zhiru in the Goddard system") then
         tk.msg(_("The Search for Cynthia"), _("As Cynthia sees her father, she begins her crying anew. You overhear the father talking about how her abusive mother died. Cynthia becomes visibly happier, so you pick up your payment and depart."))
         player.pay(reward)
         misn.cargoRm(mem.cargoID)
         neu.addMiscLog( _([[The father of Cynthia, who you had given a lift before, asked you to find her and bring her back to him, thinking that she was kidnapped. Cynthia protested, telling you that she did not want to go back to her parents, but you took her anyway. When she saw her father, she started crying, but seemed to become visibly happier when her father told her that her abusive mother had died.]]) )
      else
         tk.msg(_("The Search for Cynthia"), _([[You tell the father that you checked every place on the list, and then some, but his daughter was nowhere to be found. You buy the old man a drink, then go back to the spaceport. Before you leave, he hands you a few credits. "For your troubles."]]))
         player.pay(releasereward)
         neu.addMiscLog( _([[The father of Cynthia, who you had given a lift before, asked you to find her and bring her back to him, thinking that she was kidnapped. Cynthia protested, telling you that she did not want to go back to her parents. Respecting her wishes, you let her be and lied to her father, saying that you couldn't find her no matter how hard you tried.]]) )
      end

      misn.finish(true)
   end
end
