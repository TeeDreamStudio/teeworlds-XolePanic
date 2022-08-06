/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONFIG_H
#define GAME_SERVER_GAMECONFIG_H
#undef GAME_SERVER_GAMECONFIG_H // this file will be included several times


MACRO_CONFIG_STR(SvMaprotation, sv_maprotation, 768, "xp_city xp_panic", CFGFLAG_SERVER, "Maps to rotate between")
MACRO_CONFIG_INT(XoleInfectStartSec, xole_infect_start_sec, 5, 5, 30, CFGFLAG_SERVER, "Infection start second")
MACRO_CONFIG_INT(XoleWillDieSec, xole_will_die_sec, 3, 3, 5, CFGFLAG_SERVER, "will die second")
MACRO_CONFIG_INT(XoleReviverCDSec, xole_revive_cd_sec, 10, 3, 5, CFGFLAG_SERVER, "revive CD")

//Role
MACRO_CONFIG_INT(XoleSmokerHookDamage, xole_smoker_hook_dmg, 3, 3, 20, CFGFLAG_SERVER, "Smoker hook damage")

#endif