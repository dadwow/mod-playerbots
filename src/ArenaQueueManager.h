#ifndef _PLUSCRAFT_ARENAQUEUEMANAGER_H
#define _PLUSCRAFT_ARENAQUEUEMANAGER_H

#include <map>
#include <vector>

#include "Common.h"
#include "Player.h"
#include "Playerbots.h"

/**
 * @brief Manages automatic bot queue filling for arenas
 *
 * When real players queue for arena, this manager automatically
 * adds appropriate bots to fill the queue and enable faster matches.
 * Bots are selected based on rating, class balance, and availability.
 */
class ArenaQueueManager
{
public:
    static ArenaQueueManager& instance()
    {
        static ArenaQueueManager instance;
        return instance;
    }

    /**
     * Called when a player queues for arena
     * @param player The player who queued
     * @param arenaType Arena type (2v2=2, 3v3=3, 5v5=5)
     * @param asGroup Whether queued as a group
     */
    void OnPlayerQueueArena(Player* player, uint8 arenaType, bool asGroup);

    /**
     * Fill arena queue with bots
     * @param arenaType Arena type (2, 3, or 5)
     * @param neededCount How many bots are needed
     * @param avgRating Target rating for bot selection
     */
    void FillArenaQueue(uint8 arenaType, uint8 neededCount, uint32 avgRating);

    /**
     * Remove bots from arena when real players join
     * @param instanceId The arena instance ID
     */
    void RemoveBotsFromArena(uint32 instanceId);

    /**
     * Check if bot should be added to arena queue
     * @param bot The bot to check
     * @param arenaType Arena type
     * @param targetRating Target rating to match
     * @return true if bot is suitable
     */
    bool IsBotSuitableForArena(Player* bot, uint8 arenaType, uint32 targetRating);

    /**
     * Update - called periodically to manage bot queue status
     */
    void Update();

    /**
     * Enable/disable arena auto-queue feature
     * @param enabled true to enable
     */
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

private:
    ArenaQueueManager() : m_enabled(true) {}
    ~ArenaQueueManager() = default;

    // Prevent copying
    ArenaQueueManager(const ArenaQueueManager&) = delete;
    ArenaQueueManager& operator=(const ArenaQueueManager&) = delete;

    // Track bots currently in arena queues
    std::map<uint8, std::vector<ObjectGuid>> m_botsInQueue;  // arenaType -> bot GUIDs

    // Track arena instances with bots
    std::map<uint32, std::vector<ObjectGuid>> m_botsInArena;  // instanceId -> bot GUIDs

    bool m_enabled;
};

#define sArenaQueueMgr ArenaQueueManager::instance()

#endif
