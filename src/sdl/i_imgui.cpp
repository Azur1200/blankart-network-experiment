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
#include "../st_stuff.h"
#include "../deh_tables.h"
#include "../dehacked.h"
#include "../z_zone.h"
#include "../command.h"

char *glsl_version = nullptr;
static bool imgui_opengl_init = false;
static bool imgui_software_init = false;
static bool imgui_setup= false;
static bool imgui_frame_active = false;
static bool imgui_active = false;

void Impl_ImGui_CreateContext(void)
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

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

	if (cht_debug == 0)
		return;

	ImGuiIO& io = ImGui::GetIO();
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
			ImGui::Text("");
		}
		else
		{
			recursive_mobj++;
			MobjDisplay(mobj, recursive_mobj, recursive_player);
			ImGui::Text("");
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

	ImGui::Text("Position: %d", player->position);
	ImGui::Text("Distance to FInish: %d", player->distancetofinish);
	ImGui::Text("Laps: %d", player->laps);
	ImGui::Text("Rings: %d", player->rings);

	ImGui::Text("Follower Skin: %s", player->followerskin == -1 ? "NONE" : followers[player->followerskin].name);
	ImGui::Text("Follower Ready: %s", player->followerready ? "True" : "False");
	ImGui::Text("Follower Color: %s", skincolors[player->followercolor].name);
	generic_mobj(player->follower, "Follower", recursive_mobj, recursive_player);

	ImGui::Text("Char Flags: %d", player->charflags);

	ImGui::Text("Follow Item: %s", DEH_MobjtypeName(player->followitem));
	generic_mobj(player->followmobj, "followmobj", recursive_mobj, recursive_player);

	ImGui::Text("Score: %d", player->charflags);

	ImGui::Text("No Control: %d", player->nocontrol);
	ImGui::Text("Carry: %d", player->carry);
	ImGui::Text("Dye: %d", player->dye);

	generic_mobj(player->mo, "Mobj", recursive_mobj, recursive_player);
}

static void MobjDisplay(mobj_t *mobj, int32_t recursive_mobj, int32_t recursive_player)
{
	ImGui::Text("x y z: %d %d %d", mobj->x, mobj->y, mobj->z);
	ImGui::Text("old_x old_y old_z: %d %d %d", mobj->old_x, mobj->old_y, mobj->old_z);
	ImGui::Text("old_x2 old_y2 old_z2: %d %d %d", mobj->old_x2, mobj->old_y2, mobj->old_z2);

	ImGui::Text("Type: %s (%d)", DEH_MobjtypeName(mobj->type), mobj->type);
	//mobjinfo

	generic_mobj(mobj->bnext, "bnext", recursive_mobj, recursive_player);
	if (mobj->bprev)
	{
		generic_mobj(*mobj->bprev, "bprev", recursive_mobj, recursive_player);
	}

	ImGui::Text("angle pitch roll: %d %d %d", mobj->angle, mobj->pitch, mobj->roll);
	ImGui::Text("slopepitch sloperoll: %d %d", mobj->slopepitch, mobj->sloperoll);
	ImGui::Text("old_slopepitch old_sloperoll: %d %d", mobj->old_slopepitch, mobj->old_sloperoll);
	ImGui::Text("old_slopepitch2 old_sloperol2l: %d %d", mobj->old_slopepitch2, mobj->old_sloperoll2);
	ImGui::Text("old_angle old_pitch old_roll: %d %d %d", mobj->old_angle, mobj->old_pitch, mobj->old_roll);
	ImGui::Text("old_angle2 old_pitch2 old_roll2: %d %d %d", mobj->old_angle2, mobj->old_pitch2, mobj->old_roll2);

	ImGui::Text("rollangle: %d", mobj->frame);
	//sprite
	ImGui::Text("frame: %d", mobj->frame);
	ImGui::Text("sprite2: %d", mobj->sprite2);
	ImGui::Text("anim_duration: %d", mobj->anim_duration);

	ImGui::Text("renderflags: %d", mobj->renderflags);
	ImGui::Text("renderflags2: %d", mobj->renderflags2);
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

	ImGui::Text("flags: %d", mobj->flags);
	ImGui::Text("flags2: %d", mobj->flags2);
	ImGui::Text("eflags: %d", mobj->eflags);

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

	ImGui::Text("movedir: %d", mobj->movedir);
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
				ImGui::Text("");
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

	ImGui::Text("mobjnum: %d", mobj->mobjnum);

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

	ImGui::Text("mobjlifetime: %d", mobj->mobjlifetime);

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
				int32_t j;
				char **songs = NULL;
				int32_t size = 0;

				for (def = musicdefstart; def; def = def->next)
				{
					for (i = 0; i < def->numtracks; i++)
					{
						// Wow I hate these... Thanks skin code for reference I guess?
						int32_t len = strlen(def->name[i])+1;
						songs = static_cast<char **>(Z_Realloc(songs, sizeof(*songs) * (size + 1), PU_STATIC, NULL));
						songs[size] = static_cast<char *>(Z_Calloc(len, PU_STATIC, NULL));
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
					const musicdef_t *def = windowstate.music_def;

					MusicDisplay(def, songs[windowstate.music_selected]);

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
					Z_Free(songs[i]);
				}

				Z_Free(songs);
				songs = NULL;

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
