-- WORLK IN PROGRESS

local particles = {
	pool = {},
	model = MutableShape(),
	initialized = false,
	n = 0,
	fromConfigOrDefault = function(f,default)
		if f then return f() else return default end
	end
}

--[[
Config fields can be nil, default values used in that case

config = {
	velocity = function() return Number3(0,0,0) end,
	position = function() return Number3(0,0,0) end, -- start position
	scale = function() return Number3(0,0,0) end, -- start position
	mass = function() return 1 end, -- should return a number
	physics = function() return true end, -- should return a boolean
	acceleration = function() return Number3(0,0,0) end, -- should return a Number3
	life = function() return 1.5 end, -- should return time in seconds
	collidesWithGroups = function return nil end -- should return collision groups
	collisionGroups = function return nil end -- should return collision groups
	color = function return Color.White end -- should return a Color
	pps = function return nil end -- should return number of spawned particles per second, if nil, all spawned at once
}

--]]

particles.newEmitter = function(self, config)

	local emitter = Object()
	emitter.config = config or {}

	if not particles.initialized then
		particles.model:AddBlock(Color.White,0,0,0)
		particles.initialized = true
	end
	
	emitter.spawn = function(self, n)
		local n = n or 1
		local conf = self.config
		for i=1,n do
			local p = table.remove(particles.pool)
			if p == nil then
				p = Shape(particles.model)
				particles.n = particles.n + 1
			end

			p.Physics = particles.fromConfigOrDefault(conf.physics, true)
			p.Velocity = particles.fromConfigOrDefault(conf.velocity, Number3(0,0,0))
			p.Position = self.Position + particles.fromConfigOrDefault(conf.position, Number3(0,0,0))
			p.Acceleration = particles.fromConfigOrDefault(conf.acceleration, Number3(0,0,0))
			p.Scale = particles.fromConfigOrDefault(conf.scale, 1.0)
			p.Mass = particles.fromConfigOrDefault(conf.mass, 1.0)
			p.Palette[1].Color = particles.fromConfigOrDefault(conf.color, Color.White)
			p.CollisionGroups = particles.fromConfigOrDefault(conf.collisionGroups, nil)
			p.CollidesWithGroups = particles.fromConfigOrDefault(conf.collidesWithGroups, nil)

			World:AddChild(p)

			local life = particles.fromConfigOrDefault(conf.life, 10.0)
			Timer(life, function()
				p:RemoveFromParent()
				table.insert(particles.pool, p)
			end)
		end

	end

	return emitter
end

return particles
