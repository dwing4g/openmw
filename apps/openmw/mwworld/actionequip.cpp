#include "actionequip.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "class.hpp"
#include "inventorystore.hpp"

#include <components/esm3/loadweap.hpp>

namespace MWWorld
{
    ActionEquip::ActionEquip(const MWWorld::Ptr& object, bool force)
        : Action(false, object)
        , mForce(force)
    {
    }

    void ActionEquip::executeImp(const Ptr& actor)
    {
        MWWorld::Ptr object = getTarget();
        MWWorld::InventoryStore& invStore = actor.getClass().getInventoryStore(actor);

        if (actor != MWMechanics::getPlayer())
        {
            // player logic is handled in InventoryWindow::useItem
            if (object.getClass().hasItemHealth(object) && object.getCellRef().getCharge() == 0)
                return;
        }

        if (!mForce)
        {
            auto result = object.getClass().canBeEquipped(object, actor);

            // display error message if the player tried to equip something
            if (!result.second.empty() && actor == MWMechanics::getPlayer())
                MWBase::Environment::get().getWindowManager()->messageBox(result.second);

            switch (result.first)
            {
                case 0:
                    return;
                default:
                    break;
            }
        }

        // slots that this item can be equipped in
        std::pair<std::vector<int>, bool> slots = getTarget().getClass().getEquipmentSlots(getTarget());
        // Inventory use continues to select the primary weapon. Off-hand selection
        // is explicit through the combat binding or Lua's equipment-slot API.
        const bool primaryWeapon = object.getType() == ESM::Weapon::sRecordId && slots.first.size() > 1;
        if (primaryWeapon)
            slots.first.resize(1);
        if (slots.first.empty())
            return;

        // retrieve ContainerStoreIterator to the item
        MWWorld::ContainerStoreIterator it = invStore.begin();
        for (; it != invStore.end(); ++it)
        {
            if (*it == object)
            {
                break;
            }
        }

        if (it == invStore.end())
            throw std::runtime_error("ActionEquip can't find item " + object.getCellRef().getRefId().toDebugString());

        if (primaryWeapon && invStore.getSlot(InventoryStore::Slot_CarriedLeft) == it)
        {
            // Weapon hotkeys and AI may select the current off-hand item as the
            // primary weapon. Move it rather than sharing one instance between
            // both hands, and retain the iterator returned after restacking.
            const bool selected = invStore.getSelectedEnchantItem() == it;
            it = invStore.unequipSlot(InventoryStore::Slot_CarriedLeft, false);
            invStore.equip(InventoryStore::Slot_CarriedRight, it);
            if (selected)
                invStore.setSelectedEnchantItem(it);
            return;
        }

        // equip the item in the first free slot
        std::vector<int>::const_iterator slot = slots.first.begin();
        for (; slot != slots.first.end(); ++slot)
        {
            // if the item is equipped already, nothing to do
            if (invStore.getSlot(*slot) == it)
                return;

            if (invStore.getSlot(*slot) == invStore.end())
            {
                // slot is not occupied
                invStore.equip(*slot, it);
                break;
            }
        }

        // all slots are occupied -> cycle
        // move all slots one towards begin(), then equip the item in the slot that is now free
        if (slot == slots.first.end())
        {
            ContainerStoreIterator enchItem = invStore.getSelectedEnchantItem();
            bool reEquip = false;
            for (slot = slots.first.begin(); slot != slots.first.end(); ++slot)
            {
                invStore.unequipSlot(*slot, false);
                if (slot + 1 != slots.first.end())
                {
                    invStore.equip(*slot, invStore.getSlot(*(slot + 1)));
                }
                else
                {
                    invStore.equip(*slot, it);
                }

                // Fix for issue of selected enchated item getting remmoved on cycle
                if (invStore.getSlot(*slot) == enchItem)
                {
                    reEquip = true;
                }
            }
            if (reEquip)
            {
                invStore.setSelectedEnchantItem(enchItem);
            }
        }
    }
}
