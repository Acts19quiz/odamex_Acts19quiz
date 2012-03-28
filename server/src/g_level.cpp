// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
// Copyright (C) 2006-2010 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	G_LEVEL
//
//-----------------------------------------------------------------------------


#include <sstream>
#include <string>
#include <algorithm>
#include <vector>
#include <set>

#include "c_console.h"
#include "c_dispatch.h"
#include "c_level.h"
#include "d_event.h"
#include "d_main.h"
#include "doomstat.h"
#include "d_protocol.h"
#include "g_level.h"
#include "g_game.h"
#include "gstrings.h"
#include "gi.h"

#include "i_system.h"
#include "m_alloc.h"
#include "m_fileio.h"
#include "m_misc.h"
#include "minilzo.h"
#include "m_random.h"
#include "p_acs.h"
#include "p_ctf.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_saveg.h"
#include "p_setup.h"
#include "p_unlag.h"
#include "r_data.h"
#include "r_sky.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "sc_man.h"
#include "sv_main.h"
#include "sv_maplist.h"
#include "sv_vote.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"


#define lioffset(x)		myoffsetof(level_pwad_info_t,x)
#define cioffset(x)		myoffsetof(cluster_info_t,x)

extern int nextupdate;


EXTERN_CVAR (sv_endmapscript)
EXTERN_CVAR (sv_startmapscript)
EXTERN_CVAR (sv_curmap)
EXTERN_CVAR (sv_nextmap)
EXTERN_CVAR (sv_loopepisode)
EXTERN_CVAR (sv_gravity)
EXTERN_CVAR (sv_aircontrol)
EXTERN_CVAR (sv_intermissionlimit)

static level_info_t *FindDefLevelInfo (char *mapname);
static cluster_info_t *FindDefClusterInfo (int cluster);

extern int timingdemo;

extern int mapchange;
extern int shotclock;

// Start time for timing demos
int starttime;

// ACS variables with world scope
int ACS_WorldVars[NUM_WORLDVARS];

// ACS variables with global scope
int ACS_GlobalVars[NUM_GLOBALVARS];

// [SL] 2012-02-23 - Sectors that can possibly change floor/ceiling height
std::set<sector_t*> movable_sectors;

BOOL firstmapinit = true; // Nes - Avoid drawing same init text during every rebirth in single-player servers.

extern BOOL netdemo;
BOOL savegamerestore;

extern int mousex, mousey, joyxmove, joyymove, Impulse;
extern BOOL sendpause, sendsave, sendcenterview;

level_locals_t level;			// info about current level

level_pwad_info_t *wadlevelinfos;
cluster_info_t *wadclusterinfos;
size_t numwadlevelinfos = 0;
size_t numwadclusterinfos = 0;

BOOL HexenHack;

bool isFast = false;

static const char *MapInfoTopLevel[] =
{
	"map",
	"defaultmap",
	"clusterdef",
	NULL
};

enum
{
	MITL_MAP,
	MITL_DEFAULTMAP,
	MITL_CLUSTERDEF
};

static const char *MapInfoMapLevel[] =
{
	"levelnum",
	"next",
	"secretnext",
	"cluster",
	"sky1",
	"sky2",
	"fade",
	"outsidefog",
	"titlepatch",
	"par",
	"music",
	"nointermission",
	"doublesky",
	"nosoundclipping",
	"allowmonstertelefrags",
	"map07special",
	"baronspecial",
	"cyberdemonspecial",
	"spidermastermindspecial",
	"specialaction_exitlevel",
	"specialaction_opendoor",
	"specialaction_lowerfloor",
	"lightning",
	"fadetable",
	"evenlighting",
	"noautosequences",
	"forcenoskystretch",
	"allowfreelook",
	"nofreelook",
	"allowjump",
	"nojump",
	"cdtrack",
	"cd_start_track",
	"cd_end1_track",
	"cd_end2_track",
	"cd_end3_track",
	"cd_intermission_track",
	"cd_title_track",
	"warptrans",
	"gravity",
	"aircontrol",
	NULL
};

enum EMIType
{
	MITYPE_IGNORE,
	MITYPE_EATNEXT,
	MITYPE_INT,
	MITYPE_FLOAT,
	MITYPE_COLOR,
	MITYPE_MAPNAME,
	MITYPE_LUMPNAME,
	MITYPE_SKY,
	MITYPE_SETFLAG,
	MITYPE_SCFLAGS,
	MITYPE_CLUSTER,
	MITYPE_STRING,
	MITYPE_CSTRING
};

struct MapInfoHandler
{
	EMIType type;
	DWORD data1, data2;
}
MapHandlers[] =
{
	{ MITYPE_INT,		lioffset(levelnum), 0 }, // denis - fixme - lioffset, offsetof will generate warnings unless given a POD struct - but "level_pwad_info_s : public level_info_s" isn't a POD!
	{ MITYPE_MAPNAME,	lioffset(nextmap), 0 },
	{ MITYPE_MAPNAME,	lioffset(secretmap), 0 },
	{ MITYPE_CLUSTER,	lioffset(cluster), 0 },
	{ MITYPE_SKY,		lioffset(skypic), 0 },				//[ML] 5/11/06 - Remove sky scrolling
	{ MITYPE_SKY,		lioffset(skypic2), 0 },
	{ MITYPE_COLOR,		lioffset(fadeto), 0 },
	{ MITYPE_COLOR,		lioffset(outsidefog), 0 },
	{ MITYPE_LUMPNAME,	lioffset(pname), 0 },
	{ MITYPE_INT,		lioffset(partime), 0 },
	{ MITYPE_LUMPNAME,	lioffset(music), 0 },
	{ MITYPE_SETFLAG,	LEVEL_NOINTERMISSION, 0 },
	{ MITYPE_SETFLAG,	LEVEL_DOUBLESKY, 0 },
	{ MITYPE_SETFLAG,	LEVEL_NOSOUNDCLIPPING, 0 },
	{ MITYPE_SETFLAG,	LEVEL_MONSTERSTELEFRAG, 0 },
	{ MITYPE_SETFLAG,	LEVEL_MAP07SPECIAL, 0 },
	{ MITYPE_SETFLAG,	LEVEL_BRUISERSPECIAL, 0 },
	{ MITYPE_SETFLAG,	LEVEL_CYBORGSPECIAL, 0 },
	{ MITYPE_SETFLAG,	LEVEL_SPIDERSPECIAL, 0 },
	{ MITYPE_SCFLAGS,	0, ~LEVEL_SPECACTIONSMASK },
	{ MITYPE_SCFLAGS,	LEVEL_SPECOPENDOOR, ~LEVEL_SPECACTIONSMASK },
	{ MITYPE_SCFLAGS,	LEVEL_SPECLOWERFLOOR, ~LEVEL_SPECACTIONSMASK },
	{ MITYPE_IGNORE,	0, 0 },		// lightning
	{ MITYPE_LUMPNAME,	lioffset(fadetable), 0 },
	{ MITYPE_SETFLAG,	LEVEL_EVENLIGHTING, 0 },
	{ MITYPE_SETFLAG,	LEVEL_SNDSEQTOTALCTRL, 0 },
	{ MITYPE_SETFLAG,	LEVEL_FORCENOSKYSTRETCH, 0 },
	{ MITYPE_SCFLAGS,	LEVEL_FREELOOK_YES, ~LEVEL_FREELOOK_NO },
	{ MITYPE_SCFLAGS,	LEVEL_FREELOOK_NO, ~LEVEL_FREELOOK_YES },
	{ MITYPE_SCFLAGS,	LEVEL_JUMP_YES, ~LEVEL_JUMP_NO },
	{ MITYPE_SCFLAGS,	LEVEL_JUMP_NO, ~LEVEL_JUMP_YES },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_EATNEXT,	0, 0 },
	{ MITYPE_FLOAT,		lioffset(gravity), 0 },
	{ MITYPE_FLOAT,		lioffset(aircontrol), 0 },
};

static const char *MapInfoClusterLevel[] =
{
	"entertext",
	"exittext",
	"music",
	"flat",
	"hub",
	NULL
};

MapInfoHandler ClusterHandlers[] =
{
	{ MITYPE_STRING,	cioffset(entertext), 0 },
	{ MITYPE_STRING,	cioffset(exittext), 0 },
	{ MITYPE_CSTRING,	cioffset(messagemusic), 8 },
	{ MITYPE_LUMPNAME,	cioffset(finaleflat), 0 },
	{ MITYPE_SETFLAG,	CLUSTER_HUB, 0 }
};

static void ParseMapInfoLower (MapInfoHandler *handlers,
							   const char *strings[],
							   level_pwad_info_t *levelinfo,
							   cluster_info_t *clusterinfo,
							   DWORD levelflags);

static int FindWadLevelInfo (char *name)
{
	for (size_t i = 0; i < numwadlevelinfos; i++)
		if (!strnicmp (name, wadlevelinfos[i].mapname, 8))
			return i;

	return -1;
}

static int FindWadClusterInfo (int cluster)
{
	for (size_t i = 0; i < numwadclusterinfos; i++)
		if (wadclusterinfos[i].cluster == cluster)
			return i;

	return -1;
}

static void SetLevelDefaults (level_pwad_info_t *levelinfo)
{
	memset (levelinfo, 0, sizeof(*levelinfo));
	levelinfo->snapshot = NULL;
	levelinfo->outsidefog = 0xff000000;
	strncpy (levelinfo->fadetable, "COLORMAP", 8);
}

//
// G_ParseMapInfo
// Parses the MAPINFO lumps of all loaded WADs and generates
// data for wadlevelinfos and wadclusterinfos.
//
void G_ParseMapInfo (void)
{
	int lump, lastlump = 0;
	level_pwad_info_t defaultinfo;
	level_pwad_info_t *levelinfo;
	int levelindex;
	cluster_info_t *clusterinfo;
	int clusterindex;
	DWORD levelflags;

	while ((lump = W_FindLump ("MAPINFO", &lastlump)) != -1)
	{
		SetLevelDefaults (&defaultinfo);
		SC_OpenLumpNum (lump, "MAPINFO");

		while (SC_GetString ())
		{
			switch (SC_MustMatchString (MapInfoTopLevel))
			{
			case MITL_DEFAULTMAP:
				SetLevelDefaults (&defaultinfo);
				ParseMapInfoLower (MapHandlers, MapInfoMapLevel, &defaultinfo, NULL, 0);
				break;

			case MITL_MAP:		// map <MAPNAME> <Nice Name>
				levelflags = defaultinfo.flags;
				SC_MustGetString ();
				if (IsNum (sc_String))
				{	// MAPNAME is a number, assume a Hexen wad
					int map = atoi (sc_String);
					sprintf (sc_String, "MAP%02d", map);
					SKYFLATNAME[5] = 0;
					HexenHack = true;
					// Hexen levels are automatically nointermission
					// and even lighting and no auto sound sequences
					levelflags |= LEVEL_NOINTERMISSION
								| LEVEL_EVENLIGHTING
								| LEVEL_SNDSEQTOTALCTRL;
				}
				levelindex = FindWadLevelInfo (sc_String);
				if (levelindex == -1)
				{
					levelindex = numwadlevelinfos++;
					wadlevelinfos = (level_pwad_info_t *)Realloc (wadlevelinfos, sizeof(level_pwad_info_t)*numwadlevelinfos);
				}
				levelinfo = wadlevelinfos + levelindex;
				memcpy (levelinfo, &defaultinfo, sizeof(*levelinfo));
				uppercopy (levelinfo->mapname, sc_String);
				SC_MustGetString ();
				ReplaceString (&levelinfo->level_name, sc_String);
				// Set up levelnum now so that the Teleport_NewMap specials
				// in hexen.wad work without modification.
				if (!strnicmp (levelinfo->mapname, "MAP", 3) && levelinfo->mapname[5] == 0)
				{
					int mapnum = atoi (levelinfo->mapname + 3);

					if (mapnum >= 1 && mapnum <= 99)
						levelinfo->levelnum = mapnum;
				}
				ParseMapInfoLower (MapHandlers, MapInfoMapLevel, levelinfo, NULL, levelflags);
				break;

			case MITL_CLUSTERDEF:	// clusterdef <clusternum>
				SC_MustGetNumber ();
				clusterindex = FindWadClusterInfo (sc_Number);
				if (clusterindex == -1)
				{
					clusterindex = numwadclusterinfos++;
					wadclusterinfos = (cluster_info_t *)Realloc (wadclusterinfos, sizeof(cluster_info_t)*numwadclusterinfos);
					memset (wadclusterinfos + clusterindex, 0, sizeof(cluster_info_t));
				}
				clusterinfo = wadclusterinfos + clusterindex;
				clusterinfo->cluster = sc_Number;
				ParseMapInfoLower (ClusterHandlers, MapInfoClusterLevel, NULL, clusterinfo, 0);
				break;
			}
		}
		SC_Close ();
	}
}

static void ParseMapInfoLower (MapInfoHandler *handlers,
							   const char *strings[],
							   level_pwad_info_t *levelinfo,
							   cluster_info_t *clusterinfo,
							   DWORD flags)
{
	int entry;
	MapInfoHandler *handler;
	byte *info;

	info = levelinfo ? (byte *)levelinfo : (byte *)clusterinfo;

	while (SC_GetString ())
	{
		if (SC_MatchString (MapInfoTopLevel) != -1)
		{
			SC_UnGet ();
			break;
		}
		entry = SC_MustMatchString (strings);
		handler = handlers + entry;
		switch (handler->type)
		{
		case MITYPE_IGNORE:
			break;

		case MITYPE_EATNEXT:
			SC_MustGetString ();
			break;

		case MITYPE_INT:
			SC_MustGetNumber ();
			*((int *)(info + handler->data1)) = sc_Number;
			break;

		case MITYPE_FLOAT:
			SC_MustGetFloat ();
			*((float *)(info + handler->data1)) = sc_Float;
			break;

		case MITYPE_COLOR:
			{
				SC_MustGetString ();
				//std::string string = V_GetColorStringByName (sc_String);
				//if (string.length())
				//{
				//	*((DWORD *)(info + handler->data1)) =
				//		V_GetColorFromString (NULL, string.c_str());
				//}
				//else
				//{
				//	*((DWORD *)(info + handler->data1)) =
				//						V_GetColorFromString (NULL, sc_String);
				//}
			}
			break;

		case MITYPE_MAPNAME:
			SC_MustGetString ();
			if (IsNum (sc_String))
			{
				int map = atoi (sc_String);
				sprintf (sc_String, "MAP%02d", map);
			}
			strncpy ((char *)(info + handler->data1), sc_String, 8);
			break;

		case MITYPE_LUMPNAME:
			SC_MustGetString ();
			uppercopy ((char *)(info + handler->data1), sc_String);
			break;

		case MITYPE_SKY:
			SC_MustGetString ();	// get texture name;
			uppercopy ((char *)(info + handler->data1), sc_String);
			SC_MustGetFloat ();		// get scroll speed
			//if (HexenHack)
			//{
			//	*((fixed_t *)(info + handler->data2)) = sc_Number << 8;
			//}
			//else
			//{
			//	*((fixed_t *)(info + handler->data2)) = (fixed_t)(sc_Float * 65536.0f);
			//}
			break;

		case MITYPE_SETFLAG:
			flags |= handler->data1;
			break;

		case MITYPE_SCFLAGS:
			flags = (flags & handler->data2) | handler->data1;
			break;

		case MITYPE_CLUSTER:
			SC_MustGetNumber ();
			*((int *)(info + handler->data1)) = sc_Number;
			if (HexenHack)
			{
				cluster_info_t *clusterH = FindClusterInfo (sc_Number);
				if (clusterH)
					clusterH->flags |= CLUSTER_HUB;
			}
			break;

		case MITYPE_STRING:
			SC_MustGetString ();
			ReplaceString ((const char **)(info + handler->data1), sc_String);
			break;

		case MITYPE_CSTRING:
			SC_MustGetString ();
			strncpy ((char *)(info + handler->data1), sc_String, handler->data2);
			*((char *)(info + handler->data1 + handler->data2)) = '\0';
			break;
		}
	}
	if (levelinfo)
		levelinfo->flags = flags;
	else
		clusterinfo->flags = flags;
}

static void zapDefereds (acsdefered_t *def)
{
	while (def) {
		acsdefered_t *next = def->next;
		delete def;
		def = next;
	}
}

void P_RemoveDefereds (void)
{
	unsigned int i;

	// Remove any existing defereds
	for (i = 0; i < numwadlevelinfos; i++)
		if (wadlevelinfos[i].defered) {
			zapDefereds (wadlevelinfos[i].defered);
			wadlevelinfos[i].defered = NULL;
		}

	for (i = 0; LevelInfos[i].level_name; i++)
		if (LevelInfos[i].defered) {
			zapDefereds (LevelInfos[i].defered);
			LevelInfos[i].defered = NULL;
		}
}

// [ML] Not sure where to put this for now...
// 	G_ParseMusInfo
void G_ParseMusInfo(void)
{
	// Nothing yet...
}

//
// G_InitNew
// Can be called by the startup code or the menu task,
// consoleplayer, displayplayer, should be set.
//
static char d_mapname[9];

void G_DeferedInitNew (char *mapname)
{
	strncpy (d_mapname, mapname, 8);
	gameaction = ga_newgame;

	// sv_nextmap cvar may be overridden by a script
	sv_nextmap.ForceSet(d_mapname);
}

BEGIN_COMMAND (map)
{
	if (argc > 1)
	{
		// [Dash|RD] -- We can make a safe assumption that the user might not specify
		//              the whole lumpname for the level, and might opt for just the
		//              number. This makes sense, so why isn't there any code for it?
		if (W_CheckNumForName (argv[1]) == -1 && isdigit(argv[1][0]))
		{ // The map name isn't valid, so lets try to make some assumptions for the user.
			char mapname[32];

			// If argc is 2, we assume Doom 2/Final Doom. If it's 3, Ultimate Doom.
			if ( argc == 2 )
			{
				sprintf( mapname, "MAP%02i", atoi( argv[1] ) );
			}
			else if ( argc == 3 )
			{
				sprintf( mapname, "E%iM%i", atoi( argv[1] ), atoi( argv[2] ) );
			}

			if (W_CheckNumForName (mapname) == -1)
			{ // Still no luck, oh well.
				Printf (PRINT_HIGH, "Map %s not found.\n", argv[1]);
			}
			else
			{ // Success
				unnatural_level_progression = true;
				G_DeferedInitNew (mapname);
			}

		}
		else
		{
			unnatural_level_progression = true;
			G_DeferedInitNew (argv[1]);
		}
	}
	else
	{
		Printf (PRINT_HIGH, "The current map is %s: \"%s\"\n", level.mapname, level.level_name);
	}
}
END_COMMAND (map)


const char* GetBase(const char* in)
{
	const char* out = &in[strlen(in) - 1];

	while (out > in && *(out-1) != PATHSEPCHAR)
		out--;

	return out;
}

BEGIN_COMMAND (wad) // denis - changes wads
{
	std::vector<std::string> wads, patches, hashes;
	bool AddedIWAD = false;
	bool Reboot = false;
	QWORD i, j;

	// [Russell] print out some useful info
	if (argc == 1)
	{
	    Printf(PRINT_HIGH, "Usage: wad pwad [...] [deh/bex [...]]\n");
	    Printf(PRINT_HIGH, "       wad iwad [pwad [...]] [deh/bex [...]]\n");
	    Printf(PRINT_HIGH, "\n");
	    Printf(PRINT_HIGH, "Load a wad file on the fly, pwads/dehs/bexs require extension\n");
	    Printf(PRINT_HIGH, "eg: wad doom\n");

	    return;
	}

	// Did we pass an IWAD?
	if (W_IsIWAD(argv[1])) {
		std::string ext;

		if (!M_ExtractFileExtension(argv[1], ext)) {
			wads.push_back(std::string(argv[1]) + ".wad");
		} else {
			wads.push_back(argv[1]);
		}
		AddedIWAD = true;
	}

	// Are the passed params WAD files or patch files?
	for (i = 1; i < argc; i++) {
		std::string ext;

		if (M_ExtractFileExtension(argv[i], ext)) {
			if ((ext == "wad") && !W_IsIWAD(argv[i])) {
				// Wad that isn't an IWAD
				wads.push_back(argv[i]);
			} else if  (ext == "deh" || ext == "bex") {
				// Patch file
				patches.push_back(argv[i]);
			}
		}
	}

	// Check our environment, if the same WADs are used, ignore this command.

	// Did we switch IWAD files?
	if (AddedIWAD && !wadfiles.empty()) {
		if (StdStringCompare(M_ExtractFileName(wads[0]), M_ExtractFileName(wadfiles[1]), true) != 0) {
			Reboot = true;
		}
	}

	// Do the sizes of the WAD lists not match up?
	if (!Reboot) {
		if (wadfiles.size() - 2 != wads.size() - (AddedIWAD ? 1 : 0)) {
			Reboot = true;
		}
	}

	// Do our WAD lists match up exactly?
	if (!Reboot) {
		for (i = 2, j = (AddedIWAD ? 1 : 0); i < wadfiles.size() && j < wads.size(); i++, j++) {
			if (StdStringCompare(M_ExtractFileName(wads[j]), M_ExtractFileName(wadfiles[i]), true) != 0) {
				Reboot = true;
				break;
			}
		}
	}

	// Do the sizes of the patch lists not match up?
	if (!Reboot) {
		if (patchfiles.size() != patches.size()) {
			Reboot = true;
		}
	}

	// Do our patchfile lists match up exactly?
	if (!Reboot) {
		for (i = 0, j = 0; i < patchfiles.size() && j < patches.size(); i++, j++) {
			if (StdStringCompare(M_ExtractFileName(patches[j]), M_ExtractFileName(patchfiles[i]), true) != 0) {
				Reboot = true;
				break;
			}
		}
	}

	if (Reboot) {
		if (!AddedIWAD) {
			wads.insert(wads.begin(), wadfiles[1]);
		}

		D_DoomWadReboot(wads, patches);
		unnatural_level_progression = true;
		G_DeferedInitNew(startmap);
	}
}
END_COMMAND (wad)

BOOL 			secretexit;
static int		startpos;	// [RH] Support for multiple starts per level

EXTERN_CVAR(sv_shufflemaplist)

void G_ChangeMap (void) {
	unnatural_level_progression = false;

	if (sv::Maplist::instance().maplist.empty()) {
		char *next = level.nextmap;

		// if deathmatch, stay on same level
		// [ML] 1/25/10: OR if next is empty
		if(gamestate == GS_STARTUP ||
			sv_gametype != GM_COOP || !strlen(next))
			next = level.mapname;
		else
			if(secretexit && W_CheckNumForName (level.secretmap) != -1)
				next = level.secretmap;

		if (!strncmp (next, "EndGame", 7) || (gamemode == retail_chex && !strncmp (level.nextmap, "E1M6", 4)))
		{
			// NES - exiting a Doom 1 episode moves to the next episode, rather than always going back to E1M1
			if (gameinfo.flags & GI_MAPxx || gamemode == shareware || (!sv_loopepisode &&
				((gamemode == registered && level.cluster == 3) || (gamemode == retail && level.cluster == 4))))
					next = CalcMapName(1, 1);
				else if (sv_loopepisode)
					next = CalcMapName(level.cluster, 1);
				else
					next = CalcMapName(level.cluster+1, 1);
		}

		G_DeferedInitNew(next);
	} else {
		sv::maplist_entry_t &maplist_entry = sv::Maplist::instance().maplist[sv::Maplist::instance().nextmap_index];

		// Load any wads given
		if (maplist_entry.wads.empty() == false)
			AddCommandString("wad " + maplist_entry.wads);

		// Change the map and bump the position of the next maplist entry
		G_DeferedInitNew((char *)maplist_entry.map.c_str());

		// The "Next Map" is now the "Current Map"
		sv::Maplist::instance().maplist_index = sv::Maplist::instance().nextmap_index;

		// Reset position in maplist if we go off the end
		if (sv::Maplist::instance().nextmap_index + 1 < sv::Maplist::instance().maplist.size())
			++sv::Maplist::instance().nextmap_index;
		else {
			sv::Maplist::instance().nextmap_index = 0;

			if (sv_shufflemaplist)
				sv::Maplist::instance().random_shuffle();
		}
	}

	// run script at the end of each map
	// [ML] 8/22/2010: There are examples in the wiki that outright don't work
	// when onlcvars (addcommandstring's second param) is true.  Is there a
	// reason why the mapscripts ahve to be safe mode?
	if(strlen(sv_endmapscript.cstring()))
		AddCommandString(sv_endmapscript.cstring()/*, true*/);
}

void SV_ClientFullUpdate(player_t &pl);
void SV_CheckTeam(player_t &pl);
void G_DoReborn(player_t &playernum);

//
// G_DoNewGame
//
// denis - rewritten so that it does not force client reconnects
//
void G_DoNewGame (void)
{
	size_t i;

	for(i = 0; i < players.size(); i++)
	{
		if(!players[i].ingame())
			continue;

		client_t *cl = &clients[i];

		MSG_WriteMarker   (&cl->reliablebuf, svc_loadmap);
		MSG_WriteString (&cl->reliablebuf, d_mapname);
	}

	sv_curmap.ForceSet(d_mapname);

	G_InitNew (d_mapname);
	gameaction = ga_nothing;

	// run script at the start of each map
	// [ML] 8/22/2010: There are examples in the wiki that outright don't work
	// when onlcvars (addcommandstring's second param) is true.  Is there a
	// reason why the mapscripts ahve to be safe mode?
	if(strlen(sv_startmapscript.cstring()))
		AddCommandString(sv_startmapscript.cstring()/*,true*/);

	for(i = 0; i < players.size(); i++)
	{
		if(!players[i].ingame())
			continue;

		if (sv_gametype == GM_TEAMDM || sv_gametype == GM_CTF)
			SV_CheckTeam(players[i]);
		else
			players[i].userinfo.color = players[i].prefcolor;

		SV_ClientFullUpdate (players[i]);
	}
}

EXTERN_CVAR (sv_skill)
EXTERN_CVAR (sv_monstersrespawn)
EXTERN_CVAR (sv_fastmonsters)
EXTERN_CVAR (sv_maxplayers)

void G_PlayerReborn (player_t &player);
void SV_ServerSettingChange();

void G_InitNew (const char *mapname)
{
	size_t i;

	if (!savegamerestore)
		G_ClearSnapshots ();

	// [RH] Mark all levels as not visited
	if (!savegamerestore)
	{
		for (i = 0; i < numwadlevelinfos; i++)
			wadlevelinfos[i].flags &= ~LEVEL_VISITED;

		for (i = 0; LevelInfos[i].mapname[0]; i++)
			LevelInfos[i].flags &= ~LEVEL_VISITED;
	}

	int old_gametype = sv_gametype.asInt();

	cvar_t::UnlatchCVars ();

	if(old_gametype != sv_gametype || sv_gametype != GM_COOP) {
		unnatural_level_progression = true;

		// Nes - Force all players to be spectators when the sv_gametype is not now or previously co-op.
		for (i = 0; i < players.size(); i++) {
			// [SL] 2011-07-30 - Don't force downloading players to become spectators
			// it stops their downloading
			if (!players[i].ingame())
				continue;

			for (size_t j = 0; j < players.size(); j++) {
				if (!players[j].ingame())
					continue;
				MSG_WriteMarker (&(players[j].client.reliablebuf), svc_spectate);
				MSG_WriteByte (&(players[j].client.reliablebuf), players[i].id);
				MSG_WriteByte (&(players[j].client.reliablebuf), true);
			}
			players[i].spectator = true;
			players[i].playerstate = PST_LIVE;
			players[i].joinafterspectatortime = -(TICRATE*5);
		}
	}

	// [SL] 2011-09-01 - Change gamestate here so SV_ServerSettingChange will
	// send changed cvars
	gamestate = GS_LEVEL;
	SV_ServerSettingChange();

	if (paused)
	{
		paused = false;
	}

	// [RH] If this map doesn't exist, bomb out
	if (W_CheckNumForName (mapname) == -1)
	{
		I_Error ("Could not find map %s\n", mapname);
	}

	if (sv_skill == sk_nightmare || sv_monstersrespawn)
		respawnmonsters = true;
	else
		respawnmonsters = false;

	bool wantFast = sv_fastmonsters || (sv_skill == sk_nightmare);
	if (wantFast != isFast)
	{
		if (wantFast)
		{
			for (i=S_SARG_RUN1 ; i<=S_SARG_PAIN2 ; i++)
				states[i].tics >>= 1;
			mobjinfo[MT_BRUISERSHOT].speed = 20*FRACUNIT;
			mobjinfo[MT_HEADSHOT].speed = 20*FRACUNIT;
			mobjinfo[MT_TROOPSHOT].speed = 20*FRACUNIT;
		}
		else
		{
			for (i=S_SARG_RUN1 ; i<=S_SARG_PAIN2 ; i++)
				states[i].tics <<= 1;
			mobjinfo[MT_BRUISERSHOT].speed = 15*FRACUNIT;
			mobjinfo[MT_HEADSHOT].speed = 10*FRACUNIT;
			mobjinfo[MT_TROOPSHOT].speed = 10*FRACUNIT;
		}
		isFast = wantFast;
	}

	// [SL] 2011-05-11 - Reset all reconciliation system data for unlagging
	Unlag::getInstance().reset();

	if (!savegamerestore)
	{
		M_ClearRandom ();
		memset (ACS_WorldVars, 0, sizeof(ACS_WorldVars));
		memset (ACS_GlobalVars, 0, sizeof(ACS_GlobalVars));
		level.time = 0;
		level.timeleft = 0;
		level.inttimeleft = 0;

		// force players to be initialized upon first level load
		for (size_t i = 0; i < players.size(); i++)
		{
			// [SL] 2011-05-11 - Register the players in the reconciliation
			// system for unlagging
			Unlag::getInstance().registerPlayer(players[i].id);

			if(!players[i].ingame())
				continue;

			// denis - dead players should have their stuff looted, otherwise they'd take their ammo into their afterlife!
			if(players[i].playerstate == PST_DEAD)
				G_PlayerReborn(players[i]);

			players[i].playerstate = PST_ENTER; // [BC]

			players[i].joinafterspectatortime = -(TICRATE*5);
		}

		sv::Voting::instance().event_initlevel();
	}

	// [SL] 2012-12-08 - Multiplayer is always true for servers
	multiplayer = true;

	usergame = true;				// will be set false if a demo
	paused = false;
	demoplayback = false;
	viewactive = true;
	shotclock = 0;

	strncpy (level.mapname, mapname, 8);
	G_DoLoadLevel (0);

	// denis - hack to fix ctfmode, as it is only known after the map is processed!
	//if(old_ctfmode != ctfmode)
	//	SV_ServerSettingChange();
}

//
// G_DoCompleted
//

void G_ExitLevel (int position, int drawscores)
{
	SV_ExitLevel();

	if (drawscores)
        SV_DrawScores();
	
	int intlimit = (sv_intermissionlimit < 1 || sv_gametype == GM_COOP ? DEFINTSECS : sv_intermissionlimit);

	gamestate = GS_INTERMISSION;
	shotclock = 0;
	mapchange = TICRATE*intlimit;  // wait n seconds, default 10

    secretexit = false;

    gameaction = ga_completed;

	// denis - this will skip wi_stuff and allow some time for finale text
	//G_WorldDone();
}

// Here's for the german edition.
void G_SecretExitLevel (int position, int drawscores)
{
	SV_ExitLevel();

    if (drawscores)
        SV_DrawScores();
        
	int intlimit = (sv_intermissionlimit < 1 || sv_gametype == GM_COOP ? DEFINTSECS : sv_intermissionlimit);

	gamestate = GS_INTERMISSION;
	shotclock = 0;
	mapchange = TICRATE*intlimit;  // wait n seconds, defaults to 10

	// IF NO WOLF3D LEVELS, NO SECRET EXIT!
	if ( (gamemode == commercial)
		 && (W_CheckNumForName("map31")<0))
		secretexit = false;
	else
		secretexit = true;

    gameaction = ga_completed;

	// denis - this will skip wi_stuff and allow some time for finale text
	//G_WorldDone();
}

void G_DoCompleted (void)
{
	size_t i;

	gameaction = ga_nothing;

	for(i = 0; i < players.size(); i++)
		if(players[i].ingame())
			G_PlayerFinishLevel(players[i]);
}

//
// G_DoLoadLevel
//
extern gamestate_t 	wipegamestate;
extern float BaseBlendA;

void G_DoLoadLevel (int position)
{
	static int lastposition = 0;
	size_t i;

	if (position != -1)
		firstmapinit = true;

	if (position == -1)
		position = lastposition;
	else
		lastposition = position;

	G_InitLevelLocals ();

	if (firstmapinit) {
		Printf (PRINT_HIGH, "--- %s: \"%s\" ---\n", level.mapname, level.level_name);
		firstmapinit = false;
	}

	if (wipegamestate == GS_LEVEL)
		wipegamestate = GS_FORCEWIPE;

	gamestate = GS_LEVEL;

//	if (demoplayback || oldgs == GS_STARTUP)
//		C_HideConsole ();

	// Set the sky map.
	// First thing, we have a dummy sky texture name,
	//	a flat. The data is in the WAD only because
	//	we look for an actual index, instead of simply
	//	setting one.
	skyflatnum = R_FlatNumForName ( SKYFLATNAME );

	// DOOM determines the sky texture to be used
	// depending on the current episode, and the game version.
	// [RH] Fetch sky parameters from level_locals_t.
	// [ML] 5/11/06 - remove sky2 remenants
	// [SL] 2012-03-19 - Add sky2 back
	sky1texture = R_TextureNumForName (level.skypic);
	if (strlen(level.skypic2))
		sky2texture = R_TextureNumForName (level.skypic2);
	else
		sky2texture = 0;

	for (i = 0; i < players.size(); i++)
	{
		if (players[i].ingame() && players[i].playerstate == PST_DEAD)
			players[i].playerstate = PST_REBORN;

		players[i].fragcount = 0;
		players[i].deathcount = 0; // [Toke - Scores - deaths]
		players[i].killcount = 0; // [deathz0r] Coop kills
		players[i].points = 0;
	}

	// [deathz0r] It's a smart idea to reset the team points
	if (sv_gametype == GM_TEAMDM || sv_gametype == GM_CTF)
	{
		for (size_t i = 0; i < NUMTEAMS; i++)
			TEAMpoints[i] = 0;
	}

	// initialize the msecnode_t freelist.					phares 3/25/98
	// any nodes in the freelist are gone by now, cleared
	// by Z_FreeTags() when the previous level ended or player
	// died.

	{
		extern msecnode_t *headsecnode; // phares 3/25/98
		headsecnode = NULL;

		// [RH] Need to prevent the AActor destructor from trying to
		//		free the nodes
		AActor *actor;
		TThinkerIterator<AActor> iterator;

		while ( (actor = iterator.Next ()) )
		{
			actor->touching_sectorlist = NULL;

			// denis - clear every actor netid so that they don't announce their destruction to clients
			ServerNetID.ReleaseNetID(actor->netid);
			actor->netid = 0;
		}
	}

	// For single-player servers.
	for (i = 0; i < players.size(); i++)
		players[i].joinafterspectatortime -= level.time;

	flagdata *tempflag;

	// Nes - CTF Pre flag setup
	if (sv_gametype == GM_CTF) {
		tempflag = &CTFdata[it_blueflag];
		tempflag->flaglocated = false;

		tempflag = &CTFdata[it_redflag];
		tempflag->flaglocated = false;
	}

	P_SetupLevel (level.mapname, position);

	// Nes - CTF Post flag setup
	if (sv_gametype == GM_CTF) {
		tempflag = &CTFdata[it_blueflag];
		if (!tempflag->flaglocated)
			SV_BroadcastPrintf(PRINT_HIGH, "WARNING: Blue flag pedestal not found! No blue flags in game.\n");

		tempflag = &CTFdata[it_redflag];
		if (!tempflag->flaglocated)
			SV_BroadcastPrintf(PRINT_HIGH, "WARNING: Red flag pedestal not found! No red flags in game.\n");
	}

	displayplayer_id = consoleplayer_id;				// view the guy you are playing

	gameaction = ga_nothing;
	Z_CheckHeap ();

	// clear cmd building stuff // denis - todo - could we get rid of this?
	Impulse = 0;
	for (i = 0; i < NUM_ACTIONS; i++)
		if (i != ACTION_MLOOK && i != ACTION_KLOOK)
			Actions[i] = 0;

	joyxmove = joyymove = 0;
	mousex = mousey = 0;
	sendpause = sendsave = paused = sendcenterview = false;

	if (timingdemo) {
		static BOOL firstTime = true;

		if (firstTime) {
			starttime = I_GetTimePolled ();
			firstTime = false;
		}
	}

	level.starttime = I_GetTime ();
	G_UnSnapshotLevel (!savegamerestore);	// [RH] Restore the state of the level.
	P_DoDeferedScripts ();	// [RH] Do script actions that were triggered on another map.
	//	C_FlushDisplay ();
}

//
// G_WorldDone
//
void G_WorldDone (void)
{
	cluster_info_t *nextcluster;
	cluster_info_t *thiscluster;

	//gameaction = ga_worlddone;

	if (level.flags & LEVEL_CHANGEMAPCHEAT)
		return;

	const char *finaletext = NULL;
	thiscluster = FindClusterInfo (level.cluster);
	if (!strncmp (level.nextmap, "EndGame", 7) || (gamemode == retail_chex && !strncmp (level.nextmap, "E1M6", 4))) {
//		F_StartFinale (thiscluster->messagemusic, thiscluster->finaleflat, thiscluster->exittext); // denis - fixme - what should happen on the server?
		finaletext = thiscluster->exittext;
	} else {
		if (!secretexit)
			nextcluster = FindClusterInfo (FindLevelInfo (level.nextmap)->cluster);
		else
			nextcluster = FindClusterInfo (FindLevelInfo (level.secretmap)->cluster);

		if (nextcluster->cluster != level.cluster && sv_gametype == GM_COOP) {
			// Only start the finale if the next level's cluster is different
			// than the current one and we're not in deathmatch.
			if (nextcluster->entertext) {
//				F_StartFinale (nextcluster->messagemusic, nextcluster->finaleflat, nextcluster->entertext); // denis - fixme
				finaletext = nextcluster->entertext;
			} else if (thiscluster->exittext) {
//				F_StartFinale (thiscluster->messagemusic, thiscluster->finaleflat, thiscluster->exittext); // denis - fixme
				finaletext = thiscluster->exittext;
			}
		}
	}

	if(finaletext)
		mapchange += strlen(finaletext)*2;
}

void G_DoWorldDone (void)
{
	gamestate = GS_LEVEL;
	if (wminfo.next[0] == 0) {
		// Don't die if no next map is given,
		// just repeat the current one.
		Printf (PRINT_HIGH, "No next map specified.\n");
	} else {
		strncpy (level.mapname, wminfo.next, 8);
	}
	G_DoLoadLevel (startpos);
	startpos = 0;
	gameaction = ga_nothing;
	viewactive = true;
}


extern dyncolormap_t NormalLight;

void G_InitLevelLocals ()
{
//	unsigned long oldfade = level.fadeto;
	level_info_t *info;
	int i;

	NormalLight.maps = realcolormaps;

	level.gravity = sv_gravity;
	level.aircontrol = (fixed_t)(sv_aircontrol * 65536.f);

	if ((i = FindWadLevelInfo (level.mapname)) > -1)
	{
		level_pwad_info_t *pinfo = wadlevelinfos + i;

		// [ML] 5/11/06 - Remove sky scrolling and sky2
		// [SL] 2012-03-19 - Add sky2 back
		level.info = (level_info_t *)pinfo;
		info = (level_info_t *)pinfo;
		strncpy (level.skypic2, pinfo->skypic2, 8);
		level.fadeto = pinfo->fadeto;
		if (level.fadeto) {
//			NormalLight.maps = DefaultPalette->maps.colormaps;
		} else {
//			R_SetDefaultColormap (pinfo->fadetable);
		}
		level.outsidefog = pinfo->outsidefog;
		level.flags |= LEVEL_DEFINEDINMAPINFO;
		if (pinfo->gravity != 0.f)
		{
			level.gravity = pinfo->gravity;
		}
		if (pinfo->aircontrol != 0.f)
		{
			level.aircontrol = (fixed_t)(pinfo->aircontrol * 65536.f);
		}
	} else {
		info = FindDefLevelInfo (level.mapname);
		level.info = info;
		level.skypic2[0] = 0;
		level.fadeto = 0;
		level.outsidefog = 0xff000000;	// 0xff000000 signals not to handle it special
		R_SetDefaultColormap ("COLORMAP");
	}

	if (info->level_name) {
		level.partime = info->partime;
		level.cluster = info->cluster;
		level.flags = info->flags;
		level.levelnum = info->levelnum;

		strncpy (level.level_name, info->level_name, 63);
		strncpy (level.nextmap, info->nextmap, 8);
		strncpy (level.secretmap, info->secretmap, 8);
		strncpy (level.music, info->music, 8);
		strncpy (level.skypic, info->skypic, 8);
		if (!level.skypic2[0])
			strncpy(level.skypic2, level.skypic, 8);
	} else {
		level.partime = level.cluster = 0;
		strcpy (level.level_name, "Unnamed");
		level.nextmap[0] =
			level.secretmap[0] =
			level.music[0] = 0;
		strncpy (level.skypic, "SKY1", 8);
		strncpy (level.skypic2, "SKY1", 8);
		level.flags = 0;
		level.levelnum = 1;
	}
//  [deathz0r] Doesn't appear to affect client
//	if (oldfade != level.fadeto)
//		RefreshPalettes ();
}

char *CalcMapName (int episode, int level)
{
	static char lumpname[9];

	if (gameinfo.flags & GI_MAPxx)
	{
		sprintf (lumpname, "MAP%02d", level);
	}
	else
	{
		lumpname[0] = 'E';
		lumpname[1] = '0' + episode;
		lumpname[2] = 'M';
		lumpname[3] = '0' + level;
		lumpname[4] = 0;
	}
	return lumpname;
}

static level_info_t *FindDefLevelInfo (char *mapname)
{
	level_info_t *i;

	i = LevelInfos;
	while (i->level_name) {
		if (!strnicmp (i->mapname, mapname, 8))
			break;
		i++;
	}
	return i;
}

level_info_t *FindLevelInfo (char *mapname)
{
	int i;

	if ((i = FindWadLevelInfo (mapname)) > -1)
		return (level_info_t *)(wadlevelinfos + i);
	else
		return FindDefLevelInfo (mapname);
}

level_info_t *FindLevelByNum (int num)
{
	{
		for (size_t i = 0; i < numwadlevelinfos; i++)
			if (wadlevelinfos[i].levelnum == num)
				return (level_info_t *)(wadlevelinfos + i);
	}
	{
		level_info_t *i = LevelInfos;
		while (i->level_name) {
			if (i->levelnum == num && W_CheckNumForName (i->mapname) != -1)
				return i;
			i++;
		}
		return NULL;
	}
}

static cluster_info_t *FindDefClusterInfo (int cluster)
{
	cluster_info_t *i;

	i = ClusterInfos;
	while (i->cluster && i->cluster != cluster)
		i++;

	return i;
}

cluster_info_t *FindClusterInfo (int cluster)
{
	int i;

	if ((i = FindWadClusterInfo (cluster)) > -1)
		return wadclusterinfos + i;
	else
		return FindDefClusterInfo (cluster);
}

void G_SetLevelStrings (void)
{
	char temp[8];
	const char *namepart;
	int i, start;

	temp[0] = '0';
	temp[1] = ':';
	temp[2] = 0;
	for (i = HUSTR_E1M1; i <= HUSTR_E4M9; ++i)
	{
		if (temp[0] < '9')
			temp[0]++;
		else
			temp[0] = '1';

		if ( (namepart = strstr (GStrings(i), temp)) )
		{
			namepart += 2;
			while (*namepart && *namepart <= ' ')
				namepart++;
		}
		else
		{
			namepart = GStrings(i);
		}

		ReplaceString (&LevelInfos[i-HUSTR_E1M1].level_name, namepart);
		//ReplaceString (&LevelInfos[i-HUSTR_E1M1].music, Musics1[i-HUSTR_E1M1]);
	}

	for (i = 0; i < 4; i++)
		ReplaceString (&ClusterInfos[i].exittext, GStrings(E1TEXT+i));

	if (gamemission == pack_plut)
		start = PHUSTR_1;
	else if (gamemission == pack_tnt)
		start = THUSTR_1;
	else
		start = HUSTR_1;

 	for (i = 0; i < 32; i++) {
 		sprintf (temp, "%d:", i + 1);
		if ( (namepart = strstr (GStrings(i+start), temp)) ) {
 			namepart += strlen (temp);
 			while (*namepart && *namepart <= ' ')
 				namepart++;
 		} else {
			namepart = GStrings(i+start);
 		}
 		ReplaceString (&LevelInfos[36+i].level_name, namepart);
 	}

	if (gamemission == pack_plut)
		start = P1TEXT;		// P1TEXT
	else if (gamemission == pack_tnt)
		start = T1TEXT;		// T1TEXT
	else
		start = C1TEXT;		// C1TEXT

	for (i = 0; i < 4; i++)
		ReplaceString (&ClusterInfos[4 + i].exittext, GStrings(start+i));
	for (; i < 6; i++)
		ReplaceString (&ClusterInfos[4 + i].entertext, GStrings(start+i));

	//for (i = 0; i < 15; i++)
	//	ReplaceString (&ClusterInfos[i].messagemusic, Musics4[i]);

	if (level.info)
		strncpy (level.level_name, level.info->level_name, 63);
}


void G_AirControlChanged ()
{
	if (level.aircontrol <= 256)
	{
		level.airfriction = FRACUNIT;
	}
	else
	{
		// Friction is inversely proportional to the amount of control
		float fric = ((float)level.aircontrol/65536.f) * -0.0941f + 1.0004f;
		level.airfriction = (fixed_t)(fric * 65536.f);
	}
}

void G_SerializeLevel (FArchive &arc, bool hubLoad)
{
	if (arc.IsStoring ())
	{
		unsigned int playernum = players.size();
		arc << level.flags
			<< level.fadeto
			<< level.found_secrets
			<< level.found_items
			<< level.killed_monsters
			<< level.gravity
			<< level.aircontrol;

		G_AirControlChanged ();

		for (int i = 0; i < NUM_MAPVARS; i++)
			arc << level.vars[i];

		arc << playernum;
	}
	else
	{
		unsigned int playernum;
		arc >> level.flags
			>> level.fadeto
			>> level.found_secrets
			>> level.found_items
			>> level.killed_monsters
			>> level.gravity
			>> level.aircontrol;

		G_AirControlChanged ();

		for (int i = 0; i < NUM_MAPVARS; i++)
			arc >> level.vars[i];

       	arc >> playernum;

		players.resize(playernum);
	}

	if (!hubLoad)
		P_SerializePlayers (arc);

	P_SerializeThinkers (arc, hubLoad);
	P_SerializeWorld (arc);
	P_SerializePolyobjs (arc);
	P_SerializeSounds (arc);
}

// Archives the current level
void G_SnapshotLevel ()
{
	delete level.info->snapshot;

	level.info->snapshot = new FLZOMemFile;
	level.info->snapshot->Open ();

	FArchive arc (*level.info->snapshot);

	G_SerializeLevel (arc, false);
}

// Unarchives the current level based on its snapshot
// The level should have already been loaded and setup.
void G_UnSnapshotLevel (bool hubLoad)
{
	if (level.info->snapshot == NULL)
		return;

	level.info->snapshot->Reopen ();
	FArchive arc (*level.info->snapshot);
	if (hubLoad)
		arc.SetHubTravel (); // denis - hexen?
	G_SerializeLevel (arc, hubLoad);
	arc.Close ();
	// No reason to keep the snapshot around once the level's been entered.
	delete level.info->snapshot;
	level.info->snapshot = NULL;
}

void G_ClearSnapshots (void)
{
	size_t i;

	for (i = 0; i < numwadlevelinfos; i++)
		if (wadlevelinfos[i].snapshot)
		{
			delete wadlevelinfos[i].snapshot;
			wadlevelinfos[i].snapshot = NULL;
		}

	for (i = 0; LevelInfos[i].level_name; i++)
		if (LevelInfos[i].snapshot)
		{
			delete LevelInfos[i].snapshot;
			LevelInfos[i].snapshot = NULL;
		}
}

static void writeSnapShot (FArchive &arc, level_info_t *i)
{
	arc.Write (i->mapname, 8);
	i->snapshot->Serialize (arc);
}

void G_SerializeSnapshots (FArchive &arc)
{
	if (arc.IsStoring ())
	{
		size_t i;

		for (i = 0; i < numwadlevelinfos; i++)
			if (wadlevelinfos[i].snapshot)
				writeSnapShot (arc, (level_info_s *)&wadlevelinfos[i]);

		for (i = 0; LevelInfos[i].level_name; i++)
			if (LevelInfos[i].snapshot)
				writeSnapShot (arc, &LevelInfos[i]);

		// Signal end of snapshots
		arc << (char)0;
	}
	else
	{
		char mapname[8];

		G_ClearSnapshots ();

		arc >> mapname[0];
		while (mapname[0])
		{
			arc.Read (&mapname[1], 7);
			level_info_t *i = FindLevelInfo (mapname);
			i->snapshot = new FLZOMemFile;
			i->snapshot->Serialize (arc);
			arc >> mapname[0];
		}
	}
}

static void writeDefereds (FArchive &arc, level_info_t *i)
{
	arc.Write (i->mapname, 8);
	arc << i->defered;
}

void P_SerializeACSDefereds (FArchive &arc)
{
	if (arc.IsStoring ())
	{
		unsigned int i;

		for (i = 0; i < numwadlevelinfos; i++)
			if (wadlevelinfos[i].defered)
				writeDefereds (arc, (level_info_s *)&wadlevelinfos[i]);

		for (i = 0; LevelInfos[i].level_name; i++)
			if (LevelInfos[i].defered)
				writeDefereds (arc, &LevelInfos[i]);

		// Signal end of defereds
		BYTE zero = 0;
		arc << zero;
	}
	else
	{
		char mapname[8];

		P_RemoveDefereds ();

		arc >> mapname[0];
		while (mapname[0])
		{
			arc.Read (&mapname[1], 7);
			level_info_t *i = FindLevelInfo (mapname);
			if (i == NULL)
			{
				char name[9];

				strncpy (name, mapname, 8);
				name[8] = 0;
				I_Error ("Unknown map '%s' in savegame", name);
			}
			arc >> i->defered;
			arc >> mapname[0];
		}
	}
}

VERSION_CONTROL (g_level_cpp, "$Id$")
