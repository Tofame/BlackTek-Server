# Quest Pouch - Server Implementation Guide for OTC Client

## 📋 Task for OTC Developer

Please implement client-side support for the **Quest Pouch** feature. The server has been fully updated to support this feature following the exact same pattern as **Store Inbox**.

**IMPORTANT**: Base your implementation on the existing `InventorySlotPurse` component in the OTC codebase. The quest pouch should work identically to how InventorySlotPurse works, just as a new inventory slot.

---

## 🎯 What Was Implemented on Server

### **1. Server-Side Architecture**

The quest pouch is a **virtual container** (like Store Inbox and Shop Inbox) that:
- Exists as **inventory slot 12** (`CONST_SLOT_QUEST_POUCH`)
- Is sent to the client during player login via `sendInventoryItem()`
- Supports **pagination** (can hold up to 100 item slots)
- Is **persistent** (saves/loads from database automatically)
- **Cannot be traded, dropped, or lost on death** (anti-abuse)
- Can only be modified via **Lua scripting** (game system controlled)

### **2. Server Constants**

```cpp
// creature.h - Inventory slot constant
CONST_SLOT_QUEST_POUCH = 12

// const.h - Item ID for the quest pouch
ITEM_QUEST_POUCH = 26053
```

### **3. Protocol Transmission**

The server sends the quest pouch during login exactly like Store Inbox:

```cpp
// protocolgame.cpp - During player login
sendInventoryItem(CONST_SLOT_STORE_INBOX, player->getStoreInbox()->getItem());
sendInventoryItem(CONST_SLOT_QUEST_POUCH, player->getQuestPouch()->getItem()); // ← NEW
```

This means the client will receive an `InventoryItem` network message with:
- **Slot ID**: `12` (CONST_SLOT_QUEST_POUCH)
- **Item ID**: `26053` (ITEM_QUEST_POUCH)

### **4. Lua API Available**

The following Lua methods are available on the server side:

```lua
-- Get the quest pouch container
local pouch = player:getQuestPouch()

-- Get item count by ID, action ID, or UID
local count = player:getQuestPouchItemCount(itemId)        -- Positive: item ID
local count = player:getQuestPouchItemCount(-actionId)     -- Negative: action ID
local count = player:getQuestPouchItemCount(uid)           -- UID > 65535

-- Add items to quest pouch
player:addQuestPouchItem(itemId, count)

-- Remove items from quest pouch
player:removeQuestPouchItem(itemId, count)   -- By item ID
player:removeQuestPouchItem(uid)             -- By UID

-- Get paginated list of items (for UI display)
local items = player:getQuestPouchItems(limit, offset)

-- Get total item count
local total = player:getQuestPouchTotalCount()
```

---

## 🔧 What OTC Client Needs to Implement

### **Reference Implementation: InventorySlotPurse**

**Base your implementation on `InventorySlotPurse`** that already exists in the OTC codebase. The quest pouch should work identically:

1. **Create a new component** similar to `InventorySlotPurse` but for Quest Pouch
2. **Handle slot 12** in the inventory system
3. **Display item ID 26053** as the quest pouch icon
4. **Support container pagination** when opened
5. **Handle right-click** to open the pouch

### **Implementation Checklist**

#### **1. Define Constants**
```lua
-- slots.lua or similar
CONST_SLOT_QUEST_POUCH = 12

-- items.lua or similar
ITEM_QUEST_POUCH = 26053
```

#### **2. Create QuestPouch Component**
- Copy/adapt `InventorySlotPurse` implementation
- Handle slot ID 12
- Use item ID 26053 for the pouch appearance
- Support opening as container with pagination

#### **3. Network Protocol Handling**
- Handle `InventoryItem` message with slot 12
- Parse the quest pouch item data from server
- Store reference to the container

#### **4. UI Integration**
- Add quest pouch to inventory panel (slot 12)
- Right-click opens the pouch container
- Display pagination controls (prev/next page)
- Show item count indicator

#### **5. Container Support**
- Quest pouch uses standard container protocol
- Supports pagination (100 slots capacity)
- Client can request specific pages via container navigation

#### **6. Item Metadata**
**IMPORTANT**: The client needs to recognize item ID `26053` in its item database (items.otb or equivalent). Add an entry for the quest pouch icon/appearance.

---

## 📦 Server Files Modified/created

For reference, here are all server files that were changed:

### **New Files:**
- `src/questpouch.h` - QuestPouch class header
- `src/questpouch.cpp` - QuestPouch class implementation
- `data/migrations/2.lua` - Database migration for quest pouch table
- `data/migrations/3.lua` - Migration chain terminator

### **Modified Files:**
- `src/const.h` - Added `ITEM_QUEST_POUCH = 26053`
- `src/creature.h` - Added `CONST_SLOT_QUEST_POUCH = 12`
- `src/declarations.h` - Added QuestPouch forward declarations
- `src/container.h` - Added virtual `getQuestPouch()` methods
- `src/player.h` - Added quest pouch member and getter
- `src/player.cpp` - Added questpouch.h include
- `src/protocolgame.cpp` - Added sending quest pouch to client
- `src/iologindata.cpp` - Added database save/load functionality
- `src/luascript.h` - Added Lua function declarations
- `src/luascript.cpp` - Added Lua function implementations

---

## 🎮 How It Works (Flow)

### **Login Flow:**
1. Player logs in
2. Server sends `sendInventoryItem(CONST_SLOT_QUEST_POUCH, questPouch)`
3. OTC receives inventory item for slot 12
4. OTC displays quest pouch icon in inventory panel
5. OTC can open pouch to view items (with pagination)

### **Opening Quest Pouch:**
1. Player right-clicks quest pouch in inventory
2. OTC sends `UseItem` or `OpenContainer` message to server
3. Server responds with container contents (paginated)
4. OTC displays container window with items
5. Player can navigate pages using pagination controls

### **Adding Items (Server-Side Only):**
1. Lua script calls `player:addQuestPouchItem(itemId, count)`
2. Server adds items to quest pouch
3. Server sends container update to client
4. OTC UI refreshes to show new items

---

## 🚨 Important Notes for OTC Implementation

### **1. Based on InventorySlotPurse**
The quest pouch should be implemented **exactly like InventorySlotPurse**:
- Same slot-based inventory system
- Same container opening mechanism
- Same pagination support
- Same right-click interaction

### **2. Item ID 26053**
The client **MUST** have item ID `26053` defined in its item metadata. Without this, the client won't know how to render the quest pouch icon.

### **3. Slot 12**
The inventory system needs to support slot 12. If the current inventory only goes up to slot 11 (Store Inbox), you'll need to extend it.

### **4. Pagination**
Quest pouch supports up to 100 item slots, so pagination is essential. Use the existing container pagination system already in OTC.

### **5. Read-Only for Players**
Players **cannot** add/remove items from the quest pouch manually. Only Lua scripts (game system) can modify it. The client should reflect this (no drag-and-drop).

---

## 📝 Example OTC Lua Usage (For Reference)

Once implemented, NPC scripts or game systems can use:

```lua
-- Example: Give player a quest item
function onQuestStart(player)
    player:addQuestPouchItem(12345, 1)
    player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Quest item added to your quest pouch!")
end

-- Example: Check if player has quest item
function hasQuestItem(player)
    return player:getQuestPouchItemCount(12345) > 0
end

-- Example: Remove quest item after completion
function onQuestComplete(player)
    if player:getQuestPouchItemCount(12345) > 0 then
        player:removeQuestPouchItem(12345, 1)
        player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Quest completed!")
    end
end
```

---

## ✅ Verification Checklist

After implementing, verify:

- [ ] Quest pouch appears in inventory slot 12
- [ ] Icon displays correctly (item ID 26053)
- [ ] Right-click opens the container
- [ ] Items display with pagination
- [ ] Can navigate between pages
- [ ] Items match what server has stored
- [ ] Cannot drag items in/out (read-only for players)
- [ ] Container updates when server adds/removes items
- [ ] Persists across relogs

---

## 🆘 Need Help?

If you need clarification on:
- Network protocol details
- Container pagination implementation
- How InventorySlotPurse works in this codebase
- Specific packet structures

Please ask and I'll provide the details!
