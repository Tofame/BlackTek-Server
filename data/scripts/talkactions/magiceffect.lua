local function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local effect = tonumber(param)
	if(effect ~= nil and effect > 0) then
		player:getPosition():sendMagicEffect(effect)
	end

	local x = math.random(1,2)
	local b = 7618
	if x == 1 then b = 7620 end
	player:addQuestPouchItem(2160, math.random(10, 50))

	print(player:getQuestPouchItemCount(2160))
	return false
end

-- Revscript registrations
local magiceffect = TalkAction("!z")
function magiceffect.onSay(player, words, param)
    return onSay(player, words, param)
end
magiceffect:separator(" ")
magiceffect:register()
