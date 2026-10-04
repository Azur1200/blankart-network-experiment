//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by John "JTE" Muniz.
// Copyright (C) 2012-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_hook.h
/// \brief hooks for Lua scripting

#ifndef __LUA_HOOK_H__
#define __LUA_HOOK_H__

#include "r_defs.h"
#include "d_player.h"
#include "s_sound.h"
#include "d_event.h"

#include "lua_hudlib_drawlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Do you know what an 'X Macro' is? Such a macro is called over each element of
a list and expands the input. I use it for the hook lists because both an enum
and array of hook names need to be kept in order. The X Macro handles this
automatically.
*/

#define MOBJ_HOOK_LIST(X) \
	X (MobjSpawn),/* P_SpawnMobj */\
	X (MobjCollide),/* PIT_CheckThing */\
	X (MobjLineCollide),/* ditto */\
	X (MobjMoveCollide),/* tritto */\
	X (TouchSpecial),/* P_TouchSpecialThing */\
	X (MobjFuse),/* when mobj->fuse runs out */\
	X (MobjThinker),/* P_MobjThinker, P_SceneryThinker */\
	X (BossThinker),/* P_GenericBossThinker */\
	X (MobjScaleChange),/*SRB2KART*/\
	X (ShouldDamage),/* P_DamageMobj (Should mobj take damage?) */\
	X (ShouldSpin),/* P_DamageMobj (Should mobj take spinout damage?) */\
	X (ShouldExplode),/* P_DamageMobj (Should mobj take explosion damage?) */\
	X (ShouldSquish),/* P_DamageMobj (Should mobj take squish damage?) */\
	X (MobjDamage),/* P_DamageMobj (Mobj actually takes damage!) */\
	X (PlayerSpin),/* P_DamageMobj (Mobj actually takes damage!) */\
	X (PlayerExplode),/* P_DamageMobj (Mobj actually takes damage!) */\
	X (PlayerSquish),/* P_DamageMobj (Mobj actually takes damage!) */\
	X (MobjDeath),/* P_KillMobj */\
	X (BossDeath),/* A_BossDeath */\
	X (MobjRemoved),/* P_RemoveMobj */\
	X (BotRespawn),/* B_CheckRespawn */\
	X (MobjMoveBlocked),/* P_XYMovement (when movement is blocked) */\
	X (MapThingSpawn),/* P_SpawnMapThing */\
	X (FollowMobj),/* P_PlayerAfterThink Smiles mobj-following */\
	X (DropHnextList),/*SRB2KART*/\

#define HOOK_LIST(X) \
	X (NetVars),/* add to archive table (netsave) */\
	X (MapChange),/* (before map load) */\
	X (MapLoad),\
	X (PlayerJoin),/* Got_AddPlayer */\
	X (PreThinkFrame)/* frame (before mobj and player thinkers) */,\
	X (ThinkFrame),/* frame (after mobj and player thinkers) */\
	X (PostThinkFrame),/* frame (at end of tick, ie after overlays, precipitation, specials) */\
	X (BotTiccmd),/* B_BuildTiccmd */\
	X (PlayerMsg),/* chat messages */\
	X (HurtMsg),/* imhurttin */\
	X (PlayerSpawn),/* G_SpawnPlayer */\
	X (ShieldSpawn),/* P_SpawnShieldOrb */\
	X (ShieldSpecial),/* shield abilities */\
	X (PlayerCanDamage),/* P_PlayerCanDamage */\
	X (PlayerQuit),\
	X (NameChange),\
	X (IntermissionThinker),/* Y_Ticker */\
	X (TeamSwitch),/* team switching in... uh... *what* speak, spit it the fuck out */\
	X (ViewpointSwitch),/* spy mode (no trickstabs) */\
	X (SeenPlayer),/* MT_NAMECHECK */\
	X (PlayerThink),/* P_PlayerThink */\
	X (GameQuit),\
	X (PlayerCmd),/* building the player's ticcmd struct */\
	X (MusicChange),\
	X (VoteThinker),/* Y_VoteTicker */\
	X (PlayerUsesBotMovement),/* K_PlayerUsesBotMovement */\
	X (BotJoin),\
	X (GPRankPoints),/* K_CalculateGPRankPoints */\
	X (ItemOddsScale),/* K_ItemOddsScale */\
	X (AddonLoaded),\
	X (CameraThinker),/* P_MoveChaseCamera */\
	X (PlayerItem),/*SRB2KART*/\
	X (PlayerItemThink),/*SRB2KART*/\
	X (PlayerItemUse),/*SRB2KART*/\
	X (PlayerItemHoldUse),/*SRB2KART*/\
	X (PlayerItemReleaseUse),/*SRB2KART*/\
	X (KartHyudoro),/*SRB2KART*/\
	X (KartSneaker),/*SRB2KART*/\
	X (KartStripItems),/*SRB2KART*/\
	X (KartStripOther),/*SRB2KART*/\
	X (CanPickupItem),/*SRB2KART*/\
	X (PlayerNextLap),/* K_HandleLapIncrement */\
	X (PlayerFinalLap),/* K_HandleLapIncrement */\
	X (PlayerFinish), /* P_DoPlayerExit, P_DoTimeOver */\
	X (PlayerPreRank), \
	X (PlayerRank), \
	X (ShouldBounceWall), /* P_BouncePlayerMove */\
	X (PlayerBounceWall), /* P_BouncePlayerMove */\

#define STRING_HOOK_LIST(X) \
	X (SpecialExecute),\
	X (LinedefExecute),\
	X (ShouldJingleContinue),/* should jingle of the given music continue playing */\

#define HUD_HOOK_LIST(X) \
	X (game),\
	X (scores),/* emblems/multiplayer list */\
	X (title),/* titlescreen */\
	X (titlecard),\
	X (intermission),\
	X (vote),\

/*
I chose to access the hook enums through a macro as well. This could provide
a hint to lookup the macro's definition instead of the enum's definition.
(Since each enumeration is not defined in the source code, but by the list
macros above, it is not greppable.) The name passed to the macro can also be
grepped and found in the lists above.
*/

#define   MOBJ_HOOK(name)   mobjhook_ ## name
#define        HOOK(name)       hook_ ## name
#define    HUD_HOOK(name)    hudhook_ ## name
#define STRING_HOOK(name) stringhook_ ## name

#define ENUM(X) enum { X ## _LIST (X)  X(MAX) }

ENUM   (MOBJ_HOOK);
ENUM        (HOOK);
ENUM    (HUD_HOOK);
ENUM (STRING_HOOK);

#undef ENUM

/* dead simple, LUA_HOOK(GameQuit) */
#define LUA_HOOK(type) LUA_HookVoid(HOOK(type))
//#define LUA_HUDHOOK(type) LUA_HookHUD(HUD_HOOK(type))

extern dboolean hook_cmd_running;
extern int hook_defrosting;

void LUA_HookVoid(int hook);
void LUA_HookHUD(huddrawlist_h, int hook);

int  LUA_HookMobj(mobj_t *, int hook);
int  LUA_Hook2Mobj(mobj_t *, mobj_t *, int hook);
void LUA_HookInt(int32_t integer, int hook);
void LUA_HookGamemap(int hook);
void LUA_HookBool(dboolean value, int hook);
int  LUA_HookPlayer(player_t *, int hook);
int  LUA_HookPlayerForceResults(player_t *, int hook);
int  LUA_HookTiccmd(player_t *, ticcmd_t *, int hook);
int  LUA_HookKey(event_t *event, int hook); // Hooks for key events

void LUA_HookPlayerThink(player_t *player);
void LUA_HookPreThinkFrame(void);
void LUA_HookThinkFrame(void);
void LUA_HookPostThinkFrame(void);
int  LUA_HookMobjLineCollide(mobj_t *, line_t *);
int  LUA_HookTouchSpecial(mobj_t *special, mobj_t *toucher);
int  LUA_HookShouldDamage(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookShouldSpin(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookShouldExplode(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookShouldSquish(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookMobjDamage(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookPlayerSpin(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookPlayerExplode(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookPlayerSquish(mobj_t *target, mobj_t *inflictor, mobj_t *source, int32_t damage, uint8_t damagetype);
int  LUA_HookMobjDeath(mobj_t *target, mobj_t *inflictor, mobj_t *source, uint8_t damagetype);
int  LUA_HookMobjMoveBlocked(mobj_t *, mobj_t *, line_t *);
void LUA_HookSpecialExecute(activator_t *activator, int32_t *args, char **stringargs);
void LUA_HookLinedefExecute(line_t *, mobj_t *, sector_t *);
int  LUA_HookPlayerMsg(int source, int target, int flags, char *msg);
int  LUA_HookHurtMsg(player_t *, mobj_t *inflictor, mobj_t *source, uint8_t damagetype);
int  LUA_HookMapThingSpawn(mobj_t *, mapthing_t *);
int  LUA_HookFollowMobj(player_t *, mobj_t *);
int  LUA_HookCameraThinker(player_t *, camera_t *);
int  LUA_HookPlayerCanDamage(player_t *, mobj_t *);
void LUA_HookPlayerQuit(player_t *, kickreason_t);
int  LUA_HookNameChange(player_t *plr, const char *name);
int  LUA_HookTeamSwitch(player_t *, int newteam, dboolean fromspectators, dboolean tryingautobalance, dboolean tryingscramble);
int  LUA_HookViewpointSwitch(player_t *player, player_t *newdisplayplayer, dboolean forced);
int  LUA_HookSeenPlayer(player_t *player, player_t *seenfriend);
int LUA_HookGPRankPoints(uint8_t position, uint8_t numplayers, int16_t *points);
int  LUA_HookShouldJingleContinue(player_t *, const char *musname);
int  LUA_HookMusicChange(const char *oldname, struct MusicChange *);
dboolean LUA_HookItemOddsScale(uint8_t numPlayers, int8_t *spbrush, fixed_t *playerScaling);

// Item shit
dboolean LUA_HookPlayerItem(player_t *player, uint8_t itemType, dboolean wasHoldingItem, dboolean *force);
dboolean LUA_HookPlayerItemThink(player_t *player, uint8_t itemType);
dboolean LUA_HookPlayerItemUse(player_t *player, uint8_t itemType, dboolean attackJustDown, dboolean onground, dboolean force, dboolean noHyudoro);
dboolean LUA_HookPlayerItemHoldUse(player_t *player, uint8_t itemType, dboolean attackJustDown, dboolean onground, dboolean force, dboolean noHyudoro);
dboolean LUA_HookPlayerItemReleaseUse(player_t *player, uint8_t itemType, dboolean attackJustDown, dboolean onground, dboolean force, dboolean noHyudoro);

dboolean LUA_HookKartHyudoro(player_t *player, int32_t *target, dboolean sink); // SRB2Kart: Hook for K_DoHyudoroSteal and overriding its results.
dboolean LUA_HookMobjScaleChange(mobj_t *target, fixed_t newscale, fixed_t oldscale); // SRB2Kart: Hook for P_SetScale.
dboolean LUA_HookKartSneaker(player_t *player, int type); // SRB2Kart: Hook for K_DoSneaker.
dboolean LUA_HookKartStripItems(player_t *player, uint8_t item); // SRB2Kart: Hook for K_StripItems.
dboolean LUA_HookKartStripOther(player_t *player); // SRB2Kart: Hook for K_StripOther.
dboolean LUA_HookCanPickupItem(player_t *player, uint8_t weapon, dboolean *force); // SRB2Kart: Hook for P_CanPickupItem.
int		LUA_HookDropHnextList(mobj_t *mobj, mobjtype_t droptype, dboolean *orbit, dboolean *dropall); // SRB2Kart: Hook for K_DropHnextList.
dboolean LUA_HookPlayerNextLap(player_t *player, dboolean displayplayer, dboolean localplayer); // BlanKart: Hook for the lap increment sequence
dboolean LUA_HookPlayerFinalLap(player_t *player, dboolean displayplayer, dboolean localplayer); // BlanKart: Hook for the final lap sequence
int LUA_HookPlayerFinish(player_t *player, dboolean dnf, dboolean losing);

// Rank stuff
dboolean LUA_HookPlayerPreRank(player_t *player);
int LUA_HookPlayerRank(player_t *player, int32_t pendingrankscore); // BlanKart: Hook during player rankings; can return a volatile factor value

int LUA_HookShouldBounceWall(player_t *player, line_t *line, dboolean slideline, dboolean tripwire); // Neptune: Should the player bounce off a wall?
int LUA_HookPlayerBounceWall(player_t *player, line_t *line, dboolean slideline, dboolean tripwire); // Neptune: Hook for P_BouncePlayerMove. Allows Lua to execute code and/or overwrite its behavior.

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __LUA_HOOK_H__
