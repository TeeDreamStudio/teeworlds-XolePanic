/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"
#include "building.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;
	m_AirJumpCounter = 0;
	m_WillDieTick = 0;
	m_WillDieKiller = -1;
	m_WillDieWeapon = -1;
	m_LastReviveTick = 0;
	m_HookDmgTick = 0;
	m_HelpTick = 0;
	m_WillDie = false;
	m_InInfectZone = false;
	m_InvisibleTick = 0;
	m_VisibleTick = 0;
	m_LastBuildTick = 0;
	m_IsInvisible = false;

	m_pPlayer = pPlayer;
	m_Pos = Pos;
	m_OldPos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	GiveRoleWeapon();

	return true;
}

void CCharacter::Destroy()
{
	DestroyChrEntity();
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamage(vec2(0, -10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	if(m_WillDie)
	{
		return;
	}
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(m_pPlayer->GetSwitchRoleState())
	{
		int Start = (IsZombie() ? START_ZOMBIEROLE : START_HUMANROLE ) + 1;
		int End = (IsZombie() ? END_ZOMBIEROLE : END_HUMANROLE ) - 1;
		if(Prev && Prev < 128)
		{
			if(GetRole() == Start)
			{
				m_pPlayer->SetRole(End);
				GameServer()->SendRoleChooser(m_pPlayer->GetCID());
				GameServer()->CreateSoundGlobal(SOUND_WEAPON_SWITCH, m_pPlayer->GetCID());
			}
			else
			{
				m_pPlayer->SetRole(GetRole()-1);
				GameServer()->SendRoleChooser(m_pPlayer->GetCID());
				GameServer()->CreateSoundGlobal(SOUND_WEAPON_SWITCH, m_pPlayer->GetCID());
			}
		}else if(Next && Next < 128)
		{
			if(GetRole() == End)
			{
				m_pPlayer->SetRole(Start);
				GameServer()->SendRoleChooser(m_pPlayer->GetCID());
				GameServer()->CreateSoundGlobal(SOUND_WEAPON_SWITCH, m_pPlayer->GetCID());
			}
			else
			{
				m_pPlayer->SetRole(GetRole()+1);
				GameServer()->SendRoleChooser(m_pPlayer->GetCID());
				GameServer()->CreateSoundGlobal(SOUND_WEAPON_SWITCH, m_pPlayer->GetCID());
			}
		}
		return;
	}
	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_WillDie)
	{
		return;
	}

	if(m_ReloadTimer != 0)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;


	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			if(GetRole() == PLAYERROLE_SNIPER)
			{
				if(Server()->Tick() - m_VisibleTick >= g_Config.m_XoleSniperReInvisibleSec * 50)
				{
					m_IsInvisible = !m_IsInvisible;
					if(m_IsInvisible)
					{
						m_InvisibleTick = g_Config.m_XoleSniperInvisibleSec * 50;
						int Time = m_InvisibleTick/50;
						GameServer()->SendBroadcast_VL(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,_("You are invisible: {sec:Time}"), "Time", &Time, NULL);
					}else 
					{
						m_InvisibleTick = 0;
						m_VisibleTick = Server()->Tick();
						GameServer()->ClearBroadcast(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE);
					}
				}
				GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);
			}
			else if(GetRole() == PLAYERROLE_BUILDER && !m_HasWall)
			{
				new CBuilding(GameWorld(), m_Pos, m_pPlayer->GetCID(), BUILDTYPE_WALL, Direction, 200);
				m_HasWall = true;
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
			else
			{
				// reset objects Hit
				m_NumObjectsHit = 0;
				GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

				CCharacter *apEnts[MAX_CLIENTS];
				int Hits = 0;
				int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
															MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

				for (int i = 0; i < Num; ++i)
				{
					CCharacter *pTarget = apEnts[i];

					if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
						continue;

					// set his velocity to fast upward (for now)
					if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
						GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
					else
						GameServer()->CreateHammerHit(ProjStartPos);

					vec2 Dir;
					if (length(pTarget->m_Pos - m_Pos) > 0.0f)
						Dir = normalize(pTarget->m_Pos - m_Pos);
					else
						Dir = vec2(0.f, -1.f);

					if(IsZombie())
					{
						if(pTarget->IsZombie())
						{
							if(pTarget->IncreaseHealthAndArmor(4))
							{
								IncreaseHealthAndArmor(1);

								pTarget->m_EmoteType = EMOTE_HAPPY;
								pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
							}
						}else if(GameServer()->m_pController->IsInfectionStarted())
						{
							pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_XOLEPANIC);
						}
					}else if(GetRole() == PLAYERROLE_MEDIC)
					{
						if(pTarget->IsZombie())
						{
							pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20,
								m_pPlayer->GetCID(), m_ActiveWeapon);
						}else
						{
							if(pTarget->IsWillDie())
							{
								pTarget->UnWillDie();
							}
							else pTarget->IncreaseHealthAndArmor(2);

							pTarget->m_EmoteType = EMOTE_HAPPY;
							pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
						}
					}else 
					{
						if(pTarget->IsZombie())
						{
							pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20,
								m_pPlayer->GetCID(), m_ActiveWeapon);
						}
					}
					Hits++;
				}

				// if we Hit anything, we have to wait for the reload
				if(Hits)
					m_ReloadTimer = Server()->TickSpeed()/3;
			}
		} break;

		case WEAPON_GUN:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				1, 0, 0, -1, WEAPON_GUN);

			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 2;
			float Force = 0.0f;

			if(GetRole() == PLAYERROLE_MEDIC)
			{
				Force = 8.0f;
			}

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = GetAngle(Direction);
				a += Spreading[i+2];
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
					1, 0, Force, -1, WEAPON_SHOTGUN);
			}

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
				1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		} break;

		case WEAPON_RIFLE:
		{
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		} break;

		case WEAPON_NINJA:
		{
			// reset Hit objects
			m_NumObjectsHit = 0;

			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

			GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
		} break;

	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
		m_ReloadTimer = Server()->GetWeaponFireDelay(GetXoleWeaponID(m_ActiveWeapon)) * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
	for(int i=WEAPON_GUN; i<=WEAPON_RIFLE; i++)
	{
		int WID = GetXoleWeaponID(i);
		int AmmoRegenTime = Server()->GetWeaponAmmoRegenTime(WID);
		int MaxAmmo = Server()->GetWeaponMaxAmmo(GetXoleWeaponID(i));
		
		if(AmmoRegenTime)
		{
			if(m_ReloadTimer <= 0)
			{
				if (m_aWeapons[i].m_AmmoRegenStart < 0)
					m_aWeapons[i].m_AmmoRegenStart = Server()->Tick();

				if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
				{
					// Add some ammo
					m_aWeapons[i].m_Ammo = min(m_aWeapons[i].m_Ammo + 1, MaxAmmo);
					m_aWeapons[i].m_AmmoRegenStart = -1;
				}
			}
		}
	}

	if(IsRoleCanHookDamage())
	{
		if(m_Core.m_HookedPlayer >= 0)
		{
			CCharacter *VictimChar = GameServer()->GetPlayerChar(m_Core.m_HookedPlayer);
			if(VictimChar)
			{
				int Damage = 1;
				if(GetRole() == PLAYERROLE_SMOKER)
				{
					Damage = g_Config.m_XoleSmokerHookDamage;
				}

				if(m_HookDmgTick + Server()->TickSpeed() < Server()->Tick())
				{
					m_HookDmgTick = Server()->Tick();
					VictimChar->TakeDamage(vec2(0.0f,0.0f), Damage, m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_TEEWORLDS	);
					if(GetRole() == PLAYERROLE_SMOKER && VictimChar->IsHuman())
						IncreaseHealthAndArmor(2);
				}
			}
		}
	}

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(Ammo < 0 && Weapon != WEAPON_HAMMER)
	{
		Ammo = 10;
	}

	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{

	if(m_WillDie)
	{
		if(m_WillDieTick)
		{
			int Second = m_WillDieTick / 50;
			m_WillDieTick--;
			if(!(m_WillDieTick % 50))
			{
				GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
			}
			m_Input.m_Jump = 0;
			m_Input.m_Direction = 0;
			m_Input.m_TargetX = 0;
			m_Input.m_TargetY = 0;
			m_Input.m_Hook = 0;
			GameServer()->SendBroadcast_VL(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_GAMEANNOUNCE
				, _("If no medics to help You.\nYou will die in {sec:Time}"), "Time", &Second, NULL);
		}else
		{
			m_pPlayer->StartInfection();
			Die(m_WillDieKiller, m_WillDieWeapon);
		}
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true, m_pPlayer->GetNextTuningParams());

	if(GameServer()->m_pController->IsInfectionStarted() && IsHuman())
	{
		m_pPlayer->SetSwitchRoleState(0);
	}

	if(m_InvisibleTick)
	{
		m_InvisibleTick--;
		int Time = round_to_int(m_InvisibleTick/50.0f);
		GameServer()->SendBroadcast_VL(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_GAMEANNOUNCE,
			_("You are invisible: {sec:Time}"), "Time", &Time, NULL);
		
		if(!m_InvisibleTick)
		{
			m_IsInvisible = false;
			m_VisibleTick = Server()->Tick();
			GameServer()->ClearBroadcast(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE);
		}
	}

	// handle Zones
	HandleZone();

	// handle Weapons
	HandleWeapons();

	if(GetRole() == PLAYERROLE_HUNTER || GetRole() == PLAYERROLE_SNIPER)
	{
		if(IsGrounded()) m_AirJumpCounter = 0;
		if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP && m_AirJumpCounter < 1)
		{
			m_Core.m_Jumped &= ~2;
			m_AirJumpCounter++;
		}
	}

	UpdateTuningParam();

	m_OldPos = m_Core.m_Pos;

	// Previnput
	m_PrevInput = m_Input;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false, &TempWorld.m_Tuning);
		m_ReckoningCore.Move(&TempWorld.m_Tuning);
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move(m_pPlayer->GetNextTuningParams());
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealthAndArmor(int Amount)
{
	int AddArmor = Amount- (10 - m_Health);
	if(AddArmor > 0)
	{
		IncreaseArmor(AddArmor);
	}
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();
	
	if(GameServer()->m_pController->IsInfectionStarted())
	{
		m_pPlayer->StartInfection();
		DestroyChrEntity();
	}

	GameServer()->CountPlayer();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon, int TakeDmgMode)
{
	if(m_WillDie)
	{
		return false;
	}
	m_Core.m_Vel += Force;

	CPlayer *pFrom = GameServer()->m_apPlayers[From];

	// m_pPlayer only inflicts no damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = 0;

	if(m_pPlayer->IsHuman() == pFrom->IsHuman())
	{
		return false;
	}

	if(TakeDmgMode == TAKEDAMAGEMODE_XOLEPANIC)
	{
		if(((Server()->Tick() - m_LastReviveTick) / 50) >= g_Config.m_XoleReviveCDSec)
		{
			m_WillDie = true;
			m_WillDieTick = g_Config.m_XoleWillDieSec * 50;
			m_WillDieKiller = From;
			m_WillDieWeapon = Weapon;
		}
		else
		{
			m_pPlayer->StartInfection();
			Die(From, Weapon);
		}
		return false;
	}

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	int Id = m_pPlayer->GetCID();

	if (!Server()->Translate(Id, SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *SnapPlayer = GameServer()->m_apPlayers[SnappingClient];
	if(!SnapPlayer)
	{
		return;
	}
	else if(SnapPlayer->IsZombie() != IsZombie() && IsInvisible())
	{
		return;
	}

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, Id, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}
	
	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	if (pCharacter->m_HookedPlayer != -1)
	{
		if (!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
	}

	if(m_WillDie)
	{
		pCharacter->m_Emote = EMOTE_PAIN;
	}
	else if(IsZombie()) 
		pCharacter->m_Emote = EMOTE_ANGRY;
	else if(IsInvisible())
		pCharacter->m_Emote = EMOTE_BLINK;
	else 
		pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}
// XolePanic Start
int CCharacter::GetRole() const
{
	return m_pPlayer->GetRole();
}

bool CCharacter::IsZombie() const
{
	return m_pPlayer->IsZombie();
}

bool CCharacter::IsHuman() const
{
	return m_pPlayer->IsHuman();
}

bool CCharacter::IsWillDie() const
{
	return m_WillDie;
}

void CCharacter::UnWillDie()
{
	m_WillDie = false;
	m_LastReviveTick = Server()->Tick();
	m_WillDieTick = 0;
}

void CCharacter::RemoveAllWeapon()
{
	for(int i = WEAPON_HAMMER+1;i < NUM_WEAPONS;i++)
	{
		m_aWeapons[i].m_Got = false;
		m_aWeapons[i].m_Ammo = 0;
	}
}

void CCharacter::GiveRoleWeapon()
{
	m_LastBuildTick = 0;
	RemoveAllWeapon();

	switch (GetRole())
	{
		case PLAYERROLE_MEDIC:
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_SHOTGUN, 5);
			m_ActiveWeapon = WEAPON_SHOTGUN;
			break;
		case PLAYERROLE_SNIPER:
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_RIFLE, 5);
			m_ActiveWeapon = WEAPON_RIFLE;
			break;
		case PLAYERROLE_BUILDER:
			m_Health = 10;
			m_HasWall = false;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_SHOTGUN, 5);
			m_ActiveWeapon = WEAPON_HAMMER;
			break;
		
		case PLAYERROLE_SMOKER:
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			break;
		case PLAYERROLE_HUNTER:
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			break;
		case PLAYERROLE_PICKER:
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			break;

		default:
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			break;
	}
}

void CCharacter::HandleZone()
{
	// handle death-tiles and leaving gamelayer
	if(GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle death-zones
	if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_TeeWorlds, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f) == TILE_DEATH ||
		GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_TeeWorlds, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f) == TILE_DEATH ||
		GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_TeeWorlds, m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f) == TILE_DEATH ||
		GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_TeeWorlds, m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f) == TILE_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}
	// handle Teeuniverse Zone
	int Index = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Panic, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f);
	{
		// handle safe zone
		if(Index == ZONE_PANIC_SAFE_ZONE)
		{
			if(!(GameServer()->m_HasZombieInSafeZone) && IsZombie())
			{
				GameServer()->m_HasZombieInSafeZone = true;
			}
		}
		// handle infect zone
		else if(Index == ZONE_PANIC_INFECT_ZONE)
		{
			if(IsHuman())
			{
				m_pPlayer->StartInfection();
			}
			m_InInfectZone = true;
			m_pPlayer->SetSwitchRoleState(1);
		}else 
		{
			if(IsZombie())
			{
				m_pPlayer->SetSwitchRoleState(0);
			}
			m_InInfectZone = false;
		}
	}
}

void CCharacter::UpdateTuningParam()
{
	CTuningParams* pTuningParams = &m_pPlayer->m_NextTuningParams;

	if(m_WillDie)
	{
		pTuningParams->m_GroundControlAccel = 0.0f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
		pTuningParams->m_AirControlAccel = 0.0f;
		pTuningParams->m_HookLength = 0.0f;
	}
	return;
}

int CCharacter::GetXoleWeaponID(int Weapon)
{
	if(Weapon == WEAPON_HAMMER)
	{
		switch(GetRole())
		{
			case PLAYERROLE_MEDIC:
				return XOLEWEAPON_MEDIC_HAMMER;
				break;
			default:
				return XOLEWEAPON_HAMMER;
				break;
		}
	}
	else if(Weapon == WEAPON_GUN)
	{
		switch(GetRole())
		{
			default:
				return XOLEWEAPON_GUN;
				break;
		}
	}
	else if(Weapon == WEAPON_SHOTGUN)
	{
		switch(GetRole())
		{
			case PLAYERROLE_MEDIC:
				return XOLEWEAPON_MEDIC_SHOTGUN;
				break;
			default:
				return XOLEWEAPON_SHOTGUN;
				break;
		}
	}
	else if(Weapon == WEAPON_GRENADE)
	{
		switch(GetRole())
		{
			default:
				return XOLEWEAPON_GRENADE;
				break;
		}
	}
	else if(Weapon == WEAPON_RIFLE)
	{
		switch(GetRole())
		{	
			case PLAYERROLE_SNIPER:
				return XOLEWEAPON_SNIPER_RIFLE;
				break;
			default:
				return XOLEWEAPON_RIFLE;
				break;
		}
	}
	else if(Weapon == WEAPON_NINJA)
	{
		return XOLEWEAPON_NINJA;
	}

	return XOLEWEAPON_NONE;
}

bool CCharacter::IsRoleCanHookDamage() const
{
	switch (GetRole())
	{
		case PLAYERROLE_SMOKER: 
			return true;
			break;
		default:return false;break;
	}
}

bool CCharacter::IsInvisible() const
{
	return m_InvisibleTick > 0;
}

void CCharacter::DestroyChrEntity()
{
	for(CBuilding *pBuild = (CBuilding *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_BUILDING); 
			pBuild; pBuild = (CBuilding *)pBuild->TypeNext())
	{
		if(pBuild->m_Owner != m_pPlayer->GetCID())continue;
		GameServer()->m_World.DestroyEntity(pBuild);
	}
}
// XolePanic End