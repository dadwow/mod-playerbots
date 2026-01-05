#ifndef _PLUSCRAFT_BATTLEGROUNDBALANCER_H
#define _PLUSCRAFT_BATTLEGROUNDBALANCER_H

#include <map>
#include <vector>

#include "Battleground.h"
#include "Common.h"
#include "Player.h"
#include "Playerbots.h"

/**
 * @brief Manages automatic team balancing in battlegrounds
 *
 * Automatically adds bots to battlegrounds to balance teams when
 * there's a population imbalance. Bots are distributed based on
 * class balance and faction needs.
 */
class BattlegroundBalancer
{
public:
    static BattlegroundBalancer& instance()
    {
        static BattlegroundBalancer instance;
        return instance;
    }

    /**
     * Called when a player joins a battleground queue
     * @param player The player who queued
     * @param bgTypeId Battleground type
     */
    void OnPlayerQueueBG(Player* player, BattlegroundTypeId bgTypeId);

    /**
     * Balance teams in a specific battleground
     * @param bg The battleground to balance
     */
    void BalanceBattleground(Battleground* bg);

    /**
     * Add bots to balance BG teams
     * @param bgTypeId Battleground type
     * @param faction Team that needs bots (ALLIANCE or HORDE)
     * @param count Number of bots to add
     */
    void AddBotsToTeam(BattlegroundTypeId bgTypeId, TeamId faction, uint8 count);

    /**
     * Remove bots when real players join
     * @param bg The battleground
     * @param faction Team to remove bots from
     * @param count Number of bots to remove
     */
    void RemoveBotsFromTeam(Battleground* bg, TeamId faction, uint8 count);

    /**
     * Check if bot is suitable for battleground
     * @param bot The bot to check
     * @param bgTypeId Battleground type
     * @param faction Required faction
     * @return true if bot is suitable
     */
    bool IsBotSuitableForBG(Player* bot, BattlegroundTypeId bgTypeId, TeamId faction);

    /**
     * Get class distribution for team balancing
     * @param bg The battleground
     * @param faction Team to check
     * @return Map of class -> count
     */
    std::map<uint8, uint8> GetClassDistribution(Battleground* bg, TeamId faction);

    /**
     * Update - called periodically to check battleground balance
     */
    void Update();

    /**
     * Enable/disable battleground auto-balance feature
     * @param enabled true to enable
     */
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    /**
     * Set minimum real players before adding bots
     * @param minPlayers Minimum player count
     */
    void SetMinRealPlayers(uint8 minPlayers) { m_minRealPlayers = minPlayers; }
    uint8 GetMinRealPlayers() const { return m_minRealPlayers; }

    /**
     * Set maximum bots per battleground
     * @param maxBots Maximum bot count
     */
    void SetMaxBots(uint8 maxBots) { m_maxBots = maxBots; }
    uint8 GetMaxBots() const { return m_maxBots; }

private:
    BattlegroundBalancer() : m_enabled(true), m_minRealPlayers(5), m_maxBots(15) {}
    ~BattlegroundBalancer() = default;

    // Prevent copying
    BattlegroundBalancer(const BattlegroundBalancer&) = delete;
    BattlegroundBalancer& operator=(const BattlegroundBalancer&) = delete;

    // Track bots in battlegrounds
    std::map<uint32, std::vector<ObjectGuid>> m_botsInBG;  // bgInstanceId -> bot GUIDs

    bool m_enabled;
    uint8 m_minRealPlayers;
    uint8 m_maxBots;
};

#define sBGBalancer BattlegroundBalancer::instance()

#endif
