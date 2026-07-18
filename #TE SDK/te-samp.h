#pragma once

#include <cstdint>
#include <functional>

namespace te::sdk::helper::samp
{
	bool AddChatMessage(const char* text, uint32_t color = 0xFFFFFFFF);

	// Send a command to the server (e.g. "/kill")
	bool SendCommand(const char* command);

	// Get the server hostname
	const char* GetServerName();

	// Get local player name
	const char* GetPlayerName();

	// Get local player ID as its true uint16 width (0xFFFF = INVALID_PLAYER_ID on failure)
	uint16_t GetPlayerId();

	// Check if connected to a server (game state >= 5)
	bool IsGameLoaded();

	// Check if the player is fully spawned in-game
	bool IsPlayerSpawned();

	// Register a custom chat command handler
	// cmd should NOT include the '/' prefix
	// callback receives everything after the command name as params
	using ChatCommandCallback = std::function<void(const char* params)>;
	bool RegisterChatCommand(const char* cmd, ChatCommandCallback callback);
}
