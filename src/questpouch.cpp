// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "container.h"
#include "game.h"
#include "player.h"
#include "questpouch.h"

QuestPouch::QuestPouch(uint16_t type) : Container(type, items[type].maxItems, false, true) {
	// Quest pouch: 100 slots, locked (items can only be added/removed via Lua), pagination enabled
	container_subtype = ContainerSubType::None;
	thing_subtype = ThingSubType::None;
	item_subtype = ItemSubType::QuestPouch;
	cylinder_subtype = CylinderSubType::None;
}

ReturnValue QuestPouch::queryAdd(int32_t index, const ThingPtr& thing, uint32_t count,
                                 uint32_t flags, CreaturePtr actor) {
	// Only allow adding items with FLAG_NOLIMIT (from Lua or game system)
	if (!(flags & FLAG_NOLIMIT)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!thing->getItem()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (size() >= capacity()) {
		return RETURNVALUE_CONTAINERNOTENOUGHROOM;
	}

	return RETURNVALUE_NOERROR;
}

void QuestPouch::postAddNotification(ThingPtr thing, CylinderPtr oldParent, int32_t index, cylinderlink_t link) {
	if (parent.lock()) {
		parent.lock()->postAddNotification(thing, oldParent, index, LINK_TOPPARENT);
	}
}

void QuestPouch::postRemoveNotification(ThingPtr thing, CylinderPtr newParent, int32_t index, cylinderlink_t link) {
	if (parent.lock()) {
		parent.lock()->postRemoveNotification(thing, newParent, index, LINK_TOPPARENT);
	}
}

uint32_t QuestPouch::getItemIdCount(uint16_t itemId) const {
	uint32_t count = 0;
	for (const auto& item : itemlist) {
		if (item && item->getID() == itemId) {
			count += item->getSubType();
		}
	}
	return count;
}

uint32_t QuestPouch::getItemByAidCount(int32_t aid) const {
	uint32_t count = 0;
	for (const auto& item : itemlist) {
		if (item && item->getActionId() == aid) {
			count += item->getSubType();
		}
	}
	return count;
}

uint32_t QuestPouch::getItemByUid(uint32_t uid) const {
	for (const auto& item : itemlist) {
		if (item && item->getUniqueId() == uid) {
			return item->getSubType();
		}
	}
	return 0;
}

bool QuestPouch::removeItemById(uint16_t itemId, uint32_t count) {
	uint32_t remaining = count;

	for (auto it = itemlist.begin(); it != itemlist.end() && remaining > 0;) {
		auto item = *it;
		if (item && item->getID() == itemId) {
			uint32_t itemSubType = item->getSubType();
			int32_t index = getThingIndex(item);

			if (itemSubType <= remaining) {
				// Remove entire stack
				remaining -= itemSubType;

				// Send update to client if container has a parent
				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onRemoveContainerItem(index, item);
				}

				updateItemWeight(-item->getWeight());
				ammoCount -= item->getItemCount();
				item->clearParent();
				it = itemlist.erase(it);
			} else {
				// Reduce stack size
				const int32_t oldWeight = item->getWeight();
				ammoCount -= (itemSubType - remaining);
				item->setSubType(itemSubType - remaining);
				updateItemWeight(-oldWeight + item->getWeight());

				// Send update to client if container has a parent
				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onUpdateContainerItem(index, item, item);
				}

				remaining = 0;
				++it;
			}
		} else {
			++it;
		}
	}

	return remaining == 0;
}

bool QuestPouch::removeItemByUid(uint32_t uid) {
	for (auto it = itemlist.begin(); it != itemlist.end(); ++it) {
		auto item = *it;
		if (item && item->getUniqueId() == uid) {
			int32_t index = getThingIndex(item);

			// Send update to client if container has a parent
			if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
				onRemoveContainerItem(index, item);
			}

			updateItemWeight(-item->getWeight());
			ammoCount -= item->getItemCount();
			item->clearParent();
			itemlist.erase(it);
			return true;
		}
	}
	return false;
}

bool QuestPouch::addItem(uint16_t itemId, uint32_t count) {
	if (itemId == 0 || count == 0) {
		return false;
	}

	const ItemType& it = Item::items[itemId];

	if (it.stackable) {
		// For stackable items, create stacks of up to 100
		uint32_t remaining = count;
		while (remaining > 0) {
			uint16_t stackCount = std::min<uint16_t>(remaining, 100);
			auto newItem = Item::CreateItem(itemId, stackCount);
			if (!newItem) {
				return false;
			}

			if (size() >= capacity()) {
				return false;
			}

			// Add item and trigger notification
			newItem->setParent(getContainer());
			itemlist.push_front(newItem);
			updateItemWeight(newItem->getWeight());
			ammoCount += newItem->getItemCount();

			// Send update to client if container has a parent
			if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
				onAddContainerItem(newItem);
			}

			remaining -= stackCount;
		}
	} else {
		// For non-stackable items, create individual items
		for (uint32_t i = 0; i < count; i++) {
			auto newItem = Item::CreateItem(itemId, 1);
			if (!newItem) {
				return false;
			}

			if (size() >= capacity()) {
				return false;
			}

			// Add item and trigger notification
			newItem->setParent(getContainer());
			itemlist.push_front(newItem);
			updateItemWeight(newItem->getWeight());
			ammoCount += newItem->getItemCount();

			// Send update to client if container has a parent
			if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
				onAddContainerItem(newItem);
			}
		}
	}

	return true;
}

bool QuestPouch::addItemByUid(uint32_t uid, uint32_t count) {
	// This method would require a game-wide UID lookup which is complex
	// For now, use addItem(itemId, count) instead
	return false;
}

std::vector<ItemPtr> QuestPouch::getItems(uint32_t limit, uint32_t offset) const {
	std::vector<ItemPtr> result;
	uint32_t index = 0;
	
	for (const auto& item : itemlist) {
		if (index >= offset && result.size() < limit) {
			result.push_back(item);
		}
		++index;
		
		if (result.size() >= limit) {
			break;
		}
	}
	
	return result;
}

uint32_t QuestPouch::getTotalItemsCount() const {
	uint32_t count = 0;
	for (const auto& item : itemlist) {
		if (item) {
			count += item->getSubType();
		}
	}
	return count;
}

void QuestPouch::removeAllItems() {
	for (auto it = itemlist.begin(); it != itemlist.end();) {
		auto item = *it;
		int32_t index = getThingIndex(item);

		// Send update to client if container has a parent
		if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
			onRemoveContainerItem(index, item);
		}

		updateItemWeight(-item->getWeight());
		ammoCount -= item->getItemCount();
		item->clearParent();
		it = itemlist.erase(it);
	}
}

ItemPtr QuestPouch::transferItemToContainer(uint16_t itemId, uint32_t count, const ContainerPtr& targetContainer) {
	if (!targetContainer) {
		return nullptr;
	}

	for (auto it = itemlist.begin(); it != itemlist.end(); ++it) {
		auto item = *it;
		if (item && item->getID() == itemId) {
			uint32_t itemSubType = item->getSubType();
			uint32_t transferCount = std::min(count, itemSubType);

			// Create the item to transfer
			auto transferredItem = Item::CreateItem(itemId, transferCount);
			if (!transferredItem) {
				return nullptr;
			}

			// Remove from quest pouch
			int32_t index = getThingIndex(item);
			if (itemSubType <= transferCount) {
				// Remove entire stack
				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onRemoveContainerItem(index, item);
				}
				updateItemWeight(-item->getWeight());
				ammoCount -= item->getItemCount();
				item->clearParent();
				it = itemlist.erase(it);
			} else {
				// Reduce stack
				const int32_t oldWeight = item->getWeight();
				ammoCount -= (itemSubType - transferCount);
				item->setSubType(itemSubType - transferCount);
				updateItemWeight(-oldWeight + item->getWeight());

				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onUpdateContainerItem(index, item, item);
				}
			}

			// Add to target container
			if (targetContainer->internalAddThing(transferredItem) == RETURNVALUE_NOERROR) {
				if (targetContainer->getParent() && (targetContainer->getParent() != VirtualCylinder::virtualCylinder)) {
					targetContainer->onAddContainerItem(transferredItem);
				}
				return transferredItem;
			}

			return nullptr;
		}
	}

	return nullptr;
}

ItemPtr QuestPouch::transferItemByUidToContainer(uint32_t uid, uint32_t count, const ContainerPtr& targetContainer) {
	if (!targetContainer) {
		return nullptr;
	}

	for (auto it = itemlist.begin(); it != itemlist.end(); ++it) {
		auto item = *it;
		if (item && item->getUniqueId() == uid) {
			uint32_t itemSubType = item->getSubType();
			uint32_t transferCount = std::min(count, itemSubType);

			// Create the item to transfer
			auto transferredItem = Item::CreateItem(item->getID(), transferCount);
			if (!transferredItem) {
				return nullptr;
			}

			// Remove from quest pouch
			int32_t index = getThingIndex(item);
			if (itemSubType <= transferCount) {
				// Remove entire stack
				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onRemoveContainerItem(index, item);
				}
				updateItemWeight(-item->getWeight());
				ammoCount -= item->getItemCount();
				item->clearParent();
				it = itemlist.erase(it);
			} else {
				// Reduce stack
				const int32_t oldWeight = item->getWeight();
				ammoCount -= (itemSubType - transferCount);
				item->setSubType(itemSubType - transferCount);
				updateItemWeight(-oldWeight + item->getWeight());

				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onUpdateContainerItem(index, item, item);
				}
			}

			// Add to target container
			if (targetContainer->internalAddThing(transferredItem) == RETURNVALUE_NOERROR) {
				if (targetContainer->getParent() && (targetContainer->getParent() != VirtualCylinder::virtualCylinder)) {
					targetContainer->onAddContainerItem(transferredItem);
				}
				return transferredItem;
			}

			return nullptr;
		}
	}

	return nullptr;
}
