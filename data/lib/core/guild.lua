function Guild.addBankBalance(self, amount, saveToDb)
	saveToDb = saveToDb or false
	return self:setBankBalance(self:getBankBalance() + amount, saveToDb)
end

function Guild.removeBankBalance(self, amount, saveToDb)
	saveToDb = saveToDb or false
	local balance = self:getBankBalance()
	if balance < amount then
		return false
	end
	return self:setBankBalance(balance - amount, saveToDb)
end
