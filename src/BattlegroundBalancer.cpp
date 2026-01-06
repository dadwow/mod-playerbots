#include "BattlegroundBalancer.h"

#include <cstdlib>

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "World.h"

void BattlegroundBalancer::OnPlayerQueueBG(Player* player, BattlegroundTypeId bgTypeId)
{
    if (!m_enabled || !player || !sPlayerbotAIConfig->pluscraftEnabled || !sPlayerbotAIConfig->bgDynamicSpawn)
    {
        if (player && !sPlayerbotAIConfig->pluscraftEnabled)
            LOG_DEBUG("playerbots", "PlusCraft BG: Ignoring queue - PlusCraft disabled");
        else if (player && !sPlayerbotAIConfig->bgDynamicSpawn)
            LOG_DEBUG("playerbots", "PlusCraft BG: Ignoring queue - BG dynamic spawn disabled");
        return;
    }

    LOG_INFO("playerbots", "╔════════════════════════════════════════════════╗");
    LOG_INFO("playerbots", "║  PlusCraft: Real Player Queued for BG          ║");
    LOG_INFO("playerbots", "╟────────────────────────────────────────────────╢");
    LOG_INFO("playerbots", "║  Player: {:<38} ║", player->GetName());
    LOG_INFO("playerbots", "║  Player Level: {:<32} ║", player->GetLevel());
    LOG_INFO("playerbots", "║  BG Type: {:<37} ║", bgTypeId);

    // Get minimum players needed for this BG type
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bgTemplate)
    {
        LOG_ERROR("playerbots", "║  ERROR: BG template not found!                 ║");
        LOG_INFO("playerbots", "╚════════════════════════════════════════════════╝");
        return;
    }

    // Get the player's level bracket for this BG
    uint32 mapId = bgTemplate->GetMapId();
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(mapId, player->GetLevel());
    if (!bracketEntry)
    {
        LOG_ERROR("playerbots", "║  ERROR: No bracket found for level {}!        ║", player->GetLevel());
        LOG_INFO("playerbots", "╚════════════════════════════════════════════════╝");
        return;
    }

    uint32 minLevel = bracketEntry->minLevel;
    uint32 maxLevel = bracketEntry->maxLevel;
    BattlegroundBracketId bracketId = bracketEntry->GetBracketId();

    uint32 minPlayers = bgTemplate->GetMinPlayersPerTeam() * 2; // Total for both teams
    uint32 maxPlayers = bgTemplate->GetMaxPlayersPerTeam() * 2;

    LOG_INFO("playerbots", "║  Level Bracket: {}-{} (ID: {})                 ║", minLevel, maxLevel, bracketId);
    LOG_INFO("playerbots", "║  Min Players: {:<32} ║", minPlayers);
    LOG_INFO("playerbots", "║  Max Players: {:<32} ║", maxPlayers);

    // Calculate how many bots we need to spawn to fill the BG
    // We'll spawn enough to reach minimum, with balanced teams
    uint32 botsToSpawn = minPlayers - 1; // -1 for the real player who just queued

    LOG_INFO("playerbots", "║  Bots to spawn: {:<28} ║", botsToSpawn);
    LOG_INFO("playerbots", "╟────────────────────────────────────────────────╢");

    if (botsToSpawn > 0 && botsToSpawn <= maxPlayers)
    {
        LOG_INFO("playerbots", "║  Requesting bot spawn from RandomPlayerbotMgr  ║");
        LOG_INFO("playerbots", "╚════════════════════════════════════════════════╝");

        // Use RandomPlayerbotMgr to spawn bots dynamically with BG type and level range
        uint32 spawned = sRandomPlayerbotMgr->AddRandomBotsForPvP(botsToSpawn, bgTypeId, minLevel, maxLevel);

        LOG_INFO("playerbots", "PlusCraft BG: Result - spawned {}/{} bots", spawned, botsToSpawn);

        if (spawned < botsToSpawn)
        {
            LOG_WARN("playerbots", "PlusCraft BG: WARNING - Only spawned {}/{} requested bots", spawned, botsToSpawn);
        }

        // TODO: Make the spawned bots actually queue for this specific BG
        // This might require additional logic in RandomPlayerbotMgr to queue bots after spawning
    }
    else
    {
        LOG_WARN("playerbots", "║  SKIPPED: Invalid bot count ({})              ║", botsToSpawn);
        LOG_INFO("playerbots", "╚════════════════════════════════════════════════╝");
    }
}

void BattlegroundBalancer::BalanceBattleground(Battleground* bg)
{
    if (!m_enabled || !bg)
        return;

    uint32 allianceCount = bg->GetPlayersCountByTeam(TEAM_ALLIANCE);
    uint32 hordeCount = bg->GetPlayersCountByTeam(TEAM_HORDE);

    LOG_INFO("playerbots", "PlusCraft BG: Balancing BG {} - Alliance: {}, Horde: {}", bg->GetInstanceID(),
             allianceCount, hordeCount);

    // Calculate imbalance
    int32 difference = static_cast<int32>(allianceCount) - static_cast<int32>(hordeCount);

    if (std::abs(difference) <= 1)
    {
        LOG_DEBUG("playerbots", "PlusCraft BG: Teams are balanced (diff: {})", difference);
        return;
    }

    // Determine which team needs bots
    TeamId needsBotsTeam;
    uint8 botsNeeded;

    if (difference > 0)
    {
        // Alliance has more, add to Horde
        needsBotsTeam = TEAM_HORDE;
        botsNeeded = (difference + 1) / 2;
    }
    else
    {
        // Horde has more, add to Alliance
        needsBotsTeam = TEAM_ALLIANCE;
        botsNeeded = (std::abs(difference) + 1) / 2;
    }

    // Check current bot count
    uint32 currentBots = m_botsInBG[bg->GetInstanceID()].size();
    if (currentBots >= m_maxBots)
    {
        LOG_WARN("playerbots", "PlusCraft BG: Max bots ({}) reached for BG {}", m_maxBots, bg->GetInstanceID());
        return;
    }

    botsNeeded = std::min<uint8>(botsNeeded, m_maxBots - currentBots);

    if (botsNeeded > 0)
    {
        LOG_INFO("playerbots", "PlusCraft BG: Adding {} bots to {} team", botsNeeded,
                 needsBotsTeam == TEAM_ALLIANCE ? "Alliance" : "Horde");

        AddBotsToTeam(bg->GetBgTypeID(), needsBotsTeam, botsNeeded);
    }
}

void BattlegroundBalancer::AddBotsToTeam(BattlegroundTypeId bgTypeId, TeamId faction, uint8 count)
{
    if (!m_enabled || count == 0)
        return;

    LOG_INFO("playerbots", "PlusCraft BG: Adding {} bots to {} for BG type {}", count,
             faction == TEAM_ALLIANCE ? "Alliance" : "Horde", bgTypeId);

    RandomPlayerbotMgr* randomMgr = RandomPlayerbotMgr::instance();
    if (!randomMgr)
    {
        LOG_ERROR("playerbots", "PlusCraft BG: RandomPlayerbotMgr not available");
        return;
    }

    // Step 1: Find suitable bots already in world
    std::vector<Player*> availableBots;
    for (Player* bot : randomMgr->GetPlayers())
    {
        if (IsBotSuitableForBG(bot, bgTypeId, faction))
            availableBots.push_back(bot);
    }

    LOG_INFO("playerbots", "PlusCraft BG: Found {} suitable bots in world", availableBots.size());

    // Step 2: If not enough, spawn more
    uint32 botsNeeded = count > availableBots.size() ? (count - availableBots.size()) : 0;

    if (botsNeeded > 0)
    {
        LOG_INFO("playerbots", "PlusCraft BG: Need to spawn {} additional bots", botsNeeded);

        // Use dynamic spawning if PlusCraft is enabled
        if (sPlayerbotAIConfig->pluscraftEnabled && sPlayerbotAIConfig->bgDynamicSpawn)
        {
            uint32 spawned = randomMgr->AddRandomBotsForPvP(botsNeeded);
            LOG_INFO("playerbots", "PlusCraft BG: Dynamically spawned {} bots for PvP", spawned);
        }
        else
        {
            // Fallback: Trigger RandomPlayerbotMgr to add more bots (limited by MaxRandomBots)
            for (uint32 i = 0; i < botsNeeded; ++i)
            {
                randomMgr->AddRandomBots();
            }
        }
    }

    // Step 3: Prepare and add bots
    uint8 botsAdded = 0;
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return;

    // Get class distribution for balance
    std::map<uint8, uint8> classDistribution = GetClassDistribution(bg, faction);

    for (Player* bot : availableBots)
    {
        if (botsAdded >= count)
            break;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;

        // Check class balance - avoid too many of same class
        uint8 botClass = bot->getClass();
        if (classDistribution[botClass] >= 3)
        {
            LOG_DEBUG("playerbots", "PlusCraft BG: Skipping bot {} - too many {} already", bot->GetName(), botClass);
            continue;
        }

        LOG_INFO("playerbots", "PlusCraft BG: Preparing bot {} (class: {}) for BG", bot->GetName(), botClass);

        // Prepare bot for battleground
        Event emptyEvent;

        // Run maintenance
        LOG_DEBUG("playerbots", "PlusCraft BG: Running maintenance for {}", bot->GetName());
        botAI->DoSpecificAction("maintenance", emptyEvent, true);

        // Auto-gear
        LOG_DEBUG("playerbots", "PlusCraft BG: Auto-gearing {}", bot->GetName());
        botAI->DoSpecificAction("autogear", emptyEvent, true);

        // Set talents
        LOG_DEBUG("playerbots", "PlusCraft BG: Setting talents for {}", bot->GetName());
        botAI->DoSpecificAction("talents autopick", emptyEvent, true);

        // Add to BG tracking
        m_botsInBG[bg->GetInstanceID()].push_back(bot->GetGUID());
        classDistribution[botClass]++;
        botsAdded++;

        // TODO: Actually add bot to battleground
        // bg->AddPlayer(bot);

        LOG_INFO("playerbots", "PlusCraft BG: Bot {} ready for battleground", bot->GetName());
    }

    LOG_INFO("playerbots", "PlusCraft BG: Prepared {}/{} bots for team", botsAdded, count);
}

void BattlegroundBalancer::RemoveBotsFromTeam(Battleground* bg, TeamId faction, uint8 count)
{
    if (!bg || count == 0)
        return;

    auto it = m_botsInBG.find(bg->GetInstanceID());
    if (it == m_botsInBG.end())
        return;

    LOG_INFO("playerbots", "PlusCraft BG: Removing {} bots from {} in BG {}", count,
             faction == TEAM_ALLIANCE ? "Alliance" : "Horde", bg->GetInstanceID());

    uint8 removed = 0;
    auto& bots = it->second;

    for (auto botIt = bots.begin(); botIt != bots.end() && removed < count;)
    {
        Player* bot = ObjectAccessor::FindPlayer(*botIt);
        if (bot && bot->GetTeamId() == faction)
        {
            bg->RemovePlayerAtLeave(bot);
            botIt = bots.erase(botIt);
            removed++;
            LOG_DEBUG("playerbots", "PlusCraft BG: Removed bot from team");
        }
        else
        {
            ++botIt;
        }
    }

    LOG_INFO("playerbots", "PlusCraft BG: Removed {}/{} bots", removed, count);
}

bool BattlegroundBalancer::IsBotSuitableForBG(Player* bot, BattlegroundTypeId bgTypeId, TeamId faction)
{
    if (!bot || !bot->IsInWorld())
        return false;

    // Check faction
    if (bot->GetTeamId() != faction)
        return false;

    // Must not already be in BG
    if (bot->InBattleground())
        return false;

    // Must not be in combat
    if (bot->IsInCombat())
        return false;

    // Check level requirements
    uint8 minLevel = 10;
    uint8 maxLevel = 80;

    switch (bgTypeId)
    {
        case BATTLEGROUND_AV:
        case BATTLEGROUND_AB:
        case BATTLEGROUND_EY:
        case BATTLEGROUND_WS:
            minLevel = 10;
            break;
        case BATTLEGROUND_SA:
        case BATTLEGROUND_IC:
            minLevel = 75;
            break;
        default:
            break;
    }

    if (bot->GetLevel() < minLevel || bot->GetLevel() > maxLevel)
        return false;

    return true;
}

std::map<uint8, uint8> BattlegroundBalancer::GetClassDistribution(Battleground* bg, TeamId faction)
{
    std::map<uint8, uint8> distribution;

    if (!bg)
        return distribution;

    // Count classes in team
    for (auto const& [guid, player] : bg->GetPlayers())
    {
        if (player && player->GetTeamId() == faction)
        {
            distribution[player->getClass()]++;
        }
    }

    return distribution;
}

void BattlegroundBalancer::Update()
{
    if (!m_enabled)
        return;

    // Check all active battlegrounds for rebalancing
    for (uint8 i = BATTLEGROUND_AV; i < MAX_BATTLEGROUND_TYPE_ID; ++i)
    {
        BattlegroundTypeId bgTypeId = BattlegroundTypeId(i);
        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);

        if (bg && bg->GetStatus() == STATUS_IN_PROGRESS)
        {
            uint32 allianceCount = bg->GetPlayersCountByTeam(TEAM_ALLIANCE);
            uint32 hordeCount = bg->GetPlayersCountByTeam(TEAM_HORDE);

            int32 difference = std::abs(static_cast<int32>(allianceCount) - static_cast<int32>(hordeCount));

            // Rebalance if difference > 2
            if (difference > 2)
            {
                BalanceBattleground(bg);
            }
        }
    }

    // Clean up finished battlegrounds
    for (auto it = m_botsInBG.begin(); it != m_botsInBG.end();)
    {
        Battleground* bg = sBattlegroundMgr->GetBattleground(it->first, BATTLEGROUND_TYPE_NONE);
        if (!bg || bg->GetStatus() == STATUS_WAIT_LEAVE)
        {
            LOG_DEBUG("playerbots", "PlusCraft BG: Cleaning up finished BG {}, removed {} bots", it->first,
                      it->second.size());
            it = m_botsInBG.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void BattlegroundBalancer::OnBattlegroundEnd(Battleground* bg)
{
    if (!m_enabled || !bg)
        return;

    auto it = m_botsInBG.find(bg->GetInstanceID());
    if (it == m_botsInBG.end())
        return;

    LOG_INFO("playerbots", "PlusCraft BG: Battleground {} ended, marking {} bots for cleanup", bg->GetInstanceID(),
             it->second.size());

    // Tell RandomPlayerbotMgr to check these bots for cleanup
    // They'll be removed during the next periodic cleanup if idle
    m_botsInBG.erase(it);
}
