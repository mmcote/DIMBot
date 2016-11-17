#include "SquadData.h"

using namespace UAlbertaBot;

SquadData::SquadData() 
{
	
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

void SquadData::updateAllSquads()
{
	for (auto & kv : _squads)
	{
		if (kv.second.getName().c_str() == "NeutralZoneAttack") {
			continue;
		}
		// Prevent the MainAttack Squad from being baited to follow an individual combat unit 
		// dragging the our attack squad around preventing our units from applying continuous 
		// pressure to the enemy
		if (!std::strcmp(kv.second.getName().c_str(), "MainAttack") && kv.second.getUnits().size() != 0) {
			// Size of enemy base radius
			int eRadius = 1000;
			// First grab the information of both enemy and our own units
			// Grab our own units information 
			BWAPI::Unitset ourUnits = kv.second.getUnits();
			std::vector<std::pair<BWAPI::Unit, bool> > squadUnits;
			for (auto & u : ourUnits) { squadUnits.push_back(std::make_pair(u, false)); }

			// to retrieve information on enemy units
			InformationManager infoManager = InformationManager::Instance();

			BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
			auto enemyBasePosition = enemyBaseLocation->getPosition();
			

			// Check if any of the units are in the enemy attack zone, don't need to worry, everyone should
			// just attack the base since there baiting technique is not working
			bool alreadyInEnemyBase = false;
			for (auto & u : ourUnits) {
				if (enemyBasePosition.getDistance(u->getPosition()) < eRadius) {
					alreadyInEnemyBase = true;
				}
			}

			// Enemy's unit info, InformationManager only cares about combat units
			const auto & enemyUnitInfo = infoManager.getUnitInfo(BWAPI::Broodwar->enemy());

			// Create vector of enemy units 
			// -That are a valid combat unit
			// -Are currently visible to our units
			// -Not in the final attack area (Targeting the bait situation in the "neutral" zone)
			std::vector<BWAPI::Unit> validEnemyUnits;
			for (auto & enemyUnit : enemyUnitInfo) {
				if (infoManager.isCombatUnit(enemyUnit.second.type) &&
					enemyUnit.second.unit->isVisible(BWAPI::Broodwar->self()) &&
					enemyBasePosition.getDistance(enemyUnit.second.lastPosition) > eRadius) {
					validEnemyUnits.push_back(enemyUnit.second.unit);
				}
			}

			// There will only be a baiting maneuver if there is much less enemy units in the 
			// neutral zone than in our main attack squad, make sure there are enemy units near
			if (!alreadyInEnemyBase && squadUnits.size() > validEnemyUnits.size() && validEnemyUnits.size() != 0) {
				// Form a seperate squad in order to handle enemies in the neutral zone, then those who
				// are left should continue to pressure the enemy
				// Take the frontmost unit
				BWAPI::Unit unitClosest = kv.second.unitClosestToEnemy();
				UAB_ASSERT(unitClosest, "No unit close to enemy.");

				// Start checking for the amount of enemy units in radius around the closest unit to 
				// determine if this is a possible baiting tactic, (if the enemy is to bait us into following
				// them around the map then they will only send a very small amount of enemies).			
				BWAPI::Unitset unitsNear = unitClosest->getUnitsInRadius(150);
				
				int enemyCount = 0;
				int ourCount = 0;
				std::vector<BWAPI::Unit> enemyUnitsNear;
				std::vector<BWAPI::Unit> possibleNeutralZoneAttackSquad;
				for (auto & unit : unitsNear) {
					// filter out enemy units near by
					if (std::find(validEnemyUnits.begin(), validEnemyUnits.end(), unit) != validEnemyUnits.end()) {
						enemyUnitsNear.push_back(unit);
						enemyCount++;
					}
					else if (std::find(squadUnits.begin(), squadUnits.end(), std::make_pair(unit, false)) != squadUnits.end()) {
						possibleNeutralZoneAttackSquad.push_back(unit);
						ourCount++;
					}
				}
			
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
				/*UAB_ASSERT(false, result);*/

				// if there is only a lone enemy assign two of our units to attack it, as it may
				// just be a bait
				Squad & neutralAttackSquad = getSquad("NeutralZoneAttack");
				if (enemyCount == 1 && ourCount >= 1 && neutralAttackSquad.getUnits().size() <=2) {
					neutralAttackSquad.addUnit(possibleNeutralZoneAttackSquad[0]);
					kv.second.removeUnit(possibleNeutralZoneAttackSquad[0]);
					if (ourCount > 1) {
						neutralAttackSquad.addUnit(possibleNeutralZoneAttackSquad[1]);
						kv.second.removeUnit(possibleNeutralZoneAttackSquad[1]);
					}
					for (auto & neutralZoneUnit : neutralAttackSquad.getUnits()) {
						neutralZoneUnit->attack(enemyUnitsNear[0]);
					}
					for (auto & remainingUnit : kv.second.getUnits()) {
						Micro::SmartAttackMove(remainingUnit, enemyBasePosition);
					}
					continue;
				}
			}

			

			//if (validEnemyUnits.size() > 0) {
			//	// To test lets just assign units to a target nearby
			//	double closest;
			//	double tempClosest;
			//	std::pair<BWAPI::Unit, bool> *closestUnit;

			//	for (auto & enemyUnit : enemyUnitInfo) {
			//		closest = 10000000;
			//		if (std::find(validEnemyUnits.begin(), validEnemyUnits.end(), enemyUnit.second.unit) != validEnemyUnits.end()) {
			//			for (auto & squadUnit : squadUnits) {
			//				tempClosest = squadUnit.first->getPosition().getDistance(enemyUnit.second.lastPosition);
			//				if (tempClosest < closest && squadUnit.second == false) {
			//					closestUnit = &squadUnit;
			//					closest = tempClosest;
			//				}
			//			}
			//			Micro::SmartAttackUnit(closestUnit->first, enemyUnit.second.unit);
			//			closestUnit->second = true;
			//			//validEnemyUnits.erase(std::remove(validEnemyUnits.begin(), validEnemyUnits.end(), enemyUnit.second.unit), validEnemyUnits.end());
			//		}
			//	}

			//	// for the rest of the units still remaining, set to attack main target
			//	for (auto & ourUnit : ourUnits) {
			//		Micro::SmartAttackMove(ourUnit, enemyBasePosition);
			//	}
			//	return;
			//}
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