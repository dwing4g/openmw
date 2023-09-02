#ifndef MWLUA_ACTOR_H
#define MWLUA_ACTOR_H

#include <sol/sol.hpp>

#include <components/esm/defs.hpp>
#include <components/lua/luastate.hpp>

#include "apps/openmw/mwworld/esmstore.hpp"
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadnpc.hpp>

#include "../context.hpp"
#include "../object.hpp"
namespace MWLua
{

    template <class T>
    void addActorServicesBindings(sol::usertype<T>& record, const Context& context)
    {
        record["servicesOffered"] = sol::readonly_property([](const T& rec) -> std::map<std::string_view, bool> {
            std::map<std::string_view, bool> providedServices;
            constexpr std::array<std::pair<int, std::string_view>, 19> serviceNames = { { { ESM::NPC::Spells,
                                                                                              "Spells" },
                { ESM::NPC::Spellmaking, "Spellmaking" }, { ESM::NPC::Enchanting, "Enchanting" },
                { ESM::NPC::Training, "Training" }, { ESM::NPC::Repair, "Repair" }, { ESM::NPC::AllItems, "Barter" },
                { ESM::NPC::Weapon, "Weapon" }, { ESM::NPC::Armor, "Armor" }, { ESM::NPC::Clothing, "Clothing" },
                { ESM::NPC::Books, "Books" }, { ESM::NPC::Ingredients, "Ingredients" }, { ESM::NPC::Picks, "Picks" },
                { ESM::NPC::Probes, "Probes" }, { ESM::NPC::Lights, "Lights" }, { ESM::NPC::Apparatus, "Apparatus" },
                { ESM::NPC::RepairItem, "RepairItem" }, { ESM::NPC::Misc, "Misc" }, { ESM::NPC::Potions, "Potions" },
                { ESM::NPC::MagicItems, "MagicItems" } } };

            int services = rec.mAiData.mServices;
            if constexpr (std::is_same_v<T, ESM::NPC>)
            {
                if (rec.mFlags & ESM::NPC::Autocalc)
                    services = MWBase::Environment::get()
                                   .getWorld()
                                   ->getStore()
                                   .get<ESM::Class>()
                                   .find(rec.mClass)
                                   ->mData.mServices;
            }
            for (const auto& [flag, name] : serviceNames)
            {
                providedServices[name] = (services & flag) != 0;
            }
            providedServices["Travel"] = !rec.getTransport().empty();
            return providedServices;
        });
    }
}
#endif // MWLUA_ACTOR_H
