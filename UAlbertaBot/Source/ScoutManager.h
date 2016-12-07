#pragma once

#include "Common.h"
#include "MicroManager.h"
#include "InformationManager.h"

namespace UAlbertaBot
{
class ScoutManager 
{
	BWAPI::Unit	        _workerScout;
    std::string                     _scoutStatus;
    std::string                     _gasStealStatus;
	int				                _numWorkerScouts;
	bool			                _scoutUnderAttack;
    bool                            _didGasSteal;
    bool                            _gasStealFinished;
	bool							_cannonRushEnemyRushPrevented;
	bool							_cannonRushEnemyInsultHurled;
	bool							_cannonRushEnemyBaseExplored;
	bool							_cannonRushReady;
	bool							_cannonRushDone;
	BWAPI::TilePosition				_cannonRushChokepoint = BWAPI::TilePositions::Unknown;
	BWAPI::Position					_cannonRushChokepointPos;
	BWAPI::TilePosition				_cannonRushChokepointCloser = BWAPI::TilePositions::Unknown;
	BWAPI::Position					_cannonRushChokepointPosCloser;
	BWAPI::TilePosition				_cannonRushChokepointClosest = BWAPI::TilePositions::Unknown;
	BWAPI::Position					_cannonRushChokepointPosClosest;
	bool							_cannonRushSecondPylonDone;
	bool							_nextProbeIsScout;
    int                             _currentRegionVertexIndex;
    int                             _previousScoutHP;
	std::vector<BWAPI::Position>    _enemyRegionVertices;

	bool                            enemyWorkerInRadius(int radius=300);
    bool			                immediateThreat();
    void                            gasSteal();
    int                             getClosestVertexIndex(BWAPI::Unit unit);
    BWAPI::Position                 getFleePosition();
	BWAPI::Unit	        getEnemyGeyser();
	BWAPI::Unit	        closestEnemyWorker();
    void                            followPerimeter();
	void                            moveScouts();
    void                            drawScoutInformation(int x, int y);
    void                            calculateEnemyRegionVertices();

	ScoutManager();

public:

    static ScoutManager & Instance();

	void update();

	BWAPI::Unit getWorkerScout();
    void setWorkerScout(BWAPI::Unit unit);

	void onSendText(std::string text);
	void onUnitShow(BWAPI::Unit unit);
	void onUnitHide(BWAPI::Unit unit);
	void onUnitCreate(BWAPI::Unit unit);
	void onUnitRenegade(BWAPI::Unit unit);
	void onUnitDestroy(BWAPI::Unit unit);
	void onUnitMorph(BWAPI::Unit unit);

	bool isCannonRushReady();
	bool isCannonRushDone();
	bool isNextProbeScout();
	void setNextProbeScout(bool isScout);
	BWAPI::TilePosition getChokepoint();
	BWAPI::TilePosition getChokepointCloser();
};
}