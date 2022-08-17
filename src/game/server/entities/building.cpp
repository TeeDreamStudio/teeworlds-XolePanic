

#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "building.h"

const float g_WallMaxLength = 300.0;

CBuilding::CBuilding(CGameWorld *pGameWorld, vec2 Pos, int m_Owner, int Type, vec2 Dir, int Health)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_BUILDING)
{
    m_Pos = Pos;
    m_Type = Type;
    m_Health = Health;

	GameWorld()->InsertEntity(this);

    switch (Type)
    {
        case BUILDTYPE_WALL: m_IDs.set_size(2);m_aPos.set_size(2);
            m_MaxHealth = g_Config.m_XoleWallMaxHP; 
            m_aPos[0] = m_Pos;
            m_aPos[1] = m_Pos;
            while(!(GameServer()->Collision()->GetCollisionAt(m_aPos[1].x, m_aPos[1].y)&CCollision::COLFLAG_SOLID))
            {
                if(distance(m_aPos[0], m_aPos[1]) > g_WallMaxLength)
                    break;
                m_aPos[1] += Dir * 4;
            } break;
    }

    for(int i = 0;i < m_IDs.size(); i++)
    {
        m_IDs[i] = Server()->SnapNewID();
    }
    
}

CBuilding::~CBuilding()
{
    for(int i = 0;i < m_IDs.size(); i++)
    {
        Server()->SnapFreeID(m_IDs[i]);
    }
}

void CBuilding::Build(CCharacter *pChr)
{
    if((pChr->m_LatestInput.m_Fire&1) && 
        ((pChr->m_LastBuildTick + Server()->TickSpeed() *
            g_Config.m_XoleRebuildSec) <= Server()->Tick()))
    {
        m_Health = min(m_Health+20, m_MaxHealth);
        pChr->m_LastBuildTick = Server()->Tick();
        GameServer()->CreateSoundGlobal(SOUND_HOOK_LOOP, pChr->GetPlayer()->GetCID());
    }
    GameServer()->SendBroadcast_VL(
        pChr->GetPlayer()->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE,
        BROADCAST_DURATION_REALTIME, _("Health: {int:Health}:{int:MaxHealth}"),
        "Health", &m_Health, "MaxHealth", &m_MaxHealth, NULL);
}

void CBuilding::Destroy(CCharacter *pChr)
{
    if((pChr->m_LatestInput.m_Fire&1) && 
        ((pChr->m_LastBuildTick + Server()->TickSpeed() *
            g_Config.m_XoleRebuildSec) <= Server()->Tick()))
    {
        int Damage;
        if(pChr->GetRole() == PLAYERROLE_PICKER)
        {
            Damage = 50;
        }else Damage = 20;
        m_Health = max(m_Health-Damage, 0);
        pChr->m_LastBuildTick = Server()->Tick();
        GameServer()->CreateSoundGlobal(SOUND_HOOK_LOOP, pChr->GetPlayer()->GetCID());
    }
    GameServer()->SendBroadcast_VL(
        pChr->GetPlayer()->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE,
        BROADCAST_DURATION_REALTIME, _("Health: {int:Health}:{int:MaxHealth}"),
        "Health", &m_Health, "MaxHealth", &m_MaxHealth, NULL);
}

void CBuilding::Tick()
{
    switch (m_Type)
    {
        case BUILDTYPE_WALL:
            CCharacter *apEnts[MAX_CLIENTS];
            int Num = FindCharacters(m_aPos[1], m_aPos[0], 8.0f, apEnts, MAX_CLIENTS);
            for(int i = 0; i < Num; i++)
            {
                if(apEnts[i]->GetPlayer()->IsZombie())
                {
                    vec2 IntersectPos = closest_point_on_line(m_aPos[0], m_aPos[1], apEnts[i]->m_Pos);
                    float Len = distance(apEnts[i]->m_Pos, IntersectPos);
                    if(Len < apEnts[i]->m_ProximityRadius+2.0f)
                    {
                        apEnts[i]->m_Core.m_Vel = -apEnts[i]->m_Core.m_Vel;
                        apEnts[i]->m_Core.m_Pos = apEnts[i]->m_OldPos;
                    }
                    Destroy(apEnts[i]);
                }else if(apEnts[i]->GetRole() == PLAYERROLE_BUILDER && apEnts[i]->m_ActiveWeapon == WEAPON_HAMMER)
                {
                    Build(apEnts[i]);
                }
            }
            break;
    }

    if(m_Health <= 0)
    {
        if(GameServer()->m_apPlayers[m_Owner])
        {
            CCharacter *pChr = GameServer()->GetPlayerChar(m_Owner);
            if(pChr)
            {
                pChr->m_HasWall = false;
            }
        }
        GameServer()->CreatePlayerSpawn(m_Pos);
	    GameServer()->m_World.DestroyEntity(this);
    }
}

int CBuilding::FindCharacters(vec2 Pos0, vec2 Pos1, float Radius, CCharacter **ppChars, int Max)
{
	int Num = 0;
	CCharacter *pCh = (CCharacter *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
	for(; pCh; pCh = (CCharacter *)pCh->TypeNext())
	{
		vec2 IntersectPos = Pos0;
		if(Pos0 != Pos1)
			IntersectPos = closest_point_on_line(Pos0, Pos1, pCh->m_Pos);
		if(distance(pCh->m_Pos, IntersectPos) < pCh->m_ProximityRadius+Radius)
		{
			if(ppChars)
				ppChars[Num] = pCh;
			Num++;
			if(Num == Max)
				break;
		}
	}
	return Num;
}


void CBuilding::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	switch (m_Type)
    {
        case BUILDTYPE_WALL:
            int Tick = Server()->Tick()-(m_MaxHealth - m_Health)/100;
            CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[0], sizeof(CNetObj_Laser)));
            if(pObj)
            {
                pObj->m_X = m_aPos[0].x;
                pObj->m_Y = m_aPos[0].y;
                pObj->m_FromX = m_aPos[1].x;
                pObj->m_FromY = m_aPos[1].y;
                pObj->m_StartTick = Tick;
            }
            CNetObj_Laser *pL = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
            if(pL)
            {
                pL->m_X = m_aPos[1].x;
                pL->m_Y = m_aPos[1].y;
                pL->m_FromX = m_aPos[1].x;
                pL->m_FromY = m_aPos[1].y;
                pL->m_StartTick = Tick-3;
            }
            break;
    }
}
