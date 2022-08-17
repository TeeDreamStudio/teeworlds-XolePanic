/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONFIG_H
#define GAME_SERVER_GAMECONFIG_H
#undef GAME_SERVER_GAMECONFIG_H // this file will be included several times

// Server
MACRO_CONFIG_INT(XoleMapWindow, xole_map_window, 15, 0, 100, CFGFLAG_SERVER, "Map downloading send-ahead window")
MACRO_CONFIG_INT(XoleConWaitingTime, xole_con_waiting_time, 500, 0, 2000, CFGFLAG_SERVER, "Number of ms to wait before enter the game")

MACRO_CONFIG_INT(MapFastDownload, map_fast_download, 1, 0, 1, CFGFLAG_SERVER, "Enables fast download of maps")
MACRO_CONFIG_STR(SvMaprotation, sv_maprotation, 768, "xp_city xp_panic", CFGFLAG_SERVER, "Maps to rotate between")
// Game
MACRO_CONFIG_INT(XoleInfectStartSec, xole_infect_start_sec, 5, 5, 30, CFGFLAG_SERVER, "Infection start second")

MACRO_CONFIG_INT(XoleWillDieSec, xole_will_die_sec, 3, 3, 5, CFGFLAG_SERVER, "will die second")
MACRO_CONFIG_INT(XoleReviveCDSec, xole_revive_cd_sec, 10, 3, 5, CFGFLAG_SERVER, "revive CD")
MACRO_CONFIG_INT(XoleRebuildSec, xole_rebuild_sec, 1, 1, 20, CFGFLAG_SERVER, "Rebuild second")

MACRO_CONFIG_INT(XoleSmokerHookDamage, xole_smoker_hook_dmg, 3, 3, 20, CFGFLAG_SERVER, "Smoker hook damage")
MACRO_CONFIG_INT(XoleSniperInvisibleSec, xole_sniper_invisible_sec, 3, 1, 20, CFGFLAG_SERVER, "Sniper invisible second")
MACRO_CONFIG_INT(XoleSniperReInvisibleSec, xole_sniper_reinvisible_sec, 5, 1, 20, CFGFLAG_SERVER, "Sniper reinvisible second")

MACRO_CONFIG_INT(XoleWallMaxHP, xole_wall_max_hp, 500, 50, 1000, CFGFLAG_SERVER, "Wall max health")

#endif