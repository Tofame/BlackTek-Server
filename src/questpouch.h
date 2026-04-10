// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_QUESTPOUCH_H
#define FS_QUESTPOUCH_H

#include "container.h"

class QuestPouch final : public Container
{
	public:
		explicit QuestPouch(uint16_t type);

		//cylinder implementations
		ReturnValue queryAdd(int32_t index, const ThingPtr& thing, uint32_t count,
		                     uint32_t flags, CreaturePtr actor = nullptr) override;

		void postAddNotification(ThingPtr thing, CylinderPtr oldParent, int32_t index, cylinderlink_t link = LINK_OWNER) override;
		void postRemoveNotification(ThingPtr thing, CylinderPtr newParent, int32_t index, cylinderlink_t link = LINK_OWNER) override;

		//overrides
		bool canRemove() const override {
			return false;
		}

		// Override virtual method from Container
		QuestPouchPtr getQuestPouch() override {
			return static_shared_this<QuestPouch>();
		}

		QuestPouchConstPtr getQuestPouch() const override {
			return static_shared_this<const QuestPouch>();
		}

		// Quest pouch specific methods
		uint32_t getItemIdCount(uint16_t itemId) const;
		uint32_t getItemByAidCount(int32_t aid) const;
		uint32_t getItemByUid(uint32_t uid) const;
		
		bool removeItemById(uint16_t itemId, uint32_t count);
		bool removeItemByUid(uint32_t uid);
		
		bool addItem(uint16_t itemId, uint32_t count);
		bool addItemByUid(uint32_t uid, uint32_t count);
		
		std::vector<ItemPtr> getItems(uint32_t limit, uint32_t offset) const;
		uint32_t getTotalItemsCount() const;
};

using QuestPouchPtr = std::shared_ptr<QuestPouch>;
using QuestPouchConstPtr = std::shared_ptr<const QuestPouch>;

#endif
