#!/usr/bin/python

# yeah, I know my python code sucks. get over it

import sys
import re

def ReadMobjinfo(f):
	mobjs = []
	mobj = None
	for line in f:
		l, _, comment = line.strip().partition("//")
		if mobj is not None:
			if re.findall("[\t ]*},", l):
				mobjs.append(mobj)
				mobj = None
			elif comment:
				val = re.sub("[\t ,]", "", l)
				# no casts allowed!
				for match in re.finditer("\\(([A-Za-z0-9_]+)\\)", val):
					start, end = match.span()
					val = val[:start] + val[end:]
				mobj.append(val)
		else:
			if "{" in l:
				mobj = []
			elif l.startswith("}") and l.endswith(";"):
				return mobjs
	sys.exit("error: unexpected EOF reading mobjinfo")

def ReadStruct(f, pattern):
	states = []
	for line in f:
		l, _, comment = line.strip().partition("//")
		if l.startswith("#"):
			if l == "#ifndef ROTSPRITE": # bruh
				f.readline()
			elif l == "#else" or l == "#endif":
				continue
			else:
				sys.exit("error: unknown directive "+l)
		elif "{" in l:
			state = re.search(pattern, l.strip("{} ,\t"))
			if not state:
				sys.exit("error: invalid line "+l)
			states.append(list(state.groups()))
		elif l.startswith("}") and l.endswith(";"):
			return states
	sys.exit("error: unexpected EOF")

def FindStruct(f, match, pattern):
	for l in f:
		if l.startswith(match):
			if not l.endswith("{\n"): # not for skincolors
				f.readline()
			return ReadStruct(f, pattern) if pattern != "mobj" else ReadMobjinfo(f)

def ReadEnum(f, match):
	out = []
	comments = {} # why not?
	for line in f:
		l, _, comment = line.strip().partition("//")
		if comment:
			i = len(out)
			if comments.get(i) is None:
				comments[i] = []
			comments[i].append(comment)
		if e := re.findall(f"({match}[A-Za-z0-9_]+),?", l):
			out.append(e[0])
		elif l.startswith("}") and l.endswith(";"):
			enumname = re.findall("} *([A-Za-z0-9_]+);", l)
			return out, comments, enumname[0]
	sys.exit("error: unexpected EOF reading enum")

def FindEnum(f, match, enum):
	enumname = None
	typedef = "typedef enum"
	for l in f:
		if l.startswith(typedef):
			f.readline()
			out, comments, tname = ReadEnum(f, match)
			if tname == enum:
				return out, comments
	sys.exit("error: couldn't find "+enum)

def WriteSoc(states, fields, keyword, names, comments):
	i = 0
	for state in states:
		comment = comments.get(i)
		if comment:
			for c in comment:
				out.write(f"#{c}\n")
		out.write(f"{keyword} {names[i]}\n")
		for j in range(len(state)):
			v = state[j].strip("{} \t\"")
			if v != fields[j][1]:
				if fields[j][0] != "Caption":
					v = re.sub("[\t ]", "", v)
				if fields[j][0] == "Accessible":
					# lowercase doesn't work, might as well use numbers
					v = "1" if v == "true" else "0"
				out.write(f"{fields[j][0]} = {v}\n")
		out.write("\n")
		i += 1

if __name__ == "__main__":
	if len(sys.argv) < 2:
		print("Usage: info2soc.py <path to src> <outfile>")
		sys.exit()
	path = sys.argv[1]
	if len(sys.argv) < 3:
		outpath = "info.soc"
	else:
		outpath = sys.argv[2]
	with open(path+"/info.h", "r") as f:
		statenames, statecom = FindEnum(f, "S_", "statenum_t")
		mobjnames, mobjcom = FindEnum(f, "MT_", "mobjtype_t")
	with open(path+"/sounds.h", "r") as f:
		sfxnames, sfxcom = FindEnum(f, "sfx_", "sfxenum_t")
	with open(path+"/doomdef.h", "r") as f:
		colornames, colorcom = FindEnum(f, "SKINCOLOR_", "skincolornum_t")
	out = open(outpath, "w")
	with open(path+"/info.c", "r") as f:
		states = FindStruct(f, "state_t states[NUMSTATES]",
		                    "([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)")
		fields = [ # field name, and default value (so it can be omitted)
			("SpriteName", "SPR_NULL"),
			("SpriteFrame", "0"),
			("Duration", "0"),
			("Action", "NULL"),
			("Var1", "0"),
			("Var2", "0"),
			("Next", "S_NULL"),
		]
		WriteSoc(states, fields, "State", statenames, statecom)

		mobjs = FindStruct(f, "mobjinfo_t mobjinfo[NUMMOBJTYPES]", "mobj")
		fields = [
			("MapThingNum", "-1"),
			("SpawnState", "S_NULL"),
			("SpawnHealth", "0"),
			("SeeState", "S_NULL"),
			("SeeSound", "sfx_None"),
			("ReactionTime", "0"),
			("AttackSound", "sfx_None"),
			("PainState", "S_NULL"),
			("PainChance", "0"),
			("PainSound", "sfx_None"),
			("MeleeState", "S_NULL"),
			("MissileState", "S_NULL"),
			("DeathState", "S_NULL"),
			("XDeathState", "S_NULL"),
			("DeathSound", "sfx_None"),
			("Speed", "0"),
			("Radius", "0"),
			("Height", "0"),
			("DispOffset", "0"),
			("Mass", "0"),
			("Damage", "0"),
			("ActiveSound", "sfx_None"),
			("Flags", "0"),
			("RaiseState", "S_NULL"),
		]
		WriteSoc(mobjs, fields, "Mobj", mobjnames, mobjcom)

		colors = FindStruct(f, "skincolor_t skincolors[MAXSKINCOLORS]",
		                    "^([^,]+),(.+),([^,]+),([^,]+),([^,]+),([^,]+)")
		fields = [
			("Name", ""),
			("Ramp", ""),
			("InvColor", ""),
			("InvShade", ""),
			("ChatColor", ""),
			("Accessible", "false"),
		]
		WriteSoc(colors, fields, "Color", colornames, colorcom)
	with open(path+"/sounds.c", "r") as f:
		sounds = FindStruct(f, "sfxinfo_t S_sfx[NUMSFX]",
		                    "[^,]+,([^,]+),([^,]+),([^,]+),([^,]+),[^,]+,[^,]+,([^,]+),[^,]+,[^,]+,(.+)")
		fields = [
			("Singular", "false"),
			("Priority", "0"),
			("Flags", "0"),
			("Volume", "-1"),
			("SkinSound", "-1"),
			("Caption", ""),
		]
		WriteSoc(sounds, fields, "Sound", sfxnames, sfxcom)
	out.close()
