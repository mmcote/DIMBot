#include "SquadData.h"

using namespace UAlbertaBot;

BWAPI::Unit SquadData::baitPreventionUnit = NULL;
BWAPI::Unit SquadData::baitUnitSent = NULL;
std::vector<BWAPI::Position> SquadData::vertices;
BWTA::Region * SquadData::baitRegion = nullptr;
int SquadData::gameStartTime = NULL;
int SquadData::_currentRegionVertexIndex = -1;
bool SquadData::baitSent = false;
bool SquadData::mainAttackSqaudSent = false;
bool SquadData::baitMode = false;
bool SquadData::reachedBaitRegion = false;
int SquadData::priorHealth = NULL;

SquadData::SquadData() 
{
	if (gameStartTime == NULL) {
		gameStartTime = 0;
	}
}

void SquadData::update()
{
	updateAllSquads();
    verifySquadUniqueMembership();
}

void SquadData::clearSquadData()
{
    // give back workers who were in squads
    for (auto & kv : _squads)
	{
        Squad & squad = kv.second;

        const BWAPI::Unitset & units = squad.getUnits();

        for (auto & unit : units)
        {
            if (unit->getType().isWorker())
            {
                WorkerManager::Instance().finishedWithWorker(unit);
            }
        }
	}

	baitPreventionUnit = NULL;
	_squads.clear();
}

void SquadData::removeSquad(const std::string & squadName)
{
    auto & squadPtr = _squads.find(squadName);

    UAB_ASSERT_WARNING(squadPtr != _squads.end(), "Trying to clear a squad that didn't exist: %s", squadName.c_str());
    if (squadPtr == _squads.end())
    {
        return;
    }

    for (auto & unit : squadPtr->second.getUnits())
    {
        if (unit->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(unit);
        }
    }

    _squads.erase(squadName);
}

const std::map<std::string, Squad> & SquadData::getSquads() const
{
    return _squads;
}

bool SquadData::squadExists(const std::string & squadName)
{
    return _squads.find(squadName) != _squads.end();
}

void SquadData::addSquad(const std::string & squadName, const Squad & squad)
{
	_squads[squadName] = squad;
}

std::vector<BWAPI::Unit> grabEnemiesInNeutralZone(int radius) {
	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
	auto enemyBasePosition = enemyBaseLocation->getPosition();

	/* Grab enemy units information */
	InformationManager infoManager = InformationManager::Instance();

	// Enemy's unit info from InformationManager
	const auto & enemyUnitInfo = infoManager.getUnitInfo(BWAPI::Broodwar->enemy());

	// Create vector of enemy units 
	// - That are a valid combat unit
	// - Are currently visible to our units
	// - Not in the final attack area (Targeting the bait situation in the "neutral" zone)
	std::vector<BWAPI::Unit> validEnemyUnits;
	for (auto & enemyUnit : enemyUnitInfo) {
		if (infoManager.isCombatUnit(enemyUnit.second.type) &&
			enemyUnit.second.unit->isVisible(BWAPI::Broodwar->self()) &&
			enemyBasePosition.getDistance(enemyUnit.second.lastPosition) > radius) {
			validEnemyUnits.push_back(enemyUnit.second.unit);
		}
	}
	return validEnemyUnits;
}

std::vector<BWAPI::Unit> grabAlliesInNeutralZone(BWAPI::Unitset ourUnits, int radius) {
	BWTA::BaseLocation * homeBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->self());
	auto homeBasePosition = homeBaseLocation->getPosition();

	/* -- Grab the information of both enemy and our own units -- */
	std::vector<BWAPI::Unit> squadUnits;
	for (auto & u : ourUnits) {
		if (homeBasePosition.getDistance(u->getPosition()) > radius) {
			squadUnits.push_back(u);
		}
	}
	return squadUnits;
}

bool checkIfInEnemyBase(BWAPI::Unitset ourUnits, int radius) {
	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
	auto enemyBasePosition = enemyBaseLocation->getPosition();

	/* Check if any of our units are in the enemy attack zone, if so then
	we do not have to worry about a bait tactic, just attack the base since
	there baiting technique is not working */
	for (auto & u : ourUnits) {
		if (enemyBasePosition.getDistance(u->getPosition()) < radius) {
			return true;
			break;
		}
	}
	return false;
}


/* splitUnits function is a filter that splits enemy units into allies and
enemies within a specific are based off of prior filter results in a larger
area. (For example in the case of the Neutral Zone Attack Squad, first we 
found all of the corresponding units in the Neutral Zone then now it
can be used to filter specific units within a certain radius around the 
foremost unit unitClosest).
*/
std::pair<std::vector<BWAPI::Unit>, std::vector<BWAPI::Unit> > splitUnits( BWAPI::Unit unitClosest, 
	int radius, std::vector<BWAPI::Unit> validEnemyUnits, std::vector<BWAPI::Unit> validOwnUnits) {
	BWAPI::Unitset unitsNear = unitClosest->getUnitsInRadius(radius);
	std::vector<BWAPI::Unit> enemyUnits;
	std::vector<BWAPI::Unit> ownUnits;
	ownUnits.push_back(unitClosest);
	for (auto & unit : unitsNear) {
		// filter out enemy units near by
		if (std::find(validEnemyUnits.begin(), validEnemyUnits.end(), unit) != validEnemyUnits.end()) {
			enemyUnits.push_back(unit);
		}
		else if (std::find(validOwnUnits.begin(), validOwnUnits.end(), unit) != validOwnUnits.end()) {
			ownUnits.push_back(unit);
		}
	}

	return std::make_pair(enemyUnits, ownUnits);
}

/* updateNeutralZoneAttackSquad is used to update the NeutralZoneAttackSquad
with each frame. The reason this is separate from the squad update function
is due to the fact that the squad update function is based on the type of 
attack squad one is. Whereas the Neutral Zone Attack Squad is built from the 
Main Attack Squad and will be reassigned once there job is accomplished.
Therefore there update is much different.
*/
void SquadData::updateNeutralZoneAttackSquad() {
// TODO: If there is already an Neutral Zone Attack Squad how should we add to
// it if we have to?
	Squad *neutralZoneAttackSquad = &getSquad("NeutralZoneAttack");
	// if there are no units then just return
	if (neutralZoneAttackSquad->getUnits().size() <= 0) {
		return;
	}
	// -- remove all dead units and reassign to the mainAttackSquad
	neutralZoneAttackSquad->setAllUnits();
	BWAPI::Unitset nZASUnits = neutralZoneAttackSquad->getUnits();
	if (nZASUnits.size() <= 0) {
		baitPreventionUnit = NULL;
	}
	// check if the target unit is still alive
	if (baitPreventionUnit != NULL && (baitPreventionUnit->getHitPoints() <= 0 || !baitPreventionUnit->exists())) {
		// release neutralZoneAttackSquad units to the mainAttackSquad
		// clean up the _units vector just in case one of them died
		baitPreventionUnit = NULL;
		Squad *mainAttackSquad = &getSquad("MainAttack");
		for (auto & unit : nZASUnits) {
			mainAttackSquad->addUnit(unit);
		}
		neutralZoneAttackSquad->clear();
	}
}

std::vector<BWAPI::Position> calculateBaitRegionVertices(BWTA::Region * region)
{
	const BWAPI::Position basePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
	const std::vector<BWAPI::TilePosition> & closestTobase = MapTools::Instance().getClosestTilesTo(basePosition);

	std::set<BWAPI::Position> unsortedVertices;

	// check each tile position
	for (size_t i(0); i < closestTobase.size(); ++i)
	{
		const BWAPI::TilePosition & tp = closestTobase[i];

		if (BWTA::getRegion(tp) != region)
		{
			continue;
		}

		// a tile is 'surrounded' if
		// 1) in all 4 directions there's a tile position in the current region
		// 2) in all 4 directions there's a buildable tile
		bool surrounded = true;
		if (BWTA::getRegion(BWAPI::TilePosition(tp.x + 1, tp.y)) != region || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x + 1, tp.y))
			|| BWTA::getRegion(BWAPI::TilePosition(tp.x, tp.y + 1)) != region || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x, tp.y + 1))
			|| BWTA::getRegion(BWAPI::TilePosition(tp.x - 1, tp.y)) != region || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x - 1, tp.y))
			|| BWTA::getRegion(BWAPI::TilePosition(tp.x, tp.y - 1)) != region || !BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(tp.x, tp.y - 1)))
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
				int x2 = (tp.x + 1) * 32 - 2;
				int y2 = (tp.y + 1) * 32 - 2;

				BWAPI::Broodwar->drawTextMap(x1 + 3, y1 + 2, "%d", MapTools::Instance().getGroundDistance(BWAPI::Position(tp), basePosition));
				BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Green, false);
			}

			unsortedVertices.insert(BWAPI::Position(tp) + BWAPI::Position(16, 16));
		}
	}


	std::vector<BWAPI::Position> sortedVertices;
	BWAPI::Position current = *unsortedVertices.begin();
	std::vector<BWAPI::Position> resultingVertices;
	resultingVertices.push_back(current);
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
			for (size_t j(1); j < sortedVertices.size() / 2; ++j)
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

		for (size_t s(maxFarthestEnd); s != maxFarthestStart; s = (s + 1) % sortedVertices.size())
		{
			temp.push_back(sortedVertices[s]);
		}

		sortedVertices = temp;
	}

	resultingVertices = sortedVertices;
	return resultingVertices;
}

int SquadData::getClosestVertexIndex(BWAPI::Unit unit)
{
	int closestIndex = -1;
	double closestDistance = 10000000;

	for (size_t i(0); i < vertices.size(); ++i)
	{
		double dist = unit->getDistance(vertices[i]);
		if (dist < closestDistance)
		{
			closestDistance = dist;
			closestIndex = i;
		}
	}

	return closestIndex;
}

BWAPI::Position SquadData::getFleePosition()
{
	UAB_ASSERT_WARNING(!vertices.empty(), "We should have an enemy region vertices if we are fleeing");

	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());

	// if this is the first flee, we will not have a previous perimeter index
	if (_currentRegionVertexIndex == -1)
	{
		// so return the closest position in the polygon
		int closestPolygonIndex = getClosestVertexIndex(baitUnitSent);

		UAB_ASSERT_WARNING(closestPolygonIndex != -1, "Couldn't find a closest vertex");

		if (closestPolygonIndex == -1)
		{
			return BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
		}
		else
		{
			// set the current index so we know how to iterate if we are still fleeing later
			_currentRegionVertexIndex = closestPolygonIndex;
			return vertices[closestPolygonIndex];
		}
	}
	// if we are still fleeing from the previous frame, get the next location if we are close enough
	else
	{
		double distanceFromCurrentVertex = vertices[_currentRegionVertexIndex].getDistance(baitUnitSent->getPosition());

		// keep going to the next vertex in the perimeter until we get to one we're far enough from to issue another move command
		while (distanceFromCurrentVertex < 128)
		{
			_currentRegionVertexIndex = (_currentRegionVertexIndex + 1) % vertices.size();

			distanceFromCurrentVertex = vertices[_currentRegionVertexIndex].getDistance(baitUnitSent->getPosition());
		}

		return vertices[_currentRegionVertexIndex];
	}

}

void SquadData::followPerimeter()
{
	BWAPI::Position fleeTo = getFleePosition();

	if (Config::Debug::DrawScoutInfo)
	{
		BWAPI::Broodwar->drawCircleMap(fleeTo, 5, BWAPI::Colors::Red, true);
	}

	Micro::SmartMove(baitUnitSent, fleeTo);
}

void SquadData::updateBaitSquad(int releaseTime) {
	Squad *baitSquad = &getSquad("Bait");

	/* FIRST */
	/* If there are no units then just return */
	if (baitSquad->getUnits().size() <= 0) {
		return;
	}
	/* SECOND */
	/* Check if our bait unit is still alive */
	baitSquad->setAllUnits();
	
	/* THIRD */
	/* Release any units if the time has run out */
	BWAPI::Unitset ourUnits = baitSquad->getUnits();
	
	if (ourUnits.size() <= 0) {
		baitSent = false;
	}

	/* FOURTH */
	/* Update for when we are still within the bait time limit */
	if (BWAPI::Broodwar->getFrameCount() - gameStartTime < releaseTime) {
		int enemyBaseRadius = 1050;
		BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
		auto enemyBasePosition = enemyBaseLocation->getPosition();
		BWTA::Region * enemyRegion = BWTA::getRegion(enemyBasePosition);
		///* Stop the enemy unit at the edge of the enemy base and then wait to be attacked (CHECK IF COMBAT UNIT) */
		//if (baitUnitSent->isMoving() && enemyBasePosition.getDistance(baitUnitSent->getPosition()) < enemyBaseRadius) {
		//	baitUnitSent->stop();
		//	baitMode = true;
		//}
		
		// TODO: Do not just send to new region everytime hit, only do once
		// and then don't do again, one attempt at baiting the enemy
		int currentHealth = baitUnitSent->getHitPoints();
		if (priorHealth - baitUnitSent->getHitPoints() > 0 && !baitMode) {
			BWAPI::Position currentBaitUnitPosition = baitUnitSent->getPosition();
			BWTA::Region * region = BWTA::getRegion(currentBaitUnitPosition);

			std::vector<BWTA::Region *> surroundingRegions;
			for (auto region : region->getReachableRegions()) {
				surroundingRegions.push_back(region);
			}
			baitRegion = surroundingRegions[0];
			for (auto region : surroundingRegions) {
				int tempSize = calculateBaitRegionVertices(region).size();
				int temp = abs(currentBaitUnitPosition.y - region->getCenter().y);
				if ((abs(currentBaitUnitPosition.x - region->getCenter().x) >
					abs(currentBaitUnitPosition.x - baitRegion->getCenter().x)) &&
					(abs(currentBaitUnitPosition.y - region->getCenter().y) < 1000) &&
					calculateBaitRegionVertices(region).size() >= 70 && 
					region != enemyRegion) {
					baitRegion = region;
				}
			}
			
			baitMode = true;
			baitUnitSent->move(baitRegion->getCenter());
			vertices = calculateBaitRegionVertices(baitRegion);
		}

		/* check if the unit has reached the bait region */
		if (baitRegion != nullptr && !reachedBaitRegion && baitUnitSent->getPosition() == baitRegion->getCenter()) {
			reachedBaitRegion = true;
		}		
	/* FIFTH */
	/* Update for when the bait time has run out */
	/* Give an extra 500 frames to let the rest of the mainAttackSquad catch up to
	the enemy base */
	} else if (BWAPI::Broodwar->getFrameCount() - gameStartTime > releaseTime + 500) {
		/* Release bait unit to the mainAttackSquad */
		Squad *mainAttackSquad = &getSquad("MainAttack");
		
		for (auto & unit : ourUnits) {
			mainAttackSquad->addUnit(unit);
		}

		baitSquad->clear();
		baitMode = false;
	}

	/* if the unit has reached the bait region then start circling */
	if (reachedBaitRegion) {
		followPerimeter();
	}

	/* Update the priorHealth if hit or regenrated */
	if (baitSquad->getUnits().size() > 0) {
		priorHealth = baitUnitSent->getHitPoints();
	}
}

void SquadData::updateAllSquads()
{
	for (auto & kv : _squads)
	{
		if (Config::Strategy::BaitEnemy) {
			/* Define when the mainAttackSquad should be released */
			int releaseMainAttackSquad = 8000; // frames (20 frames/sec)

			/* Update the bait squad */
			if (!std::strcmp(kv.second.getName().c_str(), "Bait")) {
				updateBaitSquad(releaseMainAttackSquad);
				continue;
			}

			/* Only control hold the mainAttackSquad for the specified amount of frames */
			if (BWAPI::Broodwar->getFrameCount() - gameStartTime < releaseMainAttackSquad &&
				!std::strcmp(kv.second.getName().c_str(), "MainAttack") &&
				kv.second.getUnits().size() != 0) {
				if (!baitSent) {
					/* Grab the bait squad*/
					Squad *baitSquad = &getSquad("Bait");


					/* Size of radius to find enemies within */
					int radius = 1000;

					/* Grab our own units information */
					BWAPI::Unitset ourUnits = kv.second.getUnits();

					/* Grab all of our units */
					std::vector<BWAPI::Unit> squadUnits = grabAlliesInNeutralZone(ourUnits, radius);
					std::vector<BWAPI::Unit> allUnits;
					for (auto & unit : ourUnits) {
						allUnits.push_back(unit);
					}

					/* Definitely shouldn't be any of our units in the enemy base, but check anyways */
					bool alreadyInEnemyBase = checkIfInEnemyBase(ourUnits, radius);

					/* Only if the bait wasn't sent yet and there are no units in the enemy base */
					if (!alreadyInEnemyBase) {
						BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
						auto enemyBasePosition = enemyBaseLocation->getPosition();
						baitUnitSent = allUnits[0];

						/* Health of unit before goinng on bait mission */
						priorHealth = baitUnitSent->getHitPoints();

						/* Add the unit to the bait squad and remove from the mainAttackSquad */
						baitSquad->addUnit(baitUnitSent);
						kv.second.removeUnit(baitUnitSent);

						/* Move towards the enemy base */
						Micro::SmartAttackMove(baitUnitSent, enemyBasePosition);

						/* Unit has been sent */
						baitSent = true;
					}
				}
				/* Skip updating the main attack squad since they are on hold until the required time */
				continue;
			}

			// TODO: let the Main Attack Squad attack the enemy base
				

			// next need to find a place to drag the unit to
			// next need to circle the area, trying to avoid dying
			/* next after a certain number have been collected, send the
			main attack squad to put on pressure */

			// put the main attack squad back with teh 
		}

		if (Config::Strategy::PreventBaiting) {
			/* Instead of calling the usual squad update we want to call the
			Neutral Zone Attack Squad to handle the current bait situation */
			if (!std::strcmp(kv.second.getName().c_str(), "NeutralZoneAttack")) {
				updateNeutralZoneAttackSquad();
				continue;
			}

			/* Prevent the MainAttack Squad from being baited to follow an
			individual combat unit dragging the main attack squad around and
			preventing our units from applying continuous pressure to the enemy */
			if (!std::strcmp(kv.second.getName().c_str(), "MainAttack") && kv.second.getUnits().size() != 0) {
				/* Size of enemy base radius, approximately */
				int eRadius = 1000;
				/* Grab our own units information */
				BWAPI::Unitset ourUnits = kv.second.getUnits();
				std::vector<BWAPI::Unit> squadUnits = grabAlliesInNeutralZone(ourUnits, eRadius);

				/* Check if any of our units are already in the enemy base, if so 
				forget about prevent baiting*/
				bool alreadyInEnemyBase = checkIfInEnemyBase(ourUnits, eRadius);
				
				/* Grab enemy units in the neutral zone */
				std::vector<BWAPI::Unit> validEnemyUnits = grabEnemiesInNeutralZone(eRadius);

				/* There will only be a baiting maneuver if there is much less enemy units in the
				neutral zone than in our main attack squad, make sure an enemy unit is near */
				if (!alreadyInEnemyBase && validEnemyUnits.size() != 0 && squadUnits.size() > validEnemyUnits.size()) {
					/* Take the frontmost unit */
					BWAPI::Unit unitClosest = kv.second.unitClosestToEnemy();
					UAB_ASSERT(unitClosest, "No unit close to enemy.");

					/* Start checking for the amount of enemy units in radius around the
					closest unit to determine if this is a possible baiting tactic */
					int baitRadius = 300;
					std::pair<std::vector<BWAPI::Unit>, std::vector<BWAPI::Unit> > enemyAllies =
						splitUnits(unitClosest, baitRadius, validEnemyUnits, squadUnits);
					int enemyCount = enemyAllies.first.size();
					int ourCount = enemyAllies.second.size();

					// TODO: If there is already an Neutral Zone Attack Squad how should we add to
					// it if we have to?

					std::stringstream ss;
					ss << enemyCount;
					std::string enemyCountString = ss.str();
					ss << ourCount;
					std::string friendlyCountString = ss.str();
					char result[100];   // array to hold the result.

					strcpy(result, "Enemy Count: ");
					strcat(result, enemyCountString.c_str());
					strcat(result, " Friendly Count: ");
					strcat(result, friendlyCountString.c_str());
					UAB_ASSERT_WARNING(false, result);

					/* Use the branching Neutral Zone Attack Squad to handle enemies in the neutral zone */
					Squad & neutralAttackSquad = getSquad("NeutralZoneAttack");

					if (enemyCount == 1 && ourCount >= 1 && neutralAttackSquad.getUnits().size() == 0) {
						/*
							The code between the lines of ~236 - 266, was originally how I intended to
							implement the decision of how many units will be assigned to the Neutral Zone
							Attack Squad.

							Plan: Was going to continually grab units within a certain radius of the closest
							unit and run the sparcraft simulation. Using the smallest radius to decide how
							many units were needed to defeat the lone bait unit. Hence smaller radius, less
							units needed to get rid of the bait unit.
							*/
						// first find out how many units are needed to defeat the bait unit with a sparcraft simulation
						/*
						bool win = false;
						int radius = 100;
						SparCraft::ScoreType score = 0;
						CombatSimulation sim;
						while (radius <= baitRadius) {
						sim.setCombatUnits(unitClosest->getPosition(), radius);
						score = sim.simulateCombat();
						if (score > 0) {
						win = true;
						break;
						} else {
						radius += 50;
						}
						}
						if (win) {
						// produce the minimum set of units needed to meet the sparcraft win
						std::pair<std::vector<BWAPI::Unit>, std::vector<BWAPI::Unit> > baitUnits =
						splitUnits(unitClosest, radius, enemyAllies.first, enemyAllies.second);

						// since the check ensures that the bait is a lone unit
						if (baitUnit == NULL) {
						baitUnit = baitUnits.first[0];
						}

						for (auto & unit : baitUnits.second) {
						neutralAttackSquad.addUnit(unit);
						kv.second.removeUnit(unit);

						unit->attack(baitUnit);
						}
						}
						*/

						// since the check ensures that the bait is a lone unit
						if (baitPreventionUnit == NULL) {
							baitPreventionUnit = enemyAllies.first[0];
						}

						// the following code will now be used to replace a unit if someone dies
						bool inValid = false;
						for (auto & unit : enemyAllies.second) {
							for (auto & currentNeutralZoneUnit : neutralAttackSquad.getUnits()) {
								// check that the possible neutral zone attack unit is not already in the N.Z.A.S.
								if (unit != currentNeutralZoneUnit) {
									inValid = true;
								}
							}
							// stop adding units to a squad if there is already two members, the max
							if (neutralAttackSquad.getUnits().size() > 2) {
								break;
							}
							// add the unit to the N.Z.A.S. and remove the unit from the main attack squad
							if (!inValid) {
								neutralAttackSquad.addUnit(unit);
								kv.second.removeUnit(unit);

								unit->attack(baitPreventionUnit);
							}
						}
					}
				}
			}
		}
		kv.second.update();
	}
}

void SquadData::drawSquadInformation(int x, int y) 
{
    if (!Config::Debug::DrawSquadInfo)
    {
        return;
    }

	BWAPI::Broodwar->drawTextScreen(x, y, "\x04Squads");
	BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04NAME");
	BWAPI::Broodwar->drawTextScreen(x+150, y+20, "\x04SIZE");
	BWAPI::Broodwar->drawTextScreen(x+200, y+20, "\x04LOCATION");

	int yspace = 0;

	for (auto & kv : _squads) 
	{
        const Squad & squad = kv.second;

		const BWAPI::Unitset & units = squad.getUnits();
		const SquadOrder & order = squad.getSquadOrder();

		BWAPI::Broodwar->drawTextScreen(x, y+40+((yspace)*10), "\x03%s", squad.getName().c_str());
		BWAPI::Broodwar->drawTextScreen(x+150, y+40+((yspace)*10), "\x03%d", units.size());
		BWAPI::Broodwar->drawTextScreen(x+200, y+40+((yspace++)*10), "\x03(%d,%d)", order.getPosition().x, order.getPosition().y);

		BWAPI::Broodwar->drawCircleMap(order.getPosition(), 10, BWAPI::Colors::Green, true);
        BWAPI::Broodwar->drawCircleMap(order.getPosition(), order.getRadius(), BWAPI::Colors::Red, false);
        BWAPI::Broodwar->drawTextMap(order.getPosition() + BWAPI::Position(0, 12), "%s", squad.getName().c_str());

        for (const BWAPI::Unit unit : units)
        {
            BWAPI::Broodwar->drawTextMap(unit->getPosition() + BWAPI::Position(0, 10), "%s", squad.getName().c_str());
        }
	}
}

void SquadData::verifySquadUniqueMembership()
{
    BWAPI::Unitset assigned;

    for (const auto & kv : _squads)
    {
        for (auto & unit : kv.second.getUnits())
        {
            if (assigned.contains(unit))
            {
                BWAPI::Broodwar->printf("Unit is in at least two squads: %s", unit->getType().getName().c_str());
            }

            assigned.insert(unit);
        }
    }
}

bool SquadData::unitIsInSquad(BWAPI::Unit unit) const
{
    return getUnitSquad(unit) != nullptr;
}

const Squad * SquadData::getUnitSquad(BWAPI::Unit unit) const
{
    for (const auto & kv : _squads)
    {
        if (kv.second.getUnits().contains(unit))
        {
            return &kv.second;
        }
    }

    return nullptr;
}

Squad * SquadData::getUnitSquad(BWAPI::Unit unit)
{
    for (auto & kv : _squads)
    {
        if (kv.second.getUnits().contains(unit))
        {
            return &kv.second;
        }
    }

    return nullptr;
}

void SquadData::assignUnitToSquad(BWAPI::Unit unit, Squad & squad)
{
    UAB_ASSERT_WARNING(canAssignUnitToSquad(unit, squad), "We shouldn't be re-assigning this unit!");

    Squad * previousSquad = getUnitSquad(unit);

    if (previousSquad)
    {
        previousSquad->removeUnit(unit);
    }

    squad.addUnit(unit);
}

bool SquadData::canAssignUnitToSquad(BWAPI::Unit unit, const Squad & squad) const
{
    const Squad * unitSquad = getUnitSquad(unit);

    // make sure strictly less than so we don't reassign to the same squad etc
    return !unitSquad || (unitSquad->getPriority() < squad.getPriority());
}

Squad & SquadData::getSquad(const std::string & squadName)
{
    UAB_ASSERT_WARNING(squadExists(squadName), "Trying to access squad that doesn't exist: %s", squadName);
    if (!squadExists(squadName))
    {
        int a = 10;
    }

    return _squads[squadName];
}

BWAPI::Unit SquadData::getBaitUnit() {
	return baitPreventionUnit;
}

BWAPI::Unit SquadData::getBaitUnitSent() {
	return baitPreventionUnit;
}

int	SquadData::getGameStartTime() {
	return gameStartTime;
}
bool SquadData::getBaitSent() {
	return baitSent;
}
bool SquadData::getMainAttackSquadSent() {
	return mainAttackSqaudSent;
}

