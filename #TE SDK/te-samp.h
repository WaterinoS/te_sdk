#pragma once

#include <cstdint>
#include <functional>

namespace te::sdk::helper::samp
{
	// SA-MP's own "no player" sentinel
	constexpr uint16_t kInvalidPlayerId = 0xFFFF;

	bool AddChatMessage(const char* text, uint32_t color = 0xFFFFFFFF);

	// Send a command to the server (e.g. "/kill")
	bool SendCommand(const char* command);

	// Get the server hostname.
	// The returned pointer is a snapshot owned by a thread-local buffer: it
	// stays valid until the next GetServerName() call on the same thread, and
	// cannot be invalidated by the game underneath you.
	const char* GetServerName();

	// Get local player name. Same thread-local snapshot semantics as above.
	const char* GetPlayerName();

	// Get local player ID as its true uint16 width
	// (kInvalidPlayerId == 0xFFFF on failure)
	uint16_t GetPlayerId();

	// Check if connected to a server (game state >= 5)
	bool IsGameLoaded();

	// Check if the player is fully spawned in-game
	bool IsPlayerSpawned();

	// ---- custom chat commands ----

	// cmd should NOT include the '/' prefix.
	// callback receives everything after the command name as params, and is
	// invoked on the game thread with no SDK lock held - it is safe to call
	// back into the SDK (including RegisterChatCommand) from inside it.
	using ChatCommandCallback = std::function<void(const char* params)>;

	// Register a custom chat command handler. Matching is case-insensitive.
	// Registering an already-registered command replaces its handler.
	bool RegisterChatCommand(const char* cmd, ChatCommandCallback callback);

	// Remove a previously registered command. Returns false if it was not
	// registered. Commands MUST be unregistered before the module that owns
	// the callback is unloaded.
	bool UnregisterChatCommand(const char* cmd);

	// Remove every registered command and uninstall the command hook.
	void ClearChatCommands();

	// True when `cmd` currently has a handler
	bool IsChatCommandRegistered(const char* cmd);
}
