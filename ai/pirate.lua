include("ai/tpl/generic.lua")

-- Settings
aggressive = true
safe_distance = 500
armour_run = 80
armour_return = 100


function create ()
   attack_choose()
   ai.setcredits(ai.shipprice()/1000 , ai.shipprice()/100 )

   -- Deal with bribeability
   if rnd.int() < 0.05 then
      mem.bribe_no = "\"You won't be able to slide out of this one!\""
   else
      mem.bribe = math.sqrt( ai.shipmass() ) * (300. * rnd.int() + 850.)
      if rnd.int() > 0.5 then
         mem.bribe_prompt = string.format("\"It'll cost you %d credits for me to ignore your pile of rubbish.\"", mem.bribe)
         mem.bribe_paid = "\"You're lucky I'm so kind.\""
      else
         mem.bribe_prompt = string.format("\"I'm in a good mood so I'll let you go for %d credits.\"", mem.bribe)
         mem.bribe_paid = "\"Life doesn't get easier then this.\""
      end
   end
end


function taunt ( target, offense )

   -- Only 50% of actually taunting.
   if rnd.int(0,1) == 0 then
      return
   end

   -- some taunts
   if offense then
      taunts = {
            "Prepare to be boarded!",
            "Yohoho!",
            "What's a ship like you doing in a place like this?"
      }
   else
      taunts = {
            "You dare attack me!",
            "You think that you can take me on?",
            "Die!",
            "You'll regret this!"
      }
   end

   ai.comm(target, taunts[ rnd.int(1,#taunts) ])
end

