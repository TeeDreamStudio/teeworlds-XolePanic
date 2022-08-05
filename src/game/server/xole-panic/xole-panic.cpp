/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include <game/server/role.h>

#include "xole-panic.h"

CGameControllerXole::CGameControllerXole(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	// Exchange this to a string that identifies your game mode.
	// DM, TDM and CTF are reserved for teeworlds original modes.
	m_pGameType = "XolePanic";
	m_RoundStartTick = 0;
	m_FristInfectNum = 0;
}

void CGameControllerXole::Tick()
{
	if(GameServer()->m_NumPlayers < 2)
	{
		m_RoundStartTick = Server()->Tick();
	}
	DoWincheck();
	IGameController::Tick();
	return;
}

void CGameControllerXole::DoWincheck()
{
	if(m_GameOverTick == -1 && !GameServer()->m_World.m_ResetRequested)
	{
		// check score win condition
		if((Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60)
		{
			EndRound();
			GameServer()->SendChatTarget(-1, _("{int:Num} Humans win this round."), "Num", &GameServer()->m_NumHumans, NULL);
			GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
		}else if(GameServer()->m_SafeZoneTick >= 5 * Server()->TickSpeed())
		{
			int Second = (Server()->Tick()-m_RoundStartTick) / 50;
			EndRound();
			GameServer()->SendChatTarget(-1, _("Zombies win this round in {sec:Second}."), "Second", &Second, NULL);
			GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
		}

		if(IsInfectionStarted())
		{
			bool IsDoInfection =  (Server()->Tick() - m_RoundStartTick) == (Server()->TickSpeed() * g_Config.m_XoleInfectStartSec);
		
			if(IsDoInfection)
			{
				GetFristInfectNum();
				DoFairInfection();
				DoUnfairInfection();
			}
		}
	}
}

void CGameControllerXole::StartRound()
{
	GameServer()->CountPlayer();
	IGameController::StartRound();
}

void CGameControllerXole::OnCharacterSpawn(class CCharacter *pChr)
{
	GameServer()->SendRoleChooser(pChr->GetPlayer()->GetCID());
	// default health
	pChr->IncreaseHealth(10);
	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
}

void CGameControllerXole::GetFristInfectNum()
{
	GameServer()->CountPlayer();
	int NumPlayers = GameServer()->m_NumPlayers;
	if(NumPlayers < 4)
	{
		m_FristInfectNum = 1;
	}else if(NumPlayers < 8)
	{
		m_FristInfectNum = 2;
	}else if(NumPlayers < 12)
	{
		m_FristInfectNum = 3;
	}else if(NumPlayers < 16)
	{
		m_FristInfectNum = 4;
	}else if(NumPlayers < 24)
	{
		m_FristInfectNum = 5;
	}else if(NumPlayers < 32)
	{
		m_FristInfectNum = 6;
	}else m_FristInfectNum = 7;
}

void CGameControllerXole::DoFairInfection()
{
	std::vector<int> FairInfVector;

	//initiate infection vector when player is human and was no infected before
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		//note: spectators are already zombies

		//do not infect zombies
		if(Iter.Player()->IsZombie()) continue;

		//do not infect clients in two rounds consecutively
		if(Server()->IsClientInfectedBefore(Iter.ClientID())) continue;

		FairInfVector.push_back(Iter.ClientID());
	}

	// fair infection process,
	while( FairInfVector.size() > 0 && GameServer()->m_NumHumans > 1 && GameServer()->m_NumZombies < m_FristInfectNum)
	{
		//dbg_msg("Game", "#FairToInfect: %d", FairInfVector.size());

		//generate random number
		int random = random_int(0, FairInfVector.size()-1);

		//do not infect client consecutively in two rounds
		Server()->InfectClient(FairInfVector[random]);

		//infect player behind clientid taken from vector
		GameServer()->m_apPlayers[FairInfVector[random]]->StartInfection();

		//notification to other players
		GameServer()->SendChatTarget(-1, _("{str:VictimName} has been infected"),
		                                          "VictimName", Server()->ClientName(FairInfVector[random]),
		                                          NULL
		                                          );

		//remove infected vector element
		FairInfVector.erase(FairInfVector.begin() + random);
	}
}

void CGameControllerXole::DoUnfairInfection()
{
	std::vector<int> UnfairInfVector;

	//initiate infection vector when player is human and was no infected before
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		//note: spectators are already zombies

		//do not infect zombies
		if(Iter.Player()->IsZombie()) continue;

		UnfairInfVector.push_back(Iter.ClientID());
	}

	// Unfair infection process
	while( UnfairInfVector.size() > 0 && GameServer()->m_NumHumans > 1 && GameServer()->m_NumZombies < m_FristInfectNum)
	{
		//dbg_msg("Game", "#NotFairToInfect: %d", UnfairInfVector.size());

		//generate random number
		int random = random_int(0, UnfairInfVector.size() - 1);

		//infect player behind clientid taken from vector
		GameServer()->m_apPlayers[UnfairInfVector[random]]->StartInfection();

		//notification to other players
		GameServer()->SendChatTarget(-1, _("{str:VictimName} has been infected"),
		                                          "VictimName", Server()->ClientName(UnfairInfVector[random]),
		                                          NULL
		                                          );

		//remove infected vector element
		UnfairInfVector.erase(UnfairInfVector.begin() + random);
	}

	//Reset not infected players of the UnfairInfVector
	//for next round, next round they can be fairly infected again
	for(std::vector<int>::iterator it = UnfairInfVector.begin(); it != UnfairInfVector.end(); ++it)
	{
		Server()->UnInfectClient(*it);
	}
}