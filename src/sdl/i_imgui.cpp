// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 Kart Krew
// Copyright (C) 2026 Team BlanKart
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  i_imgui.cpp
/// \brief imgui support for SRB2

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_render.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "sdlmain.h"
#include "../i_video.h"
#include "../v_video.h"
#include "../m_misc.h"
#include "../k_kart.h"
#include "../k_follower.h"
#include "../k_bot.h"
#include "../st_stuff.h"
#include "../deh_tables.h"
#include "../dehacked.h"
#include "../command.h"

char *glsl_version = nullptr;
static bool imgui_opengl_init = false;
static bool imgui_software_init = false;
static bool imgui_setup= false;
static bool imgui_frame_active = false;

void Impl_ImGui_CreateContext(void)
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
}

void Impl_ImGui_DestroyContext(void)
{
	if (imgui_setup == true)
	{

		if (imgui_software_init == true)
		{
			ImGui_ImplSDLRenderer3_Shutdown();
			imgui_software_init = false;
		}

		if (imgui_opengl_init == true)
		{
			ImGui_ImplOpenGL3_Shutdown();
			imgui_opengl_init = false;
		}

		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		imgui_setup = false;
		imgui_frame_active = false;
	}
}

void Impl_ImGui_Frame(void)
{
	if (imgui_setup == false)
		return;

	if (imgui_frame_active == true)
	{
		ImGui::EndFrame();
		imgui_frame_active = false;
	}

	if (rendermode == render_soft)
		ImGui_ImplSDLRenderer3_NewFrame();
	else if (rendermode == render_opengl)
		ImGui_ImplOpenGL3_NewFrame();

	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	imgui_frame_active = true;
}

void Impl_ImGui_InitSoftware(SDL_Window *window, SDL_Renderer *renderer)
{
	imgui_setup = ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer3_Init(renderer);
	imgui_software_init = true;
}

void Impl_ImGui_InitOpenGL(SDL_Window *window, SDL_GLContext *sdlglcontext)
{
	imgui_setup = ImGui_ImplSDL3_InitForOpenGL(window, sdlglcontext);
	ImGui_ImplOpenGL3_Init(glsl_version);
	imgui_opengl_init = true;
}

void Impl_ImGui_SoftwareRenderDrawData(SDL_Renderer *renderer)
{
	if (imgui_setup == false || imgui_frame_active == false || imgui_software_init == false)
		return;

	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void Impl_ImGui_OpenGLRenderDrawData(void)
{
	if (imgui_setup == false || imgui_frame_active == false || imgui_opengl_init == false)
		return;

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool Impl_ImGui_HandleEvent(SDL_Event *event)
{
	if (imgui_setup == false)
		return false;

	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplSDL3_ProcessEvent(event);

	if (io.WantCaptureMouse || io.WantCaptureKeyboard)
	{
		return true;
	}

	return false;
}

typedef struct
{
	bool player_display;
	char player_search[255];
	bool mobj_display;
	char mobj_search[255];
	bool music_display;
	int32_t music_selected;
	musicdef_t *music_def;
	bool map_display;
	int32_t map_selected;
	mapheader_t *mapheader;
} imgui_state_t;

imgui_state_t windowstate = {0};
int32_t recursive_max = 25;

static void PlayerDisplay(player_t *player, int32_t recursive_mobj, int32_t recursive_player);
static void MobjDisplay(mobj_t *mobj, int32_t recursive_mobj, int32_t recursive_player);

static void generic_mobj(mobj_t *mobj, const char* name, int32_t recursive_mobj, int32_t recursive_player)
{
	if (mobj == NULL || P_MobjWasRemoved(mobj))
		return;

	char nameid[512];

	snprintf(nameid, sizeof(nameid), "%s###%s%d_%d", name, name, recursive_mobj, recursive_player);

	if (ImGui::CollapsingHeader(nameid))
	{

		if (recursive_mobj >= recursive_max)
		{
			ImGui::Text("Max mobj recursion reached");
			ImGui::Text("\n");
		}
		else
		{
			recursive_mobj++;
			MobjDisplay(mobj, recursive_mobj, recursive_player);
			ImGui::Text("\n");
		}
	}
}

static void PlayerDisplay(player_t *player, int32_t recursive_mobj, int32_t recursive_player)
{
	fixed_t totalspeed = player->mo == NULL? 0 : (FixedDiv(player->speed, FixedMul(K_GetKartSpeed(player, false, false), K_PlayerBaseFriction(player, ORIG_FRICTION)))*100)>>FRACBITS;

	ImGui::Text("BKID: %s", GetPrettyRRID(player->public_key, true));
	ImGui::Text("Name: %s", player_names[player-players]);
	ImGui::Text("Skin: %s (%s)", skins[player->skin]->name, skins[player->skin]->realname);
	ImGui::Text("Color: %s", skincolors[player->skincolor].name);
	ImGui::Text("Bot: %s", player->bot ? "True" : "False");
	if (player->bot)
	{
		char nameid[512];
		snprintf(nameid, sizeof(nameid), "BotVars###BotVars%d_%d", recursive_mobj, recursive_player);
		if (ImGui::CollapsingHeader(nameid))
		{
			botdata_t *botdata = K_GetBotData(player-players);
			ImGui::Text("Bot Predict X: %d", botdata->predict.x);
			ImGui::Text("Bot Predict Y: %d", botdata->predict.y);
			ImGui::Text("Bot Predict Radius: %d", botdata->predict.radius);
			ImGui::Text("Bot Predict Base Radius: %d", botdata->predict.baseRadius);
			ImGui::Text("Item Delay: %d", botdata->itemdelay);
			ImGui::Text("Item Confirm: %d", botdata->itemconfirm);
			ImGui::Text("Turn Confirm: %d", botdata->turnconfirm);
			ImGui::Text("Respawn Confirm: %d", botdata->respawnconfirm);

			ImGui::Text("Drift State: %d", botdata->driftstate);
			ImGui::Text("Drift Turn: %d", botdata->driftturn);
			ImGui::Text("Drift Time: %d", botdata->drifttime);
			ImGui::Text("Drift Lockout: %d", botdata->driftlockout);
			ImGui::Text("Drift Max Distance: %d", botdata->driftmaxdist);
			ImGui::Text("Drift Power Divisor: %d", botdata->driftpowerdiv);
			ImGui::Text("Drift State Delay: %d", botdata->driftstatedelay);
			ImGui::Text("Drift Skill: %d", botdata->driftskill);

			ImGui::Text("Accel Down: %d", botdata->acceldown);
			ImGui::Text("Brake Down: %d", botdata->brakedown);
			ImGui::Text("Drift Down: %d", botdata->driftdown);
			ImGui::Text("Item Down: %d", botdata->itemdown);
			ImGui::Text("Do Lookback: %d", botdata->dolookback);
			ImGui::Text("Item Was Down: %d", botdata->dolookback);
			ImGui::Text("Item Throw: %d", botdata->itemthrow);
			ImGui::Text("Turn Amount: %d", botdata->turnamt);
			ImGui::Text("Prediction Error: %d", botdata->predictionerror);
			ImGui::Text("Grip Lockout: %d", botdata->griplockout);
			ImGui::Text("\n");
		}
	}
	ImGui::Text("Speed Weight: %d %d", player->kartspeed, player->kartweight);
	ImGui::Text("Req.Speed Req.Weight: %d %d", player->kartspeedrestat, player->kartspeedrestat);
	ImGui::Text("Random Restat: %s", player->randomrestat ? "True" : "False");

	ImGui::Text("Speed: %d (%d%%)", player->speed, totalspeed);

	ImGui::Text("BoostPower: %d (%d%%)", player->boostpower, ST_FixedToPercent(player->boostpower));
	ImGui::Text("SpeedBoost: %d (%d%%)", player->speedboost, ST_FixedToPercent(player->speedboost));
	ImGui::Text("AccelBoost: %d (%d%%)", player->accelboost, ST_FixedToPercent(player->accelboost));
	ImGui::Text("HandleBoost: %d (%d%%)", player->handleboost, ST_FixedToPercent(player->handleboost));
	ImGui::Text("NumBoosts: %d", player->numboosts);

	ImGui::Text("Item: %s (%d)", DEH_KartItemName(static_cast<kartitemtype_e>(player->itemtype)), player->itemtype);

	generic_mobj(player->shieldtracer, "Shield Tracer", recursive_mobj, recursive_player);

	ImGui::Text("Attraction Charge: %d", player->attractioncharge);
	ImGui::Text("Attraction Attack: %d", player->attractionattack);
	ImGui::Text("Attraction Attack Hipower: %d", player->attractionattack_hipower);
	ImGui::Text("Attraction Boost: %d", player->attractionboost);

	ImGui::Text("Bubble Cool: %d", player->bubblecool);
	ImGui::Text("Bubble Blowup: %d", player->bubbleblowup);
	ImGui::Text("Bubble Health: %d", player->bubblehealth);
	ImGui::Text("Bubble Boost: %d", player->bubbleboost);
	ImGui::Text("Bubble Graze: %u", player->bubblegraze);
	ImGui::Text("Bubble Buffer: %d", player->bubblebuffer);

	ImGui::Text("Flame Dash: %d", player->flamedash);
	ImGui::Text("Flame Timer: %d", player->flametimer);
	ImGui::Text("Flame Store: %d", player->flamestore);
	ImGui::Text("Flame Burn Stop: %d", player->flameburnstop);
	ImGui::Text("Flame Overheat: %d", player->flameoverheat);

	ImGui::Text("Hyudoro Timer: %d", player->hyudorotimer);
	ImGui::Text("Stealing Timer: %d", player->stealingtimer);
	ImGui::Text("Stolen Timer: %d", player->stolentimer);

	ImGui::Text("Sneaker Timer: %d", player->sneakertimer);
	ImGui::Text("Real Sneaker Timer: %d", player->realsneakertimer);
	ImGui::Text("Floor boost: %d", player->floorboost);

	ImGui::Text("Chain Timer: %d", player->chaintimer);

	ImGui::Text("Panel Graze Timer: %d", (int32_t)(player->panelgraze));

	ImGui::Text("Boost Charge: %d", player->boostcharge);

	ImGui::Text("Slope Boost: %d", player->slopeboost);
	ImGui::Text("Prev Slope Boost: %d", player->prevslopeboost);
	ImGui::Text("Slope Accel: %d", player->slopeaccel);

	ImGui::Text("Grow Shrink Timer: %d", player->growshrinktimer);
	ImGui::Text("Grow Cancel: %d", player->growcancel);
	ImGui::Text("Squished Timer: %d", player->squishedtimer);

	ImGui::Text("Alt Shrink Times Hit: %d", player->altshrinktimeshit);
	ImGui::Text("In Arrow Bullet: %d", player->inarrowbullet);
	generic_mobj(player->arrowbullet, "Arrow Bullet", recursive_mobj, recursive_player);

	ImGui::Text("Rocket Sneaker Timer: %d", player->rocketsneakertimer);

	ImGui::Text("Invincibility Timer: %d", player->invincibilitytimer);

	ImGui::Text("S-Monitor Timer: %d", player->smonitortimer);
	ImGui::Text("Max S-Monitor TIme: %d", player->maxsmonitortime);
	ImGui::Text("S-Monitor Expiring: %d", player->smonitorexpiring);
	ImGui::Text("S-Monitor Cancel: %d", player->smonitorcancel);
	ImGui::Text("S-Monitor Warning: %d", player->smonitorwarning);

	ImGui::Text("Eggman Explode: %d", player->eggmanexplode);
	ImGui::Text("Eggman Blame: %d", player->eggmanblame);

	ImGui::Text("Banana Drag: %d", player->bananadrag);

	ImGui::Text("Last Item Target: %d", player->lastitemtarget);
	ImGui::Text("Item Target Delay: %d", player->itemtargetdelay);

	ImGui::Text("Position: %d", player->position);
	ImGui::Text("Distance to FInish: %u", player->distancetofinish);
	ImGui::Text("Laps: %d", player->laps);
	ImGui::Text("Rings: %d", player->rings);

	ImGui::Text("Follower Skin: %s", player->followerskin == -1 ? "NONE" : followers[player->followerskin].name);
	ImGui::Text("Follower Ready: %s", player->followerready ? "True" : "False");
	ImGui::Text("Follower Color: %s", skincolors[player->followercolor].name);
	generic_mobj(player->follower, "Follower", recursive_mobj, recursive_player);

	ImGui::Text("Char Flags: %u", player->charflags);

	ImGui::Text("Follow Item: %s", DEH_MobjtypeName(player->followitem));
	generic_mobj(player->followmobj, "followmobj", recursive_mobj, recursive_player);

	ImGui::Text("Score: %u", player->score);

	ImGui::Text("No Control: %d", player->nocontrol);
	ImGui::Text("Carry: %d", player->carry);
	ImGui::Text("Dye: %d", player->dye);

	ImGui::Text("Drop Dash: %d", player->dropdash);
	ImGui::Text("Respawn: %d", player->respawn);

	ImGui::Text("Flashing: %d", player->flashing);

	ImGui::Text("Spinout Timer: %d", player->spinouttimer);
	ImGui::Text("Spinout Type: %d", player->spinouttype);
	ImGui::Text("Wipeout Slow: %d", player->wipeoutslow);

	ImGui::Text("Flipover Timer: %d", player->flipovertimer);
	ImGui::Text("Flipover Angle: %u", player->flipoverangle);

	ImGui::Text("Crash State: %d", player->crash_state);
	ImGui::Text("Crash Timer: %d", player->crashtimer);
	ImGui::Text("Crash Elapsed: %d", player->crash_elapsed);
	ImGui::Text("Crash Reorient Start: %d", player->crash_reorient_start);
	ImGui::Text("Crash Rot: %d", player->crash_rot);
	ImGui::Text("Crash Yaw: %d", player->crash_yaw);
	ImGui::Text("Crash Z Offset: %d", player->crash_zoffset);
	ImGui::Text("Crash Z Offset Delta: %d", player->crash_zoffset_delta);
	ImGui::Text("Crash Z Offset Lerp: %d", player->crash_zoffset_lerp);
	ImGui::Text("Crash Side: %d", player->crash_side);
	ImGui::Text("Crash Bumpme: %d", player->crash_bumpme);
	ImGui::Text("Crash Speed: %d", player->crash_speed);

	ImGui::Text("Squished Timer: %d", player->squishedtimer);

	ImGui::Text("Just Bumped %d", player->justbumped);
	ImGui::Text("Just Shoved: %d", player->justshoved);

	generic_mobj(player->mo, "Mobj", recursive_mobj, recursive_player);

	ImGui::Text("\n");
}

static void MobjDisplay(mobj_t *mobj, int32_t recursive_mobj, int32_t recursive_player)
{
	ImGui::Text("x y z: %d %d %d", mobj->x, mobj->y, mobj->z);
	ImGui::Text("old_x old_y old_z: %d %d %d", mobj->old_x, mobj->old_y, mobj->old_z);
	ImGui::Text("old_x2 old_y2 old_z2: %d %d %d", mobj->old_x2, mobj->old_y2, mobj->old_z2);

	ImGui::Text("Type: %s (%d)", DEH_MobjtypeName(mobj->type), mobj->type);
	//mobjinfo

	if (mobj->blocknode)
	{
		char nameid[512];
		snprintf(nameid, sizeof(nameid), "BlockNode###BlockNode%d_%d", recursive_mobj, recursive_player);
		if (ImGui::CollapsingHeader(nameid))
		{
			if (mobj->blocknode->mobj.mobj)
			{
				generic_mobj(mobj->blocknode->mobj.mobj, "mobj.mobj", recursive_mobj, recursive_player);
			}

			if (mobj->blocknode->bprev && *mobj->blocknode->bprev)
			{
				blocknode_t *prev = *mobj->blocknode->bprev;
				if (prev->mobj.mobj)
				{
					generic_mobj(prev->mobj.mobj, "bprev", recursive_mobj, recursive_player);
				}
			}

			if (mobj->blocknode->bnext)
			{
				if (mobj->blocknode->bnext->mobj.mobj)
				{
					generic_mobj(mobj->blocknode->bnext->mobj.mobj, "bnext", recursive_mobj, recursive_player);
				}
			}

			if (mobj->blocknode->mprev && *mobj->blocknode->mprev)
			{
				blocknode_t *prev = *mobj->blocknode->mprev;
				if (prev->mobj.mobj)
				{
					generic_mobj(prev->mobj.mobj, "mprev", recursive_mobj, recursive_player);
				}
			}

			if (mobj->blocknode->mnext)
			{
				if (mobj->blocknode->mnext->mobj.mobj)
				{
					generic_mobj(mobj->blocknode->mnext->mobj.mobj, "mnext", recursive_mobj, recursive_player);
				}
			}
		}
	}

	ImGui::Text("angle pitch roll: %u %u %u", mobj->angle, mobj->pitch, mobj->roll);
	ImGui::Text("slopepitch sloperoll: %u %u", mobj->slopepitch, mobj->sloperoll);
	ImGui::Text("old_slopepitch old_sloperoll: %u %u", mobj->old_slopepitch, mobj->old_sloperoll);
	ImGui::Text("old_slopepitch2 old_sloperol2l: %u %u", mobj->old_slopepitch2, mobj->old_sloperoll2);
	ImGui::Text("old_angle old_pitch old_roll: %u %u %u", mobj->old_angle, mobj->old_pitch, mobj->old_roll);
	ImGui::Text("old_angle2 old_pitch2 old_roll2: %u %u %u", mobj->old_angle2, mobj->old_pitch2, mobj->old_roll2);

	ImGui::Text("rollangle: %u", mobj->rollangle);
	//sprite
	ImGui::Text("frame: %u", mobj->frame);
	ImGui::Text("sprite2: %d", mobj->sprite2);
	ImGui::Text("anim_duration: %d", mobj->anim_duration);

	ImGui::Text("renderflags: %u", mobj->renderflags);
	ImGui::Text("renderflags2: %u", mobj->renderflags2);
	ImGui::Text("spritexscale spriteyscale: %d %d", mobj->spritexscale, mobj->spriteyscale);
	ImGui::Text("spritexoffset spriteyoffset: %d %d", mobj->spritexoffset, mobj->spriteyoffset);
	ImGui::Text("old_spritexscale old_spriteyscale: %d %d", mobj->old_spritexscale, mobj->old_spriteyscale);
	ImGui::Text("old_spritexscale2 old_spriteyscale2: %d %d", mobj->old_spritexscale2, mobj->old_spriteyscale2);
	ImGui::Text("old_spritexoffset old_spriteyoffset: %d %d", mobj->old_spritexoffset, mobj->old_spriteyoffset);
	ImGui::Text("old_spritexoffset2 old_spriteyoffset2: %d %d", mobj->old_spritexoffset2, mobj->old_spriteyoffset2);

	ImGui::Text("rollingxoffset rollingyoffset: %d %d", mobj->rollingxoffset, mobj->rollingyoffset);

	//floorslopesprite
	ImGui::Text("lightlevel: %d", mobj->lightlevel);

	//toucingsector

	//subsector

	ImGui::Text("floorz: %d", mobj->floorz);
	ImGui::Text("ceilingz: %d", mobj->ceilingz);
	ImGui::Text("floordrop: %d", mobj->floordrop);
	ImGui::Text("ceilingdrop: %d", mobj->ceilingdrop);

	ImGui::Text("radius: %d", mobj->radius);
	ImGui::Text("height: %d", mobj->height);

	ImGui::Text("momx momy momz: %d %d %d",mobj->momx, mobj->momy, mobj->momz);
	ImGui::Text("pmomz: %d", mobj->pmomz);

	ImGui::Text("tics: %d", mobj->tics);

	ImGui::Text("flags: %u", mobj->flags);
	ImGui::Text("flags2: %u", mobj->flags2);
	ImGui::Text("eflags: %u", mobj->eflags);

	ImGui::Text("tid: %d", mobj->tid);

	if (mobj->tid_prev)
	{
		generic_mobj(*mobj->tid_prev, "tid_prev", recursive_mobj, recursive_player);
	}
	generic_mobj(mobj->tid_next, "tid_next", recursive_mobj, recursive_player);

	ImGui::Text("Color: %s", skincolors[mobj->color].name);


	if (mobj->sprev)
	{
		generic_mobj(*mobj->sprev, "sprev", recursive_mobj, recursive_player);
	}
	generic_mobj(mobj->snext, "snext", recursive_mobj, recursive_player);

	generic_mobj(mobj->hprev, "hprev", recursive_mobj, recursive_player);
	generic_mobj(mobj->hnext, "hnext", recursive_mobj, recursive_player);

	generic_mobj(mobj->itnext, "itnext", recursive_mobj, recursive_player);

	ImGui::Text("health: %d", mobj->health);

	ImGui::Text("movedir: %u", mobj->movedir);
	ImGui::Text("movecount: %d", mobj->movecount);

	generic_mobj(mobj->target, "target", recursive_mobj, recursive_player);

	ImGui::Text("reactiontime: %d", mobj->reactiontime);

	ImGui::Text("threshold: %d", mobj->threshold);

	if (mobj->player)
	{
		char nameid[512];
		snprintf(nameid, sizeof(nameid), "Player###Player%d_%d", recursive_mobj, recursive_player);
		if (ImGui::CollapsingHeader(nameid))
		{
			if (recursive_player >= recursive_max)
			{
				ImGui::Text("Max player recursion reached");
				ImGui::Text("\n");
			}
			else
			{
				recursive_player++;
				PlayerDisplay(mobj->player, recursive_mobj, recursive_player);
			}
		}
	}

	ImGui::Text("lastlook: %d", mobj->lastlook);

	//spawnpoint

	generic_mobj(mobj->tracer, "tracer", recursive_mobj, recursive_player);

	ImGui::Text("friction: %d", mobj->friction);
	ImGui::Text("movefactor: %d", mobj->movefactor);
	ImGui::Text("gravity: %d", mobj->gravity);
	ImGui::Text("lastmomz: %d", mobj->lastmomz);

	ImGui::Text("fuse: %d", mobj->fuse);
	ImGui::Text("watertop: %d", mobj->watertop);
	ImGui::Text("waterbottom: %d", mobj->waterbottom);

	ImGui::Text("mobjnum: %u", mobj->mobjnum);

	ImGui::Text("scale: %d", mobj->scale);
	ImGui::Text("old_scale: %d", mobj->old_scale);
	ImGui::Text("old_scale2: %d", mobj->old_scale2);
	ImGui::Text("destscale: %d", mobj->destscale);
	ImGui::Text("scalespeed: %d", mobj->scalespeed);

	ImGui::Text("extravalue1: %d", mobj->extravalue1);
	ImGui::Text("extravalue2: %d", mobj->extravalue2);
	ImGui::Text("extravalue3: %d", mobj->extravalue3);

	ImGui::Text("cusval: %d", mobj->cusval);
	ImGui::Text("cusmem: %d", mobj->cvmem);

	ImGui::Text("mobjlifetime: %u", mobj->mobjlifetime);

	//standingslope

	ImGui::Text("resetinterp: %s", mobj->resetinterp ? "True" : "False");
	ImGui::Text("colorized: %s", mobj->resetinterp ? "True" : "False");
	ImGui::Text("mirrored: %s", mobj->mirrored ? "True" : "False");

	ImGui::Text("shadowscale: %d", mobj->shadowscale);
	ImGui::Text("whiteshadow: %s", mobj->whiteshadow ? "True" : "False");
	ImGui::Text("shadowscale: %d", mobj->shadowcolor);

	//terrian
	generic_mobj(mobj->terrainOverlay, "terrianoverlay", recursive_mobj, recursive_player);

	ImGui::Text("dispoffset: %d", mobj->dispoffset);

	ImGui::Text("args: %d %d %d %d %d %d %d %d %d %d", mobj->args[0], mobj->args[1], mobj->args[2], mobj->args[3], mobj->args[4], mobj->args[5], mobj->args[6], mobj->args[7], mobj->args[8], mobj->args[9]);
	ImGui::Text("stringargs: %s %s", mobj->stringargs[0] ? mobj->stringargs[0] : "NULL", mobj->stringargs[1] ? mobj->stringargs[1] : "NULL" );

	ImGui::Text("special: %d", mobj->special);
	ImGui::Text("script_args: %d %d %d %d %d %d %d %d %d %d", mobj->script_args[0], mobj->script_args[1], mobj->script_args[2], mobj->script_args[3], mobj->script_args[4], mobj->script_args[5], mobj->script_args[6], mobj->script_args[7], mobj->args[8], mobj->args[9]);
	ImGui::Text("script_stringargs: %s %s", mobj->script_stringargs[0] ? mobj->script_stringargs[0] : "NULL", mobj->script_stringargs[1] ? mobj->script_stringargs[1] : "NULL" );

	ImGui::Text("po_movecount: %d", mobj->po_movecount);

	ImGui::Text("\n");
}

static void MusicDisplay(const musicdef_t *def, char *filename)
{
	if (def == NULL || filename == NULL)
		return;

	ImGui::Text("Volume: %4d/100", def->volume);

	ImGui::Text("File Name: %8s", filename);

	if (def->title)
		ImGui::Text("Title: %s", def->title);

	if (def->source)
		ImGui::Text("Source: %s", def->source);

	if (def->author)
		ImGui::Text("Author: %s", def->author);

	if (def->composers)
		ImGui::Text(" Composers: %s", def->composers);

	if (def->usage)
		ImGui::Text("Usage: %s", def->usage);

	ImGui::Text("Content-id unsafe: %s", def->contentidunsafe ? "True" : "False");

	ImGui::Text("Legacy: %s", def->legacy ? "True" : "False");
}

static void MapDisplay(const mapheader_t *map)
{
	int32_t i;

	ImGui::Text("Map ID: %s", map->lumpname);

	ImGui::Text("Map ID Hash: %u", map->lumpnamehash);

	ImGui::Text("Map ID Lump Number: %u", map->lumpnum);

	ImGui::Text("Map ID Wad Number: %u", WADFILENUM(map->lumpnum));

	ImGui::Text("Just Played: %lu", map->justPlayed);

	if (map->lvlttl[0] != 0)
		ImGui::Text("Level Title: %s", map->lvlttl);

	if (map->subttl[0] != 0)
		ImGui::Text("Subtitle: %s", map->subttl);

	if (map->zonttl[0] != 0)
		ImGui::Text("Zone Title: %s", map->zonttl);

	if (map->actnum[0] != 0)
		ImGui::Text("Actnum: %s", map->actnum);

	if (map->keywords[0] != 0)
		ImGui::Text("Keywords: %s", map->keywords);

	ImGui::Text("Unlock Required: %d", map->unlockrequired);

	ImGui::Text("Level Select: %d", map->levelselect);

	ImGui::Text("Menu Flags: %d", map->menuflags);

	ImGui::Text("Level Flags: %d", map->levelflags);

	ImGui::Text("Type of Level: %u", map->typeoflevel);

	ImGui::Text("Laps: %d", map->numlaps);

	ImGui::Text("Laps Per Section: %d", map->lapspersection);

	ImGui::Text("Gravity: %d", map->gravity);

	for (i = 0; i < map->musname_size; i++)
	{
		if (map->musname[i][0] == 0)
			continue;

		ImGui::Text("Song Filename: %s", map->musname[i]);
	}

	for (i = 0; i < map->encoremusname_size; i++)
	{
		if (map->encoremusname[i][0] == 0)
			continue;

		ImGui::Text("Encore Song Filename: %s", map->encoremusname[i]);
	}

	ImGui::Text("Music Track: %d", map->mustrack);

	ImGui::Text("Music Position: %d", map->mustrack);

	ImGui::Text("Number of Songs: %d", map->musname_size);

	ImGui::Text("Number of Encore Songs: %d", map->encoremusname_size);

	ImGui::Text("Weather: %d", map->weather);

	ImGui::Text("Sky Texture: %s", map->skytexture);

	ImGui::Text("Skybox Scale X: %d", map->skybox_scalex);

	ImGui::Text("Skybox Scale Y: %d", map->skybox_scaley);

	ImGui::Text("Skybox Scale Z: %d", map->skybox_scalez);

	ImGui::Text("Mobj Scale: %d", map->mobj_scale);

	ImGui::Text("Default Waypoint Radius: %d", map->default_waypoint_radius);

	ImGui::Text("Force Wall Transfer: %d", map->use_walltransfer);

	ImGui::Text("Use Terrain: %d", map->use_terrain);

	ImGui::Text("Base Track Complexity: %d", map->base_track_complexity);

	ImGui::Text("Track Modifier Max: %d", map->track_modifier_max);
}

void Impl_ImGui_HandleWindow(void)
{
	if (imgui_setup == false || imgui_frame_active == false)
		return;

	if (cht_debug == 0)
		return;

	int32_t i;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Players"))
		{
			windowstate.player_display = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Mobjs"))
		{
			windowstate.mobj_display = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Music"))
		{
			windowstate.music_display = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Maps"))
		{
			windowstate.map_display = true;
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	if (windowstate.player_display)
	{
		ImGui::Begin("Players", &windowstate.player_display);
		ImGui::InputText("Search", windowstate.player_search, IM_COUNTOF(windowstate.player_search));
		ImGui::SliderInt("Recursion limit", &recursive_max, 0, 255);

		if (ImGui::BeginTabBar("PlayerList"))
		{
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i])
					continue;

				if (windowstate.player_search[0] != 0 && !strcasestr(player_names[i], windowstate.player_search))
					continue;

				player_t *player = &players[i];

				if (ImGui::BeginTabItem(player_names[i]))
				{
					PlayerDisplay(player, 0, 0);

					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	if (windowstate.mobj_display)
	{
		ImGui::Begin("Mobjs", &windowstate.mobj_display);
		ImGui::InputText("Search", windowstate.mobj_search, IM_COUNTOF(windowstate.mobj_search));
		ImGui::SliderInt("Recursion limit", &recursive_max, 0, 255);

		if (ImGui::BeginTabBar("MobjList"))
		{
			thinker_t *th = NULL;
			mobj_t *mobj = NULL;
			i = 0;
			for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
			{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;

				mobj = (mobj_t *)th;

				if (!mobj)
					continue;

				if (windowstate.mobj_search[0] != 0 && !strcasestr(DEH_MobjtypeName(mobj->type), windowstate.mobj_search))
					continue;

				i++;

				char nameid[512];

				snprintf(nameid, sizeof(nameid), "%s###%s%d", DEH_MobjtypeName(mobj->type), DEH_MobjtypeName(mobj->type), i);

				if (ImGui::BeginTabItem(nameid))
				{
					MobjDisplay(mobj, 0, 0);

					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	if (windowstate.music_display)
	{
		ImGui::Begin("Music", &windowstate.music_display);

		if (ImGui::BeginTabBar("Music"))
		{
			if (ImGui::BeginTabItem("Current"))
			{
				char mname[7];
				uint16_t mflags; // unused
				dboolean looping;
				uint8_t i = 0;

				const musicdef_t *def;
				musictype_t format;

				if (!S_MusicInfo(mname, &mflags, &looping))
				{
					ImGui::Text("Song: <NOTHING>");
				}
				else
				{
					def = S_FindMusicDef(mname, &i);
					format = S_MusicType();

#define TEXTMS(label, ms) \
	ImGui::Text("%s%02d:%05.2f", label, ms / 60000, ms % 60000 / 1000.f)

					TEXTMS("Elapsed: ", S_GetMusicPosition());
					TEXTMS(looping ? "Loop B: " : "Duration: ", S_GetMusicLength());

					if (looping)
					{
						TEXTMS("Loop A: ", S_GetMusicLoopPoint());
					}

					if (format)
					{
						musictype_t mustype = S_MusicType();
						ImGui::Text("Format: %s", S_MusicTypeName(mustype));
					}

					MusicDisplay(def, mname);
				}

				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("List"))
			{
				musicdef_t *def;
				char **songs = NULL;
				int32_t size = 0;

				for (def = musicdefstart; def; def = def->next)
				{
					for (i = 0; i < def->numtracks; i++)
					{
						// Wow I hate these... Thanks skin code for reference I guess?
						int32_t len = strlen(def->name[i])+1;
						songs = static_cast<char **>(realloc(songs, sizeof(*songs) * (size + 1)));
						songs[size] = static_cast<char *>(calloc(len, sizeof(songs[size])));
						strncpy(songs[size], def->name[i], len);
						size++;
					}
				}

				if (ImGui::Combo("SongList", &windowstate.music_selected, songs, size) || windowstate.music_def == NULL)
				{
					uint8_t j;
					windowstate.music_def = S_FindMusicDef(songs[windowstate.music_selected], &j);
				}

				if (windowstate.music_def)
				{
					MusicDisplay(windowstate.music_def, songs[windowstate.music_selected]);

					if (ImGui::Button("Play"))
					{
						COM_ImmedExecute("tunes -none");
						COM_ImmedExecute(va("tunes %s", songs[windowstate.music_selected]));
						S_ShowMusicCredit(0, 5*TICRATE, 0);
					}

					if (ImGui::Button("Stop"))
					{
						COM_ImmedExecute("tunes -none");
					}
				}

				for (i = 0; i < size; i++)
				{
					free(songs[i]);
				}

				free(songs);
				songs = NULL;

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	if (windowstate.map_display)
	{
		ImGui::Begin("Maps", &windowstate.map_display);


		if (ImGui::BeginTabBar("Maps"))
		{
			if (ImGui::BeginTabItem("Current"))
			{
				MapDisplay(mapheaderinfo[gamemap-1]);

				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("List"))
			{
				char **maps = NULL;
				int32_t size = 0;

				for (i = 0; i < nummapheaders; i++)
				{
					if (!mapheaderinfo[i])
						continue;
					if (mapheaderinfo[i]->lumpnum == LUMPERROR)
						continue;

					// Wow I hate these... Thanks skin code for reference I guess?
					int32_t len = strlen(mapheaderinfo[i]->lumpname)+1;
					maps = static_cast<char **>(realloc(maps, sizeof(*maps) * (size + 1)));
					maps[size] = static_cast<char *>(calloc(len, sizeof(maps[size])));
					strncpy(maps[size], mapheaderinfo[i]->lumpname, len);
					size++;
				}

				if (ImGui::Combo("MapList", &windowstate.map_selected, maps, size) || windowstate.mapheader == NULL)
				{
					windowstate.mapheader = mapheaderinfo[windowstate.map_selected];
				}

				if (windowstate.mapheader)
				{
					MapDisplay(windowstate.mapheader);
				}

				if (ImGui::Button("Warp"))
				{
					COM_ImmedExecute(va("map %s -f", maps[windowstate.map_selected]));
				}

				for (i = 0; i < size; i++)
				{
					free(maps[i]);
				}

				free(maps);
				maps = NULL;

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}

void Impl_ImGui_HandleRender(void)
{
	if (imgui_setup == false || imgui_frame_active == false)
		return;

	ImGui::Render();
}
