// TODO 2: Fix wrong experience star color for manned objects

#include "ZMannedObject.h"
#include "zrobot.h"
#include "rgrunt.h"
#include <iostream>

ZMannedObject::ZMannedObject(ZTime *ztime_, ZSettings *zsettings_) : ZObject(ztime_, zsettings_) {

	display_health_bar = true;
	driver_type = GRUNT;
	can_be_sniped = true;
	ejectable = true;
}

ZMannedObject::~ZMannedObject() {
	// TODO Auto-generated destructor stub
}

void ZMannedObject::AddDriver(int health, int experience) {
	driver_info_s new_driver;

	new_driver.health = health;
	new_driver.experience = experience;

	AddDriver(new_driver);
}

void ZMannedObject::AddDriver(driver_info_s new_driver) {
	new_driver.next_attack_time = 0;

	// IMPORTANT: Force re-generation of unit hover name with experience star:
	if (!driver_info.size()) {
		hover_name_star_img.Unload();
	}

	driver_info.push_back(new_driver);

	//drivers can mess with damage info
	ResetDamageInfo();
}



bool ZMannedObject::CanBeEntered() {
	if (owner != NULL_TEAM || IsDestroyed()) {
		return false;
	}

	return true;
}

bool ZMannedObject::CanBeSniped() {
	return can_be_sniped && driver_info.size();
}

void ZMannedObject::ClearDrivers() {
	driver_info.clear();

	hover_name_star_img.Unload();

	//drivers can mess with damage info
	ResetDamageInfo();
}

void ZMannedObject::CreateTeamData(char *&data, int &size) {

	object_team_packet packet_header;

	size = sizeof(object_team_packet) + (driver_info.size() * sizeof(driver_info_s));
	data = (char*) malloc(size);

	packet_header.ref_id = ref_id;
	packet_header.owner = owner;
	packet_header.driver_type = driver_type;
	packet_header.driver_amount = driver_info.size();

	memcpy(data, &packet_header, sizeof(object_team_packet));

	int shift_amt = sizeof(object_team_packet);
	for (vector<driver_info_s>::iterator i = driver_info.begin(); i != driver_info.end(); i++) {
		memcpy(data + shift_amt, &(*i), sizeof(driver_info_s));
		shift_amt += sizeof(driver_info_s);
	}

}


int ZMannedObject::DamageDriverHealth(int damage_amount) {

	if (!driver_info.size()) {
		return 0;
	}

	driver_info_s &the_driver = *driver_info.begin();

	if (the_driver.health <= 0) {
		return 0;
	}

	the_driver.health -= damage_amount;

	if (the_driver.health <= 0) {
		//we killed the driver

		//clear drivers
		ClearDrivers();

		//go to NULL_TEAM
		SetOwner(NULL_TEAM);
	}

	return 1;
}

float ZMannedObject::GetDamageChance() {

	// Any drivers onboard?
	if (!driver_info.size()) {
		return 0;
	}

	ZUnit_Settings& unitSettings = zsettings->GetUnitSettings(ROBOT_OBJECT, driver_type);
	return unitSettings.attack_damage_chance * GetSmartness();

}

int ZMannedObject::GetDriverHealth() {
	if (driver_info.size()) {
		return driver_info.begin()->health;
	} else {
		return 0;
	}
}

vector<driver_info_s>& ZMannedObject::GetDrivers() {
	return driver_info;
}

int ZMannedObject::GetDriverType() {
	return driver_type;
}

bool ZMannedObject::GetEjectable() {
	return ejectable;
}

unsigned char ZMannedObject::GetExperience() {
	if (driver_info.size()) {
		return driver_info.begin()->experience;
	} else {
		return 0;
	}
}

int ZMannedObject::GetSmartness() {

	if (driver_info.size()) {
		return (driver_info.begin()->experience + 1) * zsettings->GetUnitSettings(ROBOT_OBJECT, driver_type).smartness;
	} else {
		return 0;
	}

}

float ZMannedObject::GetSnipeChance() {

	// Any drivers onboard?
	if (!driver_info.size()) {
		return 0;
	}

	ZUnit_Settings& unitSettings = zsettings->GetUnitSettings(ROBOT_OBJECT, driver_type);
	return unitSettings.attack_snipe_chance * GetSmartness();

}


/**
 * Return health of first driver
 */

void ZMannedObject::SetAttackObject(ZObject *obj) {
	m_attacked_object = obj;

	if (m_attacked_object) {

		int tx, ty;
		m_attacked_object->GetCenterCords(tx, ty);

		int new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);

		if (new_dir != -1) {
			direction = new_dir;
		}
	}
}

void ZMannedObject::SetDriverType(int driver_type_) {
	driver_type = driver_type_;

	if (driver_type < 0)
		driver_type = 0;
	if (driver_type >= MAX_ROBOT_TYPES)
		driver_type = MAX_ROBOT_TYPES - 1;

//drivers can mess with damage info
	ResetDamageInfo();
}

void ZMannedObject::SetEjectable(bool ejectable_) {
	ejectable = ejectable_;
}

void ZMannedObject::SetExperience(unsigned char exp) {

	if (last_experience_gain_time + 10 > ztime->ztime) {
		std::cout << "Last experience gain was not long ago" << std::endl;
		return;
	}

	last_experience_gain_time = ztime->ztime;


	if (exp < 0)
		exp = 0;
	if (exp > 2)
		exp = 2;

	if (driver_info.size() && driver_info.begin()->experience != exp) {
		// Force update of hover name image with experience star:
		hover_name_star_img.Unload();

		for(unsigned int ii = 0; ii < driver_info.size(); ++ii) {
			driver_info[ii].experience = exp;
		}

		sflags.updated_experience = true;

	}
}

void ZMannedObject::SetInitialDrivers() {

	driver_type = GRUNT;

	ClearDrivers();

	if (owner != NULL_TEAM) {
		//AddDriver(GRUNT_MAX_HEALTH);
		AddDriver(zsettings->GetUnitSettings(ROBOT_OBJECT, GRUNT).health * MAX_UNIT_HEALTH, 0);

		// New add driver:
		//	NewAddDriver(RGrunt(ztime, zsettings));
	}
}

