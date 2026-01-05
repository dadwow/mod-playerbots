#include "ArenaQueueManager.h"

#include <cstdlib>

#include "ArenaTeamMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "PlayerbotFactory.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "World.h"

void ArenaQueueManager::OnPlayerQueueArena(Player* player, uint8 arenaType, bool asGroup)
{
    if (!m_enabled || !player)
        return;

    LOG_INFO("playerbots", "PlusCraft Arena: Player {} queued for {}v{} arena", player->GetName(), arenaType,
             arenaType);

    // Calculate total bots needed for this arena type
    // 2v2 = 4 total, 3v3 = 6 total, 5v5 = 10 total
    uint8 totalPlayers = arenaType * 2;

    // Get player's rating for matchmaking
    uint32 playerRating = 1500;  // Default
    // TODO: Get actual rating from arena team

    FillArenaQueue(arenaType, totalPlayers, playerRating);
}

void ArenaQueueManager::FillArenaQueue(uint8 arenaType, uint8 neededCount, uint32 avgRating)
{
    if (!m_enabled)
        return;

    LOG_INFO("playerbots", "PlusCraft Arena: Need {} bots for {}v{} arena (rating bracket: {})", neededCount, arenaType,
             arenaType, avgRating);

    RandomPlayerbotMgr* randomMgr = RandomPlayerbotMgr::instance();
    if (!randomMgr)
    {
        LOG_ERROR("playerbots", "PlusCraft Arena: RandomPlayerbotMgr not available");
        return;
    }

    // Step 1: Find bots already in the world
    std::vector<Player*> availableBots;
    for (Player* bot : randomMgr->GetPlayers())
    {
        if (IsBotSuitableForArena(bot, arenaType, avgRating))
            availableBots.push_back(bot);
    }

    LOG_INFO("playerbots", "PlusCraft Arena: Found {} suitable bots in world", availableBots.size());

    // Step 2: If not enough, spawn more random bots
    uint32 botsNeeded = neededCount > availableBots.size() ? (neededCount - availableBots.size()) : 0;

    if (botsNeeded > 0)
    {
        LOG_INFO("playerbots", "PlusCraft Arena: Need to spawn {} additional bots", botsNeeded);

        // Trigger RandomPlayerbotMgr to add more bots
        for (uint32 i = 0; i < botsNeeded; ++i)
        {
            randomMgr->AddRandomBots();
        }

        // Note: Newly added bots will be picked up on next Update() cycle
    }

    // Step 3: Prepare and queue existing bots
    uint8 botsAdded = 0;
    std::vector<ObjectGuid> queuedBots;

    for (Player* bot : availableBots)
    {
        if (botsAdded >= neededCount)
            break;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;

        LOG_INFO("playerbots", "PlusCraft Arena: Preparing bot {} for arena", bot->GetName());

        // Prepare bot using PlayerbotAI commands
        Event emptyEvent;

        // Run maintenance (talents, spells, etc.)
        LOG_DEBUG("playerbots", "PlusCraft Arena: Running maintenance for {}", bot->GetName());
        botAI->DoSpecificAction("maintenance", emptyEvent, true);

        // Auto-gear the bot
        LOG_DEBUG("playerbots", "PlusCraft Arena: Auto-gearing {}", bot->GetName());
        botAI->DoSpecificAction("autogear", emptyEvent, true);

        // Auto-set talents for arena spec
        LOG_DEBUG("playerbots", "PlusCraft Arena: Setting talents for {}", bot->GetName());
        botAI->DoSpecificAction("talents autopick", emptyEvent, true);

        // Add to queue tracking
        queuedBots.push_back(bot->GetGUID());
        botsAdded++;

        // TODO: Actually queue bot in arena system
        // bg->AddPlayer(bot);

        LOG_INFO("playerbots", "PlusCraft Arena: Bot {} ready for {}v{} arena", bot->GetName(), arenaType, arenaType);
    }

    if (!queuedBots.empty())
    {
        m_botsInQueue[arenaType] = queuedBots;
    }

    LOG_INFO("playerbots", "PlusCraft Arena: Prepared {}/{} bots for queue", botsAdded, neededCount);
}

void ArenaQueueManager::RemoveBotsFromArena(uint32 instanceId)
{
    auto it = m_botsInArena.find(instanceId);
    if (it == m_botsInArena.end())
        return;

    LOG_INFO("playerbots", "PlusCraft Arena: Removing {} bots from arena {}", it->second.size(), instanceId);

    for (ObjectGuid botGuid : it->second)
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld())
            continue;

        if (Battleground* bg = bot->GetBattleground())
        {
            bg->RemovePlayerAtLeave(botGuid, false, true);
            LOG_DEBUG("playerbots", "PlusCraft Arena: Removed bot from arena");
        }
    }

    m_botsInArena.erase(it);
}

bool ArenaQueueManager::IsBotSuitableForArena(Player* bot, uint8 arenaType, uint32 targetRating)
{
    if (!bot || !bot->IsInWorld())
        return false;

    // Must be level 80 for rated arenas
    if (bot->GetLevel() < 80)
        return false;

    // Must not be in combat
    if (bot->IsInCombat())
        return false;

    // Must not already be in a battleground or arena
    if (bot->InBattleground())
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    // Check if bot's rating is within acceptable range (±200)
    uint32 botRating = 1500;  // Default rating
    // TODO: Get actual bot arena rating

    if (std::abs(static_cast<int32>(botRating) - static_cast<int32>(targetRating)) > 200)
        return false;

    return true;
}

void ArenaQueueManager::Update()
{
    if (!m_enabled)
        return;

    // Clean up bots that have left queues
    for (auto queueIt = m_botsInQueue.begin(); queueIt != m_botsInQueue.end();)
    {
        auto& bots = queueIt->second;

        // Remove bots no longer in queue
        bots.erase(std::remove_if(bots.begin(), bots.end(),
                                  [](ObjectGuid guid)
                                  {
                                      Player* bot = ObjectAccessor::FindPlayer(guid);
                                      return !bot || !bot->IsInWorld() || bot->InBattleground();
                                  }),
                   bots.end());

        // Remove empty queues
        if (bots.empty())
            queueIt = m_botsInQueue.erase(queueIt);
        else
            ++queueIt;
    }

    // Clean up finished arenas
    for (auto arenaIt = m_botsInArena.begin(); arenaIt != m_botsInArena.end();)
    {
        uint32 instanceId = arenaIt->first;
        Battleground* bg = sBattlegroundMgr->GetBattleground(instanceId);

        // If arena is finished or doesn't exist, clean up
        if (!bg || bg->GetStatus() == STATUS_WAIT_LEAVE)
        {
            LOG_DEBUG("playerbots", "PlusCraft Arena: Cleaning up finished arena {}", instanceId);
            arenaIt = m_botsInArena.erase(arenaIt);
        }
        else
        {
            ++arenaIt;
        }
    }
}
