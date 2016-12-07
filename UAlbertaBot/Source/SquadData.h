#pragma once

#include "Squad.h"

namespace UAlbertaBot
{
class SquadData
{
	static BWAPI::Unit baitPreventionUnit;
	static BWAPI::Unit baitUnitSent;
	static BWAPI::Unit baitedEnemyUnit;
	static BWTA::Region * baitRegion;
	static bool baitMode;
	static bool reachedBaitRegion;
	static int baitCreationTime;
	static int suitableBaitRegion;
	static int priorHealth;
	std::map<std::string, Squad> _squads;
	std::map<std::string, Squad> _miniAttackSquads;
	static int _currentRegionVertexIndex;
	static std::vector<BWAPI::Position> vertices;


    void    updateAllSquads();
    void    verifySquadUniqueMembership();

public:

	SquadData();

    void            clearSquadData();
	
    bool            canAssignUnitToSquad(BWAPI::Unit unit, const Squad & squad) const;
    void            assignUnitToSquad(BWAPI::Unit unit, Squad & squad);
    void            addSquad(const std::string & squadName, const Squad & squad);
    void            removeSquad(const std::string & squadName);
    void            clearSquad(const std::string & squadName);
	void            drawSquadInformation(int x, int y);

    void            update();
    void            setRegroup();

    bool            squadExists(const std::string & squadName);
    bool            unitIsInSquad(BWAPI::Unit unit) const;
    const Squad *   getUnitSquad(BWAPI::Unit unit) const;
    Squad *         getUnitSquad(BWAPI::Unit unit);

    Squad &         getSquad(const std::string & squadName);
    const std::map<std::string, Squad> & getSquads() const;

	void			updateNeutralZoneAttackSquad();
	void			updateBaitSquad(int releaseTime);
	BWAPI::Unit		getBaitUnit();
	BWAPI::Unit		getBaitUnitSent();
	BWAPI::Unitset	getUnitsBaited();
	int				getGameStartTime();
	bool			getBaitSent();
	bool			getMainAttackSquadSent();
	void			followPerimeter();
	BWAPI::Position getFleePosition();
	int				getClosestVertexIndex(BWAPI::Unit unit);
	BWTA::Region *	findBaitRegion();
};
}