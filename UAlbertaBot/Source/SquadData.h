#pragma once

#include "Squad.h"

namespace UAlbertaBot
{
class SquadData
{
	static BWAPI::Unit baitPreventionUnit;
	static BWAPI::Unit baitUnitSent;
	static BWTA::Region * baitRegion;
	static std::vector<BWAPI::Position> vertices;
	static bool baitSent;
	static bool mainAttackSqaudSent;
	static bool baitMode;
	static bool reachedBaitRegion;
	static int gameStartTime;
	static int priorHealth;
	static int _currentRegionVertexIndex;
	std::map<std::string, Squad> _squads;
	std::map<std::string, Squad> _miniAttackSquads;
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

};
}