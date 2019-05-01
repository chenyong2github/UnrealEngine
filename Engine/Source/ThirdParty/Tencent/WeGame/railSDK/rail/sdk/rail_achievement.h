// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_ACHIEVEMENT_H
#define RAIL_SDK_RAIL_ACHIEVEMENT_H

#include "rail/sdk/base/rail_component.h"
#include "rail/sdk/base/rail_string.h"
#include "rail/sdk/rail_event.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailPlayerAchievement;
class IRailGlobalAchievement;

class IRailAchievementHelper {
  public:
    virtual IRailPlayerAchievement* CreatePlayerAchievement(const RailID& player) = 0;
    virtual IRailGlobalAchievement* GetGlobalAchievement() = 0;
};

class IRailPlayerAchievement : public IRailComponent {
  public:
    virtual RailID GetRailID() = 0;

    // trigger event PlayerAchievementReceived
    virtual RailResult AsyncRequestAchievement(const RailString& user_data) = 0;

    virtual RailResult HasAchieved(const RailString& name, bool* achieved) = 0;

    // infos contains some key-value pairs, the key may be expanded in future.
    // infos is formatted using JSON, such as:
    //  {
    //  "name": "name",
    //  "description": "desc",
    //  "display_name": "display name",
    //  "achieved": 1,
    //  "achieved_time":123456, // seconds since January 1 1970
    //  "icon_index": 1,
    //  "icon_url": "http://...",
    //  "unachieved_icon_url": "http://...",
    //  "is_process" : true,
    //  "hidden" : false,
    //  "cur_value" : 100,
    //  "unlock_value" : 100
    //  }
    virtual RailResult GetAchievementInfo(const RailString& name, RailString* achievement_info) = 0;

    // trigger event PlayerAchievementStored
    // max_value could be indicated by game every time or configured at rail back-end server.
    // we recommend that you configure it at back-end while the max_value was inflexible.
    // return kErrorAchievementNotMyAchievement via trigger other player's achievement
    virtual RailResult AsyncTriggerAchievementProgress(const RailString& name,
                        uint32_t current_value,
                        uint32_t max_value = 0,
                        const RailString& user_data = "") = 0;

    // return kErrorAchievementNotMyAchievement via make other player's achievement
    virtual RailResult MakeAchievement(const RailString& name) = 0;

    // return kErrorAchievementNotMyAchievement via cancel other player's achievement
    virtual RailResult CancelAchievement(const RailString& name) = 0;

    // trigger event PlayerAchievementStored.
    virtual RailResult AsyncStoreAchievement(const RailString& user_data) = 0;

    // return kErrorAchievementNotMyAchievement via reset other player's achievement
    virtual RailResult ResetAllAchievements() = 0;

    // return all achievements name, include locked and unlocked
    virtual RailResult GetAllAchievementsName(RailArray<RailString>* names) = 0;
};

class IRailGlobalAchievement : public IRailComponent {
  public:
    // trigger event GlobalAchievementReceived
    virtual RailResult AsyncRequestAchievement(const RailString& user_data) = 0;

    virtual RailResult GetGlobalAchievedPercent(const RailString& name, double* percent) = 0;

    // index's range is [0, GlobalAchievementReceived::count),
    // and the first one is the most achieved achievement.
    // return kErrorAchievementOutofRange if there is not any more achievement
    virtual RailResult GetGlobalAchievedPercentDescending(int32_t index,
                        RailString* name,
                        double* percent) = 0;
};

namespace rail_event {

struct PlayerAchievementReceived
    : public RailEvent<kRailEventAchievementPlayerAchievementReceived> {
    PlayerAchievementReceived() {}
};

struct PlayerAchievementStored : public RailEvent<kRailEventAchievementPlayerAchievementStored> {
    PlayerAchievementStored() {
        group_achievement = false;
        current_progress = 0;
        max_progress = 0;
    }

    bool group_achievement;
    RailString achievement_name;
    uint32_t current_progress;
    uint32_t max_progress;
};

struct GlobalAchievementReceived
    : public RailEvent<kRailEventAchievementGlobalAchievementReceived> {
    GlobalAchievementReceived() { count = 0; }

    int32_t count;  // number of all global achievements
};

}  // namespace rail_event

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_ACHIEVEMENT_H
