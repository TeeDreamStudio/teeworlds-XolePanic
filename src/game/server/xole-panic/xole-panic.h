/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_XOLE_PANIC_H
#define GAME_SERVER_GAMEMODES_XOLE_PANIC_H
#include <game/server/gamecontroller.h>

// you can subclass GAMECONTROLLER_CTF, GAMECONTROLLER_TDM etc if you want
// todo a modification with their base as well.
class CGameControllerXole : public IGameController
{
public:
	CGameControllerXole(class CGameContext *pGameServer);
	virtual bool PreSpawn(CPlayer* pPlayer, vec2 *pPos);
	virtual void Tick();
	virtual void DoWincheck();
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	void GetFristInfectNum();
	void DoFairInfection();
	void DoUnfairInfection();
	int m_FristInfectNum;
private:
	bool IsSpawnable(vec2 Pos);
};
#endif
