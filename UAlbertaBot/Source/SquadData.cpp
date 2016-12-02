#include "SquadData.h"

using namespace UAlbertaBot;

BWAPI::Unit SquadData::baitUnit = NULL;

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

	SquadData::baitUnit = NULL;
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
		baitUnit = NULL;
	}
	// check if the target unit is still alive
	if (baitUnit != NULL && (baitUnit->getHitPoints() <= 0 || !baitUnit->exists())) {
		// release neutralZoneAttackSquad units to the mainAttackSquad
		// clean up the _units vector just in case one of them died
		baitUnit = NULL;
		Squad *mainAttackSquad = &getSquad("MainAttack");
		for (auto & unit : nZASUnits) {
			mainAttackSquad->addUnit(unit);
		}
		neutralZoneAttackSquad->clear();
	}
}

void SquadData::updateAllSquads()
{
	for (auto & kv : _squads)
	{
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

			BWTA::BaseLocation * homeBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->self());
			auto homeBasePosition = homeBaseLocation->getPosition();

			/* -- Grab the information of both enemy and our own units -- */
			/* Grab our own units information */
			BWAPI::Unitset ourUnits = kv.second.getUnits();
			std::vector<BWAPI::Unit> squadUnits;
			for (auto & u : ourUnits) { 
				if (homeBasePosition.getDistance(u->getPosition()) > eRadius) {
					squadUnits.push_back(u);
				}
			}

			BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());
			auto enemyBasePosition = enemyBaseLocation->getPosition();

			/* Check if any of our units are in the enemy attack zone, if so then
			we do not have to worry about a bait tactic, just attack the base since 
			there baiting technique is not working */
			bool alreadyInEnemyBase = false;
			for (auto & u : ourUnits) {
				if (enemyBasePosition.getDistance(u->getPosition()) < eRadius) {
					alreadyInEnemyBase = true;
					break;
				}
			}

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
					enemyBasePosition.getDistance(enemyUnit.second.lastPosition) > eRadius) {
					validEnemyUnits.push_back(enemyUnit.second.unit);
				}
			}

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
				UAB_ASSERT(false, result);

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
					if (baitUnit == NULL) {
						baitUnit = enemyAllies.first[0];
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

							unit->attack(baitUnit);
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
	return SquadData::baitUnit;
}
