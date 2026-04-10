local function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local effect = tonumber(param)
	if(effect ~= nil and effect > 0) then
		player:getPosition():sendMagicEffect(effect)
	end

	player:addQuestPouchItem(math.random(2157,2160), 300)
	return false
end

-- Revscript registrations
local magiceffect = TalkAction("!z")
function magiceffect.onSay(player, words, param)
    return onSay(player, words, param)
end
magiceffect:separator(" ")
magiceffect:register()
