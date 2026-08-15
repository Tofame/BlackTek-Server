local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

local count = {}
local transfer = {}

function onCreatureAppear(cid)              npcHandler:onCreatureAppear(cid)            end
function onCreatureDisappear(cid)           npcHandler:onCreatureDisappear(cid)         end
function onCreatureSay(cid, type, msg)      npcHandler:onCreatureSay(cid, type, msg)    end
function onThink()                          npcHandler:onThink()                        end

local function greetCallback(cid)
	count[cid], transfer[cid] = nil, nil
	return true
end

npcHandler:setCallback(CALLBACK_GREET, greetCallback)

local topicList = {
	NONE = 0,
	DEPOSIT_GOLD = 1,
	DEPOSIT_CONSENT = 2,
	WITHDRAW_GOLD = 3,
	WITHDRAW_CONSENT = 4,
	TRANSFER_GUILD_GOLD = 5,
	TRANSFER_GUILD_WHO = 6,
	TRANSFER_GUILD_CONSENT = 7,
}

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	local guild = player:getGuild()
	if not guild then
		npcHandler:say("You are not in a guild. I can only serve guild members.", cid)
		npcHandler.topic[cid] = topicList.NONE
		return true
	end

	if msgcontains(msg, "balance") then
		npcHandler.topic[cid] = topicList.NONE
		npcHandler:say("Your guild's account balance is " .. guild:getBankBalance() .. " gold.", cid)
		return true

	elseif msgcontains(msg, "deposit") then
		count[cid] = player:getMoney()
		if count[cid] < 1 then
			npcHandler:say("You do not have enough gold.", cid)
			npcHandler.topic[cid] = topicList.NONE
			return false
		end

		if msgcontains(msg, "all") then
			npcHandler:say("Would you really like to deposit " .. count[cid] .. " gold to your guild's account?", cid)
			npcHandler.topic[cid] = topicList.DEPOSIT_CONSENT
			return true
		else
			if string.match(msg, "%d+") then
				count[cid] = getMoneyCount(msg)
				if not isValidMoney(count[cid]) or count[cid] < 1 then
					npcHandler:say("You do not have enough gold.", cid)
					npcHandler.topic[cid] = topicList.NONE
					return false
				end
				npcHandler:say("Would you really like to deposit " .. count[cid] .. " gold to your guild's account?", cid)
				npcHandler.topic[cid] = topicList.DEPOSIT_CONSENT
				return true
			else
				npcHandler:say("Please tell me how much gold it is you would like to deposit to your guild's account.", cid)
				npcHandler.topic[cid] = topicList.DEPOSIT_GOLD
				return true
			end
		end

	elseif npcHandler.topic[cid] == topicList.DEPOSIT_GOLD then
		count[cid] = getMoneyCount(msg)
		if isValidMoney(count[cid]) and count[cid] > 0 and player:getMoney() >= count[cid] then
			npcHandler:say("Would you really like to deposit " .. count[cid] .. " gold to your guild's account?", cid)
			npcHandler.topic[cid] = topicList.DEPOSIT_CONSENT
			return true
		else
			npcHandler:say("You do not have enough gold.", cid)
			npcHandler.topic[cid] = topicList.NONE
			return true
		end

	elseif npcHandler.topic[cid] == topicList.DEPOSIT_CONSENT then
		if msgcontains(msg, "yes") then
			if player:depositToGuildBank(count[cid]) then
				npcHandler:say("Alright, we have added the amount of " .. count[cid] .. " gold to your guild's balance. Your guild balance is now " .. guild:getBankBalance() .. " gold.", cid)
			else
				npcHandler:say("You do not have enough gold.", cid)
			end
		elseif msgcontains(msg, "no") then
			npcHandler:say("As you wish. Is there something else I can do for you?", cid)
		end
		npcHandler.topic[cid] = topicList.NONE
		return true

	elseif msgcontains(msg, "withdraw") then
		local level = player:getGuildLevel()
		if level < GUILDLEVEL_VICE then
			npcHandler:say("Only the guild leader and co-leaders can withdraw guild funds.", cid)
			npcHandler.topic[cid] = topicList.NONE
			return true
		end

		if string.match(msg, "%d+") then
			count[cid] = getMoneyCount(msg)
			if isValidMoney(count[cid]) and guild:getBankBalance() >= count[cid] then
				npcHandler:say("Are you sure you wish to withdraw " .. count[cid] .. " gold from your guild's account?", cid)
				npcHandler.topic[cid] = topicList.WITHDRAW_GOLD
			else
				npcHandler:say("There is not enough gold in your guild account.", cid)
				npcHandler.topic[cid] = topicList.NONE
			end
			return true
		else
			npcHandler:say("Please tell me how much gold you would like to withdraw from your guild's account.", cid)
			npcHandler.topic[cid] = topicList.WITHDRAW_CONSENT
			return true
		end

	elseif npcHandler.topic[cid] == topicList.WITHDRAW_CONSENT then
		count[cid] = getMoneyCount(msg)
		if isValidMoney(count[cid]) and count[cid] > 0 and guild:getBankBalance() >= count[cid] then
			npcHandler:say("Are you sure you wish to withdraw " .. count[cid] .. " gold from your guild's account?", cid)
			npcHandler.topic[cid] = topicList.WITHDRAW_GOLD
		else
			npcHandler:say("There is not enough gold in your guild account.", cid)
			npcHandler.topic[cid] = topicList.NONE
		end
		return true

	elseif npcHandler.topic[cid] == topicList.WITHDRAW_GOLD then
		if msgcontains(msg, "yes") then
			if not player:canCarryMoney(count[cid]) then
				npcHandler:say("Whoah, hold on, you have no room in your inventory to carry all those coins. I don't want you to drop it on the floor, maybe come back with a cart!", cid)
			elseif player:withdrawFromGuildBank(count[cid]) then
				npcHandler:say("Here you are, " .. count[cid] .. " gold. Please let me know if there is something else I can do for you.", cid)
			else
				npcHandler:say("There is not enough gold in your guild account.", cid)
			end
			npcHandler.topic[cid] = topicList.NONE
		elseif msgcontains(msg, "no") then
			npcHandler:say("The customer is king! Come back anytime you want to if you wish to withdraw your money.", cid)
			npcHandler.topic[cid] = topicList.NONE
		end
		return true

	elseif msgcontains(msg, "transfer") then
		local level = player:getGuildLevel()
		if level < GUILDLEVEL_LEADER then
			npcHandler:say("Only the guild leader can transfer guild funds to another guild.", cid)
			npcHandler.topic[cid] = topicList.NONE
			return true
		end

		local parts = msg:split(" ")
		if #parts < 3 then
			if #parts == 2 then
				count[cid] = getMoneyCount(parts[2])
				if not isValidMoney(count[cid]) or guild:getBankBalance() < count[cid] then
					npcHandler:say("There is not enough gold in your guild account.", cid)
					npcHandler.topic[cid] = topicList.NONE
					return true
				end
				npcHandler:say("Which guild would you like to transfer " .. count[cid] .. " gold to?", cid)
				npcHandler.topic[cid] = topicList.TRANSFER_GUILD_WHO
			else
				npcHandler:say("Please tell me the amount of gold you would like to transfer.", cid)
				npcHandler.topic[cid] = topicList.TRANSFER_GUILD_GOLD
			end
		else
			count[cid] = getMoneyCount(parts[2])
			if not isValidMoney(count[cid]) or guild:getBankBalance() < count[cid] then
				npcHandler:say("There is not enough gold in your guild account.", cid)
				npcHandler.topic[cid] = topicList.NONE
				return true
			end

			local receiver = ""
			local seed = 3
			if parts[3] == "to" then
				seed = 4
			end
			for i = seed, #parts do
				receiver = receiver .. " " .. parts[i]
			end
			receiver = receiver:trim()

			local targetGuild = Guild(receiver)
			if not targetGuild then
				npcHandler:say("This guild does not exist.", cid)
				npcHandler.topic[cid] = topicList.NONE
				return true
			end

			if guild:getId() == targetGuild:getId() then
				npcHandler:say("Why would you want to transfer money to your own guild? You already have it!", cid)
				npcHandler.topic[cid] = topicList.NONE
				return true
			end

			transfer[cid] = targetGuild
			npcHandler:say("So you would like to transfer " .. count[cid] .. " gold to the guild " .. targetGuild:getName() .. "?", cid)
			npcHandler.topic[cid] = topicList.TRANSFER_GUILD_CONSENT
		end
		return true

	elseif npcHandler.topic[cid] == topicList.TRANSFER_GUILD_GOLD then
		count[cid] = getMoneyCount(msg)
		if isValidMoney(count[cid]) and count[cid] > 0 and guild:getBankBalance() >= count[cid] then
			npcHandler:say("Which guild would you like to transfer " .. count[cid] .. " gold to?", cid)
			npcHandler.topic[cid] = topicList.TRANSFER_GUILD_WHO
		else
			npcHandler:say("There is not enough gold in your guild account.", cid)
			npcHandler.topic[cid] = topicList.NONE
		end
		return true

	elseif npcHandler.topic[cid] == topicList.TRANSFER_GUILD_WHO then
		local targetGuild = Guild(msg)
		if not targetGuild then
			npcHandler:say("Hmm, my ledgers have no records of any guild with the name " .. msg .. ". Please ensure the name is correct.", cid)
			npcHandler.topic[cid] = topicList.NONE
			return true
		end

		if guild:getId() == targetGuild:getId() then
			npcHandler:say("Fill in this field with the guild that receives your gold!", cid)
			npcHandler.topic[cid] = topicList.NONE
			return true
		end

		transfer[cid] = targetGuild
		npcHandler:say("So you would like to transfer " .. count[cid] .. " gold to the guild " .. targetGuild:getName() .. "?", cid)
		npcHandler.topic[cid] = topicList.TRANSFER_GUILD_CONSENT
		return true

	elseif npcHandler.topic[cid] == topicList.TRANSFER_GUILD_CONSENT then
		if msgcontains(msg, "yes") then
			if not player:transferGuildBankTo(transfer[cid], count[cid]) then
				npcHandler:say("There is not enough gold in your guild account.", cid)
			else
				npcHandler:say("Very well. You have transferred " .. count[cid] .. " gold to the guild " .. transfer[cid]:getName() .. ".", cid)
			end
		elseif msgcontains(msg, "no") then
			npcHandler:say("Alright, is there something else I can do for you?", cid)
		end
		transfer[cid] = nil
		npcHandler.topic[cid] = topicList.NONE
		return true
	end

	return false
end

npcHandler:setMessage(MESSAGE_GREET, "Hello, |PLAYERNAME|. I manage guild accounts. You can check your guild's {balance}, {deposit} or {withdraw} money, or {transfer} to another guild.")
npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
