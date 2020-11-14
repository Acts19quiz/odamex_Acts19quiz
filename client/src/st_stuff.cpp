// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2006-2020 by The Odamex Team.
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
//		Status bar code.
//		Does the face/direction indicator animatin.
//		Does palette indicators as well (red pain/berserk, bright pickup)
//		[RH] Widget coordinates are relative to the console, not the screen!
//
//-----------------------------------------------------------------------------

#include "i_video.h"
#include "z_zone.h"
#include "m_random.h"
#include "w_wad.h"
#include "doomdef.h"
#include "g_game.h"
#include "st_stuff.h"
#include "st_lib.h"
#include "r_local.h"
#include "p_inter.h"
#include "am_map.h"
#include "m_cheat.h"
#include "s_sound.h"
#include "v_video.h"
#include "v_text.h"
#include "doomstat.h"
#include "gstrings.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "version.h"
#include "cl_main.h"
#include "gi.h"
#include "cl_demo.h"
#include "c_console.h"

#include "p_ctf.h"


static bool st_needrefresh = true;

static bool st_stopped = true;


// lump number for PLAYPAL
static int		lu_palette;

EXTERN_CVAR(sv_allowredscreen)
EXTERN_CVAR(st_scale)
EXTERN_CVAR(screenblocks)

// [RH] Status bar background
IWindowSurface* stbar_surface;
IWindowSurface* stnum_surface;

// functions in st_new.c
void ST_initNew();
void ST_unloadNew();

extern bool simulated_connection;

//
// STATUS BAR DATA
//


// N/256*100% probability
//	that the normal face state will change
#define ST_FACEPROBABILITY		96

// Location of status bar face
#define ST_FX					(143)
#define ST_FY					(0)

// Should be set to patch width
//	for tall numbers later on
#define ST_TALLNUMWIDTH 		(tallnum[0]->width)

// Number of status faces.
#define ST_NUMPAINFACES 		5
#define ST_NUMSTRAIGHTFACES 	3
#define ST_NUMTURNFACES 		2
#define ST_NUMSPECIALFACES		3

#define ST_FACESTRIDE \
		  (ST_NUMSTRAIGHTFACES+ST_NUMTURNFACES+ST_NUMSPECIALFACES)

#define ST_NUMEXTRAFACES		2

#define ST_NUMFACES \
		  (ST_FACESTRIDE*ST_NUMPAINFACES+ST_NUMEXTRAFACES)

#define ST_TURNOFFSET			(ST_NUMSTRAIGHTFACES)
#define ST_OUCHOFFSET			(ST_TURNOFFSET + ST_NUMTURNFACES)
#define ST_EVILGRINOFFSET		(ST_OUCHOFFSET + 1)
#define ST_RAMPAGEOFFSET		(ST_EVILGRINOFFSET + 1)
#define ST_GODFACE				(ST_NUMPAINFACES*ST_FACESTRIDE)
#define ST_DEADFACE 			(ST_GODFACE+1)

#define ST_FACESX				(143)
#define ST_FACESY				(0)

#define ST_EVILGRINCOUNT		(2*TICRATE)
#define ST_STRAIGHTFACECOUNT	(TICRATE/2)
#define ST_TURNCOUNT			(1*TICRATE)
#define ST_OUCHCOUNT			(1*TICRATE)
#define ST_RAMPAGEDELAY 		(2*TICRATE)

#define ST_MUCHPAIN 			20


// Location and size of statistics,
//	justified according to widget type.
// Problem is, within which space? STbar? Screen?
// Note: this could be read in by a lump.
//		 Problem is, is the stuff rendered
//		 into a buffer,
//		 or into the frame buffer?

// AMMO number pos.
#define ST_AMMOWIDTH			3
#define ST_AMMOX				(44)
#define ST_AMMOY				(3)

// HEALTH number pos.
#define ST_HEALTHWIDTH			3
#define ST_HEALTHX				(90)
#define ST_HEALTHY				(3)

// Weapon pos.
#define ST_ARMSX				(111)
#define ST_ARMSY				(4)
#define ST_ARMSBGX				(104)
#define ST_ARMSBGY				(0)
#define ST_ARMSXSPACE			12
#define ST_ARMSYSPACE			10

// Flags pos.
#define ST_FLAGSBGX				(106)
#define ST_FLAGSBGY				(0)

// Frags pos.
#define ST_FRAGSX				(138)
#define ST_FRAGSY				(3)
#define ST_FRAGSWIDTH			2

// ARMOR number pos.
#define ST_ARMORWIDTH			3
#define ST_ARMORX				(221)
#define ST_ARMORY				(3)

// Flagbox positions.
#define ST_FLGBOXX				(236)
#define ST_FLGBOXY				(0)
#define ST_FLGBOXBLUX			(239)
#define ST_FLGBOXBLUY			(3)
#define ST_FLGBOXREDX			(239)
#define ST_FLGBOXREDY			(18)

// Key icon positions.
#define ST_KEY0WIDTH			8
#define ST_KEY0HEIGHT			5
#define ST_KEY0X				(239)
#define ST_KEY0Y				(3)
#define ST_KEY1WIDTH			ST_KEY0WIDTH
#define ST_KEY1X				(239)
#define ST_KEY1Y				(13)
#define ST_KEY2WIDTH			ST_KEY0WIDTH
#define ST_KEY2X				(239)
#define ST_KEY2Y				(23)

// Ammunition counter.
#define ST_AMMO0WIDTH			3
#define ST_AMMO0HEIGHT			6
#define ST_AMMO0X				(288)
#define ST_AMMO0Y				(5)
#define ST_AMMO1WIDTH			ST_AMMO0WIDTH
#define ST_AMMO1X				(288)
#define ST_AMMO1Y				(11)
#define ST_AMMO2WIDTH			ST_AMMO0WIDTH
#define ST_AMMO2X				(288)
#define ST_AMMO2Y				(23)
#define ST_AMMO3WIDTH			ST_AMMO0WIDTH
#define ST_AMMO3X				(288)
#define ST_AMMO3Y				(17)

// Indicate maximum ammunition.
// Only needed because backpack exists.
#define ST_MAXAMMO0WIDTH		3
#define ST_MAXAMMO0HEIGHT		5
#define ST_MAXAMMO0X			(314)
#define ST_MAXAMMO0Y			(5)
#define ST_MAXAMMO1WIDTH		ST_MAXAMMO0WIDTH
#define ST_MAXAMMO1X			(314)
#define ST_MAXAMMO1Y			(11)
#define ST_MAXAMMO2WIDTH		ST_MAXAMMO0WIDTH
#define ST_MAXAMMO2X			(314)
#define ST_MAXAMMO2Y			(23)
#define ST_MAXAMMO3WIDTH		ST_MAXAMMO0WIDTH
#define ST_MAXAMMO3X			(314)
#define ST_MAXAMMO3Y			(17)

// pistol
#define ST_WEAPON0X 			(110)
#define ST_WEAPON0Y 			(4)

// shotgun
#define ST_WEAPON1X 			(122)
#define ST_WEAPON1Y 			(4)

// chain gun
#define ST_WEAPON2X 			(134)
#define ST_WEAPON2Y 			(4)

// missile launcher
#define ST_WEAPON3X 			(110)
#define ST_WEAPON3Y 			(13)

// plasma gun
#define ST_WEAPON4X 			(122)
#define ST_WEAPON4Y 			(13)

 // bfg
#define ST_WEAPON5X 			(134)
#define ST_WEAPON5Y 			(13)

// WPNS title
#define ST_WPNSX				(109)
#define ST_WPNSY				(23)

 // DETH title
#define ST_DETHX				(109)
#define ST_DETHY				(23)

// [RH] Turned these into variables
// Size of statusbar.
// Now ([RH] truly) sensitive for scaling.
int						ST_HEIGHT;
int						ST_WIDTH;
int						ST_X;
int						ST_Y;

// used for making messages go away
static int				st_msgcounter=0;

// whether in automap or first-person
static st_stateenum_t	st_gamestate;

// whether left-side main status bar is active
static bool			st_statusbaron;

// whether status bar chat is active
static bool			st_chat;

// value of st_chat before message popped up
static bool			st_oldchat;

// whether chat window has the cursor on
static bool			st_cursoron;

// !deathmatch && st_statusbaron
static bool			st_armson;

// !deathmatch
static bool			st_fragson;

// main bar left
static patch_t* 		sbar;

// 0-9, tall numbers
// [RH] no longer static
patch_t*		 		tallnum[10];

// tall % sign
// [RH] no longer static
patch_t*		 		tallpercent;

// 0-9, short, yellow (,different!) numbers
static patch_t* 		shortnum[10];

// 3 key-cards, 3 skulls, [RH] 3 combined
patch_t* 				keys[NUMCARDS+NUMCARDS/2];

// face status patches [RH] no longer static
patch_t* 				faces[ST_NUMFACES];

// face background
static patch_t* 		faceback;

// classic face background
static patch_t*			faceclassic[4];

 // main bar right
static patch_t* 		armsbg;

// score/flags
static patch_t* 		flagsbg;

// weapon ownership patches
static patch_t* 		arms[6][2];

// ready-weapon widget
static st_number_t		w_ready;

 // in deathmatch only, summary of frags stats
static st_number_t		w_frags;

// health widget
static st_percent_t 	w_health;

// weapon ownership widgets
static st_multicon_t	w_arms[6];

// face status widget
static st_multicon_t	w_faces;

// keycard widgets
static st_multicon_t	w_keyboxes[3];

// armor widget
static st_percent_t 	w_armor;

// ammo widgets
static st_number_t		w_ammo[4];

// max ammo widgets
static st_number_t		w_maxammo[4];

// number of frags so far in deathmatch
static int		st_fragscount;

// used to use appopriately pained face
static int		st_oldhealth = -1;

// used for evil grin
static bool		oldweaponsowned[NUMWEAPONS+1];

 // count until face changes
static int		st_facecount = 0;

// current face index, used by w_faces
// [RH] not static anymore
int				st_faceindex = 0;

// holds key-type for each key box on bar
static int		keyboxes[3];

// copy of player info
static int		st_health, st_armor;
static int		st_ammo[4], st_maxammo[4];
static int		st_weaponowned[6] = {0}, st_current_ammo;

// a random number per tick
static int		st_randomnumber;

// these are now in d_dehacked.cpp
extern byte cheat_mus_seq[9];
extern byte cheat_choppers_seq[11];
extern byte cheat_god_seq[6];
extern byte cheat_ammo_seq[6];
extern byte cheat_ammonokey_seq[5];
extern byte cheat_noclip_seq[11];
extern byte cheat_commercial_noclip_seq[7];
extern byte cheat_powerup_seq[7][10];
extern byte cheat_clev_seq[10];
extern byte cheat_mypos_seq[8];

// Now what?
cheatseq_t		cheat_mus = { cheat_mus_seq, 0 };
cheatseq_t		cheat_god = { cheat_god_seq, 0 };
cheatseq_t		cheat_ammo = { cheat_ammo_seq, 0 };
cheatseq_t		cheat_ammonokey = { cheat_ammonokey_seq, 0 };
cheatseq_t		cheat_noclip = { cheat_noclip_seq, 0 };
cheatseq_t		cheat_commercial_noclip = { cheat_commercial_noclip_seq, 0 };

cheatseq_t		cheat_powerup[7] =
{
	{ cheat_powerup_seq[0], 0 },
	{ cheat_powerup_seq[1], 0 },
	{ cheat_powerup_seq[2], 0 },
	{ cheat_powerup_seq[3], 0 },
	{ cheat_powerup_seq[4], 0 },
	{ cheat_powerup_seq[5], 0 },
	{ cheat_powerup_seq[6], 0 }
};

cheatseq_t		cheat_choppers = { cheat_choppers_seq, 0 };
cheatseq_t		cheat_clev = { cheat_clev_seq, 0 };
cheatseq_t		cheat_mypos = { cheat_mypos_seq, 0 };


//
// STATUS BAR CODE
//
void ST_Stop(void);
void ST_createWidgets(void);

int ST_StatusBarHeight(int surface_width, int surface_height)
{
	if (!R_StatusBarVisible())
		return 0;

	if (st_scale)
		return 32 * surface_height / 200;
	else
		return 32;
}

int ST_StatusBarWidth(int surface_width, int surface_height)
{
	if (!R_StatusBarVisible())
		return 0;

	if (!st_scale)
		return 320;

	// [AM] Scale status bar width according to height, unless there isn't
	//      enough room for it.  Fixes widescreen status bar scaling.
	// [ML] A couple of minor changes for true 4:3 correctness...
	if (I_IsProtectedResolution(surface_width, surface_height))
		return 10 * ST_StatusBarHeight(surface_width, surface_height);
	else
		return 4 * surface_height / 3;
}

int ST_StatusBarX(int surface_width, int surface_height)
{
	if (!R_StatusBarVisible())
		return 0;

	if (consoleplayer().spectator && displayplayer_id == consoleplayer_id)
		return 0;
	else
		return (surface_width - ST_StatusBarWidth(surface_width, surface_height)) / 2;
}

int ST_StatusBarY(int surface_width, int surface_height)
{
	if (!R_StatusBarVisible())
		return surface_height;

	if (consoleplayer().spectator && displayplayer_id == consoleplayer_id)
		return surface_height;
	else
		return surface_height - ST_StatusBarHeight(surface_width, surface_height);
}


//
// ST_ForceRefresh
//
//
void ST_ForceRefresh()
{
	st_needrefresh = true;
}


CVAR_FUNC_IMPL (st_scale)
{
	R_SetViewSize((int)screenblocks);
	ST_ForceRefresh();
}


EXTERN_CVAR (sv_allowcheats)

// Checks whether cheats are enabled or not, returns true if they're NOT enabled
// and false if they ARE enabled (stupid huh? not my work [Russell])
BOOL CheckCheatmode (void)
{
	// [SL] 2012-04-04 - Don't allow cheat codes to be entered while playing
	// back a netdemo
	if (simulated_connection)
		return true;

	// [Russell] - Allow vanilla style "no message" in singleplayer when cheats
	// are disabled
	if (sv_skill == sk_nightmare && !multiplayer)
        return true;

	if ((multiplayer || sv_gametype != GM_COOP) && !sv_allowcheats)
	{
		Printf (PRINT_WARNING, "You must run the server with '+set sv_allowcheats 1' to enable this command.\n");
		return true;
	}
	else
	{
		return false;
	}
}

// Respond to keyboard input events, intercept cheats.
// [RH] Cheats eatkey the last keypress used to trigger them
bool ST_Responder (event_t *ev)
{
	player_t *plyr = &consoleplayer();
	bool eatkey = false;
	int i;

	// Filter automap on/off.
	if (ev->type == ev_keyup && ((ev->data1 & 0xffff0000) == AM_MSGHEADER))
	{
		switch(ev->data1)
		{
		case AM_MSGENTERED:
			st_gamestate = AutomapState;
			ST_ForceRefresh();
			break;

		case AM_MSGEXITED:
			st_gamestate = FirstPersonState;
			break;
		}
	}

	// if a user keypress...
	else if (ev->type == ev_keydown && ev->data3)
	{
		char key = ev->data3;

        // 'dqd' cheat for toggleable god mode
        if (cht_CheckCheat(&cheat_god, key))
        {
            if (CheckCheatmode ())
                return false;

            // [Russell] - give full health
            plyr->mo->health = deh.StartHealth;
            plyr->health = deh.StartHealth;

            AddCommandString("god");

            // Net_WriteByte (DEM_GENERICCHEAT);
            // Net_WriteByte (CHT_IDDQD);
            eatkey = true;
        }

        // 'fa' cheat for killer fucking arsenal
        else if (cht_CheckCheat(&cheat_ammonokey, key))
        {
            if (CheckCheatmode ())
                return false;

            Printf(PRINT_HIGH, "Ammo (No keys) Added\n");

            plyr->armorpoints = deh.FAArmor;
            plyr->armortype = deh.FAAC;

            weapontype_t pendweap = plyr->pendingweapon;
            for (i = 0; i<NUMWEAPONS; i++)
                P_GiveWeapon (plyr, (weapontype_t)i, false);
            plyr->pendingweapon = pendweap;

            for (i=0; i<NUMAMMO; i++)
                plyr->ammo[i] = plyr->maxammo[i];

            MSG_WriteMarker(&net_buffer, clc_cheatpulse);
            MSG_WriteByte(&net_buffer, 1);

            eatkey = true;
        }

        // 'kfa' cheat for key full ammo
        else if (cht_CheckCheat(&cheat_ammo, key))
        {
            if (CheckCheatmode ())
                return false;

            Printf(PRINT_HIGH, "Very Happy Ammo Added\n");

            plyr->armorpoints = deh.KFAArmor;
            plyr->armortype = deh.KFAAC;

            weapontype_t pendweap = plyr->pendingweapon;
            for (i = 0; i<NUMWEAPONS; i++)
                P_GiveWeapon (plyr, (weapontype_t)i, false);
            plyr->pendingweapon = pendweap;

            for (i=0; i<NUMAMMO; i++)
                plyr->ammo[i] = plyr->maxammo[i];

            for (i=0; i<NUMCARDS; i++)
                plyr->cards[i] = true;

            MSG_WriteMarker(&net_buffer, clc_cheatpulse);
            MSG_WriteByte(&net_buffer, 2);

            eatkey = true;
        }
        // [Russell] - Only doom 1/registered can have idspispopd and
        // doom 2/final can have idclip
        else if (cht_CheckCheat(&cheat_noclip, key))
        {
            if (CheckCheatmode ())
                return false;

            if (gamemode != shareware && gamemode != registered &&
                gamemode != retail && gamemode != retail_bfg)
                return false;

            AddCommandString("noclip");

            // Net_WriteByte (DEM_GENERICCHEAT);
            // Net_WriteByte (CHT_NOCLIP);
            eatkey = true;
        }
        else if (cht_CheckCheat(&cheat_commercial_noclip, key))
        {
            if (CheckCheatmode ())
                return false;

            if (gamemode != commercial && gamemode != commercial_bfg)
                return false;

            AddCommandString("noclip");

            // Net_WriteByte (DEM_GENERICCHEAT);
            // Net_WriteByte (CHT_NOCLIP);
            eatkey = true;
        }
        // 'behold?' power-up cheats
        for (i=0; i<6; i++)
        {
            if (cht_CheckCheat(&cheat_powerup[i], key))
            {
                if (CheckCheatmode ())
                    return false;

                Printf(PRINT_HIGH, "Power-up toggled\n");
                if (!plyr->powers[i])
                    P_GivePower( plyr, i);
                else if (i!=pw_strength)
                    plyr->powers[i] = 1;
                else
                    plyr->powers[i] = 0;

                MSG_WriteMarker(&net_buffer, clc_cheatpulse);
                MSG_WriteByte(&net_buffer, 3);
                MSG_WriteByte(&net_buffer, (byte)i);

                eatkey = true;
            }
        }

        // 'behold' power-up menu
        if (cht_CheckCheat(&cheat_powerup[6], key))
        {
            if (CheckCheatmode ())
                return false;

            Printf (PRINT_HIGH, "%s\n", GStrings(STSTR_BEHOLD));

        }

        // 'choppers' invulnerability & chainsaw
        else if (cht_CheckCheat(&cheat_choppers, key))
        {
            if (CheckCheatmode ())
                return false;

            Printf(PRINT_HIGH, "... Doesn't suck - GM\n");
            plyr->weaponowned[wp_chainsaw] = true;

            MSG_WriteMarker(&net_buffer, clc_cheatpulse);
            MSG_WriteByte(&net_buffer, 4);

            eatkey = true;
        }

        // 'clev' change-level cheat
        else if (cht_CheckCheat(&cheat_clev, key))
        {
            char buf[11];
			//char *bb;

            cht_GetParam(&cheat_clev, buf);
            buf[2] = 0;

			// [ML] Chex mode: always set the episode number to 1.
			// FIXME: This is probably a horrible hack, it sure looks like one at least
			if (gamemode == retail_chex)
				sprintf(buf,"1%c",buf[1]);

            sprintf (buf + 3, "map %.2s\n", buf);
            AddCommandString (buf + 3);
            eatkey = true;
        }

        // 'mypos' for player position
        else if (cht_CheckCheat(&cheat_mypos, key))
        {
            AddCommandString ("toggle idmypos");
            eatkey = true;
        }

        // 'idmus' change-music cheat
        else if (cht_CheckCheat(&cheat_mus, key))
        {
            char buf[16];

            cht_GetParam(&cheat_mus, buf);
            buf[2] = 0;

            sprintf (buf + 3, "idmus %.5s\n", buf);
            AddCommandString (buf + 3);
            eatkey = true;
        }
    }

    return eatkey;
}

// Console cheats
BEGIN_COMMAND (god)
{
	if (CheckCheatmode ())
		return;

	consoleplayer().cheats ^= CF_GODMODE;

	if (consoleplayer().cheats & CF_GODMODE)
		Printf(PRINT_HIGH, "Degreelessness mode on\n");
	else
		Printf(PRINT_HIGH, "Degreelessness mode off\n");

	MSG_WriteMarker(&net_buffer, clc_cheat);
	MSG_WriteByte(&net_buffer, consoleplayer().cheats);
}
END_COMMAND (god)

BEGIN_COMMAND (notarget)
{
	if (CheckCheatmode () || connected)
		return;

	consoleplayer().cheats ^= CF_NOTARGET;

	if (consoleplayer().cheats & CF_NOTARGET)
		Printf(PRINT_HIGH, "Notarget on\n");
	else
		Printf(PRINT_HIGH, "Notarget off\n");

	MSG_WriteMarker(&net_buffer, clc_cheat);
	MSG_WriteByte(&net_buffer, consoleplayer().cheats);
}
END_COMMAND (notarget)

BEGIN_COMMAND (fly)
{
	if (!consoleplayer().spectator && CheckCheatmode ())
		return;

	consoleplayer().cheats ^= CF_FLY;

	if (consoleplayer().cheats & CF_FLY)
		Printf(PRINT_HIGH, "Fly mode on\n");
	else
		Printf(PRINT_HIGH, "Fly mode off\n");

	if (!consoleplayer().spectator)
	{
		MSG_WriteMarker(&net_buffer, clc_cheat);
		MSG_WriteByte(&net_buffer, consoleplayer().cheats);
	}
}
END_COMMAND (fly)

BEGIN_COMMAND (noclip)
{
	if (CheckCheatmode ())
		return;

	consoleplayer().cheats ^= CF_NOCLIP;

	if (consoleplayer().cheats & CF_NOCLIP)
		Printf(PRINT_HIGH, "No clipping mode on\n");
	else
		Printf(PRINT_HIGH, "No clipping mode off\n");

	MSG_WriteMarker(&net_buffer, clc_cheat);
	MSG_WriteByte(&net_buffer, consoleplayer().cheats);
}
END_COMMAND (noclip)

EXTERN_CVAR (chasedemo)

BEGIN_COMMAND (chase)
{
	if (demoplayback)
	{
		if (chasedemo)
		{
			chasedemo.Set (0.0f);
			for (Players::iterator it = players.begin();it != players.end();++it)
				it->cheats &= ~CF_CHASECAM;
		}
		else
		{
			chasedemo.Set (1.0f);
			for (Players::iterator it = players.begin();it != players.end();++it)
				it->cheats |= CF_CHASECAM;
		}
	}
	else
	{
		if (CheckCheatmode ())
			return;

		consoleplayer().cheats ^= CF_CHASECAM;

		MSG_WriteMarker(&net_buffer, clc_cheat);
		MSG_WriteByte(&net_buffer, consoleplayer().cheats);
	}
}
END_COMMAND (chase)

BEGIN_COMMAND (idmus)
{
	LevelInfos& levels = getLevelInfos();
	char *map;
	int l;

	if (argc > 1)
	{
		if (gameinfo.flags & GI_MAPxx)
		{
			l = atoi (argv[1]);
			if (l <= 99)
				map = CalcMapName (0, l);
			else
			{
				Printf (PRINT_HIGH, "%s\n", GStrings(STSTR_NOMUS));
				return;
			}
		}
		else
		{
			map = CalcMapName (argv[1][0] - '0', argv[1][1] - '0');
		}

		level_pwad_info_t& info = levels.findByName(map);
		if (level.levelnum != 0)
		{
			if (info.music[0])
			{
				S_ChangeMusic(std::string(info.music, 8), 1);
				Printf (PRINT_HIGH, "%s\n", GStrings(STSTR_MUS));
			}
		}
		else
		{
			Printf(PRINT_HIGH, "%s\n", GStrings(STSTR_NOMUS));
		}
	}
}
END_COMMAND (idmus)

BEGIN_COMMAND (give)
{
	if (CheckCheatmode ())
		return;

	if (argc < 2)
		return;

	std::string name = C_ArgCombine(argc - 1, (const char **)(argv + 1));
	if (name.length())
	{
		//Net_WriteByte (DEM_GIVECHEAT);
		//Net_WriteString (name.c_str());
		// todo
	}
}
END_COMMAND (give)

BEGIN_COMMAND (fov)
{
	if (CheckCheatmode () || !m_Instigator)
		return;

	if (argc != 2)
		Printf(PRINT_HIGH, "fov is %g\n", m_Instigator->player->fov);
	else
	{
		m_Instigator->player->fov = clamp((float)atof(argv[1]), 45.0f, 135.0f);
		R_ForceViewWindowResize();
	}
}
END_COMMAND (fov)


int ST_calcPainOffset(void)
{
	int 		health;
	static int	lastcalc;
	static int	oldhealth = -1;

	health = displayplayer().health;

	if(health < -1)
		health = -1;
	else if(health > 100)
		health = 100;

	if (health != oldhealth)
	{
		lastcalc = ST_FACESTRIDE * (((100 - health) * ST_NUMPAINFACES) / 101);
		oldhealth = health;
	}

	return lastcalc;
}


//
// This is a not-very-pretty routine which handles
//	the face states and their timing.
// the precedence of expressions is:
//	dead > evil grin > turned head > straight ahead
//
void ST_updateFaceWidget(void)
{
	int 		i;
	angle_t 	badguyangle;
	angle_t 	diffang;
	static int	lastattackdown = -1;
	static int	priority = 0;
	BOOL	 	doevilgrin;

	player_t *plyr = &displayplayer();

	if (priority < 10)
	{
		// dead
		if (!plyr->health)
		{
			priority = 9;
			st_faceindex = ST_DEADFACE;
			st_facecount = 1;
		}
	}

	if (priority < 9)
	{
		if (plyr->bonuscount)
		{
			// picking up bonus
			doevilgrin = false;

			for (i=0;i<NUMWEAPONS;i++)
			{
				if (oldweaponsowned[i] != plyr->weaponowned[i])
				{
					doevilgrin = true;
					oldweaponsowned[i] = plyr->weaponowned[i];
				}
			}
			if (doevilgrin)
			{
				// evil grin if just picked up weapon
				priority = 8;
				st_facecount = ST_EVILGRINCOUNT;
				st_faceindex = ST_calcPainOffset() + ST_EVILGRINOFFSET;
			}
		}

	}

	if (priority < 8)
	{
		if (plyr->damagecount
			&& plyr->attacker
			&& plyr->attacker != plyr->mo)
		{
			// being attacked
			priority = 7;

			if (st_oldhealth - plyr->health > ST_MUCHPAIN)
			{
				st_facecount = ST_TURNCOUNT;
				st_faceindex = ST_calcPainOffset() + ST_OUCHOFFSET;
			}
			else
			{
				badguyangle = R_PointToAngle2(plyr->mo->x,
											  plyr->mo->y,
											  plyr->attacker->x,
											  plyr->attacker->y);

				if (badguyangle > plyr->mo->angle)
				{
					// whether right or left
					diffang = badguyangle - plyr->mo->angle;
					i = diffang > ANG180;
				}
				else
				{
					// whether left or right
					diffang = plyr->mo->angle - badguyangle;
					i = diffang <= ANG180;
				} // confusing, aint it?


				st_facecount = ST_TURNCOUNT;
				st_faceindex = ST_calcPainOffset();

				if (diffang < ANG45)
				{
					// head-on
					st_faceindex += ST_RAMPAGEOFFSET;
				}
				else if (i)
				{
					// turn face right
					st_faceindex += ST_TURNOFFSET;
				}
				else
				{
					// turn face left
					st_faceindex += ST_TURNOFFSET+1;
				}
			}
		}
	}

	if (priority < 7)
	{
		// getting hurt because of your own damn stupidity
		if (plyr->damagecount)
		{
			if (st_oldhealth - plyr->health > ST_MUCHPAIN)
			{
				priority = 7;
				st_facecount = ST_TURNCOUNT;
				st_faceindex = ST_calcPainOffset() + ST_OUCHOFFSET;
			}
			else
			{
				priority = 6;
				st_facecount = ST_TURNCOUNT;
				st_faceindex = ST_calcPainOffset() + ST_RAMPAGEOFFSET;
			}

		}

	}

	if (priority < 6)
	{
		// rapid firing
		if (plyr->attackdown)
		{
			if (lastattackdown==-1)
				lastattackdown = ST_RAMPAGEDELAY;
			else if (!--lastattackdown)
			{
				priority = 5;
				st_faceindex = ST_calcPainOffset() + ST_RAMPAGEOFFSET;
				st_facecount = 1;
				lastattackdown = 1;
			}
		}
		else
			lastattackdown = -1;

	}

	if (priority < 5)
	{
		// invulnerability
		if ((plyr->cheats & CF_GODMODE)
			|| plyr->powers[pw_invulnerability])
		{
			priority = 4;

			st_faceindex = ST_GODFACE;
			st_facecount = 1;

		}
	}

	// look left or look right if the facecount has timed out
	if (!st_facecount)
	{
		st_faceindex = ST_calcPainOffset() + (st_randomnumber % 3);
		st_facecount = ST_STRAIGHTFACECOUNT;
		priority = 0;
	}

	st_facecount--;
}

void ST_updateWidgets(void)
{
	player_t *plyr = &displayplayer();

	if (weaponinfo[plyr->readyweapon].ammotype == am_noammo)
		st_current_ammo = ST_DONT_DRAW_NUM;
	else
		st_current_ammo = plyr->ammo[weaponinfo[plyr->readyweapon].ammotype];

	w_ready.data = plyr->readyweapon;

	st_health = plyr->health;
	st_armor = plyr->armorpoints;

	for (int i = 0; i < 4; i++)
	{
		st_ammo[i] = plyr->ammo[i];
		st_maxammo[i] = plyr->maxammo[i];
	}

	for (int i = 0; i < 6; i++)
	{
		// denis - longwinded so compiler optimization doesn't skip it (fault in my gcc?)
		if(plyr->weaponowned[i+1])
			st_weaponowned[i] = 1;
		else
			st_weaponowned[i] = 0;
	}

	// update keycard multiple widgets
	for (int i = 0; i < 3; i++)
	{
		keyboxes[i] = plyr->cards[i] ? i : -1;

		// [RH] show multiple keys per box, too
		if (plyr->cards[i+3])
			keyboxes[i] = (keyboxes[i] == -1) ? i+3 : i+6;
	}

	// refresh everything if this is him coming back to life
	ST_updateFaceWidget();

	// used by w_arms[] widgets
	st_armson = st_statusbaron && sv_gametype == GM_COOP;

	// used by w_frags widget
	st_fragson = sv_gametype != GM_COOP && st_statusbaron;

	//	[Toke - CTF]
	if (sv_gametype == GM_CTF)
		st_fragscount = GetTeamInfo(plyr->userinfo.team)->Points; // denis - todo - scoring for ctf
	else
		st_fragscount = plyr->fragcount;	// [RH] Just use cumulative total

	// get rid of chat window if up because of message
	if (!--st_msgcounter)
		st_chat = st_oldchat;

}

void ST_Ticker()
{
	if (!multiplayer && !demoplayback && (ConsoleState == c_down || ConsoleState == c_falling))
		return;
	st_randomnumber = M_Random();
	ST_updateWidgets();
	st_oldhealth = displayplayer().health;
}


void ST_drawWidgets(bool force_refresh)
{
	// used by w_arms[] widgets
	st_armson = st_statusbaron && sv_gametype == GM_COOP;

	// used by w_frags widget
	st_fragson = sv_gametype != GM_COOP && st_statusbaron;

	STlib_updateNum(&w_ready, force_refresh);

	for (int i = 0; i < 4; i++)
	{
		STlib_updateNum(&w_ammo[i], force_refresh);
		STlib_updateNum(&w_maxammo[i], force_refresh);
	}

	STlib_updatePercent(&w_health, force_refresh);
	STlib_updatePercent(&w_armor, force_refresh);

	for (int i = 0; i < 6; i++)
		STlib_updateMultIcon(&w_arms[i], force_refresh);

	STlib_updateMultIcon(&w_faces, force_refresh);

	for (int i = 0; i < 3; i++)
		STlib_updateMultIcon(&w_keyboxes[i], force_refresh);

	STlib_updateNum(&w_frags, force_refresh);
}


//
// ST_refreshBackground
//
// Draws the status bar background to an off-screen 320x32 buffer.
//
static void ST_refreshBackground()
{
	IWindowSurface* surface = R_GetRenderingSurface();
	int surface_width = surface->getWidth(), surface_height = surface->getHeight();

	// [RH] If screen is wider than the status bar, draw stuff around status bar.
	if (surface_width > ST_WIDTH)
	{
		R_DrawBorder(0, ST_Y, ST_X, surface_height);
		R_DrawBorder(surface_width - ST_X, ST_Y, surface_width, surface_height);
	}

	stbar_surface->lock();

	DCanvas* stbar_canvas = stbar_surface->getDefaultCanvas();
	stbar_canvas->DrawPatch(sbar, 0, 0);

	if (sv_gametype == GM_CTF)
	{
		stbar_canvas->DrawPatch(flagsbg, ST_FLAGSBGX, ST_FLAGSBGY);
	}
	else if (sv_gametype == GM_COOP)
	{
		stbar_canvas->DrawPatch(armsbg, ST_ARMSBGX, ST_ARMSBGY);
	}

	if (multiplayer)
	{
		if (!demoplayback)
		{
			// [RH] Always draw faceback with the player's color
			//		using a translation rather than a different patch.
			V_ColorMap = translationref_t(translationtables + displayplayer_id * 256, displayplayer_id);
			stbar_canvas->DrawTranslatedPatch(faceback, ST_FX, ST_FY);
		}
		else
		{
			stbar_canvas->DrawPatch(faceclassic[displayplayer_id - 1], ST_FX, ST_FY);
		}
	}

	stbar_surface->unlock();
}


//
// ST_Drawer
//
// If st_scale is disabled, the status bar is drawn directly to the rendering
// surface, with stbar_surface (the status bar background) being blitted to the
// rendering surface without scaling and then all of the widgets being drawn
// on top of it.
//
// If st_scale is enabled, the status bar is drawn to an unscaled 320x32 pixel
// off-screen surface stnum_surface. First stbar_surface (the status bar
// background) is blitted to stnum_surface, then the widgets are then drawn
// on top of it. Finally, stnum_surface is blitted onto the rendering surface
// using scaling to match the size in 320x200 resolution.
//
// Now ST_Drawer recalculates the ST_WIDTH, ST_HEIGHT, ST_X, and ST_Y globals.
//
void ST_Drawer()
{
	if (st_needrefresh)
		st_statusbaron = R_StatusBarVisible();

	if (st_statusbaron)
	{
		IWindowSurface* surface = R_GetRenderingSurface();
		int surface_width = surface->getWidth(), surface_height = surface->getHeight();

		ST_WIDTH = ST_StatusBarWidth(surface_width, surface_height);
		ST_HEIGHT = ST_StatusBarHeight(surface_width, surface_height);

		ST_X = ST_StatusBarX(surface_width, surface_height);
		ST_Y = ST_StatusBarY(surface_width, surface_height);

		stbar_surface->lock();
		stnum_surface->lock();

		if (st_needrefresh)
		{
			// draw status bar background to off-screen buffer then blit to surface
			ST_refreshBackground();

			if (st_scale)
				stnum_surface->blit(stbar_surface, 0, 0, stbar_surface->getWidth(), stbar_surface->getHeight(),
						0, 0, stnum_surface->getWidth(), stnum_surface->getHeight());
			else
				surface->blit(stbar_surface, 0, 0, stbar_surface->getWidth(), stbar_surface->getHeight(),
						ST_X, ST_Y, ST_WIDTH, ST_HEIGHT);
		}
		
		// refresh all widgets
		ST_drawWidgets(st_needrefresh);

		if (st_scale)
			surface->blit(stnum_surface, 0, 0, stnum_surface->getWidth(), stnum_surface->getHeight(),
					ST_X, ST_Y, ST_WIDTH, ST_HEIGHT);	

		stbar_surface->unlock();
		stnum_surface->unlock();

		st_needrefresh = false;
	}
}


static patch_t *LoadFaceGraphic (char *name, int namespc)
{
	char othername[9];
	int lump;

	lump = W_CheckNumForName (name, namespc);
	if (lump == -1)
	{
		strcpy (othername, name);
		othername[0] = 'S'; othername[1] = 'T'; othername[2] = 'F';
		lump = W_GetNumForName (othername);
	}
	return W_CachePatch (lump, PU_STATIC);
}

static void ST_loadGraphics()
{
	int i, j;
	int namespc;
	int facenum;
	char namebuf[9];

	namebuf[8] = 0;

	// Load the numbers, tall and short
	for (i=0;i<10;i++)
	{
		sprintf(namebuf, "STTNUM%d", i);
		tallnum[i] = W_CachePatch(namebuf, PU_STATIC);

		sprintf(namebuf, "STYSNUM%d", i);
		shortnum[i] = W_CachePatch(namebuf, PU_STATIC);
	}

	// Load percent key.
	//Note: why not load STMINUS here, too?
	tallpercent = W_CachePatch("STTPRCNT", PU_STATIC);

	// key cards
	for (i=0;i<NUMCARDS+NUMCARDS/2;i++)
	{
		sprintf(namebuf, "STKEYS%d", i);
		keys[i] = W_CachePatch(namebuf, PU_STATIC);
	}

	// arms background
	armsbg = W_CachePatch("STARMS", PU_STATIC);

	// flags background
	flagsbg = W_CachePatch("STFLAGS", PU_STATIC);

	// arms ownership widgets
	for (i=0;i<6;i++)
	{
		sprintf(namebuf, "STGNUM%d", i+2);

		// gray #
		arms[i][0] = W_CachePatch(namebuf, PU_STATIC);

		// yellow #
		arms[i][1] = shortnum[i+2];
	}

	// face backgrounds for different color players
	// [RH] only one face background used for all players
	//		different colors are accomplished with translations
	faceback = W_CachePatch("STFBANY", PU_STATIC);

	// [Nes] Classic vanilla lifebars.
	for (i = 0; i < 4; i++) {
		sprintf(namebuf, "STFB%d", i);
		faceclassic[i] = W_CachePatch(namebuf, PU_STATIC);
	}

	// status bar background bits
	sbar = W_CachePatch("STBAR", PU_STATIC);

	// face states
	facenum = 0;

	namebuf[0] = 'S'; namebuf[1] = 'T'; namebuf[2] = 'F';
	namespc = ns_global;

	for (i = 0; i < ST_NUMPAINFACES; i++)
	{
		for (j = 0; j < ST_NUMSTRAIGHTFACES; j++)
		{
			sprintf(namebuf+3, "ST%d%d", i, j);
			faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
		}
		sprintf(namebuf+3, "TR%d0", i);		// turn right
		faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
		sprintf(namebuf+3, "TL%d0", i);		// turn left
		faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
		sprintf(namebuf+3, "OUCH%d", i);		// ouch!
		faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
		sprintf(namebuf+3, "EVL%d", i);		// evil grin ;)
		faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
		sprintf(namebuf+3, "KILL%d", i);		// pissed off
		faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
	}
	strcpy (namebuf+3, "GOD0");
	faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
	strcpy (namebuf+3, "DEAD0");
	faces[facenum++] = LoadFaceGraphic (namebuf, namespc);
}

static void ST_loadData()
{
    lu_palette = W_GetNumForName("PLAYPAL");
	ST_loadGraphics();
}

static void ST_unloadGraphics()
{

	int i;

	// unload the numbers, tall and short
	for (i=0;i<10;i++)
	{
		Z_ChangeTag(tallnum[i], PU_CACHE);
		Z_ChangeTag(shortnum[i], PU_CACHE);
	}
	// unload tall percent
	Z_ChangeTag(tallpercent, PU_CACHE);

	// unload arms background
	Z_ChangeTag(armsbg, PU_CACHE);

	// unload flags background
	Z_ChangeTag(flagsbg, PU_CACHE);

	// unload gray #'s
	for (i=0;i<6;i++)
		Z_ChangeTag(arms[i][0], PU_CACHE);

	// unload the key cards
	for (i=0;i<NUMCARDS+NUMCARDS/2;i++)
		Z_ChangeTag(keys[i], PU_CACHE);

	Z_ChangeTag(sbar, PU_CACHE);
	Z_ChangeTag(faceback, PU_CACHE);

	for (i=0;i<ST_NUMFACES;i++)
		Z_ChangeTag(faces[i], PU_CACHE);

	// Note: nobody ain't seen no unloading
	//	 of stminus yet. Dude.


}

static void ST_unloadData()
{
	ST_unloadGraphics();
	ST_unloadNew();
}


void ST_createWidgets(void)
{
	// ready weapon ammo
	STlib_initNum(&w_ready,
				  ST_AMMOX,
				  ST_AMMOY,
				  tallnum,
				  &st_current_ammo,
				  &st_statusbaron,
				  ST_AMMOWIDTH );

	// health percentage
	STlib_initPercent(&w_health,
					  ST_HEALTHX,
					  ST_HEALTHY,
					  tallnum,
					  &st_health,
					  &st_statusbaron,
					  tallpercent);

	// weapons owned
	for (int i = 0 ; i < 6; i++)
	{
		STlib_initMultIcon(&w_arms[i],
						   ST_ARMSX+(i%3)*ST_ARMSXSPACE,
						   ST_ARMSY+(i/3)*ST_ARMSYSPACE,
						   arms[i],
						   &st_weaponowned[i],
						   &st_armson);
	}

	// frags sum
	STlib_initNum(&w_frags,
				  ST_FRAGSX,
				  ST_FRAGSY,
				  tallnum,
				  &st_fragscount,
				  &st_fragson,
				  ST_FRAGSWIDTH);

	// faces
	STlib_initMultIcon(&w_faces,
					   ST_FACESX,
					   ST_FACESY,
					   faces,
					   &st_faceindex,
					   &st_statusbaron);

	// armor percentage - should be colored later
	STlib_initPercent(&w_armor,
					  ST_ARMORX,
					  ST_ARMORY,
					  tallnum,
					  &st_armor,
					  &st_statusbaron, tallpercent);

	// keyboxes 0-2
	STlib_initMultIcon(&w_keyboxes[0],
					   ST_KEY0X,
					   ST_KEY0Y,
					   keys,
					   &keyboxes[0],
					   &st_statusbaron);

	STlib_initMultIcon(&w_keyboxes[1],
					   ST_KEY1X,
					   ST_KEY1Y,
					   keys,
					   &keyboxes[1],
					   &st_statusbaron);

	STlib_initMultIcon(&w_keyboxes[2],
					   ST_KEY2X,
					   ST_KEY2Y,
					   keys,
					   &keyboxes[2],
					   &st_statusbaron);

	// ammo count (all four kinds)
	STlib_initNum(&w_ammo[0],
				  ST_AMMO0X,
				  ST_AMMO0Y,
				  shortnum,
				  &st_ammo[0],
				  &st_statusbaron,
				  ST_AMMO0WIDTH);

	STlib_initNum(&w_ammo[1],
				  ST_AMMO1X,
				  ST_AMMO1Y,
				  shortnum,
				  &st_ammo[1],
				  &st_statusbaron,
				  ST_AMMO1WIDTH);

	STlib_initNum(&w_ammo[2],
				  ST_AMMO2X,
				  ST_AMMO2Y,
				  shortnum,
				  &st_ammo[2],
				  &st_statusbaron,
				  ST_AMMO2WIDTH);

	STlib_initNum(&w_ammo[3],
				  ST_AMMO3X,
				  ST_AMMO3Y,
				  shortnum,
				  &st_ammo[3],
				  &st_statusbaron,
				  ST_AMMO3WIDTH);

	// max ammo count (all four kinds)
	STlib_initNum(&w_maxammo[0],
				  ST_MAXAMMO0X,
				  ST_MAXAMMO0Y,
				  shortnum,
				  &st_maxammo[0],
				  &st_statusbaron,
				  ST_MAXAMMO0WIDTH);

	STlib_initNum(&w_maxammo[1],
				  ST_MAXAMMO1X,
				  ST_MAXAMMO1Y,
				  shortnum,
				  &st_maxammo[1],
				  &st_statusbaron,
				  ST_MAXAMMO1WIDTH);

	STlib_initNum(&w_maxammo[2],
				  ST_MAXAMMO2X,
				  ST_MAXAMMO2Y,
				  shortnum,
				  &st_maxammo[2],
				  &st_statusbaron,
				  ST_MAXAMMO2WIDTH);

	STlib_initNum(&w_maxammo[3],
				  ST_MAXAMMO3X,
				  ST_MAXAMMO3Y,
				  shortnum,
				  &st_maxammo[3],
				  &st_statusbaron,
				  ST_MAXAMMO3WIDTH);

}

void ST_Start()
{
	if (!st_stopped)
		ST_Stop();

	ST_ForceRefresh();

	st_gamestate = FirstPersonState;

	st_statusbaron = true;
	st_oldchat = st_chat = false;
	st_cursoron = false;

	st_faceindex = 0;

	st_oldhealth = -1;

	for (int i = 0; i < NUMWEAPONS; i++)
		oldweaponsowned[i] = displayplayer().weaponowned[i];

	for (int i = 0; i < 3; i++)
		keyboxes[i] = -1;

	STlib_init();
	ST_initNew();

	ST_createWidgets();
	st_stopped = false;
}

void ST_Stop()
{
	if (st_stopped)
		return;

	st_stopped = true;
}

void ST_Init()
{
	if (stbar_surface == NULL)
		stbar_surface = I_AllocateSurface(320, 32, 8);
	if (stnum_surface == NULL)
		stnum_surface = I_AllocateSurface(320, 32, 8);

	ST_loadData();
}


void STACK_ARGS ST_Shutdown()
{
	ST_unloadData();

	I_FreeSurface(stbar_surface);
	I_FreeSurface(stnum_surface);
}


VERSION_CONTROL (st_stuff_cpp, "$Id$")
