

#ifndef GAME_SERVER_ENTITIES_BUILDING_H
#define GAME_SERVER_ENTITIES_BUILDING_H

#include <game/server/entity.h>
#include <base/tl/array.h>

class CBuilding : public CEntity
{
public:

	CBuilding(CGameWorld *pGameWorld, vec2 Pos, int m_Owner, int Type, vec2 Dir, int Health);
    ~CBuilding();

    virtual void Tick();
    void Build(CCharacter *pChr);
    void Destroy(CCharacter *pChr);
	virtual void Snap(int SnappingClient);
    int m_Owner;
private:
    int FindCharacters(vec2 Pos0, vec2 Pos1, float Radius, CCharacter **ppChars, int Max);
    array<vec2> m_aPos;
    vec2 m_Dir;
    array<int> m_IDs;
    int m_Type;
    int m_Health;
    int m_MaxHealth;
};

#endif