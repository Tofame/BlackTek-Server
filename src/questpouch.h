// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_QUESTPOUCH_H
#define FS_QUESTPOUCH_H

#include "container.h"
#include <vector>
#include <string>

enum class ItemFilterType {
	ItemID,
	UID,
	AID,
};

struct ItemFilter {
	ItemFilterType type;
	uint32_t value;
};

class QuestPouch final : public Container
{
	public:
		static constexpr uint32_t maxQuestPouchItems = 100;

		explicit QuestPouch(uint16_t type);

		ReturnValue queryAdd(int32_t index, const ThingPtr& thing, uint32_t count,
		                     uint32_t flags, CreaturePtr actor = nullptr) override;

		void postAddNotification(ThingPtr thing, CylinderPtr oldParent, int32_t index, cylinderlink_t link = LINK_OWNER) override;
		void postRemoveNotification(ThingPtr thing, CylinderPtr newParent, int32_t index, cylinderlink_t link = LINK_OWNER) override;

		bool canRemove() const override {
			return false;
		}

		QuestPouchPtr getQuestPouch() override {
			return static_shared_this<QuestPouch>();
		}

		QuestPouchConstPtr getQuestPouch() const override {
			return static_shared_this<const QuestPouch>();
		}

		bool addItem(uint16_t itemId, uint32_t count);
		bool removeItems(const std::vector<ItemFilter>& filters, uint32_t count = 1);
		void removeAllItems();
		uint32_t countItems(const std::vector<ItemFilter>& filters) const;
		std::vector<ItemPtr> getItems(const std::vector<ItemFilter>& filters, uint32_t limit = 0, uint32_t offset = 0) const;
		uint32_t getTotalItemsCount() const;
		ItemPtr findMatchingItem(const std::vector<ItemFilter>& filters, uint32_t count) const;
		ItemPtr transferItemToContainer(const std::vector<ItemFilter>& filters, uint32_t count, const ContainerPtr& targetContainer);

	private:
		bool itemMatchesFilters(const ItemPtr& item, const std::vector<ItemFilter>& filters) const;
};

using QuestPouchPtr = std::shared_ptr<QuestPouch>;
using QuestPouchConstPtr = std::shared_ptr<const QuestPouch>;

#endif
