// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by John "JTE" Muniz.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_mobjlib.c
/// \brief mobj/thing library for Lua scripting

#include "doomdef.h"
#include "r_skins.h"
#include "p_local.h"
#include "g_game.h"
#include "p_setup.h"

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hud.h" // hud_running errors
#include "lua_hook.h" // hook_cmd_running errors

// mobj_ fields
#define MOBJFIELDS(X) \
	X(valid) \
	X(x) \
	X(y) \
	X(z) \
	X(snext) \
	X(sprev) \
	X(angle) \
	X(pitch) \
	X(roll) \
	X(slopepitch) \
	X(sloperoll) \
	X(rollangle) \
	X(sprite) \
	X(frame) \
	X(sprite2) \
	X(anim_duration) \
	X(spritexscale) \
	X(spriteyscale) \
	X(realxscale) \
	X(realyscale) \
	X(spritexoffset) \
	X(spriteyoffset) \
	X(realxoffset) \
	X(realyoffset) \
	X(floorspriteslope) \
	X(lightlevel) \
	X(sectors) \
	X(subsector) \
	X(floorz) \
	X(ceilingz) \
	X(floorrover) \
	X(ceilingrover) \
	X(radius) \
	X(height) \
	X(momx) \
	X(momy) \
	X(momz) \
	X(pmomz) \
	X(tics) \
	X(state) \
	X(flags) \
	X(flags2) \
	X(eflags) \
	X(renderflags) \
	X(renderflags2) \
	X(skin) \
	X(voice) \
	X(color) \
	X(bnext) \
	X(bprev) \
	X(hnext) \
	X(hprev) \
	X(type) \
	X(info) \
	X(health) \
	X(movedir) \
	X(movecount) \
	X(target) \
	X(reactiontime) \
	X(threshold) \
	X(player) \
	X(lastlook) \
	X(spawnpoint) \
	X(tracer) \
	X(friction) \
	X(movefactor) \
	X(gravity) \
	X(fuse) \
	X(watertop) \
	X(waterbottom) \
	X(mobjnum) \
	X(scale) \
	X(destscale) \
	X(scalespeed) \
	X(extravalue1) \
	X(extravalue2) \
	X(extravalue3) \
	X(cusval) \
	X(cvmem) \
	X(mobjlifetime) \
	X(standingslope) \
	X(colorized) \
	X(mirrored) \
	X(shadowscale) \
	X(whiteshadow) \
	X(sprxoff) \
	X(spryoff) \
	X(sprzoff) \
	X(bakexoff) \
	X(bakeyoff) \
	X(bakezoff) \
	X(bakexpiv) \
	X(bakeypiv) \
	X(bakezpiv) \
	X(terrain) \
	X(dispoffset) \
	X(tid) \
	X(special) \
	X(args) \
	X(stringargs) \
	X(bubbleplow) \
	X(bubbleplowforget)

enum mobj_e
{
#define ENUMITEM(name) mobj_##name,
	MOBJFIELDS(ENUMITEM)
};

static const char *const mobj_opt[] = {
#define ENUMITEMNAME(name) #name,
	MOBJFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

#define UNIMPLEMENTED luaL_error(L, LUA_QL("mobj_t") " field " LUA_QS " is not implemented for Lua and cannot be accessed.", mobj_opt[field])

#define F(x) x - 'A'
static uint32_t spr2frame(uint8_t spr2, uint32_t frame)
{
	switch (spr2)
	{
	case SPR2_STIN:
	case SPR2_STGL: case SPR2_STGR: case SPR2_STLL: case SPR2_STLR:
		return F('A') + (frame & 1);
	case SPR2_STIL:
		return F('C') + (frame & 1);
	case SPR2_STIR:
		return F('E') + (frame & 1);
	case SPR2_SLWN:
	case SPR2_SLGL: case SPR2_SLGR: case SPR2_SLLL: case SPR2_SLLR:
		return frame & 1 ? F('G') : F('J');
	case SPR2_SLWL:
		return frame & 1 ? F('H') : F('K');
	case SPR2_SLWR:
		return frame & 1 ? F('I') : F('L');
	case SPR2_FSTN:
	case SPR2_FSGL: case SPR2_FSGR: case SPR2_FSLL: case SPR2_FSLR:
		return frame & 1 ? F('J') : F('A');
	case SPR2_FSTL:
		return frame & 1 ? F('K') : F('C');
	case SPR2_FSTR:
		return frame & 1 ? F('L') : F('E');
	case SPR2_DRLN: case SPR2_DRLO: case SPR2_DRLI:
		return F('M') + (frame & 1);
	case SPR2_DRRN: case SPR2_DRRO: case SPR2_DRRI:
		return F('O') + (frame & 1);
	case SPR2_SPIN: case SPR2_DEAD:
		return F('Q');
	case SPR2_SIGN:
		return F('S');
	case SPR2_KART:
		if ((frame & FF_FRAMEMASK) == 0)
			return F('R'); // squish
		else
			return F('S') + (frame & FF_FRAMEMASK);
	default:
		return F('A'); // idk???
	}
}
#undef F

int mobj_fields_ref = LUA_NOREF;

// iterates through a mobj's 'touching' sectorlist!
static int lib_iterateMobjSectors(lua_State *L)
{
	sector_t *state = NULL;
	sector_t *sec = NULL;
	INLEVEL

	if (lua_gettop(L) < 2)
		return luaL_error(L, "Don't call mobj.sectors() directly, use it as 'for rover in mobj.sectors do <block> end'.");

	msecnode_t *node = (msecnode_t *)lua_touserdata(L, lua_upvalueindex(1));
	if (node == NULL)
		return 0; // no sectorlist to iterate through sorry!

	state = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));

	lua_settop(L, 2);
	lua_remove(L, 1); // remove state now.

	if (!lua_isnil(L, 1))
	{
		sec = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
		sec = node->m_sector;
	}
	else
		sec = state; // state is used as the "start" of the sectorlist

	if (sec)
	{
		node = node->m_sectorlist_next;
		lua_pushlightuserdata(L, node);
		lua_replace(L, lua_upvalueindex(1));			
		LUA_PushUserdata(L, sec, META_SECTOR);
		return 1;
	}
	return 0;
}

static int mobj_iterate(lua_State *L)
{
	lua_pushvalue(L, lua_upvalueindex(1)); // iterator function, or the "generator"
	lua_pushvalue(L, lua_upvalueindex(2)); // state (used as the "start" of the list for our purposes
	lua_pushnil(L); // initial value (unused)
	return 3;
}

static int mobj_get(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	enum mobj_e field = Lua_optoption(L, 2, -1, mobj_fields_ref);
	lua_settop(L, 2);

	if (!mo || !G_GamestateUsesLevel()) {
		if (field == mobj_valid) {
			lua_pushdboolean(L, 0);
			return 1;
		}
		if (!mo) {
			return LUA_ErrInvalid(L, "mobj_t");
		} else
			return luaL_error(L, "Do not access an mobj_t field outside a level!");
	}

	switch(field)
	{
	case mobj_valid:
		lua_pushdboolean(L, 1);
		break;
	case mobj_x:
		lua_pushfixed(L, mo->x);
		break;
	case mobj_y:
		lua_pushfixed(L, mo->y);
		break;
	case mobj_z:
		lua_pushfixed(L, mo->z);
		break;
	case mobj_snext:
		LUA_PushUserdata(L, mo->snext, META_MOBJ);
		break;
	case mobj_sprev:
		// sprev is actually the previous mobj's snext pointer,
		// or the subsector->sector->thing_list if there is no previous mobj,
		// i.e. it will always ultimately point to THIS mobj -- so that's actually not useful to Lua and won't be included.
		return UNIMPLEMENTED;
	case mobj_angle:
		lua_pushangle(L, mo->angle);
		break;
	case mobj_pitch:
		lua_pushangle(L, mo->pitch);
		break;
	case mobj_roll:
		lua_pushangle(L, mo->roll);
		break;
	case mobj_slopepitch:
		lua_pushangle(L, mo->slopepitch);
		break;
	case mobj_sloperoll:
		lua_pushangle(L, mo->sloperoll);
		break;
	case mobj_rollangle:
		lua_pushangle(L, mo->rollangle);
		break;
	case mobj_sprite:
		lua_pushinteger(L, mo->sprite);
		break;
	case mobj_frame:
	{
		uint32_t frame = mo->frame;
		// translate SPR2 back to frames
		if (lua_compatmode && mo->sprite == SPR_PLAY)
			frame = (frame & ~FF_FRAMEMASK) | spr2frame(mo->sprite2, mo->frame);
		lua_pushinteger(L, frame);
		break;
	}
	case mobj_sprite2:
		lua_pushinteger(L, mo->sprite2);
		break;
	case mobj_anim_duration:
		lua_pushinteger(L, mo->anim_duration);
		break;
	case mobj_spritexscale:
		lua_pushfixed(L, mo->spritexscale);
		break;
	case mobj_spriteyscale:
		lua_pushfixed(L, mo->spriteyscale);
		break;
	case mobj_realxscale:
		lua_pushfixed(L, mo->realxscale);
		break;
	case mobj_realyscale:
		lua_pushfixed(L, mo->realyscale);
		break;
	case mobj_spritexoffset:
		lua_pushfixed(L, mo->spritexoffset);
		break;
	case mobj_spriteyoffset:
		lua_pushfixed(L, mo->spriteyoffset);
		break;
	case mobj_realxoffset:
		lua_pushfixed(L, mo->realxoffset);
		break;
	case mobj_realyoffset:
		lua_pushfixed(L, mo->realyoffset);
		break;
	case mobj_floorspriteslope:
		LUA_PushUserdata(L, mo->floorspriteslope, META_SLOPE);
		break;
	case mobj_lightlevel:
		lua_pushinteger(L, mo->lightlevel);
		break;
	case mobj_sectors:
		lua_pushlightuserdata(gL, mo->touching_sectorlist);
		lua_pushcclosure(L, lib_iterateMobjSectors, 1);
		LUA_PushUserdata(L, mo->touching_sectorlist ? mo->touching_sectorlist->m_sector : NULL, META_SECTOR);
		lua_pushcclosure(L, mobj_iterate, 2);
		return 1;
	case mobj_subsector:
		LUA_PushUserdata(L, mo->subsector, META_SUBSECTOR);
		break;
	case mobj_floorz:
		lua_pushfixed(L, mo->floorz);
		break;
	case mobj_ceilingz:
		lua_pushfixed(L, mo->ceilingz);
		break;
	case mobj_floorrover:
		LUA_PushUserdata(L, mo->floorrover, META_FFLOOR);
		break;
	case mobj_ceilingrover:
		LUA_PushUserdata(L, mo->ceilingrover, META_FFLOOR);
		break;
	case mobj_radius:
		lua_pushfixed(L, mo->radius);
		break;
	case mobj_height:
		lua_pushfixed(L, mo->height);
		break;
	case mobj_momx:
		lua_pushfixed(L, mo->momx);
		break;
	case mobj_momy:
		lua_pushfixed(L, mo->momy);
		break;
	case mobj_momz:
		lua_pushfixed(L, mo->momz);
		break;
	case mobj_pmomz:
		lua_pushfixed(L, mo->pmomz);
		break;
	case mobj_tics:
		lua_pushinteger(L, mo->tics);
		break;
	case mobj_state: // state number, not struct
		lua_pushinteger(L, mo->state-states);
		break;
	case mobj_flags:
		lua_pushinteger(L, mo->flags);
		break;
	case mobj_flags2:
	{
		uint32_t flags2 = mo->flags2;
		if (lua_compatmode && (mo->renderflags & RF_DONTDRAW) == RF_DONTDRAW)
			flags2 |= MF2_DONTDRAW;
		if (lua_compatmode && (mo->renderflags & RF_GHOSTLY))
			flags2 |= MF2_SHADOW;
		lua_pushinteger(L, flags2);
		break;
	}
	case mobj_eflags:
	{
		uint32_t eflags = mo->eflags;  // yes, not uint16_t
		if (lua_compatmode)
		{
			// The old eflags compat system causes rendering issues for characters like
			// the MK64 cast. This system causes rendering issues in certain gameplay
			// mods. At least in this case, character mods take priority, so we set
			// up compatibility like so:
			eflags |=
				mo->renderflags & RF_DONTDRAW ? ~mo->renderflags & RF_DONTDRAW : 0;
		}
		lua_pushinteger(L, eflags);
		break;
	}
	case mobj_renderflags:
		lua_pushinteger(L, mo->renderflags);
		break;
	case mobj_renderflags2:
		lua_pushinteger(L, mo->renderflags2);
		break;
	case mobj_skin: // skin name or nil, not struct
		if (!mo->skin)
			return 0;
		lua_pushstring(L, ((skin_t *)mo->skin)->name);
		break;
	case mobj_voice: // just push the struct damnit!
		LUA_PushUserdata(L, mo->voice, META_VOICE);
		break;
	case mobj_color:
		lua_pushinteger(L, mo->color);
		break;
	case mobj_bnext:
		if (mo->blocknode && mo->blocknode->bnext) {
			LUA_PushUserdata(L, mo->blocknode->bnext->mobj.mobj, META_MOBJ);
			break;
		}
		else
			return 0;
	case mobj_bprev:
		// bprev -- same deal as sprev above, but for the blockmap.
		return UNIMPLEMENTED;
	case mobj_hnext:
		if (mo->hnext && P_MobjWasRemoved(mo->hnext))
		{ // don't put invalid mobj back into Lua.
			P_SetTarget(&mo->hnext, NULL);
			return 0;
		}
		LUA_PushUserdata(L, mo->hnext, META_MOBJ);
		break;
	case mobj_hprev:
		if (mo->hprev && P_MobjWasRemoved(mo->hprev))
		{ // don't put invalid mobj back into Lua.
			P_SetTarget(&mo->hprev, NULL);
			return 0;
		}
		LUA_PushUserdata(L, mo->hprev, META_MOBJ);
		break;
	case mobj_type:
		lua_pushinteger(L, mo->type);
		break;
	case mobj_info:
		LUA_PushUserdata(L, &mobjinfo[mo->type], META_MOBJINFO);
		break;
	case mobj_health:
		lua_pushinteger(L, mo->health);
		break;
	case mobj_movedir:
		lua_pushinteger(L, mo->movedir);
		break;
	case mobj_movecount:
		lua_pushinteger(L, mo->movecount);
		break;
	case mobj_target:
		if (mo->target && P_MobjWasRemoved(mo->target))
		{ // don't put invalid mobj back into Lua.
			P_SetTarget(&mo->target, NULL);
			return 0;
		}
		LUA_PushUserdata(L, mo->target, META_MOBJ);
		break;
	case mobj_reactiontime:
		lua_pushinteger(L, mo->reactiontime);
		break;
	case mobj_threshold:
		lua_pushinteger(L, mo->threshold);
		break;
	case mobj_player:
		LUA_PushUserdata(L, mo->player, META_PLAYER);
		break;
	case mobj_lastlook:
		lua_pushinteger(L, mo->lastlook);
		break;
	case mobj_spawnpoint:
		LUA_PushUserdata(L, mo->spawnpoint, META_MAPTHING);
		break;
	case mobj_tracer:
		if (mo->tracer && P_MobjWasRemoved(mo->tracer))
		{ // don't put invalid mobj back into Lua.
			P_SetTarget(&mo->tracer, NULL);
			return 0;
		}
		LUA_PushUserdata(L, mo->tracer, META_MOBJ);
		break;
	case mobj_friction:
		lua_pushfixed(L, mo->friction);
		break;
	case mobj_movefactor:
		lua_pushfixed(L, mo->movefactor);
		break;
	case mobj_gravity:
		lua_pushfixed(L, mo->gravity);
		break;
	case mobj_fuse:
		lua_pushinteger(L, mo->fuse);
		break;
	case mobj_watertop:
		lua_pushfixed(L, mo->watertop);
		break;
	case mobj_waterbottom:
		lua_pushfixed(L, mo->waterbottom);
		break;
	case mobj_mobjnum:
		// mobjnum is a networking thing generated for $$$.sav
		// and therefore shouldn't be used by Lua.
		return UNIMPLEMENTED;
	case mobj_scale:
		lua_pushfixed(L, mo->scale);
		break;
	case mobj_destscale:
		lua_pushfixed(L, mo->destscale);
		break;
	case mobj_scalespeed:
		lua_pushfixed(L, mo->scalespeed);
		break;
	case mobj_extravalue1:
		lua_pushinteger(L, mo->extravalue1);
		break;
	case mobj_extravalue2:
		lua_pushinteger(L, mo->extravalue2);
		break;
	case mobj_extravalue3:
		lua_pushinteger(L, mo->extravalue3);
		break;
	case mobj_cusval:
		lua_pushinteger(L, mo->cusval);
		break;
	case mobj_cvmem:
		lua_pushinteger(L, mo->cvmem);
		break;
	case mobj_mobjlifetime:
		lua_pushinteger(L, mo->mobjlifetime);
		break;
	case mobj_standingslope:
		LUA_PushUserdata(L, mo->standingslope, META_SLOPE);
		break;
	case mobj_colorized:
		lua_pushdboolean(L, mo->colorized);
		break;
	case mobj_mirrored:
		lua_pushdboolean(L, mo->mirrored);
		break;
	case mobj_shadowscale:
		lua_pushfixed(L, mo->shadowscale);
		break;
	case mobj_whiteshadow:
		lua_pushdboolean(L, mo->whiteshadow);
		break;
	case mobj_sprxoff:
		lua_pushfixed(L, mo->sprxoff);
		break;
	case mobj_spryoff:
		lua_pushfixed(L, mo->spryoff);
		break;
	case mobj_sprzoff:
		lua_pushfixed(L, mo->sprzoff);
		break;
	case mobj_bakexoff:
		lua_pushfixed(L, mo->bakexoff);
		break;
	case mobj_bakeyoff:
		lua_pushfixed(L, mo->bakeyoff);
		break;
	case mobj_bakezoff:
		lua_pushfixed(L, mo->bakezoff);
		break;
	case mobj_bakexpiv:
		lua_pushfixed(L, mo->bakexpiv);
		break;
	case mobj_bakeypiv:
		lua_pushfixed(L, mo->bakeypiv);
		break;
	case mobj_bakezpiv:
		lua_pushfixed(L, mo->bakezpiv);
		break;
	case mobj_terrain:
		LUA_PushUserdata(L, mo->terrain, META_TERRAIN);
		break;
	case mobj_dispoffset:
		lua_pushinteger(L, mo->dispoffset);
		break;
	case mobj_tid:
		lua_pushinteger(L, mo->tid);
		break;
	case mobj_special:
		lua_pushinteger(L, mo->special);
		break;
	case mobj_args:
		LUA_PushUserdata(L, mo->args, META_THINGARGS);
		break;
	case mobj_stringargs:
		LUA_PushUserdata(L, mo->stringargs, META_THINGSTRINGARGS);
		break;
	case mobj_bubbleplow:
		if (mo->bubbleplow && P_MobjWasRemoved(mo->bubbleplow))
		{ // don't put invalid mobj back into Lua.
			P_SetTarget(&mo->bubbleplow, NULL);
			return 0;
		}
		LUA_PushUserdata(L, mo->bubbleplow, META_MOBJ);
		break;
	case mobj_bubbleplowforget:
		lua_pushinteger(L, mo->bubbleplowforget);
		break;
	default: // extra custom variables in Lua memory
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
		I_Assert(lua_istable(L, -1));
		lua_pushlightuserdata(L, mo);
		lua_rawget(L, -2);
		if (!lua_istable(L, -1)) { // no extra values table
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no extvars table or field named '%s'; returning nil.\n"), "mobj_t", lua_tostring(L, 2));
			return 0;
		}
		lua_pushvalue(L, 2); // field name
		lua_gettable(L, -2);
		if (lua_isnil(L, -1)) // no value for this field
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "mobj_t", lua_tostring(L, 2));
		break;
	}
	return 1;
}

static inline uint32_t eflags_vischeck(uint32_t eflags)
{
	uint8_t playerhide;

	playerhide = 0;
	if (eflags & RF_DONTDRAW)
	{
		// Visibility checks are necessary
		playerhide = (uint8_t)((eflags & RF_DONTDRAW) >> RF_HIDESHIFT);

		// Hiding is the opposite of visibility.
		playerhide = (~playerhide) & 0x0F;
	}

	return ((uint32_t)(playerhide) << RF_HIDESHIFT);
}

#define NOSET luaL_error(L, LUA_QL("mobj_t") " field " LUA_QS " should not be set directly.", mobj_opt[field])
#define NOSETPOS luaL_error(L, LUA_QL("mobj_t") " field " LUA_QS " should not be set directly. Use " LUA_QL("P_Move") ", " LUA_QL("P_TryMove") ", or " LUA_QL("P_SetOrigin") ", or " LUA_QL("P_MoveOrigin") " instead.", mobj_opt[field])
static int mobj_set(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	enum mobj_e field = Lua_optoption(L, 2, mobj_valid, mobj_fields_ref);;
	lua_settop(L, 3);

	INLEVEL

	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	if (hud_running)
		return luaL_error(L, "Do not alter mobj_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter mobj_t in CMD building code!");

	switch(field)
	{
	case mobj_valid:
		return NOSET;
	case mobj_x:
		return NOSETPOS;
	case mobj_y:
		return NOSETPOS;
	case mobj_z:
	{
		// z doesn't cross sector bounds so it's okay.
		tm_t ptm = g_tm;

		mo->z = luaL_checkfixed(L, 3);
		P_CheckPosition(mo, mo->x, mo->y, NULL);
		mo->floorz = g_tm.floorz;
		mo->ceilingz = g_tm.ceilingz;
		mo->floorrover = g_tm.floorrover;
		mo->ceilingrover = g_tm.ceilingrover;
		P_RestoreTMStruct(ptm);
		break;
	}
	case mobj_snext:
		return NOSETPOS;
	case mobj_sprev:
		return UNIMPLEMENTED;
	case mobj_angle:
		mo->angle = luaL_checkangle(L, 3);
		if (mo->player)
			P_SetPlayerAngle(mo->player, mo->angle);
		break;
	case mobj_pitch:
		mo->pitch = luaL_checkangle(L, 3);
		break;
	case mobj_roll:
		mo->roll = luaL_checkangle(L, 3);
		break;
	case mobj_slopepitch:
		mo->slopepitch = luaL_checkangle(L, 3);
		break;
	case mobj_sloperoll:
		mo->sloperoll = luaL_checkangle(L, 3);
		break;
	case mobj_rollangle:
		mo->rollangle = luaL_checkangle(L, 3);
		break;
	case mobj_sprite:
	{
		spritenum_t newsprite = luaL_checkinteger(L, 3);
		if (lua_compatmode)
		{
			if (mo->sprite != SPR_PLAY && newsprite == SPR_PLAY) // switch to spr2
			{
				uint8_t outframe;
				mo->sprite2 = P_KartFrameToSprite2(mo->skin, mo->frame, &outframe);
				mo->frame = (mo->frame & ~FF_FRAMEMASK) | outframe;
			}
			else if (mo->sprite == SPR_PLAY && newsprite != SPR_PLAY) // switch from spr2
			{
				mo->frame = spr2frame(mo->sprite2, mo->frame);
			}
		}
		mo->sprite = newsprite;
		break;
	}
	case mobj_frame:
		// Check for SPR2
		if (lua_compatmode && mo->sprite == SPR_PLAY)
		{
			uint8_t outframe;
			uint32_t newframe = luaL_checkinteger(L, 3);
			mo->sprite2 = P_KartFrameToSprite2(mo->skin, newframe, &outframe);
			mo->frame = (newframe & ~FF_FRAMEMASK) | outframe;
		}
		else
			mo->frame = (uint32_t)luaL_checkinteger(L, 3);
		break;
	case mobj_sprite2:
		mo->sprite2 = P_GetSkinSprite2(((skin_t *)mo->skin), (uint8_t)luaL_checkinteger(L, 3), mo->player);
		break;
	case mobj_anim_duration:
		mo->anim_duration = (uint16_t)luaL_checkinteger(L, 3);
		break;
	case mobj_spritexscale:
		mo->spritexscale = luaL_checkfixed(L, 3);
		break;
	case mobj_spriteyscale:
		mo->spriteyscale = luaL_checkfixed(L, 3);
		break;
	case mobj_realxscale:
		mo->realxscale = luaL_checkfixed(L, 3);
		break;
	case mobj_realyscale:
		mo->realyscale = luaL_checkfixed(L, 3);
		break;
	case mobj_spritexoffset:
		mo->spritexoffset = luaL_checkfixed(L, 3);
		break;
	case mobj_spriteyoffset:
		mo->spriteyoffset = luaL_checkfixed(L, 3);
		break;
	case mobj_realxoffset:
		mo->realxoffset = luaL_checkfixed(L, 3);
		break;
	case mobj_realyoffset:
		mo->realyoffset = luaL_checkfixed(L, 3);
		break;
	case mobj_floorspriteslope:
		return NOSET;
	case mobj_lightlevel:
		mo->lightlevel = (int16_t)luaL_checkinteger(L, 3);
		break;
	case mobj_sectors:
		return NOSETPOS;
	case mobj_subsector:
		return NOSETPOS;
	case mobj_floorz:
		return NOSETPOS;
	case mobj_ceilingz:
		return NOSETPOS;
	case mobj_floorrover:
		return NOSET;
	case mobj_ceilingrover:
		return NOSET;
	case mobj_radius:
	{
		tm_t ptm = g_tm;

		mo->radius = luaL_checkfixed(L, 3);
		if (mo->radius < 0)
			mo->radius = 0;
		P_CheckPosition(mo, mo->x, mo->y, NULL);
		mo->floorz = g_tm.floorz;
		mo->ceilingz = g_tm.ceilingz;
		mo->floorrover = g_tm.floorrover;
		mo->ceilingrover = g_tm.ceilingrover;
		P_RestoreTMStruct(ptm);
		break;
	}
	case mobj_height:
	{
		tm_t ptm = g_tm;

		mo->height = luaL_checkfixed(L, 3);
		if (mo->height < 0)
			mo->height = 0;
		P_CheckPosition(mo, mo->x, mo->y, NULL);
		mo->floorz = g_tm.floorz;
		mo->ceilingz = g_tm.ceilingz;
		mo->floorrover = g_tm.floorrover;
		mo->ceilingrover = g_tm.ceilingrover;
		P_RestoreTMStruct(ptm);
		break;
	}
	case mobj_momx:
		mo->momx = luaL_checkfixed(L, 3);
		break;
	case mobj_momy:
		mo->momy = luaL_checkfixed(L, 3);
		break;
	case mobj_momz:
		mo->momz = luaL_checkfixed(L, 3);
		break;
	case mobj_pmomz:
		mo->pmomz = luaL_checkfixed(L, 3);
		mo->eflags |= MFE_APPLYPMOMZ;
		break;
	case mobj_tics:
		mo->tics = luaL_checkinteger(L, 3);
		break;
	case mobj_state: // set state by enum
	{
		statenum_t state = luaL_checkinteger(L, 3);

		if (state < 0 || state >= numstates)
			return luaL_error(L, "State %d out of range (0 - %d)", state, numstates - 1);

		if (mo->player)
			P_SetPlayerMobjState(mo, state);
		else
			P_SetMobjState(mo, state);
		break;
	}
	case mobj_flags: // special handling for MF_NOBLOCKMAP and MF_NOSECTOR
	{
		uint32_t flags = luaL_checkinteger(L, 3);
		if ((flags & (MF_NOBLOCKMAP|MF_NOSECTOR)) != (mo->flags & (MF_NOBLOCKMAP|MF_NOSECTOR)))
		{
			P_UnsetThingPosition(mo);
			mo->flags = flags;
			if (flags & MF_NOSECTOR && sector_list)
			{
				P_DelSeclist(sector_list);
				sector_list = NULL;
			}
			mo->snext = NULL, mo->sprev = NULL;
			P_SetThingPosition(mo);
		}
		else
			mo->flags = flags;
		break;
	}
	case mobj_flags2:
	{
		uint32_t flags2 = luaL_checkinteger(L, 3);
		if (lua_compatmode)
		{
			if (flags2 & MF2_DONTDRAW)
				mo->renderflags |= RF_DONTDRAW;
			else
				mo->renderflags &= ~RF_DONTDRAW;

			if (flags2 & MF2_SHADOW)
				mo->renderflags |= RF_GHOSTLY;
			else
				mo->renderflags &= ~RF_GHOSTLYMASK;
		}
		mo->flags2 = flags2;
		break;
	}
	case mobj_eflags:
	{
		uint32_t eflags = luaL_checkinteger(L, 3);
		uint32_t visflags = 0;
		if (lua_compatmode)
		{
			// eeeyup.
			// Mhm. Yeah.
			visflags = eflags_vischeck(eflags);

			if ((visflags) && ((mo->renderflags & RF_DONTDRAW) != RF_DONTDRAW))
			{
				mo->renderflags &= ~RF_DONTDRAW;
				mo->renderflags |= visflags;
			}
		}
		mo->eflags = (uint16_t)eflags;
		break;
	}
	case mobj_renderflags:
		mo->renderflags = (uint32_t)luaL_checkinteger(L, 3);
		break;
	case mobj_renderflags2:
		mo->renderflags2 = (uint32_t)luaL_checkinteger(L, 3);
		break;
	case mobj_skin: // set skin by name
	{
		int32_t i;
		char skin[SKINNAMESIZE+1]; // all skin names are limited to this length
		strlcpy(skin, luaL_checkstring(L, 3), sizeof skin);
		strlwr(skin); // all skin names are lowercase
		uint32_t hash = HASH32(skin, strlen(skin));
		for (i = 0; i < numskins; i++)
		{
			if (skins[i]->namehash != hash)
				continue;

			if (fastcmp(skins[i]->name, skin))
			{
				if (!mo->player || R_SkinUsable(mo->player-players, i))
				{
					R_ApplySkin(mo, skins[i]);
				}
				return 0;
			}
		}
		return luaL_error(L, "mobj.skin '%s' not found!", skin);
	}
	case mobj_voice:
		// as much as i want to allow ironman without overwriting the player's skin,
		// letting the mobj's voice desync with the skin has some nasty implications at the moment
		return NOSET;
		//mo->voice = *((kartvoice_t **)luaL_checkudata(L, 3, META_VOICE));
		//break;
	case mobj_color:
	{
		uint16_t newcolor = (uint16_t)luaL_checkinteger(L,3);
		if (newcolor >= numskincolors)
			return luaL_error(L, "mobj.color %d out of range (0 - %d).", newcolor, numskincolors-1);
		mo->color = newcolor;
		break;
	}
	case mobj_bnext:
		return NOSETPOS;
	case mobj_bprev:
		return UNIMPLEMENTED;
	case mobj_hnext:
		if (lua_isnil(L, 3))
			P_SetTarget(&mo->hnext, NULL);
		else
		{
			mobj_t *hnext = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
			P_SetTarget(&mo->hnext, hnext);
		}
		break;
	case mobj_hprev:
		if (lua_isnil(L, 3))
			P_SetTarget(&mo->hprev, NULL);
		else
		{
			mobj_t *hprev = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
			P_SetTarget(&mo->hprev, hprev);
		}
		break;
	case mobj_type: // yeah sure, we'll let you change the mobj's type.
	{
		mobjtype_t newtype = luaL_checkinteger(L, 3);
		if (newtype >= NUMMOBJTYPES)
			return luaL_error(L, "mobj.type %d out of range (0 - %d).", newtype, NUMMOBJTYPES-1);
		mo->type = newtype;
		mo->info = &mobjinfo[newtype];
		P_SetScale2(mo, mo->scale, lua_compatmode);
		break;
	}
	case mobj_info:
		return NOSET;
	case mobj_health:
		mo->health = luaL_checkinteger(L, 3);
		break;
	case mobj_movedir:
		mo->movedir = (angle_t)luaL_checkinteger(L, 3);
		break;
	case mobj_movecount:
		mo->movecount = luaL_checkinteger(L, 3);
		break;
	case mobj_target:
		if (lua_isnil(L, 3))
			P_SetTarget(&mo->target, NULL);
		else
		{
			mobj_t *target = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
			P_SetTarget(&mo->target, target);
		}
		break;
	case mobj_reactiontime:
		mo->reactiontime = luaL_checkinteger(L, 3);
		break;
	case mobj_threshold:
		mo->threshold = luaL_checkinteger(L, 3);
		break;
	case mobj_player:
		return NOSET;
	case mobj_lastlook:
		mo->lastlook = luaL_checkinteger(L, 3);
		break;
	case mobj_spawnpoint:
		if (lua_isnil(L, 3))
			mo->spawnpoint = NULL;
		else
		{
			mapthing_t *spawnpoint = *((mapthing_t **)luaL_checkudata(L, 3, META_MAPTHING));
			mo->spawnpoint = spawnpoint;
		}
		break;
	case mobj_tracer:
		if (lua_isnil(L, 3))
			P_SetTarget(&mo->tracer, NULL);
		else
		{
			mobj_t *tracer = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
			P_SetTarget(&mo->tracer, tracer);
		}
		break;
	case mobj_friction:
		mo->friction = luaL_checkfixed(L, 3);
		break;
	case mobj_movefactor:
		mo->movefactor = luaL_checkfixed(L, 3);
		break;
	case mobj_gravity:
		mo->gravity = luaL_checkfixed(L, 3);
		break;
	case mobj_fuse:
		mo->fuse = luaL_checkinteger(L, 3);
		break;
	case mobj_watertop:
		mo->watertop = luaL_checkfixed(L, 3);
		break;
	case mobj_waterbottom:
		mo->waterbottom = luaL_checkfixed(L, 3);
		break;
	case mobj_mobjnum:
		return UNIMPLEMENTED;
	case mobj_scale:
	{
		fixed_t scale = luaL_checkfixed(L, 3);
		if (scale < FRACUNIT/100)
			scale = FRACUNIT/100;
		mo->destscale = scale;
		P_SetScale2(mo, scale, lua_compatmode);
		mo->old_scale = scale;
		break;
	}
	case mobj_destscale:
	{
		fixed_t scale = luaL_checkfixed(L, 3);
		if (scale < FRACUNIT/100)
			scale = FRACUNIT/100;
		mo->destscale = scale;
		break;
	}
	case mobj_scalespeed:
		mo->scalespeed = luaL_checkfixed(L, 3);
		break;
	case mobj_extravalue1:
		mo->extravalue1 = luaL_checkinteger(L, 3);
		break;
	case mobj_extravalue2:
		mo->extravalue2 = luaL_checkinteger(L, 3);
		break;
	case mobj_extravalue3:
		mo->extravalue3 = luaL_checkinteger(L, 3);
		break;
	case mobj_cusval:
		mo->cusval = luaL_checkinteger(L, 3);
		break;
	case mobj_cvmem:
		mo->cvmem = luaL_checkinteger(L, 3);
		break;
	case mobj_mobjlifetime:
		mo->mobjlifetime = luaL_checkinteger(L, 3);
		break;
	case mobj_standingslope:
		return NOSET;
	case mobj_colorized:
		mo->colorized = luaL_checkdboolean(L, 3);
		break;
	case mobj_mirrored:
		mo->mirrored = luaL_checkdboolean(L, 3);
		break;
	case mobj_shadowscale:
		mo->shadowscale = luaL_checkfixed(L, 3);
		break;
	case mobj_whiteshadow:
		mo->whiteshadow = luaL_checkdboolean(L, 3);
		break;
	case mobj_sprxoff:
		mo->sprxoff = luaL_checkfixed(L, 3);
		break;
	case mobj_spryoff:
		mo->spryoff = luaL_checkfixed(L, 3);
		break;
	case mobj_sprzoff:
		mo->sprzoff = luaL_checkfixed(L, 3);
		break;
	case mobj_bakexoff:
		mo->bakexoff = luaL_checkfixed(L, 3);
		break;
	case mobj_bakeyoff:
		mo->bakeyoff = luaL_checkfixed(L, 3);
		break;
	case mobj_bakezoff:
		mo->bakezoff = luaL_checkfixed(L, 3);
		break;
	case mobj_bakexpiv:
		mo->bakexpiv = luaL_checkfixed(L, 3);
		break;
	case mobj_bakeypiv:
		mo->bakeypiv = luaL_checkfixed(L, 3);
		break;
	case mobj_bakezpiv:
		mo->bakezpiv = luaL_checkfixed(L, 3);
		break;
	case mobj_terrain:
		mo->terrain = *((terrain_t **)luaL_checkudata(L, 3, META_TERRAIN));
		break;
	case mobj_dispoffset:
		mo->dispoffset = luaL_checkinteger(L, 3);
		break;
	case mobj_tid:
		P_SetThingTID(mo, luaL_checkinteger(L, 3));
		break;
	case mobj_special:
		mo->special = luaL_checkinteger(L, 3);
		break;
	case mobj_args:
		return NOSET;
	case mobj_stringargs:
		return NOSET;
	case mobj_bubbleplow:
		if (lua_isnil(L, 3))
			P_SetTarget(&mo->bubbleplow, NULL);
		else
		{
			mobj_t *bubbleplow = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
			P_SetTarget(&mo->bubbleplow, bubbleplow);
		}
		break;
	case mobj_bubbleplowforget:
		mo->bubbleplowforget = luaL_checkinteger(L, 3);
		break;
	default:
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
		I_Assert(lua_istable(L, -1));
		lua_pushlightuserdata(L, mo);
		lua_rawget(L, -2);
		if (lua_isnil(L, -1)) {
			// This index doesn't have a table for extra values yet, let's make one.
			lua_pop(L, 1);
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; adding it as Lua data.\n"), "mobj_t", lua_tostring(L, 2));
			lua_newtable(L);
			lua_pushlightuserdata(L, mo);
			lua_pushvalue(L, -2); // ext value table
			lua_rawset(L, -4); // LREG_EXTVARS table
		}
		lua_pushvalue(L, 2); // key
		lua_pushvalue(L, 3); // value to store
		lua_settable(L, -3);
		lua_pop(L, 2);
		break;
	}
	return 0;
}

#undef UNIMPLEMENTED
#undef NOSET
#undef NOSETPOS
#undef NOFIELD

// args, i -> args[i]
static int thingargs_get(lua_State *L)
{
	int32_t *args = *((int32_t**)luaL_checkudata(L, 1, META_THINGARGS));
	int i = luaL_checkinteger(L, 2);
	if (i < 0 || i >= NUM_MAPTHING_ARGS)
		return luaL_error(L, LUA_QL("mapthing_t.args") " index cannot be %d", i);
	lua_pushinteger(L, args[i]);
	return 1;
}

// #args -> NUM_MAPTHING_ARGS
static int thingargs_len(lua_State* L)
{
	lua_pushinteger(L, NUM_MAPTHING_ARGS);
	return 1;
}

// stringargs, i -> stringargs[i]
static int thingstringargs_get(lua_State *L)
{
	char **stringargs = *((char***)luaL_checkudata(L, 1, META_THINGSTRINGARGS));
	int i = luaL_checkinteger(L, 2);
	if (i < 0 || i >= NUM_MAPTHING_STRINGARGS)
		return luaL_error(L, LUA_QL("mapthing_t.stringargs") " index cannot be %d", i);
	lua_pushstring(L, stringargs[i]);
	return 1;
}

// #stringargs -> NUM_MAPTHING_STRINGARGS
static int thingstringargs_len(lua_State *L)
{
	lua_pushinteger(L, NUM_MAPTHING_STRINGARGS);
	return 1;
}



enum mapthing_e {
	mapthing_valid = 0,
	mapthing_x,
	mapthing_y,
	mapthing_angle,
	mapthing_pitch,
	mapthing_roll,
	mapthing_type,
	mapthing_options,
	mapthing_scale,
	mapthing_spritexscale,
	mapthing_spriteyscale,
	mapthing_z,
	mapthing_extrainfo,
	mapthing_tid,
	mapthing_args,
	mapthing_stringargs,
	mapthing_mobj,
};

const char *const mapthing_opt[] = {
	"valid",
	"x",
	"y",
	"angle",
	"pitch",
	"roll",
	"type",
	"options",
	"scale",
	"spritexscale",
	"spriteyscale",
	"z",
	"extrainfo",
	"tid",
	"args",
	"stringargs",
	"mobj",
	NULL,
};

int mapthing_fields_ref = LUA_NOREF;

static int mapthing_get(lua_State *L)
{
	mapthing_t *mt = *((mapthing_t **)luaL_checkudata(L, 1, META_MAPTHING));
	enum mapthing_e field = Lua_optoption(L, 2, -1, mapthing_fields_ref);

	if (!mt) {
		if (field == mapthing_valid) {
			lua_pushdboolean(L, false);
			return 1;
		}
		if (devparm)
			return luaL_error(L, "accessed mapthing_t doesn't exist anymore.");
		return 0;
	}

	switch (field)
	{
		case mapthing_valid:
			lua_pushdboolean(L, true);
			break;
		case mapthing_x:
			lua_pushinteger(L, mt->x);
			break;
		case mapthing_y:
			lua_pushinteger(L, mt->y);
			break;
		case mapthing_angle:
			lua_pushinteger(L, mt->angle);
			break;
		case mapthing_pitch:
			lua_pushinteger(L, mt->pitch);
			break;
		case mapthing_roll:
			lua_pushinteger(L, mt->roll);
			break;
		case mapthing_type:
			lua_pushinteger(L, mt->type);
			break;
		case mapthing_options:
			lua_pushinteger(L, mt->options);
			break;
		case mapthing_scale:
			lua_pushfixed(L, mt->scale);
			break;
		case mapthing_spritexscale:
			lua_pushfixed(L, mt->spritexscale);
			break;
		case mapthing_spriteyscale:
			lua_pushfixed(L, mt->spriteyscale);
			break;
		case mapthing_z:
			lua_pushinteger(L, mt->z);
			break;
		case mapthing_extrainfo:
			lua_pushinteger(L, mt->extrainfo);
			break;
		case mapthing_tid:
			lua_pushinteger(L, mt->tid);
			break;
		case mapthing_args:
			LUA_PushUserdata(L, mt->args, META_THINGARGS);
			break;
		case mapthing_stringargs:
			LUA_PushUserdata(L, mt->stringargs, META_THINGSTRINGARGS);
			break;
		case mapthing_mobj:
			LUA_PushUserdata(L, mt->mobj, META_MOBJ);
			break;
		default:
			if (devparm)
				return luaL_error(L, LUA_QL("mapthing_t") " has no field named " LUA_QS, field);
			else
				return 0;
	}

	return 1;
}

static int mapthing_set(lua_State *L)
{
	mapthing_t *mt = *((mapthing_t **)luaL_checkudata(L, 1, META_MAPTHING));
	enum mapthing_e field = Lua_optoption(L, 2, mapthing_valid, mapthing_fields_ref);
	lua_settop(L, 3);

	if (!mt)
		return luaL_error(L, "accessed mapthing_t doesn't exist anymore.");

	if (hud_running)
		return luaL_error(L, "Do not alter mapthing_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter mapthing_t in CMD building code!");

	switch (field)
	{
		case mapthing_x:
			mt->x = (int16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_y:
			mt->y = (int16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_angle:
			mt->angle = (int16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_pitch:
			mt->pitch = (int16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_roll:
			mt->roll = (int16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_type:
			mt->type = (uint16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_options:
			mt->options = (uint16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_scale:
			mt->scale = luaL_checkfixed(L, 3);
			break;
		case mapthing_spritexscale:
			mt->spritexscale = luaL_checkfixed(L, 3);
			break;
		case mapthing_spriteyscale:
			mt->spriteyscale = luaL_checkfixed(L, 3);
			break;
		case mapthing_z:
			mt->z = (int16_t)luaL_checkinteger(L, 3);
			break;
		case mapthing_extrainfo:
		{
			int32_t extrainfo = luaL_checkinteger(L, 3);
			if (extrainfo & ~15)
				return luaL_error(L, "mapthing_t extrainfo set %d out of range (%d - %d)", extrainfo, 0, 15);
			mt->extrainfo = (uint8_t)extrainfo;
			break;
		}
		case mapthing_tid:
			luaL_error(L, LUA_QL("mapthing_t") " field " LUA_QS " should not be set directly.", mapthing_opt[field]);
			break;
		case mapthing_mobj:
			mt->mobj = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
			break;
		default:
			return luaL_error(L, LUA_QL("mapthing_t") " has no field named " LUA_QS, field);
	}

	return 0;
}

static int mapthing_num(lua_State *L)
{
	mapthing_t *mt = *((mapthing_t **)luaL_checkudata(L, 1, META_MAPTHING));
	if (!mt)
		return luaL_error(L, "accessed mapthing_t doesn't exist anymore.");
	lua_pushinteger(L, mt-mapthings);
	return 1;
}

static int lib_iterateMapthings(lua_State *L)
{
	size_t i = 0;
	INLEVEL
	if (lua_gettop(L) < 2)
		return luaL_error(L, "Don't call mapthings.iterate() directly, use it as 'for mapthing in mapthings.iterate do <block> end'.");
	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.
	if (!lua_isnil(L, 1))
		i = (size_t)(*((mapthing_t **)luaL_checkudata(L, 1, META_MAPTHING)) - mapthings) + 1;
	if (i < nummapthings)
	{
		LUA_PushUserdata(L, &mapthings[i], META_MAPTHING);
		return 1;
	}
	return 0;
}

static int lib_getMapthing(lua_State *L)
{
	INLEVEL
	if (lua_isnumber(L, 2))
	{
		size_t i = lua_tointeger(L, 2);
		if (i >= nummapthings)
			return 0;
		LUA_PushUserdata(L, &mapthings[i], META_MAPTHING);
		return 1;
	}
	
	if (fastcmp(luaL_checkstring(L, 2),"iterate"))
	{
		lua_pushcfunction(L, lib_iterateMapthings);
		return 1;
	}

	return 0;
}

static int lib_nummapthings(lua_State *L)
{
	lua_pushinteger(L, nummapthings);
	return 1;
}

int LUA_MobjLib(lua_State *L)
{
	mobj_fields_ref = Lua_CreateFieldTable(L, mobj_opt);
	mapthing_fields_ref = Lua_CreateFieldTable(L, mapthing_opt);

	luaL_newmetatable(L, META_MOBJ);
		lua_pushcfunction(L, mobj_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, mobj_set);
		lua_setfield(L, -2, "__newindex");
	lua_pop(L,1);

	luaL_newmetatable(L, META_THINGARGS);
		lua_pushcfunction(L, thingargs_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, thingargs_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_THINGSTRINGARGS);
		lua_pushcfunction(L, thingstringargs_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, thingstringargs_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_MAPTHING);
		lua_pushcfunction(L, mapthing_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, mapthing_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, mapthing_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getMapthing);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_nummapthings);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "mapthings");

	return 0;
}
