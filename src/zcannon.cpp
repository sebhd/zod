// TODO 3: Don't show 'eject' symbol mouse curser over cannons that are non-ejectable (e.g. on forts)

#include "zcannon.h"

ZSDL_Surface ZCannon::init_place[3];

ZCannon::ZCannon(ZTime *ztime_, ZSettings *zsettings_) : ZMannedObject(ztime_, zsettings_) {
	m_object_type = CANNON_OBJECT;
	selectable = true;
	can_be_sniped = true;
	ejectable = true;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
}

void ZCannon::Init() {
	int i;
	char filename_c[500];

	//load colors
	for (i = 0; i < 3; i++) {
		sprintf(filename_c, "assets/units/cannons/init-place_n%02d.png", i);
		init_place[i].LoadBaseImage(filename_c); // = IMG_Load_Error(filename_c);
	}
}

bool ZCannon::CanBeSniped() {

	// TODO 3: Understand why we need ejectable here
	return can_be_sniped && driver_info.size();
	//return can_be_sniped && driver_info.size() && ejectable;
}
