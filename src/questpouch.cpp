// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "container.h"
#include "game.h"
#include "player.h"
#include "questpouch.h"

QuestPouch::QuestPouch(uint16_t type) : Container(type, items[type].maxItems, false, true) {
	container_subtype = ContainerSubType::None;
	thing_subtype = ThingSubType::None;
	item_subtype = ItemSubType::QuestPouch;
	cylinder_subtype = CylinderSubType::None;
}

ReturnValue QuestPouch::queryAdd(int32_t index, const ThingPtr& thing, uint32_t count,
                                 uint32_t flags, CreaturePtr actor) {
	if (!(flags & FLAG_NOLIMIT)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!thing->getItem()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (size() >= maxQuestPouchItems) {
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

bool QuestPouch::itemMatchesFilters(const ItemPtr& item, const std::vector<ItemFilter>& filters) const {
	if (!item || filters.empty()) {
		return false;
	}

	for (const auto& filter : filters) {
		switch (filter.type) {
			case ItemFilterType::ItemID:
				if (item->getID() != filter.value) {
					return false;
				}
				break;
			case ItemFilterType::UID:
				if (item->getUniqueId() != filter.value) {
					return false;
				}
				break;
			case ItemFilterType::AID:
				if (item->getActionId() != filter.value) {
					return false;
				}
				break;
		}
	}
	return true;
}

uint32_t QuestPouch::countItems(const std::vector<ItemFilter>& filters) const {
	uint32_t count = 0;
	for (const auto& item : itemlist) {
		if (itemMatchesFilters(item, filters)) {
			count += item->getSubType();
		}
	}
	return count;
}

bool QuestPouch::removeItems(const std::vector<ItemFilter>& filters, uint32_t count) {
	if (filters.empty() || count == 0) {
		return false;
	}

	uint32_t remaining = count;

	for (auto it = itemlist.begin(); it != itemlist.end() && remaining > 0;) {
		auto item = *it;
		if (itemMatchesFilters(item, filters)) {
			uint32_t itemSubType = item->getSubType();
			int32_t index = getThingIndex(item);

			if (itemSubType <= remaining) {
				remaining -= itemSubType;

				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onRemoveContainerItem(index, item);
				}

				updateItemWeight(-item->getWeight());
				ammoCount -= item->getItemCount();
				item->clearParent();
				it = itemlist.erase(it);
			} else {
				const int32_t oldWeight = item->getWeight();
				ammoCount -= (itemSubType - remaining);
				item->setSubType(itemSubType - remaining);
				updateItemWeight(-oldWeight + item->getWeight());

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

void QuestPouch::removeAllItems() {
	for (auto it = itemlist.begin(); it != itemlist.end();) {
		auto item = *it;
		int32_t index = getThingIndex(item);

		if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
			onRemoveContainerItem(index, item);
		}

		updateItemWeight(-item->getWeight());
		ammoCount -= item->getItemCount();
		item->clearParent();
		it = itemlist.erase(it);
	}
}

bool QuestPouch::addItem(uint16_t itemId, uint32_t count) {
	if (itemId == 0 || count == 0) {
		return false;
	}

	const ItemType& it = Item::items[itemId];

	if (it.stackable) {
		uint32_t remaining = count;
		while (remaining > 0) {
			uint16_t stackCount = std::min<uint16_t>(remaining, 100);
			auto newItem = Item::CreateItem(itemId, stackCount);
			if (!newItem) {
				return false;
			}

			if (size() >= maxQuestPouchItems) {
				return false;
			}

			newItem->setParent(getContainer());
			itemlist.push_front(newItem);
			updateItemWeight(newItem->getWeight());
			ammoCount += newItem->getItemCount();

			if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
				onAddContainerItem(newItem);
			}

			remaining -= stackCount;
		}
	} else {
		for (uint32_t i = 0; i < count; i++) {
			auto newItem = Item::CreateItem(itemId, 1);
			if (!newItem) {
				return false;
			}

			if (size() >= maxQuestPouchItems) {
				return false;
			}

			newItem->setParent(getContainer());
			itemlist.push_front(newItem);
			updateItemWeight(newItem->getWeight());
			ammoCount += newItem->getItemCount();

			if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
				onAddContainerItem(newItem);
			}
		}
	}

	return true;
}

std::vector<ItemPtr> QuestPouch::getItems(const std::vector<ItemFilter>& filters, uint32_t limit, uint32_t offset) const {
	std::vector<ItemPtr> result;
	bool getAll = filters.empty() || (limit == 0 && offset == 0);

	if (getAll) {
		for (const auto& item : itemlist) {
			if (filters.empty() || itemMatchesFilters(item, filters)) {
				result.push_back(item);
			}
		}
		return result;
	}

	uint32_t index = 0;
	for (const auto& item : itemlist) {
		if (itemMatchesFilters(item, filters)) {
			if (index >= offset) {
				result.push_back(item);
				if (result.size() >= limit) {
					break;
				}
			}
			++index;
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

ItemPtr QuestPouch::transferItemToContainer(const std::vector<ItemFilter>& filters, uint32_t count, const ContainerPtr& targetContainer) {
	if (!targetContainer || filters.empty()) {
		return nullptr;
	}

	for (auto it = itemlist.begin(); it != itemlist.end(); ++it) {
		auto item = *it;
		if (itemMatchesFilters(item, filters)) {
			uint32_t itemSubType = item->getSubType();
			uint32_t transferCount = std::min(count, itemSubType);

			auto transferredItem = Item::CreateItem(item->getID(), transferCount);
			if (!transferredItem) {
				return nullptr;
			}

			int32_t index = getThingIndex(item);
			if (itemSubType <= transferCount) {
				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onRemoveContainerItem(index, item);
				}
				updateItemWeight(-item->getWeight());
				ammoCount -= item->getItemCount();
				item->clearParent();
				it = itemlist.erase(it);
			} else {
				const int32_t oldWeight = item->getWeight();
				ammoCount -= (itemSubType - transferCount);
				item->setSubType(itemSubType - transferCount);
				updateItemWeight(-oldWeight + item->getWeight());

				if (getParent() && (getParent() != VirtualCylinder::virtualCylinder)) {
					onUpdateContainerItem(index, item, item);
				}
			}

			targetContainer->internalAddThing(transferredItem);
			if (targetContainer->getParent() && (targetContainer->getParent() != VirtualCylinder::virtualCylinder)) {
				targetContainer->onAddContainerItem(transferredItem);
			}
			return transferredItem;
		}
	}

	return nullptr;
}
