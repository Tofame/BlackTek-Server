// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "creature.h"
#include "game.h"
#include "monster.h"
#include "configmanager.h"
#include "scheduler.h"
#include "events.h"

double Creature::speedA = 857.36;
double Creature::speedB = 261.29;
double Creature::speedC = -4795.01;

extern Game g_game;
extern ConfigManager g_config;
extern CreatureEvents* g_creatureEvents;
extern Events* g_events;

Creature::Creature()
{
	onIdleStatus();
}

Creature::~Creature()
{
	for (auto& summon : summons) {
		summon->setAttackedCreature(nullptr);
		summon->removeMaster();
	}

	for (auto& condition : conditions) {
		delete condition;
	}
}

bool Creature::canSee(const Position& myPos, const Position& pos, int32_t viewRangeX, int32_t viewRangeY)
{
	if (myPos.z <= 7) {
		// we are on ground level or above (7 -> 0)
		// view is from 7 -> 0
		if (pos.z > 7) {
			return false;
		}
	} else if (myPos.z >= 8) {
		// we are underground (8 -> 15)
		// we can't see floors above 8
		if (pos.z < 8) {
			return false;
		}

		// view is +/- 2 from the floor we stand on
		if (Position::getDistanceZ(myPos, pos) > 2) {
			return false;
		}
	}

	const int_fast32_t offsetz = myPos.getZ() - pos.getZ();
	return (pos.getX() >= myPos.getX() - viewRangeX + offsetz) && (pos.getX() <= myPos.getX() + viewRangeX + offsetz)
		&& (pos.getY() >= myPos.getY() - viewRangeY + offsetz) && (pos.getY() <= myPos.getY() + viewRangeY + offsetz);
}

bool Creature::canSee(const Position& pos) const
{
	return canSee(getPosition(), pos, Map::maxViewportX, Map::maxViewportY);
}

bool Creature::canSeeCreature(const CreatureConstPtr& creature) const
{
	if (!canSeeGhostMode(creature) && creature->isInGhostMode()) {
		return false;
	}

	if (!canSeeInvisibility() && creature->isInvisible()) {
		return false;
	}
	return true;
}

void Creature::setSkull(Skulls_t newSkull)
{
	skull = newSkull;
	g_game.updateCreatureSkull(std::dynamic_pointer_cast<Creature>(shared_from_this()));
}

int64_t Creature::getTimeSinceLastMove() const
{
	if (lastStep) {
		return OTSYS_TIME() - lastStep;
	}
	return std::numeric_limits<int64_t>::max();
}

int32_t Creature::getWalkDelay(Direction dir) const
{
	if (lastStep == 0) {
		return 0;
	}

	int64_t ct = OTSYS_TIME();
	int64_t stepDuration = getStepDuration(dir);
	return stepDuration - (ct - lastStep);
}

int32_t Creature::getWalkDelay() const
{
	//Used for auto-walking
	if (lastStep == 0) {
		return 0;
	}

	int64_t ct = OTSYS_TIME();
	int64_t stepDuration = getStepDuration() * lastStepCost;
	return stepDuration - (ct - lastStep);
}

void Creature::onThink(uint32_t interval)
{
	if (!isMapLoaded && useCacheMap()) {
		isMapLoaded = true;
		updateMapCache();
	}

	if (auto followTarget = getFollowCreature()) {
		walkUpdateTicks += interval;
		if (forceUpdateFollowPath || walkUpdateTicks >= 2000) {
			walkUpdateTicks = 0;
			forceUpdateFollowPath = false;
			isUpdatingPath = true;
		}
		if (followTarget && getMaster() != followTarget && !canSeeCreature(followTarget)) {
			onCreatureDisappear(followTarget, false);
		}
	}

	if (auto attackTarget = getAttackedCreature()) {
		if (attackTarget && getMaster() != attackTarget && !canSeeCreature(attackTarget)) {
			onCreatureDisappear(attackTarget, false);
		}
	}

	blockTicks += interval;
	if (blockTicks >= 1000) {
		blockCount = std::min<uint32_t>(blockCount + 1, 2);
		blockTicks = 0;
	}

	if (isUpdatingPath) {
		isUpdatingPath = false;
		goToFollowCreature();
	}

	//scripting event - onThink
	const CreatureEventList& thinkEvents = getCreatureEvents(CREATURE_EVENT_THINK);
	for (CreatureEvent* thinkEvent : thinkEvents) {
		thinkEvent->executeOnThink(getCreature(), interval);
	}
}

void Creature::onAttacking(uint32_t interval)
{
	if (!getAttackedCreature()) {
		return;
	}

	onAttacked();
	getAttackedCreature()->onAttacked();

	if (g_game.isSightClear(getPosition(), getAttackedCreature()->getPosition(), true)) {
		doAttacking(interval);
	}
}

void Creature::onIdleStatus()
{
	if (getHealth() > 0) {
		damageMap.clear();
		lastHitCreatureId = 0;
	}
}

void Creature::onWalk()
{
	if (getWalkDelay() <= 0) {
		Direction dir;
		uint32_t flags = FLAG_IGNOREFIELDDAMAGE;
		if (getNextStep(dir, flags)) {
			ReturnValue ret = g_game.internalMoveCreature(getCreature(), dir, flags);
			if (ret != RETURNVALUE_NOERROR) {
				if (auto player = getPlayer()) {
					player->sendCancelMessage(ret);
					player->sendCancelWalk();
				}

				forceUpdateFollowPath = true;
			}
		} else {
			stopEventWalk();

			if (listWalkDir.empty()) {
				onWalkComplete();
			}
		}
	}

	if (cancelNextWalk) {
		listWalkDir.clear();
		onWalkAborted();
		cancelNextWalk = false;
	}

	if (eventWalk != 0) {
		eventWalk = 0;
		addEventWalk();
	}
}

void Creature::onWalk(Direction& dir)
{
	if (!hasCondition(CONDITION_DRUNK)) {
		return;
	}

	uint16_t rand = uniform_random(0, 399);
	if (rand / 4 > getDrunkenness()) {
		return;
	}

	dir = static_cast<Direction>(rand % 4);
	g_game.internalCreatureSay(getCreature(), TALKTYPE_MONSTER_SAY, "Hicks!", false);
}

bool Creature::getNextStep(Direction& dir, uint32_t&)
{
	if (listWalkDir.empty()) {
		return false;
	}

	dir = listWalkDir.back();
	listWalkDir.pop_back();
	onWalk(dir);
	return true;
}

void Creature::startAutoWalk()
{
	auto player = getPlayer();
	if (player && player->isMovementBlocked()) {
		player->sendCancelWalk();
		return;
	}

	addEventWalk(listWalkDir.size() == 1);
}

void Creature::startAutoWalk(Direction direction)
{
	auto player = getPlayer();
	if (player && player->isMovementBlocked()) {
		player->sendCancelWalk();
		return;
	}

	listWalkDir.clear();
	listWalkDir.push_back(direction);
	addEventWalk(true);
}

void Creature::startAutoWalk(const std::vector<Direction>& listDir)
{
	auto player = getPlayer();
	if (player && player->isMovementBlocked()) {
		player->sendCancelWalk();
		return;
	}

	listWalkDir = listDir;
	addEventWalk(listWalkDir.size() == 1);
}

void Creature::addEventWalk(bool firstStep)
{
	cancelNextWalk = false;

	if (getStepSpeed() <= 0) {
		return;
	}

	if (eventWalk != 0) {
		return;
	}

	int64_t ticks = getEventStepTicks(firstStep);
	if (ticks <= 0) {
		return;
	}

	// Take first step right away, but still queue the next
	if (ticks == 1) {
		g_game.checkCreatureWalk(getID());
	}

	eventWalk = g_scheduler.addEvent(createSchedulerTask(ticks, [id = getID()]() { g_game.checkCreatureWalk(id); }));
}

void Creature::stopEventWalk()
{
	if (eventWalk != 0) {
		g_scheduler.stopEvent(eventWalk);
		eventWalk = 0;
	}
}

void Creature::updateMapCache()
{
	const Position& myPos = getPosition();
	Position pos(0, 0, myPos.z);

	for (int32_t y = -maxWalkCacheHeight; y <= maxWalkCacheHeight; ++y) {
		for (int32_t x = -maxWalkCacheWidth; x <= maxWalkCacheWidth; ++x) {
			pos.x = myPos.getX() + x;
			pos.y = myPos.getY() + y;
			TilePtr tile = g_game.map.getTile(pos);
			updateTileCache(tile, pos);
		}
	}
}

void Creature::updateTileCache(TilePtr tile, int32_t dx, int32_t dy)
{
	if (std::abs(dx) <= maxWalkCacheWidth && std::abs(dy) <= maxWalkCacheHeight) {
		localMapCache[maxWalkCacheHeight + dy][maxWalkCacheWidth + dx] = tile && tile->queryAdd(getCreature(), FLAG_PATHFINDING | FLAG_IGNOREFIELDDAMAGE) == RETURNVALUE_NOERROR;
	}
}

void Creature::updateTileCache(TilePtr tile, const Position& pos)
{
	const Position& myPos = getPosition();
	if (pos.z == myPos.z) {
		int32_t dx = Position::getOffsetX(pos, myPos);
		int32_t dy = Position::getOffsetY(pos, myPos);
		updateTileCache(tile, dx, dy);
	}
}

int32_t Creature::getWalkCache(const Position& pos) const
{
	if (!useCacheMap()) {
		return 2;
	}

	const Position& myPos = getPosition();
	if (myPos.z != pos.z) {
		return 0;
	}

	if (pos == myPos) {
		return 1;
	}

	int32_t dx = Position::getOffsetX(pos, myPos);
	if (std::abs(dx) <= maxWalkCacheWidth) {
		int32_t dy = Position::getOffsetY(pos, myPos);
		if (std::abs(dy) <= maxWalkCacheHeight) {
			if (localMapCache[maxWalkCacheHeight + dy][maxWalkCacheWidth + dx]) {
				return 1;
			} else {
				return 0;
			}
		}
	}

	//out of range
	return 2;
}

void Creature::onAddTileItem(TilePtr tile, const Position& pos)
{
	if (isMapLoaded && pos.z == getPosition().z) {
		updateTileCache(tile, pos);
	}
}

void Creature::onUpdateTileItem(const TilePtr& tile,
								const Position& pos,
								const ItemPtr&,
                                const ItemType& oldType,
                                const ItemPtr&,
                                const ItemType& newType)
{
	if (!isMapLoaded) {
		return;
	}

	if (oldType.blockSolid || oldType.blockPathFind || newType.blockPathFind || newType.blockSolid) {
		if (pos.z == getPosition().z) {
			updateTileCache(tile, pos);
		}
	}
}

void Creature::onRemoveTileItem(const TilePtr& tile, const Position& pos, const ItemType& iType, const ItemPtr&)
{
	if (!isMapLoaded) {
		return;
	}

	if (iType.blockSolid || iType.blockPathFind || iType.isGroundTile()) {
		if (pos.z == getPosition().z) {
			updateTileCache(tile, pos);
		}
	}
}

void Creature::onCreatureAppear(const CreaturePtr& creature, bool isLogin)
{
	if (creature == shared_from_this()) {
		if (useCacheMap()) {
			isMapLoaded = true;
			updateMapCache();
		}

		if (isLogin) {
			setLastPosition(getPosition());
		}
	} else if (isMapLoaded) {
		if (creature->getPosition().z == getPosition().z) {
			updateTileCache(creature->getTile(), creature->getPosition());
		}
	}
}

void Creature::onRemoveCreature(const CreaturePtr& creature, bool)
{
	onCreatureDisappear(creature, true);
	if (creature != shared_from_this() && isMapLoaded) {
		if (creature->getPosition().z == getPosition().z) {
			updateTileCache(creature->getTile(), creature->getPosition());
		}
	}
}

void Creature::onCreatureDisappear(const CreatureConstPtr& creature, bool isLogout)
{
	if (getAttackedCreature() == creature) {
		setAttackedCreature(nullptr);
		onAttackedCreatureDisappear(isLogout);
	}

	if (getFollowCreature() == creature) {
		setFollowCreature(nullptr);
		onFollowCreatureDisappear(isLogout);
	}
}

void Creature::onChangeZone(ZoneType_t zone)
{
	if (getAttackedCreature() && zone == ZONE_PROTECTION) {
		onCreatureDisappear(getAttackedCreature(), false);
	}
}

void Creature::onAttackedCreatureChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (auto target = getAttackedCreature()) {
			onCreatureDisappear(target, false);
		}
	}
}

void Creature::onCreatureMove(const CreaturePtr& creature,
								const TilePtr& newTile,
								const Position& newPos,
								const TilePtr& oldTile,
								const Position& oldPos,
								bool teleport)
{
	if (creature == shared_from_this()) {
		lastStep = OTSYS_TIME();
		lastStepCost = 1;

		if (!teleport) {
			if (oldPos.z != newPos.z) {
				//floor change extra cost
				lastStepCost = 2;
			} else if (Position::getDistanceX(newPos, oldPos) >= 1 && Position::getDistanceY(newPos, oldPos) >= 1) {
				//diagonal extra cost
				lastStepCost = 3;
			}
		} else {
			stopEventWalk();
		}

		if (!summons.empty()) {
			//check if any of our summons is out of range (+/- 2 floors or 30 tiles away)
			std::forward_list<CreaturePtr> despawnList;
			for (auto& summon : summons) {
				const Position& pos = summon->getPosition();
				if (Position::getDistanceZ(newPos, pos) > 2 || (std::max<int32_t>(Position::getDistanceX(newPos, pos), Position::getDistanceY(newPos, pos)) > 30)) {
					despawnList.push_front(summon);
				}
			}

			for (CreaturePtr despawnCreature : despawnList) {
				g_game.removeCreature(despawnCreature, true);
			}
		}

		if (newTile->getZone() != oldTile->getZone()) {
			onChangeZone(getZone());
		}

		//update map cache
		if (isMapLoaded) {
			if (teleport || oldPos.z != newPos.z) {
				updateMapCache();
			} else {
				const Position& myPos = getPosition();

				if (oldPos.y > newPos.y) { //north
					//shift y south
					for (int32_t y = mapWalkHeight - 1; --y >= 0;) {
						memcpy(localMapCache[y + 1], localMapCache[y], sizeof(localMapCache[y]));
					}

					//update 0
					for (int32_t x = -maxWalkCacheWidth; x <= maxWalkCacheWidth; ++x) {
						auto cacheTile = g_game.map.getTile(myPos.getX() + x, myPos.getY() - maxWalkCacheHeight, myPos.z);
						updateTileCache(cacheTile, x, -maxWalkCacheHeight);
					}
				} else if (oldPos.y < newPos.y) { // south
					//shift y north
					for (int32_t y = 0; y <= mapWalkHeight - 2; ++y) {
						memcpy(localMapCache[y], localMapCache[y + 1], sizeof(localMapCache[y]));
					}

					//update mapWalkHeight - 1
					for (int32_t x = -maxWalkCacheWidth; x <= maxWalkCacheWidth; ++x) {
						auto cacheTile = g_game.map.getTile(myPos.getX() + x, myPos.getY() + maxWalkCacheHeight, myPos.z);
						updateTileCache(cacheTile, x, maxWalkCacheHeight);
					}
				}

				if (oldPos.x < newPos.x) { // east
					//shift y west
					int32_t starty = 0;
					int32_t endy = mapWalkHeight - 1;
					int32_t dy = Position::getDistanceY(oldPos, newPos);

					if (dy < 0) {
						endy += dy;
					} else if (dy > 0) {
						starty = dy;
					}

					for (int32_t y = starty; y <= endy; ++y) {
						for (int32_t x = 0; x <= mapWalkWidth - 2; ++x) {
							localMapCache[y][x] = localMapCache[y][x + 1];
						}
					}

					//update mapWalkWidth - 1
					for (int32_t y = -maxWalkCacheHeight; y <= maxWalkCacheHeight; ++y) {
						auto cacheTile = g_game.map.getTile(myPos.x + maxWalkCacheWidth, myPos.y + y, myPos.z);
						updateTileCache(cacheTile, maxWalkCacheWidth, y);
					}
				} else if (oldPos.x > newPos.x) { // west
					//shift y east
					int32_t starty = 0;
					int32_t endy = mapWalkHeight - 1;
					int32_t dy = Position::getDistanceY(oldPos, newPos);

					if (dy < 0) {
						endy += dy;
					} else if (dy > 0) {
						starty = dy;
					}

					for (int32_t y = starty; y <= endy; ++y) {
						for (int32_t x = mapWalkWidth - 1; --x >= 0;) {
							localMapCache[y][x + 1] = localMapCache[y][x];
						}
					}

					//update 0
					for (int32_t y = -maxWalkCacheHeight; y <= maxWalkCacheHeight; ++y) {
						auto cacheTile = g_game.map.getTile(myPos.x - maxWalkCacheWidth, myPos.y + y, myPos.z);
						updateTileCache(cacheTile, -maxWalkCacheWidth, y);
					}
				}

				updateTileCache(oldTile, oldPos);
			}
		}
	} else {
		if (isMapLoaded) {
			const Position& myPos = getPosition();

			if (newPos.z == myPos.z) {
				updateTileCache(newTile, newPos);
			}

			if (oldPos.z == myPos.z) {
				updateTileCache(oldTile, oldPos);
			}
		}
	}

	if (auto target = getFollowCreature()) {
		if (creature == target || (creature == shared_from_this())) {
			if (hasFollowPath) {
				if ((creature == target) && listWalkDir.empty()) {
					isUpdatingPath = false;
					goToFollowCreature();
				}
				else {
					isUpdatingPath = true;
				}
			}

			if (newPos.z != oldPos.z || !canSee(target->getPosition())) {
				onCreatureDisappear(target, false);
			}
		}
	}

	if (auto target = getAttackedCreature()) {
		if (creature == target || (creature == shared_from_this())) {
			if (newPos.z != oldPos.z || !canSee(target->getPosition())) {
				onCreatureDisappear(target, false);
			}
			else {
				if (hasExtraSwing()) {
					//our target is moving lets see if we can get in hit
					g_dispatcher.addTask(createTask([id = getID()]() { g_game.checkCreatureAttack(id); }));
				}

				if (newTile->getZone() != oldTile->getZone()) {
					onAttackedCreatureChangeZone(target->getZone());
				}
			}
		}
	}
}

CreatureVector Creature::getKillers() const
{
	CreatureVector killers;
	const int64_t timeNow = OTSYS_TIME();
	const uint32_t inFightTicks = g_config.getNumber(ConfigManager::PZ_LOCKED);
	for (const auto& it : damageMap) {
		auto attacker = g_game.getCreatureByID(it.first);
		if (attacker && attacker != shared_from_this() && timeNow - it.second.ticks <= inFightTicks) {
			killers.push_back(attacker);
		}
	}
	return killers;
}

void Creature::onDeath()
{
	bool lastHitUnjustified = false;
	bool mostDamageUnjustified = false;
	CreaturePtr lastHitCreature = g_game.getCreatureByID(lastHitCreatureId);
	CreaturePtr lastHitCreatureMaster;
	if (lastHitCreature) {
		lastHitUnjustified = lastHitCreature->onKilledCreature(getCreature());
		lastHitCreatureMaster = lastHitCreature->getMaster();
	} else {
		lastHitCreatureMaster = nullptr;
	}

	CreaturePtr mostDamageCreature = nullptr;

	const int64_t timeNow = OTSYS_TIME();
	const uint32_t inFightTicks = g_config.getNumber(ConfigManager::PZ_LOCKED);
	int32_t mostDamage = 0;
	std::map<CreaturePtr, uint64_t> experienceMap;
	for (const auto& it : damageMap) {
		if (auto attacker = g_game.getCreatureByID(it.first)) {
			CountBlock_t cb = it.second;
			if ((cb.total > mostDamage && (timeNow - cb.ticks <= inFightTicks))) {
				mostDamage = cb.total;
				mostDamageCreature = attacker;
			}

			if (attacker != shared_from_this()) {
				uint64_t gainExp = getGainedExperience(attacker);
				if (auto attackerPlayer = attacker->getPlayer()) {
					attackerPlayer->removeAttacked(getPlayer());

					Party* party = attackerPlayer->getParty();
					if (party && party->getLeader() && party->isSharedExperienceActive() && party->isSharedExperienceEnabled()) {
						attacker = party->getLeader();
					}
				}

				auto tmpIt = experienceMap.find(attacker);
				if (tmpIt == experienceMap.end()) {
					experienceMap[attacker] = gainExp;
				} else {
					tmpIt->second += gainExp;
				}
			}
		}
	}

	for (const auto& it : experienceMap) {
		it.first->onGainExperience(it.second, getCreature());
	}

	if (mostDamageCreature) {
		if (mostDamageCreature != lastHitCreature && mostDamageCreature != lastHitCreatureMaster) {
			auto mostDamageCreatureMaster = mostDamageCreature->getMaster();
			if (lastHitCreature != mostDamageCreatureMaster && (lastHitCreatureMaster == nullptr || mostDamageCreatureMaster != lastHitCreatureMaster)) {
				mostDamageUnjustified = mostDamageCreature->onKilledCreature(getCreature(), false);
			}
		}
	}

	bool droppedCorpse = dropCorpse(lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
	death(lastHitCreature);

	if (getMaster()) {
		setMaster(nullptr);
	}

	if (droppedCorpse) {
		g_game.removeCreature(getCreature(), false);
	}
}

bool Creature::dropCorpse(const CreaturePtr& lastHitCreature, const CreaturePtr& mostDamageCreature, bool lastHitUnjustified, bool mostDamageUnjustified)
{
	if (!lootDrop && getMonster()) {
		if (getMaster()) {
			//scripting event - onDeath
			const CreatureEventList& deathEvents = getCreatureEvents(CREATURE_EVENT_DEATH);
			for (CreatureEvent* deathEvent : deathEvents) {
				deathEvent->executeOnDeath(getCreature(), nullptr, lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
			}
		}

		g_game.addMagicEffect(getPosition(), CONST_ME_POFF);
	} else {
		ItemPtr splash;
		switch (getRace()) {
			case RACE_VENOM:
				splash = Item::CreateItem(ITEM_FULLSPLASH, FLUID_SLIME);
				break;

			case RACE_BLOOD:
				splash = Item::CreateItem(ITEM_FULLSPLASH, FLUID_BLOOD);
				break;

			default:
				splash = nullptr;
				break;
		}

		TilePtr tile = getTile();
		CylinderPtr c_tile = tile;
		if (splash) {
			g_game.internalAddItem(c_tile, splash, INDEX_WHEREEVER, FLAG_NOLIMIT);
			g_game.startDecay(splash);
		}

		ItemPtr corpse = getCorpse(lastHitCreature, mostDamageCreature);
		if (corpse) {
			g_game.internalAddItem(c_tile, corpse, INDEX_WHEREEVER, FLAG_NOLIMIT);
			g_game.startDecay(corpse);
		}

		//scripting event - onDeath
		for (CreatureEvent* deathEvent : getCreatureEvents(CREATURE_EVENT_DEATH)) {
			deathEvent->executeOnDeath(getCreature(), corpse, lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
		}

		if (corpse) {
			dropLoot(corpse->getContainer(), lastHitCreature);
		}
	}

	return true;
}

bool Creature::hasBeenAttacked(uint32_t attackerId)
{
	auto it = damageMap.find(attackerId);
	if (it == damageMap.end()) {
		return false;
	}
	return (OTSYS_TIME() - it->second.ticks) <= g_config.getNumber(ConfigManager::PZ_LOCKED);
}

ItemPtr Creature::getCorpse(const CreaturePtr&, const CreaturePtr&)
{
	return Item::CreateItem(getLookCorpse());
}

void Creature::changeHealth(int32_t healthChange, bool sendHealthChange/* = true*/)
{
	int32_t oldHealth = health;

	if (healthChange > 0) {
		health += std::min<int32_t>(healthChange, getMaxHealth() - health);
	} else {
		health = std::max<int32_t>(0, health + healthChange);
	}

	if (sendHealthChange && oldHealth != health) {
		CreatureConstPtr c_creature = this->getCreature();
		g_game.addCreatureHealth(c_creature); // this is broken or something idk.
	}

	if (health <= 0) {
		g_dispatcher.addTask(createTask([id = getID()]() { g_game.executeDeath(id); }));
	}
}

void Creature::gainHealth(const CreaturePtr& healer, int32_t healthGain)
{
	changeHealth(healthGain);
	if (healer) {
		healer->onTargetCreatureGainHealth(std::dynamic_pointer_cast<Creature>(shared_from_this()), healthGain);
	}
}

void Creature::drainHealth(const CreaturePtr& attacker, int32_t damage)
{
	changeHealth(-damage, false);

	if (attacker) {
		attacker->onAttackedCreatureDrainHealth(std::dynamic_pointer_cast<Creature>(shared_from_this()), damage);
	} else {
		lastHitCreatureId = 0;
	}
}

BlockType_t Creature::blockHit(const CreaturePtr& attacker, CombatType_t combatType, int32_t& damage,
                               bool checkDefense /* = false */, bool checkArmor /* = false */, bool /* field = false */, bool /* ignoreResistances = false */)
{
	BlockType_t blockType = BLOCK_NONE;

	if (isImmune(combatType)) {
		damage = 0;
		blockType = BLOCK_IMMUNITY;
	} else if (checkDefense || checkArmor) {
		bool hasDefense = false;

		if (blockCount > 0) {
			--blockCount;
			hasDefense = true;
		}

		if (checkDefense && hasDefense && canUseDefense) {
			int32_t defense = getDefense();
			damage -= uniform_random(defense / 2, defense);
			if (damage <= 0) {
				damage = 0;
				blockType = BLOCK_DEFENSE;
				checkArmor = false;
			}
		}

		if (checkArmor) {
			int32_t armor = getArmor();
			if (armor > 3) {
				damage -= uniform_random(armor / 2, armor - (armor % 2 + 1));
			} else if (armor > 0) {
				--damage;
			}

			if (damage <= 0) {
				damage = 0;
				blockType = BLOCK_ARMOR;
			}
		}

		if (hasDefense && blockType != BLOCK_NONE) {
			onBlockHit();
		}
	}
	
	if (damage <= 0) {
		damage = 0;
		blockType = BLOCK_ARMOR;
	}

	if (attacker and combatType != COMBAT_HEALING) {
		attacker->onAttackedCreature(getCreature());
		attacker->onAttackedCreatureBlockHit(blockType);
		if (attacker->getMaster() and attacker->getMaster()->getPlayer()) {
			auto masterPlayer = attacker->getMaster()->getPlayer();
			masterPlayer->onAttackedCreature(getCreature());
		}
	}

	onAttacked();
	return blockType;
}

bool Creature::setAttackedCreature(const CreaturePtr& creature)
{
	if (creature) {
		const Position& creaturePos = creature->getPosition();
		if (creaturePos.z != getPosition().z || !canSee(creaturePos)) {
			attackedCreature.reset();
			return false;
		}

		attackedCreature = creature;
		onAttackedCreature(getAttackedCreature());
		getAttackedCreature()->onAttacked();
	} else {
		attackedCreature.reset();
	}

	for (auto& summon : summons) {
		summon->setAttackedCreature(creature);
	}
	return true;
}

void Creature::getPathSearchParams(const CreatureConstPtr&, FindPathParams& fpp) const
{
	fpp.fullPathSearch = !hasFollowPath;
	fpp.clearSight = true;
	fpp.maxSearchDist = 12;
	fpp.minTargetDist = 1;
	fpp.maxTargetDist = 1;
}

void Creature::goToFollowCreature()
{
	if (auto target = getFollowCreature()) {
		FindPathParams fpp;
		getPathSearchParams(target, fpp);

		auto monster = getMonster();
		auto& targetPos = target->getPosition();
		if (monster && !monster->getMaster() && (monster->isFleeing() || fpp.maxTargetDist > 1)) {
			Direction dir = DIRECTION_NONE;

			if (monster->isFleeing()) {
				monster->getDistanceStep(targetPos, dir, true);
			} else { // maxTargetDist > 1
				if (!monster->getDistanceStep(targetPos, dir)) {
					// if we can't get anything then let the A* calculate
					listWalkDir.clear();
					if (getPathTo(targetPos, listWalkDir, fpp)) {
						hasFollowPath = true;
						startAutoWalk();
					} else {
						hasFollowPath = false;
					}
					return;
				}
			}

			if (dir != DIRECTION_NONE) {
				listWalkDir.clear();
				listWalkDir.push_back(dir);

				hasFollowPath = true;
				startAutoWalk();
			}
		} else {
			listWalkDir.clear();
			if (getPathTo(targetPos, listWalkDir, fpp)) {
				hasFollowPath = true;
				startAutoWalk();
			} else {
				hasFollowPath = false;
			}
		}
	}

	onFollowCreatureComplete(getFollowCreature());
}

bool Creature::setFollowCreature(const CreaturePtr& creature)
{
	if (creature) {
		if (getFollowCreature() == creature) {
			return true;
		}

		const Position& creaturePos = creature->getPosition();
		if (creaturePos.z != getPosition().z || !canSee(creaturePos)) {
			followCreature.reset();
			return false;
		}

		if (!listWalkDir.empty()) {
			listWalkDir.clear();
			onWalkAborted();
		}

		hasFollowPath = false;
		forceUpdateFollowPath = false;
		followCreature = creature;
		if (getMonster()) {
			isUpdatingPath = false;
			goToFollowCreature();
		} else {
			isUpdatingPath = true;
		}
	} else {
		isUpdatingPath = false;
		followCreature.reset();
	}

	onFollowCreature(creature);
	return true;
}

double Creature::getDamageRatio(const CreaturePtr& attacker) const
{
	uint32_t totalDamage = 0;
	uint32_t attackerDamage = 0;

	for (const auto& it : damageMap) {
		const CountBlock_t& cb = it.second;
		totalDamage += cb.total;
		if (it.first == attacker->getID()) {
			attackerDamage += cb.total;
		}
	}

	if (totalDamage == 0) {
		return 0;
	}

	return (static_cast<double>(attackerDamage) / totalDamage);
}

uint64_t Creature::getGainedExperience(const CreaturePtr& attacker) const
{
	return std::floor(getDamageRatio(attacker) * getLostExperience());
}

void Creature::addDamagePoints(const CreaturePtr& attacker, int32_t damagePoints)
{
	if (damagePoints <= 0) {
		return;
	}

	uint32_t attackerId = attacker->id;

	auto& cb = damageMap[attackerId];
	cb.ticks = OTSYS_TIME();
	cb.total += damagePoints;

	lastHitCreatureId = attackerId;
}

void Creature::onAddCondition(ConditionType_t type)
{
	if (type == CONDITION_PARALYZE && hasCondition(CONDITION_HASTE)) {
		removeCondition(CONDITION_HASTE);
	} else if (type == CONDITION_HASTE && hasCondition(CONDITION_PARALYZE)) {
		removeCondition(CONDITION_PARALYZE);
	}
}

void Creature::onAddCombatCondition(ConditionType_t)
{
	//
}

void Creature::onEndCondition(ConditionType_t)
{
	//
}

void Creature::onTickCondition(ConditionType_t type, bool& bRemove)
{
	const auto field = getTile()->getFieldItem();
	if (!field) {
		return;
	}

	switch (type) {
		case CONDITION_FIRE:
			bRemove = (field->getCombatType() != COMBAT_FIREDAMAGE);
			break;
		case CONDITION_ENERGY:
			bRemove = (field->getCombatType() != COMBAT_ENERGYDAMAGE);
			break;
		case CONDITION_POISON:
			bRemove = (field->getCombatType() != COMBAT_EARTHDAMAGE);
			break;
		case CONDITION_FREEZING:
			bRemove = (field->getCombatType() != COMBAT_ICEDAMAGE);
			break;
		case CONDITION_DAZZLED:
			bRemove = (field->getCombatType() != COMBAT_HOLYDAMAGE);
			break;
		case CONDITION_CURSED:
			bRemove = (field->getCombatType() != COMBAT_DEATHDAMAGE);
			break;
		case CONDITION_DROWN:
			bRemove = (field->getCombatType() != COMBAT_DROWNDAMAGE);
			break;
		case CONDITION_BLEEDING:
			bRemove = (field->getCombatType() != COMBAT_PHYSICALDAMAGE);
			break;
		default:
			break;
	}
}

void Creature::onCombatRemoveCondition(Condition* condition)
{
	removeCondition(condition);
}

void Creature::onAttacked()
{
	//
}

void Creature::onAttackedCreatureDrainHealth(const CreaturePtr& target, int32_t points)
{
	target->addDamagePoints(std::dynamic_pointer_cast<Creature>(shared_from_this()), points);
}

bool Creature::onKilledCreature(const CreaturePtr& target, bool)
{
	if (getMaster()) {
		getMaster()->onKilledCreature(target);
	}

	//scripting event - onKill
	const CreatureEventList& killEvents = getCreatureEvents(CREATURE_EVENT_KILL);
	for (CreatureEvent* killEvent : killEvents) {
		killEvent->executeOnKill(std::dynamic_pointer_cast<Creature>(shared_from_this()), target);
	}
	return false;
}

void Creature::onGainExperience(uint64_t gainExp, const CreaturePtr& target)
{
	if (gainExp == 0 || !getMaster()) {
		return;
	}

	gainExp /= 2;
	getMaster()->onGainExperience(gainExp, target);

	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, position, false, true);
	if (spectators.empty()) {
		return;
	}

	TextMessage message(MESSAGE_EXPERIENCE_OTHERS, ucfirst(getNameDescription()) + " gained " + std::to_string(gainExp) + (gainExp != 1 ? " experience points." : " experience point."));
	message.position = position;
	message.primary.color = TEXTCOLOR_WHITE_EXP;
	message.primary.value = gainExp;

	for (CreaturePtr spectator : spectators)
	{
		if (auto player = spectator->getPlayer())
		{
			player->sendTextMessage(message);
		}
	}
}

bool Creature::setMaster(const CreaturePtr& newMaster) {
	if (!newMaster && !getMaster()) {
		return false;
	}

	if (newMaster) {
		newMaster->summons.push_back(std::dynamic_pointer_cast<Creature>(shared_from_this()));
	}

	CreaturePtr oldMaster = getMaster();
	master = newMaster;

	if (oldMaster) {
		if (auto summon = std::find(oldMaster->summons.begin(), oldMaster->summons.end(), this->getCreature()); summon != oldMaster->summons.end()) {
			oldMaster->summons.erase(summon);
		}
	}
	return true;
}

bool Creature::addCondition(Condition* condition, bool force/* = false*/)
{
	if (condition == nullptr) {
		return false;
	}

	if (!force && condition->getType() == CONDITION_HASTE && hasCondition(CONDITION_PARALYZE)) {
		int64_t walkDelay = getWalkDelay();
		if (walkDelay > 0) {
			g_scheduler.addEvent(createSchedulerTask(walkDelay, [=, id = getID()]() { g_game.forceAddCondition(id, condition); }));
			return false;
		}
	}

	auto casterId = condition->getParam(CONDITION_PARAM_OWNER);
	if (condition->getType() == CONDITION_PARALYZE && this->getPlayer() && this->getPlayer()->isWearingImbuedItem() && this->getID() != casterId) {
		int32_t chance = 0;
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			auto item = this->getPlayer()->getInventoryItem(slot);
			if (item && item->hasImbuementType(IMBUEMENT_TYPE_PARALYSIS_DEFLECTION)) {
				for (auto imbuement : item->getImbuements()) {
					if (imbuement->imbuetype == IMBUEMENT_TYPE_PARALYSIS_DEFLECTION) {
						chance += imbuement->value;
						break;
					}
				}
			}
		}

		if (chance > 0) {
			
			const auto caster = g_game.getCreatureByID(casterId);
			if (const auto player = caster->getPlayer()) {
				player->addCondition(condition);
				return true;
			}
			if (const auto monster = caster->getMonster()) {
				int32_t roll = uniform_random(1, 100);
				if (roll <= chance) {
					monster->addCondition(condition);
					return true;
				}
			}
		}
	}

	Condition* prevCond = getCondition(condition->getType(), condition->getId(), condition->getSubId());
	if (prevCond) {
		prevCond->addCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()), condition);
		delete condition;
		return true;
	}

	if (condition->startCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()))) {
		conditions.push_back(condition);
		onAddCondition(condition->getType());
		return true;
	}

	delete condition;
	return false;
}

bool Creature::addCombatCondition(Condition* condition)
{
	//Caution: condition variable could be deleted after the call to addCondition
	ConditionType_t type = condition->getType();

	if (!addCondition(condition)) {
		return false;
	}

	onAddCombatCondition(type);
	return true;
}

void Creature::removeCondition(ConditionType_t type, bool force/* = false*/)
{
	auto it = conditions.begin(), end = conditions.end();
	while (it != end) {
		Condition* condition = *it;
		if (condition->getType() != type) {
			++it;
			continue;
		}

		if (!force && type == CONDITION_PARALYZE) {
			int64_t walkDelay = getWalkDelay();
			if (walkDelay > 0) {
				g_scheduler.addEvent(createSchedulerTask(walkDelay, [=, id = getID()]() { g_game.forceRemoveCondition(id, type); }));
				return;
			}
		}

		it = conditions.erase(it);

		condition->endCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()));
		delete condition;

		onEndCondition(type);
	}
}

void Creature::removeCondition(ConditionType_t type, ConditionId_t conditionId, bool force/* = false*/)
{
	auto it = conditions.begin(), end = conditions.end();
	while (it != end) {
		Condition* condition = *it;
		if (condition->getType() != type || condition->getId() != conditionId) {
			++it;
			continue;
		}

		if (!force && type == CONDITION_PARALYZE) {
			int64_t walkDelay = getWalkDelay();
			if (walkDelay > 0) {
				g_scheduler.addEvent(createSchedulerTask(walkDelay, [=, id = getID()]() { g_game.forceRemoveCondition(id, type); }));
				return;
			}
		}

		it = conditions.erase(it);

		condition->endCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()));
		delete condition;

		onEndCondition(type);
	}
}

void Creature::removeCombatCondition(ConditionType_t type)
{
	std::vector<Condition*> removeConditions;
	for (Condition* condition : conditions) {
		if (condition->getType() == type) {
			removeConditions.push_back(condition);
		}
	}

	for (Condition* condition : removeConditions) {
		onCombatRemoveCondition(condition);
	}
}

void Creature::removeCondition(Condition* condition, bool force/* = false*/)
{
	auto it = std::ranges::find(conditions, condition);
	if (it == conditions.end()) {
		return;
	}

	if (!force && condition->getType() == CONDITION_PARALYZE) {
		int64_t walkDelay = getWalkDelay();
		if (walkDelay > 0) {
			g_scheduler.addEvent(createSchedulerTask(walkDelay, [id = getID(), type = condition->getType()]() { g_game.forceRemoveCondition(id, type); }));
			return;
		}
	}

	conditions.erase(it);

	condition->endCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()));
	onEndCondition(condition->getType());
	delete condition;
}

Condition* Creature::getCondition(ConditionType_t type) const
{
	for (Condition* condition : conditions) {
		if (condition->getType() == type) {
			return condition;
		}
	}
	return nullptr;
}

Condition* Creature::getCondition(ConditionType_t type, ConditionId_t conditionId, uint32_t subId/* = 0*/) const
{
	for (Condition* condition : conditions) {
		if (condition->getType() == type && condition->getId() == conditionId && condition->getSubId() == subId) {
			return condition;
		}
	}
	return nullptr;
}

void Creature::executeConditions(uint32_t interval)
{
	ConditionList tempConditions{ conditions };
	for (Condition* condition : tempConditions) {
		auto it = std::ranges::find(conditions, condition);
		if (it == conditions.end()) {
			continue;
		}

		if (!condition->executeCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()), interval)) {
			it = std::ranges::find(conditions, condition);
			if (it != conditions.end()) {
				conditions.erase(it);
				condition->endCondition(std::dynamic_pointer_cast<Creature>(shared_from_this()));
				onEndCondition(condition->getType());
				delete condition;
			}
		}
	}
}

bool Creature::hasCondition(ConditionType_t type, uint32_t subId/* = 0*/) const
{
	if (isSuppress(type)) {
		return false;
	}

	int64_t timeNow = OTSYS_TIME();
	for (Condition* condition : conditions) {
		if (condition->getType() != type || condition->getSubId() != subId) {
			continue;
		}

		if (condition->getEndTime() >= timeNow || condition->getTicks() == -1) {
			return true;
		}
	}
	return false;
}

bool Creature::isImmune(CombatType_t type) const
{
	return hasBitSet(static_cast<uint32_t>(type), getDamageImmunities());
}

bool Creature::isImmune(ConditionType_t type) const
{
	return hasBitSet(static_cast<uint32_t>(type), getConditionImmunities());
}

bool Creature::isSuppress(ConditionType_t type) const
{
	return hasBitSet(static_cast<uint32_t>(type), getConditionSuppressions());
}

int64_t Creature::getStepDuration(Direction dir) const
{
	int64_t stepDuration = getStepDuration();
	if ((dir & DIRECTION_DIAGONAL_MASK) != 0) {
		stepDuration *= 3;
	}
	return stepDuration;
}

int64_t Creature::getStepDuration() const
{
	if (isRemoved()) {
		return 0;
	}

	uint32_t calculatedStepSpeed;
	uint32_t groundSpeed = 0;

	int32_t stepSpeed = getStepSpeed();
	if (stepSpeed > -Creature::speedB) {
		calculatedStepSpeed = floor((Creature::speedA * log((stepSpeed / 2) + Creature::speedB) + Creature::speedC) + 0.5);
		if (calculatedStepSpeed == 0) {
			calculatedStepSpeed = 1;
		}
	} else {
		calculatedStepSpeed = 1;
	}

	if (auto ground = tile.lock()->getGround())
	{
		if (ground) {
			groundSpeed = Item::items[ground->getID()].speed;
			if (groundSpeed == 0) {
				groundSpeed = 150;
			}
		}
	}  else {
		groundSpeed = 150;
	}
	
	double duration = std::floor(1000 * groundSpeed / calculatedStepSpeed);
	int64_t stepDuration = std::ceil(duration / 50) * 50;

	const MonsterConstPtr monster = getMonster();
	if (monster && monster->isTargetNearby() && !monster->isFleeing() && !monster->getMaster()) {
		stepDuration *= 2;
	}

	return stepDuration;
}

int64_t Creature::getEventStepTicks(bool onlyDelay) const
{
	int64_t ret = getWalkDelay();
	if (ret <= 0) {
		int64_t stepDuration = getStepDuration();
		if (onlyDelay && stepDuration > 0) {
			ret = 1;
		} else {
			ret = stepDuration * lastStepCost;
		}
	}
	return ret;
}

LightInfo Creature::getCreatureLight() const
{
	return internalLight;
}

void Creature::setCreatureLight(LightInfo lightInfo) {
	internalLight = std::move(lightInfo);
}

void Creature::setNormalCreatureLight()
{
	internalLight = {};
}

// This one is for trying to add new skills on the fly with set levels
// for example, creating npc's or monsters with randomized levels
// negatives are not allowed for this purpose
bool Creature::giveCustomSkill(std::string_view name, uint16_t level)
{
	auto new_skill = std::make_shared<CustomSkill>(FormulaType::EXPONENTIAL);
	new_skill->addLevels(level);
	return c_skills.try_emplace(name, new_skill).second;
}

bool Creature::giveCustomSkill(std::string_view name, std::shared_ptr<CustomSkill> new_skill)
{
	return c_skills.try_emplace(name, new_skill).second;
}

bool Creature::removeCustomSkill(std::string_view name)
{
	return c_skills.erase(name) > 0;
}

std::shared_ptr<CustomSkill> Creature::getCustomSkill(std::string_view name)
{
	auto it = c_skills.find(name);
	if (it != c_skills.end()) {
		return it->second;
	}
	return nullptr;
}


bool Creature::registerCreatureEvent(const std::string& name)
{
	CreatureEvent* event = g_creatureEvents->getEventByName(name);
	if (!event) {
		return false;
	}

	CreatureEventType_t type = event->getEventType();
	if (hasEventRegistered(type)) {
		for (CreatureEvent* creatureEvent : eventsList) {
			if (creatureEvent == event) {
				return false;
			}
		}
	} else {
		scriptEventsBitField |= static_cast<uint32_t>(1) << type;
	}

	eventsList.push_back(event);
	return true;
}

bool Creature::unregisterCreatureEvent(const std::string& name)
{
	CreatureEvent* event = g_creatureEvents->getEventByName(name);
	if (!event) {
		return false;
	}

	CreatureEventType_t type = event->getEventType();
	if (!hasEventRegistered(type)) {
		return false;
	}

	bool resetTypeBit = true;

	auto it = eventsList.begin(), end = eventsList.end();
	while (it != end) {
		CreatureEvent* curEvent = *it;
		if (curEvent == event) {
			it = eventsList.erase(it);
			continue;
		}

		if (curEvent->getEventType() == type) {
			resetTypeBit = false;
		}
		++it;
	}

	if (resetTypeBit) {
		scriptEventsBitField &= ~(static_cast<uint32_t>(1) << type);
	}
	return true;
}

CreatureEventList Creature::getCreatureEvents(CreatureEventType_t type) const
{
	CreatureEventList tmpEventList;

	if (!hasEventRegistered(type)) {
		return tmpEventList;
	}

	for (CreatureEvent* creatureEvent : eventsList) {
		if (!creatureEvent->isLoaded()) {
			continue;
		}

		if (creatureEvent->getEventType() == type) {
			tmpEventList.push_back(creatureEvent);
		}
	}

	return tmpEventList;
}

bool FrozenPathingConditionCall::isInRange(const Position& startPos, const Position& testPos,
        const FindPathParams& fpp) const
{
	if (fpp.fullPathSearch) {
		if (testPos.x > targetPos.x + fpp.maxTargetDist) {
			return false;
		}

		if (testPos.x < targetPos.x - fpp.maxTargetDist) {
			return false;
		}

		if (testPos.y > targetPos.y + fpp.maxTargetDist) {
			return false;
		}

		if (testPos.y < targetPos.y - fpp.maxTargetDist) {
			return false;
		}
	} else {
		int_fast32_t dx = Position::getOffsetX(startPos, targetPos);

		int32_t dxMax = (dx >= 0 ? fpp.maxTargetDist : 0);
		if (testPos.x > targetPos.x + dxMax) {
			return false;
		}

		int32_t dxMin = (dx <= 0 ? fpp.maxTargetDist : 0);
		if (testPos.x < targetPos.x - dxMin) {
			return false;
		}

		int_fast32_t dy = Position::getOffsetY(startPos, targetPos);

		int32_t dyMax = (dy >= 0 ? fpp.maxTargetDist : 0);
		if (testPos.y > targetPos.y + dyMax) {
			return false;
		}

		int32_t dyMin = (dy <= 0 ? fpp.maxTargetDist : 0);
		if (testPos.y < targetPos.y - dyMin) {
			return false;
		}
	}
	return true;
}

bool FrozenPathingConditionCall::operator()(const Position& startPos, const Position& testPos,
        const FindPathParams& fpp, int32_t& bestMatchDist) const
{
	if (!isInRange(startPos, testPos, fpp)) {
		return false;
	}

	if (fpp.clearSight && !g_game.isSightClear(testPos, targetPos, true)) {
		return false;
	}

	int32_t testDist = std::max<int32_t>(Position::getDistanceX(targetPos, testPos), Position::getDistanceY(targetPos, testPos));
	if (fpp.maxTargetDist == 1) {
		if (testDist < fpp.minTargetDist || testDist > fpp.maxTargetDist) {
			return false;
		}

		return true;
	} else if (testDist <= fpp.maxTargetDist) {
		if (testDist < fpp.minTargetDist) {
			return false;
		}

		if (testDist == fpp.maxTargetDist) {
			bestMatchDist = 0;
			return true;
		} else if (testDist > bestMatchDist) {
			//not quite what we want, but the best so far
			bestMatchDist = testDist;
			return true;
		}
	}
	return false;
}

bool Creature::isInvisible() const
{
	return std::ranges::find_if(conditions, [] (const Condition* condition) {
		return condition->getType() == CONDITION_INVISIBLE;
	}) != conditions.end();
}

bool Creature::getPathTo(const Position& targetPos, std::vector<Direction>& dirList, const FindPathParams& fpp)
{
	CreaturePtr t_c = getCreature();
	return g_game.map.getPathMatching(t_c, dirList, FrozenPathingConditionCall(targetPos), fpp);
}

bool Creature::getPathTo(const Position& targetPos, std::vector<Direction>& dirList, int32_t minTargetDist, int32_t maxTargetDist, bool fullPathSearch /*= true*/, bool clearSight /*= true*/, int32_t maxSearchDist /*= 0*/)
{
	FindPathParams fpp;
	fpp.fullPathSearch = fullPathSearch;
	fpp.maxSearchDist = maxSearchDist;
	fpp.clearSight = clearSight;
	fpp.minTargetDist = minTargetDist;
	fpp.maxTargetDist = maxTargetDist;
	return getPathTo(targetPos, dirList, fpp);
}
