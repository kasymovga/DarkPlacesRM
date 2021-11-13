#include "quakedef.h"
#include "discord.h"
#include "thread.h"
#include <stdbool.h>
#include <time.h>
#include <string.h>

static cvar_t integration_discord_enable = {CVAR_SAVE, "integration_discord_enable","0", "Enable discord support"};
static cvar_t integration_discord_client_id = {0, "integration_discord_client_id", "418559331265675294", "Discord client id"};
static cvar_t integration_discord_game_command = {0, "integration_discord_game_command", "", "Discord game command"};

#define DISCORD_VERSION 2
#define DISCORD_APPLICATION_MANAGER_VERSION 1
#define DISCORD_USER_MANAGER_VERSION 1
#define DISCORD_IMAGE_MANAGER_VERSION 1
#define DISCORD_ACTIVITY_MANAGER_VERSION 1
#define DISCORD_RELATIONSHIP_MANAGER_VERSION 1
#define DISCORD_LOBBY_MANAGER_VERSION 1
#define DISCORD_NETWORK_MANAGER_VERSION 1
#define DISCORD_OVERLAY_MANAGER_VERSION 1
#define DISCORD_STORAGE_MANAGER_VERSION 1
#define DISCORD_STORE_MANAGER_VERSION 1
#define DISCORD_VOICE_MANAGER_VERSION 1
#define DISCORD_ACHIEVEMENT_MANAGER_VERSION 1

enum EDiscordUserFlag {
	DiscordUserFlag_Partner = 2,
	DiscordUserFlag_HypeSquadEvents = 4,
	DiscordUserFlag_HypeSquadHouse1 = 64,
	DiscordUserFlag_HypeSquadHouse2 = 128,
	DiscordUserFlag_HypeSquadHouse3 = 256,
};

enum EDiscordPremiumType {
	DiscordPremiumType_None = 0,
	DiscordPremiumType_Tier1 = 1,
	DiscordPremiumType_Tier2 = 2,
};

enum EDiscordResult {
	DiscordResult_Ok = 0,
	DiscordResult_ServiceUnavailable = 1,
	DiscordResult_InvalidVersion = 2,
	DiscordResult_LockFailed = 3,
	DiscordResult_InternalError = 4,
	DiscordResult_InvalidPayload = 5,
	DiscordResult_InvalidCommand = 6,
	DiscordResult_InvalidPermissions = 7,
	DiscordResult_NotFetched = 8,
	DiscordResult_NotFound = 9,
	DiscordResult_Conflict = 10,
	DiscordResult_InvalidSecret = 11,
	DiscordResult_InvalidJoinSecret = 12,
	DiscordResult_NoEligibleActivity = 13,
	DiscordResult_InvalidInvite = 14,
	DiscordResult_NotAuthenticated = 15,
	DiscordResult_InvalidAccessToken = 16,
	DiscordResult_ApplicationMismatch = 17,
	DiscordResult_InvalidDataUrl = 18,
	DiscordResult_InvalidBase64 = 19,
	DiscordResult_NotFiltered = 20,
	DiscordResult_LobbyFull = 21,
	DiscordResult_InvalidLobbySecret = 22,
	DiscordResult_InvalidFilename = 23,
	DiscordResult_InvalidFileSize = 24,
	DiscordResult_InvalidEntitlement = 25,
	DiscordResult_NotInstalled = 26,
	DiscordResult_NotRunning = 27,
	DiscordResult_InsufficientBuffer = 28,
	DiscordResult_PurchaseCanceled = 29,
	DiscordResult_InvalidGuild = 30,
	DiscordResult_InvalidEvent = 31,
	DiscordResult_InvalidChannel = 32,
	DiscordResult_InvalidOrigin = 33,
	DiscordResult_RateLimited = 34,
	DiscordResult_OAuth2Error = 35,
	DiscordResult_SelectChannelTimeout = 36,
	DiscordResult_GetGuildTimeout = 37,
	DiscordResult_SelectVoiceForceRequired = 38,
	DiscordResult_CaptureShortcutAlreadyListening = 39,
	DiscordResult_UnauthorizedForAchievement = 40,
	DiscordResult_InvalidGiftCode = 41,
	DiscordResult_PurchaseError = 42,
	DiscordResult_TransactionAborted = 43,
};

enum EDiscordCreateFlags {
	DiscordCreateFlags_Default = 0,
	DiscordCreateFlags_NoRequireDiscord = 1,
};

enum EDiscordActivityType {
	DiscordActivityType_Playing,
	DiscordActivityType_Streaming,
	DiscordActivityType_Listening,
	DiscordActivityType_Watching,
};

enum EDiscordLogLevel {
	DiscordLogLevel_Error = 1,
	DiscordLogLevel_Warn,
	DiscordLogLevel_Info,
	DiscordLogLevel_Debug,
};

enum EDiscordActivityActionType {
	DiscordActivityActionType_Join = 1,
	DiscordActivityActionType_Spectate,
};

enum EDiscordActivityJoinRequestReply {
	DiscordActivityJoinRequestReply_No,
	DiscordActivityJoinRequestReply_Yes,
	DiscordActivityJoinRequestReply_Ignore,
};

enum EDiscordRelationshipType {
	DiscordRelationshipType_None,
	DiscordRelationshipType_Friend,
	DiscordRelationshipType_Blocked,
	DiscordRelationshipType_PendingIncoming,
	DiscordRelationshipType_PendingOutgoing,
	DiscordRelationshipType_Implicit,
};

enum EDiscordStatus {
	DiscordStatus_Offline = 0,
	DiscordStatus_Online = 1,
	DiscordStatus_Idle = 2,
	DiscordStatus_DoNotDisturb = 3,
};

typedef int32_t DiscordVersion;
typedef int64_t DiscordTimestamp;
typedef int64_t DiscordClientId;
typedef void* IDiscordCoreEvents;
typedef void* IDiscordApplicationEvents;
typedef void* IDiscordImageEvents;
typedef void* IDiscordStorageEvents;
typedef char DiscordLocale[128];
typedef char DiscordBranch[4096];
typedef int64_t DiscordSnowflake;
typedef DiscordSnowflake DiscordUserId;

struct DiscordUser {
	DiscordUserId id;
	char username[256];
	char discriminator[8];
	char avatar[128];
	bool bot;
};

struct DiscordActivityTimestamps {
	DiscordTimestamp start;
	DiscordTimestamp end;
};

struct DiscordActivityAssets {
	char large_image[128];
	char large_text[128];
	char small_image[128];
	char small_text[128];
};

struct DiscordPartySize {
	int32_t current_size;
	int32_t max_size;
};

struct DiscordActivityParty {
	char id[128];
	struct DiscordPartySize size;
};

struct DiscordActivitySecrets {
	char match[128];
	char join[128];
	char spectate[128];
};

struct DiscordActivity {
	enum EDiscordActivityType type;
	int64_t application_id;
	char name[128];
	char state[128];
	char details[128];
	struct DiscordActivityTimestamps timestamps;
	struct DiscordActivityAssets assets;
	struct DiscordActivityParty party;
	struct DiscordActivitySecrets secrets;
	bool instance;
};

struct DiscordPresence {
	enum EDiscordStatus status;
	struct DiscordActivity activity;
};

struct DiscordRelationship {
	enum EDiscordRelationshipType type;
	struct DiscordUser user;
	struct DiscordPresence presence;
};

struct DiscordOAuth2Token {
	char access_token[128];
	char scopes[1024];
	DiscordTimestamp expires;
};

struct IDiscordUserEvents {
	void (*on_current_user_update)(void* event_data);
};

struct IDiscordActivityEvents {
	void (*on_activity_join)(void* event_data, const char* secret);
	void (*on_activity_spectate)(void* event_data, const char* secret);
	void (*on_activity_join_request)(void* event_data, struct DiscordUser* user);
	void (*on_activity_invite)(void* event_data, enum EDiscordActivityActionType type, struct DiscordUser* user, struct DiscordActivity* activity);
};

struct IDiscordRelationshipEvents {
	void (*on_refresh)(void* event_data);
	void (*on_relationship_update)(void* event_data, struct DiscordRelationship* relationship);
};

struct DiscordCreateParams {
	DiscordClientId client_id;
	uint64_t flags;
	IDiscordCoreEvents* events;
	void* event_data;
	IDiscordApplicationEvents* application_events;
	DiscordVersion application_version;
	struct IDiscordUserEvents* user_events;
	DiscordVersion user_version;
	IDiscordImageEvents* image_events;
	DiscordVersion image_version;
	struct IDiscordActivityEvents* activity_events;
	DiscordVersion activity_version;
	struct IDiscordRelationshipEvents* relationship_events;
	DiscordVersion relationship_version;
	struct IDiscordLobbyEvents* lobby_events;
	DiscordVersion lobby_version;
	struct IDiscordNetworkEvents* network_events;
	DiscordVersion network_version;
	struct IDiscordOverlayEvents* overlay_events;
	DiscordVersion overlay_version;
	IDiscordStorageEvents* storage_events;
	DiscordVersion storage_version;
	struct IDiscordStoreEvents* store_events;
	DiscordVersion store_version;
	struct IDiscordVoiceEvents* voice_events;
	DiscordVersion voice_version;
	struct IDiscordAchievementEvents* achievement_events;
	DiscordVersion achievement_version;
};

struct IDiscordApplicationManager {
	void (*validate_or_exit)(struct IDiscordApplicationManager* manager, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result));
	void (*get_current_locale)(struct IDiscordApplicationManager* manager, DiscordLocale* locale);
	void (*get_current_branch)(struct IDiscordApplicationManager* manager, DiscordBranch* branch);
	void (*get_oauth2_token)(struct IDiscordApplicationManager* manager, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result, struct DiscordOAuth2Token* oauth2_token));
	void (*get_ticket)(struct IDiscordApplicationManager* manager, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result, const char* data));
};

struct IDiscordActivityManager {
	enum EDiscordResult (*register_command)(struct IDiscordActivityManager* manager, const char* command);
	enum EDiscordResult (*register_steam)(struct IDiscordActivityManager* manager, uint32_t steam_id);
	void (*update_activity)(struct IDiscordActivityManager* manager, struct DiscordActivity* activity, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result));
	void (*clear_activity)(struct IDiscordActivityManager* manager, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result));
	void (*send_request_reply)(struct IDiscordActivityManager* manager, DiscordUserId user_id, enum EDiscordActivityJoinRequestReply reply, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result));
	void (*send_invite)(struct IDiscordActivityManager* manager, DiscordUserId user_id, enum EDiscordActivityActionType type, const char* content, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result));
	void (*accept_invite)(struct IDiscordActivityManager* manager, DiscordUserId user_id, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result));
};

struct IDiscordUserManager {
	enum EDiscordResult (*get_current_user)(struct IDiscordUserManager* manager, struct DiscordUser* current_user);
	void (*get_user)(struct IDiscordUserManager* manager, DiscordUserId user_id, void* callback_data, void (*callback)(void* callback_data, enum EDiscordResult result, struct DiscordUser* user));
	enum EDiscordResult (*get_current_user_premium_type)(struct IDiscordUserManager* manager, enum EDiscordPremiumType* premium_type);
	enum EDiscordResult (*current_user_has_flag)(struct IDiscordUserManager* manager, enum EDiscordUserFlag flag, bool* has_flag);
};

struct IDiscordCore {
	void (*destroy)(struct IDiscordCore* core);
	enum EDiscordResult (*run_callbacks)(struct IDiscordCore* core);
	void (*set_log_hook)(struct IDiscordCore* core, enum EDiscordLogLevel min_level, void* hook_data, void (*hook)(void* hook_data, enum EDiscordLogLevel level, const char* message));
	struct IDiscordApplicationManager* (*get_application_manager)(struct IDiscordCore* core);
	struct IDiscordUserManager* (*get_user_manager)(struct IDiscordCore* core);
	struct IDiscordImageManager* (*get_image_manager)(struct IDiscordCore* core);
	struct IDiscordActivityManager* (*get_activity_manager)(struct IDiscordCore* core);
	struct IDiscordRelationshipManager* (*get_relationship_manager)(struct IDiscordCore* core);
	struct IDiscordLobbyManager* (*get_lobby_manager)(struct IDiscordCore* core);
	struct IDiscordNetworkManager* (*get_network_manager)(struct IDiscordCore* core);
	struct IDiscordOverlayManager* (*get_overlay_manager)(struct IDiscordCore* core);
	struct IDiscordStorageManager* (*get_storage_manager)(struct IDiscordCore* core);
	struct IDiscordStoreManager* (*get_store_manager)(struct IDiscordCore* core);
	struct IDiscordVoiceManager* (*get_voice_manager)(struct IDiscordCore* core);
	struct IDiscordAchievementManager* (*get_achievement_manager)(struct IDiscordCore* core);
};

enum EDiscordResult (*QDiscordCreate)(DiscordVersion version, struct DiscordCreateParams* params, struct IDiscordCore** result);

static dllfunction_t discord_funcs[] = {
	{"DiscordCreate", (void **) &QDiscordCreate},
	{NULL, NULL}
};

static dllhandle_t discord_dll = NULL;
static struct IDiscordCore *core = NULL;
static struct IDiscordActivityManager *activity_manager;

static int Discord_LoadLibrary(void) {
	const char* dllnames [] = {
#if defined(WIN32)
		"discord_game_sdk.dll",
#elif defined(MACOSX)
		"discord_game_sdk.dylib",
#else
		"discord_game_sdk.so",
#endif
		NULL
	};
	// Already loaded?
	if (discord_dll)
		return true;

	// Load the DLL
	return Sys_LoadLibrary (dllnames, &discord_dll, discord_funcs);
}

int *dummy = { 0 };

static time_t discord_start_time;
void DP_Discord_Init(void) {
	Cvar_RegisterVariable(&integration_discord_enable);
	Cvar_RegisterVariable(&integration_discord_client_id);
	Cvar_RegisterVariable(&integration_discord_game_command);
}

static void DP_Discord_Log(void *data, enum EDiscordLogLevel level, const char *message) {
	Con_Printf("Discord: %s\n", message);
}

static void Discord_UpdateActivityCallback(void* data, enum EDiscordResult result) {
	if (result != DiscordResult_Ok) {
		Con_Printf("Discord update activity failed!\n");
	}
}

static char discord_game_command[512];
long long int discord_client_id;
static void *discord_thread;
static void *discord_mutex;
static volatile int discord_shutdown;
static volatile int discord_activity_update_required;
static char discord_activity_update_details[128];
static char discord_activity_update_state[128];
static char discord_activity_update_party[128];
static volatile int discord_activity_update_party_slots;
static volatile int discord_activity_update_party_slots_used;
static void Discord_SetStatus(const char *details, const char *state, const char *party) {
	struct DiscordActivity activity;
	memset(&activity, 0, sizeof(activity));
	dpsnprintf(activity.details, sizeof(activity.details), "%s", details);
	dpsnprintf(activity.state, sizeof(activity.state), "%s", state);
	activity.type = DiscordActivityType_Playing;
	activity.timestamps.start = discord_start_time;
	strlcpy(activity.assets.large_image, "logo", sizeof(activity.assets.large_image));
	if (party && party[0]) {
		strlcpy(activity.party.id, party, sizeof(activity.party.id));
		activity.party.size.current_size = discord_activity_update_party_slots_used;
		activity.party.size.max_size = discord_activity_update_party_slots;
		dpsnprintf(activity.secrets.join, sizeof(activity.secrets.join), "secret%s", party);
	}
	activity_manager->update_activity(activity_manager, &activity, dummy, Discord_UpdateActivityCallback);
}

static void Discord_Join(void *data, const char *secret) {
	char connect_string[128];
	const char *host;
	if (strncmp(secret, "secret", 6)) {
		Con_Printf("Discord: bad join request\n");
		return;
	}
	host = &secret[6];
	dpsnprintf(connect_string, sizeof(connect_string), "\nconnect %s\n", host);
	Cbuf_AddText(connect_string);
}

static void Discord_JoinRequest(void *data, struct DiscordUser *user) {
	Con_Printf("Discord: ask-to-join request\n");
	activity_manager->send_request_reply(activity_manager, user->id, DiscordActivityJoinRequestReply_Yes, NULL, NULL);
}

static int Discord_Thread(void *dummy) {
	static struct IDiscordUserEvents users_events;
	static struct DiscordCreateParams params;
	static struct IDiscordActivityEvents activities_events;
	static struct IDiscordRelationshipEvents relationships_events;
	Con_Printf("Discord thread started\n");
	Thread_LockMutex(discord_mutex);
	discord_shutdown = 0;
	discord_start_time = time(NULL);
	memset(&users_events, 0, sizeof(users_events));
	memset(&activities_events, 0, sizeof(activities_events));
	activities_events.on_activity_join_request = Discord_JoinRequest;
	activities_events.on_activity_join = Discord_Join;
	memset(&relationships_events, 0, sizeof(relationships_events));
	memset(&params, 0, sizeof(struct DiscordCreateParams));
	params.application_version = DISCORD_APPLICATION_MANAGER_VERSION;
	params.user_version = DISCORD_USER_MANAGER_VERSION;
	params.image_version = DISCORD_IMAGE_MANAGER_VERSION;
	params.activity_version = DISCORD_ACTIVITY_MANAGER_VERSION;
	params.relationship_version = DISCORD_RELATIONSHIP_MANAGER_VERSION;
	params.lobby_version = DISCORD_LOBBY_MANAGER_VERSION;
	params.network_version = DISCORD_NETWORK_MANAGER_VERSION;
	params.overlay_version = DISCORD_OVERLAY_MANAGER_VERSION;
	params.storage_version = DISCORD_STORAGE_MANAGER_VERSION;
	params.store_version = DISCORD_STORE_MANAGER_VERSION;
	params.voice_version = DISCORD_VOICE_MANAGER_VERSION;
	params.achievement_version = DISCORD_ACHIEVEMENT_MANAGER_VERSION;
	params.client_id = discord_client_id;
	params.flags = DiscordCreateFlags_NoRequireDiscord;
	params.event_data = dummy;
	params.activity_events = &activities_events;
	params.relationship_events = &relationships_events;
	params.user_events = &users_events;
	QDiscordCreate(DISCORD_VERSION, &params, &core);
	if (core) {
		core->set_log_hook(core, DiscordLogLevel_Warn, dummy, DP_Discord_Log);
		activity_manager = core->get_activity_manager(core);
		Discord_SetStatus("Menu", "", "");
		if (!discord_game_command[0]) {
			strlcpy(discord_game_command, fs_baseexe, sizeof(discord_game_command));
		}
		if (discord_game_command[0]) {
			if (activity_manager->register_command(activity_manager, discord_game_command) != DiscordResult_Ok) {
				Con_Printf("Discord register command failed\n");
			}
		} else {
		}
		for (;!discord_shutdown;) {
			if (discord_activity_update_required) {
				discord_activity_update_required = 0;
				Discord_SetStatus(discord_activity_update_details, discord_activity_update_state, discord_activity_update_party);
			}
			core->run_callbacks(core);
			Thread_UnlockMutex(discord_mutex);
			Sys_Sleep(100000);
			Thread_LockMutex(discord_mutex);
		}
	} else {
		Con_Printf("Dicord library failed. Maybe Discord not running?\n");
	}
	Thread_UnlockMutex(discord_mutex);
	Con_Printf("Discord thread finished\n");
	return 0;
}

void DP_Discord_Start(void) {
	if (cls.state == ca_dedicated || discord_thread || !integration_discord_enable.integer)
		return;

	if (!Thread_HasThreads()) {
		Con_Printf("Discord required thread support\n");
		return;
	}
	if (!Discord_LoadLibrary()) {
		Con_Printf("Discord dll not loaded\n");
		return;
	}
	strlcpy(discord_game_command, integration_discord_game_command.string, sizeof(discord_game_command));
	discord_client_id = atoll(integration_discord_client_id.string);
	discord_mutex = Thread_CreateMutex();
	discord_thread = Thread_CreateThread(Discord_Thread, NULL);
	Con_Printf("Discord initialized\n");
}

void DP_Discord_SetStatus(const char *details, const char *state, const char *party) {
	int i;
	Thread_LockMutex(discord_mutex);
	if (details[0]) {
		strlcpy(discord_activity_update_details, details, sizeof(discord_activity_update_details));
		strlcpy(discord_activity_update_state, state, sizeof(discord_activity_update_state));
		strlcpy(discord_activity_update_party, party, sizeof(discord_activity_update_party));
	}
	if (cls.state == ca_connected) {
		discord_activity_update_party_slots = cl.maxclients;
		discord_activity_update_party_slots_used = 1;
		if (cl.scores)
			for (i = 0 ; i < cl.maxclients ; i++) {
				if (cl.scores[i].name[0] && i != cl.playerentity - 1) {
					discord_activity_update_party_slots_used++;
				}
			}
	}
	discord_activity_update_required = 1;
	Thread_UnlockMutex(discord_mutex);
}

void DP_Discord_Shutdown(void) {
	if (!discord_thread)
		return;

	Thread_LockMutex(discord_mutex);
	discord_shutdown = 1;
	Thread_UnlockMutex(discord_mutex);
	Thread_WaitThread(discord_thread, 0);
	Thread_DestroyMutex(discord_mutex);
	discord_mutex = NULL;
	discord_thread = NULL;
}
