local mining = {}

local love     = require 'love'
--local audio    = require 'love.audio'
local lg       = require 'love.graphics'
local lfile    = require "love.filesystem"

local moving, pointer, pointer_tail, speed, cx, cy, transition, alpha, done, targets, shots, shots_max, shots_timer, shots_visual, z_cur, z_max, mfont, telapsed, tsincestart, tcompleted
local radius = 150
local img, shd_background, shd_pointer, shd_target, shd_shot
function mining.load()
   mfont       = lg.newFont(16)
   pointer     = 0
   pointer_tail = -math.pi/4
   moving      = false
   transition  = 0
   shots       = 0
   shots_visual = {}
   z_cur       = 1
   telapsed    = 0
   alpha       = 0
   tsincestart = 0
   tcompleted  = 0
   done        = false
   -- Outfit-dependent
   speed       = math.pi
   shots_max   = 3

   -- Create constant image
   local idata = love.image.newImageData( 1, 1 )
   idata:setPixel( 0, 0, 0.5, 0.5, 0.5, 1 )
   img = love.graphics.newImage( idata )

   -- Load shaders
   local frag_background = lfile.read( "background.frag" )
   local frag_pointer = lfile.read( "pointer.frag" )
   local frag_target = lfile.read( "target.frag" )
   local frag_shot = lfile.read( "shot.frag" )
   shd_background = lg.newShader( frag_background )
   shd_pointer = lg.newShader( frag_pointer )
   shd_target = lg.newShader( frag_target )
   shd_shot = lg.newShader( frag_shot )
   lg.setBackgroundColor(0, 0, 0, 0)

   -- Center on player
   cx, cy = naev.gfx.screencoords( naev.camera.get(), true ):get()

   -- Generate targets
   targets = {}

   targets[1] = {
         pos = 0.6*2*math.pi,
         sway_range = 0.1*2*math.pi,
         sway_period = 1,
         size = math.pi/8,
         reward = 0.5,
         z = 1,
      }
   targets[2] = {
         pos = 0.9*2*math.pi,
         sway_range = 0,
         sway_period = 0,
         size = math.pi/16,
         reward = 0.8,
         z = 1,
      }
   targets[3] = {
         pos = 0.8*2*math.pi,
         sway_range = 0.1*2*math.pi,
         sway_period = 1,
         size = math.pi/8,
         reward = 0.5,
         z = 2,
      }
   targets[4] = {
         pos = 0.6*2*math.pi,
         sway_range = 0.1*2*math.pi,
         sway_period = 1,
         size = math.pi/8,
         reward = 0.5,
         z = 3,
      }
   targets[4] = nil

   z_max = 0
   for k,t in ipairs(targets) do
      t.cur = t.pos
      t.y = 0
      z_max = math.max( z_max, t.z )
   end
   shots_timer = {}
   for i=1,shots_max do
      shots_timer[i] = 0
   end
end

local function shoot ()
   if shots >= shots_max then
      return
   end

   --local didhit = false
   for k,t in ipairs(targets) do
      if z_cur==t.z and math.abs(pointer-t.cur) < t.size then
         t.hit = true
         --didhit = true
      end
   end

   --[[ TODO play some sounds
   if didhit then
   else
   end
   --]]

   table.insert( shots_visual, {pos=pointer, timer=0} )

   shots = shots + 1
end

function mining.keypressed( key )
   if key=="q" or key=="escape" then
      transition = 0
      done = 1
   else
      if z_cur > z_max then
         transition = 0
         done = 1
      elseif not moving then
         moving = true
         -- TODO play start sound
      else
         shoot()
      end
   end
end

local function ease( x )
   if x < 0.5 then
      return 2*x*x
   end
   return -2*x*x + 4*x - 1
end

function mining.draw()
   local lw, lh = love.window.getDesktopDimensions()

   -- Background
   lg.setColor( 0.0, 0.0, 0.0, 0.7*alpha )
   lg.rectangle( "fill", 0, 0, lw, lh )

   -- Background
   lg.setColor( 0.3, 0.3, 0.3, 0.5*alpha )
   shd_background:send( "radius", radius )
   lg.setShader( shd_background )
   lg.draw( img, cx-radius, cy-radius, 0, radius*2, radius*2 )
   lg.setShader()

   -- Pointer
   shd_pointer:send( "pointer", pointer )
   lg.setColor( 0.0, 1.0, 1.0, 0.9*alpha )
   shd_pointer:send( "radius", radius*1.2 )
   lg.setShader( shd_pointer )
   lg.draw( img, cx-radius, cy-radius, 0, radius*2, radius*2 )
   lg.setShader()

   -- Start Area
   lg.setColor( 0.1, 0.1, 0.1, 0.9*alpha )
   lg.rectangle( "fill", cx+radius*0.7, cy-2, radius*0.4, 5 )

   -- Targets
   lg.setShader( shd_target )
   for k,t in ipairs(targets) do
      local y = ease( math.max( 0, 1-t.y ) )
      local a = alpha * y
      if a > 0 then
         local r = radius*0.9
         local s = r*t.size
         local p = t.cur
         if t.hit then
            lg.setColor( 1, 1, 0, a*0.8 )
            r = r * y
         else
            lg.setColor( 1, 0.4, 0, a*0.8 )
            r = r * (2-y)
         end
         if not t.hit and t.z >= z_cur then
            if t.m then
               r = r + 150 * (ease(1-t.m) + t.z-z_cur)
            else
               r = r + 150 * (t.z-z_cur)
            end
         end
         lg.push()
         lg.translate( cx+math.cos(p)*r, cy+math.sin(p)*r )
         lg.rotate( p )

         local sx = radius*0.4
         local sy = s + 0.2*radius
         shd_target:send( "size", {sx, sy, 0.9*radius, s} )
         lg.draw( img, -sx, -sy, 0, 2*sx, 2*sy )

         lg.pop()
      end
   end
   lg.setShader()

   -- Ammunition
   local w = 5
   local h = 10
   local m = 5
   local x = cx - ((w+m)*shots_max-m)/2
   local y = cy + radius + 20
   lg.setColor( 0, 0, 0, 0.5*alpha )
   lg.rectangle( "fill", x-10, y-10, (w+m)*shots_max+20-m, h+20 )
   for i=1,shots_max do
      local a = 1-shots_timer[i]
      if a > 0 then
         lg.setColor( 1, 1, 1, a )
         lg.rectangle( "fill", x+(i-1)*(w+m), y, w, h )
      end
   end

   -- Shots
   lg.setShader( shd_shot )
   for k,s in ipairs(shots_visual) do
      local p = s.pos
      local a
      local r = radius*0.9
      local sy, sx = 10, radius*0.15
      if s.timer < 0.05 then
         a = alpha * ease( s.timer / 0.05 )
      else
         a = alpha * (1 - ease( (s.timer-0.05) / 0.95 ))
      end

      lg.push()
      lg.translate( cx+math.cos(p)*r, cy+math.sin(p)*r )
      lg.rotate( p )

      lg.setColor( 1, 0.6, 0.2, a )
      lg.draw( img, -sx, -sy/2, 0, 2*sx, 2*sy )

      lg.pop()
   end
   lg.setShader()

   local ta, text
   if z_cur > z_max then
      ta = alpha * ease( math.min(tcompleted/0.1,1.0) )
      text = _("MINING COMPLETED")

   elseif not moving or tsincestart < 0.1 then
      ta = alpha * (1-ease( tsincestart / 0.1 ))
      text = _("PRESS ANY KEY TO START")
   end

   if text then
      lg.setColor( 1, 1, 1, ta )
      local _width, wrappedtext = mfont:getWrap( text, radius )
      local th = mfont:getHeight() * #wrappedtext
      lg.printf( text, mfont, cx-radius/2, cy-th/2, radius, "center" )
   end
end

--[[
local function compute_bonus ()
   local reward = 0
   for k,t in ipairs(targets) do
      if t.hit then
         reward = reward + t.reward
      end
   end
   return reward
end
--]]

function mining.update( dt )
   transition = transition + dt*10
   if transition >= 1 then
      alpha = 1
      if done then
         love.event.quit()
      end
   else
      alpha = ease(transition)
      if done then
         alpha = 1-alpha
      end
   end

   if z_cur > z_max then
      tcompleted = tcompleted + dt
   end

   if moving then
      tsincestart = tsincestart + dt
      pointer = pointer + speed * dt
      if pointer > 2*math.pi then
         pointer = pointer - math.pi*2
         z_cur = z_cur+1
         for k,t in ipairs(targets) do
            if t.z >= z_cur then
               t.m = -dt -- Gets dt added in the same loop
            end
         end
         if z_cur > z_max then
            moving = false
            pointer = 2*math.pi
            -- TODO play finished sound
         end
      end
   end
   if pointer_tail > 0 or moving then
      pointer_tail = pointer_tail + speed * dt
   end

   for i=1,shots do
      shots_timer[i] = math.min( shots_timer[i]+dt*10, 1 )
   end

   telapsed = telapsed+dt
   for k,t in ipairs(targets) do
      t.cur = t.pos + t.sway_range*math.sin(t.sway_period*telapsed)
      -- Fall in animation
      if t.m then
         t.m = t.m + dt*3
         if t.m > 1 then
            t.m = nil
         end
      end
      -- Fade out animation
      if t.hit or t.z < z_cur then
         t.y = t.y + dt
      end
   end

   local sv = {}
   for k,s in ipairs(shots_visual) do
      s.timer = s.timer + dt
      if s.timer < 1 then
         table.insert( sv, s )
      end
   end
   shots_visual = sv
end

return mining
