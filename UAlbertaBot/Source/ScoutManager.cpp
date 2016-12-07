#include "ScoutManager.h"
#include "ProductionManager.h"

using namespace UAlbertaBot;

ScoutManager::ScoutManager() 
    : _workerScout(nullptr)
    , _numWorkerScouts(0)
    , _scoutUnderAttack(false)
    , _gasStealStatus("None")
    , _scoutStatus("None")
    , _didGasSteal(false)
    , _gasStealFinished(false)
    , _currentRegionVertexIndex(-1)
    , _previousScoutHP(0)
	, _cannonRushSecondPylonDone(false)
{
	if (BWAPI::Broodwar->enemy())
	{
		if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss) {
			// no need to explore protoss since they can't rush us fast enough
			_cannonRushEnemyBaseExplored = true;
		}
	}
}

ScoutManager & ScoutManager::Instance() 
{
	static ScoutManager instance;
	return instance;
}

void ScoutManager::update()
{
    if (!Config::Modules::UsingScoutManager)
    {
        return;
    }

    // calculate enemy region vertices if we haven't yet
    if (_enemyRegionVertices.empty())
    {
        calculateEnemyRegionVertices();
    }

	moveScouts();
    drawScoutInformation(200, 320);

	if (Config::Strategy::StrategyName == "Protoss_CannonRush")
	{
		if (_numWorkerScouts == 1 && (!_workerScout || !_workerScout->exists()))
		{
			ProductionManager::Instance().queueCannonRushNewScout();
			_nextProbeIsScout = true;
			_workerScout = nullptr;
			_numWorkerScouts = 0;
		}
	}
}

void ScoutManager::setWorkerScout(BWAPI::Unit unit)
{
    // if we have a previous worker scout, release it back to the worker manager
	if (_workerScout && _cannonRushReady == false)
    {
        WorkerManager::Instance().finishedWithWorker(_workerScout);
    }
	else
	{
		++_numWorkerScouts;
	}

    _workerScout = unit;
    WorkerManager::Instance().setScoutWorker(_workerScout);
}

void ScoutManager::drawScoutInformation(int x, int y)
{
    if (!Config::Debug::DrawScoutInfo)
    {
        return;
    }

    BWAPI::Broodwar->drawTextScreen(x, y, "ScoutInfo: %s", _scoutStatus.c_str());
    BWAPI::Broodwar->drawTextScreen(x, y+10, "GasSteal: %s", _gasStealStatus.c_str());
    for (size_t i(0); i < _enemyRegionVertices.size(); ++i)
    {
        BWAPI::Broodwar->drawCircleMap(_enemyRegionVertices[i], 4, BWAPI::Colors::Green, false);
        BWAPI::Broodwar->drawTextMap(_enemyRegionVertices[i], "%d", i);
    }
}

void ScoutManager::moveScouts()
{
	if (!_workerScout || !_workerScout->exists() || !(_workerScout->getHitPoints() > 0))
	{
		return;
	}

	if (_cannonRushReady && WorkerManager::Instance().isBuilder(_workerScout)) {
		return;
	}

    int scoutHP = _workerScout->getHitPoints() + _workerScout->getShields();
    
    gasSteal();

	// get the enemy base location, if we have one
	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());

    int scoutDistanceThreshold = 30;

    if (_workerScout->isCarryingGas())
    {
        BWAPI::Broodwar->drawCircleMap(_workerScout->getPosition(), 10, BWAPI::Colors::Purple, true);
    }

    // if we initiated a gas steal and the worker isn't idle, 
    bool finishedConstructingGasSteal = _workerScout->isIdle() || _workerScout->isCarryingGas();
    if (!_gasStealFinished && _didGasSteal && !finishedConstructingGasSteal)
    {
        return;
    }
    // check to see if the gas steal is completed
    else if (_didGasSteal && finishedConstructingGasSteal)
    {
        _gasStealFinished = true;
    }

	if (Config::Strategy::StrategyName == "Protoss_CannonRush" && _cannonRushReady && !ProductionManager::Instance().isQueueEmpty()) {
		_scoutStatus = "Waiting for an empty queue...";
		
		// move the scout around so that the build placer doesn't lock up
		if (scoutHP < _previousScoutHP)
		{
			_scoutUnderAttack = true;
			followPerimeter();
		}
		else // move the scout to the chokepoint
		{
			const std::set<BWTA::Chokepoint*>& chokepoints = enemyBaseLocation->getRegion()->getChokepoints();
			if (chokepoints.size() > 0) {
				BWTA::Chokepoint* chokepoint = *chokepoints.begin();
				Micro::SmartMove(_workerScout, chokepoint->getCenter());
			}
			else {
				Micro::SmartMove(_workerScout, _cannonRushChokepointPosCloser);
			}
		}

		_previousScoutHP = scoutHP;
		return;
	}
    
	// if we know where the enemy region is and where our scout is
	if (_workerScout && enemyBaseLocation)
	{
        if (Config::Strategy::StrategyName == "Protoss_CannonRush" || _cannonRushReady) 
		{
			_scoutUnderAttack = false; // don't care if our scout is under attack because we're rushing

			const std::set<BWTA::Chokepoint*>& chokepoints = enemyBaseLocation->getRegion()->getChokepoints();
			if (chokepoints.size() > 0) {
				BWTA::Chokepoint* chokepoint = *chokepoints.begin();

				double scoutDistanceToEnemyChokepoint = _workerScout->getPosition().getDistance(chokepoint->getCenter());
				bool scoutInRangeOfChokePoint = scoutDistanceToEnemyChokepoint > -1 && scoutDistanceToEnemyChokepoint <= 600;
				bool scoutInRangeOfCloserChokePoint = scoutDistanceToEnemyChokepoint > -1 && scoutDistanceToEnemyChokepoint <= 550;
				bool scoutOutOfRange = scoutDistanceToEnemyChokepoint > -1 && scoutDistanceToEnemyChokepoint > 600;

				if (!_cannonRushReady && scoutOutOfRange) // check for a worker rush before cannon rush is ready
				{
					BWAPI::Unitset nearbyUnits = _workerScout->getUnitsInRadius(100);
					int workerCount = 0;
					for (auto& unit : nearbyUnits) {
						if (unit->getType().isWorker())
						{
							if(BWAPI::Broodwar->enemy()->getUnits().contains(unit))
								++workerCount;
						}
						else if (unit->isBeingConstructed() && (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery
							|| unit->getType() == BWAPI::UnitTypes::Protoss_Nexus 
							|| unit->getType() == BWAPI::UnitTypes::Terran_Command_Center) && BWAPI::Broodwar->enemy()->getUnits().contains(unit))
						{ // Check if the enemy is expanding too fast for us to cannon rush
							BWAPI::Broodwar->sendText("Haha do you really think you can beat me with an expansion rush?");

							_cannonRushDone = true;

							Micro::SmartMove(_workerScout, InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->self())->getRegion()->getCenter());

							ProductionManager::Instance().queueCannonRushCannonHighPriority();
							ProductionManager::Instance().queueCannonRushPylon();

							WorkerManager::Instance().setMineralWorker(_workerScout);
							_workerScout = nullptr;
							Config::Strategy::StrategyName = "Protoss_DTRush";

							return;
						}
					}

					if (workerCount >= 3)
					{
						BWAPI::Broodwar->sendText("Haha do you really think you can beat me with a worker rush?");

						_cannonRushDone = true;

						Micro::SmartMove(_workerScout, InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->self())->getRegion()->getCenter());

						ProductionManager::Instance().queueCannonRushCannonHighPriority();
						ProductionManager::Instance().queueCannonRushCannonHighPriority();
						ProductionManager::Instance().queueCannonRushPylon();

						WorkerManager::Instance().setMineralWorker(_workerScout);
						_workerScout = nullptr;
						Config::Strategy::StrategyName = "Protoss_DTRush";

						return;
					}
				}
				
				if (scoutInRangeOfChokePoint && _cannonRushChokepoint == BWAPI::TilePositions::Unknown) {
					bool canBuild = BWAPI::Broodwar->canBuildHere(_workerScout->getTilePosition(), BWAPI::UnitTypes::Protoss_Pylon, _workerScout);
					if (canBuild)
					{
						_cannonRushChokepoint = _workerScout->getTilePosition();
						_cannonRushChokepointPos = _workerScout->getPosition();
					}
				}

				if (scoutInRangeOfCloserChokePoint && _cannonRushChokepointCloser == BWAPI::TilePositions::Unknown) {
					bool canBuild = BWAPI::Broodwar->canBuildHere(_workerScout->getTilePosition(), BWAPI::UnitTypes::Protoss_Pylon, _workerScout);
					if (canBuild)
					{
						_cannonRushChokepointCloser = _workerScout->getTilePosition();
						_cannonRushChokepointPosCloser = _workerScout->getPosition();
					}
				}

				if (!_cannonRushEnemyBaseExplored)
				{
					const BWAPI::Position& enemyBasePos = enemyBaseLocation->getRegion()->getCenter();

					double scoutDistanceToEnemy = _workerScout->getPosition().getDistance(enemyBasePos);
					bool scoutInRangeOfenemy = scoutDistanceToEnemy > -1 && scoutDistanceToEnemy <= 25;

					if (scoutInRangeOfenemy)
					{
						BWAPI::Unitset nearbyUnits = _workerScout->getUnitsInRadius(500);
						for (auto& unit : nearbyUnits) {
							if (unit->getType() == BWAPI::UnitTypes::Terran_Marine || unit->getType() == BWAPI::UnitTypes::Terran_Barracks || unit->getType() == BWAPI::UnitTypes::Zerg_Spawning_Pool)
							{
								// Get out of there!
								// Save our scout and modify our strategy quickly; a cannon rush isn't possible anymore!

								if (unit->getType().getRace() == BWAPI::Races::Terran)
								{
									BWAPI::Broodwar->sendText("Haha do you really think you can beat me with a marine rush?");
								}
								else
								{
									BWAPI::Broodwar->sendText("Haha do you really think you can beat me with a zergling rush?");
								}

								_cannonRushDone = true;

								Micro::SmartMove(_workerScout, InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->self())->getRegion()->getCenter());
								
								ProductionManager::Instance().queueCannonRushCannonHighPriority();
								ProductionManager::Instance().queueCannonRushCannonHighPriority();
								ProductionManager::Instance().queueCannonRushPylon();

								WorkerManager::Instance().setMineralWorker(_workerScout);
								_workerScout = nullptr;

								if (unit->getType() == BWAPI::UnitTypes::Terran_Marine || unit->getType() == BWAPI::UnitTypes::Zerg_Spawning_Pool)
								{
									// We don't have a whole lot of time for a better strategy...
									Config::Strategy::StrategyName = "Protoss_ZealotRush";
								}
								else
								{
									// Take some time to build a better unit in case it is a tank rush
									Config::Strategy::StrategyName = "Protoss_DTRush";
								}

								_scoutStatus = "None";
								return;
							}
						}

						_cannonRushEnemyBaseExplored = true;
					}
					else
					{
						_scoutStatus = "Seeking the enemy home base and determining a strategy...";
						Micro::SmartMove(_workerScout, enemyBaseLocation->getRegion()->getCenter());
					}
				}
				else
				{
					BWAPI::Position destination;
					if (_cannonRushChokepointCloser == BWAPI::TilePositions::Unknown) destination = chokepoint->getCenter();
					else destination = _cannonRushChokepointPosCloser;

					double scoutDistanceToEnemy = _workerScout->getPosition().getDistance(destination);
					bool scoutInRangeOfenemy = scoutDistanceToEnemy > -1 && scoutDistanceToEnemy <= 50;

					if (scoutInRangeOfenemy)
					{
						_cannonRushReady = true;

						// find if a pylon is available nearby
						// if not, build it first
						// else, build photon cannons
						BWAPI::Unitset nearbyUnits = _workerScout->getUnitsInRadius(100);
						bool isPylonFound = false;
						bool isPylonConstructing = false;
						int pylonCount = 0;
						int cannonCount = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon);
						for (auto& unit : nearbyUnits) {
							if (unit->getType() == BWAPI::UnitTypes::Protoss_Pylon)
							{
								isPylonConstructing = true;
								++pylonCount;

								if (!unit->isBeingConstructed())
								{
									isPylonFound = true;

									if (!_cannonRushDone) // queue cannons as fast as possible until done
									{
										ProductionManager::Instance().queueCannonRushCannon();
									}
								}
							}
							// uncomment below for locality
							//else if (unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon)
							//{
							//	++cannonCount;
							//}
						}

						if (cannonCount == 5 && pylonCount == 1 && _cannonRushSecondPylonDone == false) // backup pylon
						{
							isPylonFound = false;
							isPylonConstructing = false;
							_cannonRushSecondPylonDone = true;
						}

						if (cannonCount < 8)
						{
							_cannonRushDone = false; // we need more cannons!
						}
						else
						{
							_cannonRushDone = true;

							ProductionManager::Instance().queueCannonRushPylon();
							ProductionManager::Instance().queueCannonRushCannonHighPriority();
							ProductionManager::Instance().queueCannonRushCannonHighPriority();

							WorkerManager::Instance().setMineralWorker(_workerScout);
							_scoutStatus = "None";
							_workerScout = nullptr;

							// pick a decent strategy against cloaked units
							if (InformationManager::Instance().enemyHasCloakedUnits())
							{
								Config::Strategy::StrategyName = "Protoss_DTRush";
							}
							else
							{
								Config::Strategy::StrategyName = "Protoss_DragoonRush";
							}
						}

						if (!isPylonFound && !isPylonConstructing)
						{
							ProductionManager::Instance().queueCannonRushPylon();
						}
					}
					else
					{
						_scoutStatus = "Seeking the chokepoint...";
						Micro::SmartMove(_workerScout, destination);
					}
				}
			}
			else
			{
				// otherwise keep moving to the enemy region
				_scoutStatus = "Enemy region known, going there";
				// move to the enemy region
				followPerimeter();
			}
		} 
		else 
		{
			int scoutDistanceToEnemy = MapTools::Instance().getGroundDistance(_workerScout->getPosition(), enemyBaseLocation->getPosition());
			bool scoutInRangeOfenemy = scoutDistanceToEnemy <= scoutDistanceThreshold;

			// we only care if the scout is under attack within the enemy region
			// this ignores if their scout worker attacks it on the way to their base
			if (scoutHP < _previousScoutHP)
			{
				_scoutUnderAttack = true;
			}

			if (!_workerScout->isUnderAttack() && !enemyWorkerInRadius())
			{
				_scoutUnderAttack = false;
			}

			// if the scout is in the enemy region
			if (scoutInRangeOfenemy)
			{
				// get the closest enemy worker
				BWAPI::Unit closestWorker = closestEnemyWorker();

				// if the worker scout is not under attack
				if (!_scoutUnderAttack)
				{
					// if there is a worker nearby, harass it
					if (Config::Strategy::ScoutHarassEnemy && (!Config::Strategy::GasStealWithScout || _gasStealFinished) && closestWorker && (_workerScout->getDistance(closestWorker) < 800))
					{
						_scoutStatus = "Harass enemy worker";
						_currentRegionVertexIndex = -1;
						Micro::SmartAttackUnit(_workerScout, closestWorker);
					}
					// otherwise keep moving to the enemy region
					else
					{
						_scoutStatus = "Following perimeter";
						followPerimeter();
					}

				}
				// if the worker scout is under attack
				else
				{
					_scoutStatus = "Under attack inside, fleeing";
					followPerimeter();
				}
			}
			// if the scout is not in the enemy region
			else if (_scoutUnderAttack)
			{
				_scoutStatus = "Under attack inside, fleeing";

				followPerimeter();
			}
			else
			{
				_scoutStatus = "Enemy region known, going there";

				// move to the enemy region
				followPerimeter();
			}
		}
	}

	// for each start location in the level
	if (!enemyBaseLocation)
	{
        _scoutStatus = "Enemy base unknown, exploring";

		for (BWTA::BaseLocation * startLocation : BWTA::getStartLocations()) 
		{
			// if we haven't explored it yet
			if (!BWAPI::Broodwar->isExplored(startLocation->getTilePosition())) 
			{
				// assign a zergling to go scout it
				Micro::SmartMove(_workerScout, BWAPI::Position(startLocation->getTilePosition()));			
				return;
			}
		}
	}

    _previousScoutHP = scoutHP;
}

void ScoutManager::followPerimeter()
{
    BWAPI::Position fleeTo = getFleePosition();

    if (Config::Debug::DrawScoutInfo)
    {
        BWAPI::Broodwar->drawCircleMap(fleeTo, 5, BWAPI::Colors::Red, true);
    }

	Micro::SmartMove(_workerScout, fleeTo);
}

void ScoutManager::gasSteal()
{
    if (!Config::Strategy::GasStealWithScout)
    {
        _gasStealStatus = "Not using gas steal";
        return;
    }

    if (_didGasSteal)
    {
        return;
    }

    if (!_workerScout)
    {
        _gasStealStatus = "No worker scout";
        return;
    }

    BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
    if (!enemyBaseLocation)
    {
        _gasStealStatus = "No enemy base location found";
        return;
    }

    BWAPI::Unit enemyGeyser = getEnemyGeyser();
    if (!enemyGeyser)
    {
        _gasStealStatus = "No enemy geyser found";
        false;
    }

    if (!_didGasSteal)
    {
        ProductionManager::Instance().queueGasSteal();
        _didGasSteal = true;
        Micro::SmartMove(_workerScout, enemyGeyser->getPosition());
        _gasStealStatus = "Did Gas Steal";
    }
}

BWAPI::Unit ScoutManager::closestEnemyWorker()
{
	BWAPI::Unit enemyWorker = nullptr;
	double maxDist = 0;

	
	BWAPI::Unit geyser = getEnemyGeyser();
	
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker() && unit->isConstructing())
		{
			return unit;
		}
	}

	// for each enemy worker
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker())
		{
			double dist = unit->getDistance(geyser);

			if (dist < 800 && dist > maxDist)
			{
				maxDist = dist;
				enemyWorker = unit;
			}
		}
	}

	return enemyWorker;
}

BWAPI::Unit ScoutManager::getEnemyGeyser()
{
	BWAPI::Unit geyser = nullptr;
	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());

	for (auto & unit : enemyBaseLocation->getGeysers())
	{
		geyser = unit;
	}

	return geyser;
}

bool ScoutManager::enemyWorkerInRadius(int radius)
{
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker() && (unit->getDistance(_workerScout) < radius))
		{
			return true;
		}
	}

	return false;
}

bool ScoutManager::immediateThreat()
{
	BWAPI::Unitset enemyAttackingWorkers;
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isWorker() && unit->isAttacking())
		{
			enemyAttackingWorkers.insert(unit);
		}
	}
	
	if (_workerScout->isUnderAttack())
	{
		return true;
	}

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		double dist = unit->getDistance(_workerScout);
		double range = unit->getType().groundWeapon().maxRange();

		if (unit->getType().canAttack() && !unit->getType().isWorker() && (dist <= range + 32))
		{
			return true;
		}
	}

	return false;
}

int ScoutManager::getClosestVertexIndex(BWAPI::Unit unit)
{
    int closestIndex = -1;
    double closestDistance = 10000000;

    for (size_t i(0); i < _enemyRegionVertices.size(); ++i)
    {
        double dist = unit->getDistance(_enemyRegionVertices[i]);
        if (dist < closestDistance)
        {
            closestDistance = dist;
            closestIndex = i;
        }
    }

    return closestIndex;
}

BWAPI::Position ScoutManager::getFleePosition()
{
    UAB_ASSERT_WARNING(!_enemyRegionVertices.empty(), "We should have an enemy region vertices if we are fleeing");
    
    BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());

    // if this is the first flee, we will not have a previous perimeter index
    if (_currentRegionVertexIndex == -1)
    {
        // so return the closest position in the polygon
        int closestPolygonIndex = getClosestVertexIndex(_workerScout);

        UAB_ASSERT_WARNING(closestPolygonIndex != -1, "Couldn't find a closest vertex");

        if (closestPolygonIndex == -1)
        {
            return BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
        }
        else
        {
            // set the current index so we know how to iterate if we are still fleeing later
            _currentRegionVertexIndex = closestPolygonIndex;
            return _enemyRegionVertices[closestPolygonIndex];
        }
    }
    // if we are still fleeing from the previous frame, get the next location if we are close enough
    else
    {
        double distanceFromCurrentVertex = _enemyRegionVertices[_currentRegionVertexIndex].getDistance(_workerScout->getPosition());

        // keep going to the next vertex in the perimeter until we get to one we're far enough from to issue another move command
        while (distanceFromCurrentVertex < 128)
        {
            _currentRegionVertexIndex = (_currentRegionVertexIndex + 1) % _enemyRegionVertices.size();

            distanceFromCurrentVertex = _enemyRegionVertices[_currentRegionVertexIndex].getDistance(_workerScout->getPosition());
        }

        return _enemyRegionVertices[_currentRegionVertexIndex];
    }

}

void ScoutManager::calculateEnemyRegionVertices()
{
    BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
    //UAB_ASSERT_WARNING(enemyBaseLocation, "We should have an enemy base location if we are fleeing");

    if (!enemyBaseLocation)
    {
        return;
    }

    BWTA::Region * enemyRegion = enemyBaseLocation->getRegion();
    //UAB_ASSERT_WARNING(enemyRegion, "We should have an enemy region if we are fleeing");

    if (!enemyRegion)
    {
        return;
    }

    const BWAPI::Position basePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
    const std::vector<BWAPI::TilePosition> & closestTobase = MapTools::Instance().getClosestTilesTo(basePosition);

    std::set<BWAPI::Position> unsortedVertices;

    // check each tile position
    for (size_t i(0); i < closestTobase.size(); ++i)
    {
        const BWAPI::TilePosition & tp = closestTobase[i];

        if (BWTA::getRegion(tp) != enemyRegion)
        {
            continue;
        }

        // a tile is 'surrounded' if
        // 1) in all 4 directions there's a tile position in the current region
        // 2) in all 4 directions there's a buildable tile
        bool surrounded = true;
        if (BWTA::getRegion(BWAPI::TilePosition(tp.x+1, tp.y)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x+1, tp.y))
            || BWTA::getRegion(BWAPI::TilePosition(tp.x, tp.y+1)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x, tp.y+1))
            || BWTA::getRegion(BWAPI::TilePosition(tp.x-1, tp.y)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x-1, tp.y))
            || BWTA::getRegion(BWAPI::TilePosition(tp.x, tp.y-1)) != enemyRegion || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x, tp.y -1))) 
        { 
            surrounded = false; 
        }
        
        // push the tiles that aren't surrounded
        if (!surrounded && BWAPI::Broodwar->isBuildable(tp))
        {
            if (Config::Debug::DrawScoutInfo)
            {
                int x1 = tp.x * 32 + 2;
                int y1 = tp.y * 32 + 2;
                int x2 = (tp.x+1) * 32 - 2;
                int y2 = (tp.y+1) * 32 - 2;
        
                BWAPI::Broodwar->drawTextMap(x1+3, y1+2, "%d", MapTools::Instance().getGroundDistance(BWAPI::Position(tp), basePosition));
                BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Green, false);
            }
            
            unsortedVertices.insert(BWAPI::Position(tp) + BWAPI::Position(16, 16));
        }
    }


    std::vector<BWAPI::Position> sortedVertices;
    BWAPI::Position current = *unsortedVertices.begin();

    _enemyRegionVertices.push_back(current);
    unsortedVertices.erase(current);

    // while we still have unsorted vertices left, find the closest one remaining to current
    while (!unsortedVertices.empty())
    {
        double bestDist = 1000000;
        BWAPI::Position bestPos;

        for (const BWAPI::Position & pos : unsortedVertices)
        {
            double dist = pos.getDistance(current);

            if (dist < bestDist)
            {
                bestDist = dist;
                bestPos = pos;
            }
        }

        current = bestPos;
        sortedVertices.push_back(bestPos);
        unsortedVertices.erase(bestPos);
    }

    // let's close loops on a threshold, eliminating death grooves
    int distanceThreshold = 100;

    while (true)
    {
        // find the largest index difference whose distance is less than the threshold
        int maxFarthest = 0;
        int maxFarthestStart = 0;
        int maxFarthestEnd = 0;

        // for each starting vertex
        for (int i(0); i < (int)sortedVertices.size(); ++i)
        {
            int farthest = 0;
            int farthestIndex = 0;

            // only test half way around because we'll find the other one on the way back
            for (size_t j(1); j < sortedVertices.size()/2; ++j)
            {
                int jindex = (i + j) % sortedVertices.size();
            
                if (sortedVertices[i].getDistance(sortedVertices[jindex]) < distanceThreshold)
                {
                    farthest = j;
                    farthestIndex = jindex;
                }
            }

            if (farthest > maxFarthest)
            {
                maxFarthest = farthest;
                maxFarthestStart = i;
                maxFarthestEnd = farthestIndex;
            }
        }
        
        // stop when we have no long chains within the threshold
        if (maxFarthest < 4)
        {
            break;
        }

        double dist = sortedVertices[maxFarthestStart].getDistance(sortedVertices[maxFarthestEnd]);

        std::vector<BWAPI::Position> temp;

        for (size_t s(maxFarthestEnd); s != maxFarthestStart; s = (s+1) % sortedVertices.size())
        {
            temp.push_back(sortedVertices[s]);
        }

        sortedVertices = temp;
    }

    _enemyRegionVertices = sortedVertices;
}

BWAPI::Unit ScoutManager::getWorkerScout() 
{
	return _workerScout;
}

bool ScoutManager::isCannonRushReady() {
	return _cannonRushReady;
}

bool ScoutManager::isCannonRushDone() {
	return _cannonRushDone;
}

bool ScoutManager::isNextProbeScout() {
	return _nextProbeIsScout;
}

void ScoutManager::setNextProbeScout(bool isScout) {
	_nextProbeIsScout = isScout;
}

BWAPI::TilePosition ScoutManager::getChokepoint() {
	return _cannonRushChokepoint;
}

BWAPI::TilePosition ScoutManager::getChokepointCloser() {
	return _cannonRushChokepointCloser;
}