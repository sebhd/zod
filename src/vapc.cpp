#include "vapc.h"

using namespace COMMON;

const double turrent_time_int = 0.2;

ZSDL_Surface VAPC::base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VAPC::open[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][5];
ZSDL_Surface VAPC::top[MAX_ANGLE_TYPES];
ZSDL_Surface VAPC::wasted[MAX_TEAM_TYPES];

VAPC::VAPC(ZTime *ztime_, ZSettings *zsettings_) :
		ZVehicle(ztime_, zsettings_) {
	double &the_time = ztime->ztime;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
	hover_name = "APC";
	object_name = "apc";

	InitTypeId(m_object_type, APC);

	//object_id = APC;
	//move_speed = APC_SPEED;
	//max_health = APC_MAX_HEALTH;
	//health = max_health;

	t_direction = direction;
	move_i = rand() % 3;

	next_turrent_time = the_time;
	next_turrent_time += turrent_time_int;
}

void VAPC::Init() {
	int i, j, k;
	char filename_c[500];
	SDL_Surface *temp_surface;

	strcpy(filename_c, "assets/units/vehicles/apc/empty.png");
	temp_surface = IMG_Load_Error(filename_c);

	for (j = 0; j < MAX_ANGLE_TYPES; j++)
		for (k = 0; k < 3; k++)
			base[0][j][k] = temp_surface;

	ZSDL_FreeSurface(temp_surface);

	for (i = 1; i < MAX_TEAM_TYPES; i++)
		for (j = 0; j < MAX_ANGLE_TYPES; j++)
			for (k = 0; k < 3; k++) {
				sprintf(filename_c, "assets/units/vehicles/apc/base_%s_r%03d_n%02d.png", team_type_string[i].c_str(),
						ROTATION[j], k);
				//base[i][j][k].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
				ZTeam::LoadZSurface(i, base[ZTEAM_BASE_TEAM][j][k], base[i][j][k], filename_c);
			}

	for (i = 1; i < MAX_TEAM_TYPES; i++)
		for (j = 0; j < MAX_ANGLE_TYPES; j++)
			for (k = 0; k < 5; k++) {
				sprintf(filename_c, "assets/units/vehicles/apc/open_%s_r%03d_n%02d.png", team_type_string[i].c_str(),
						ROTATION[j], k);
				//open[i][j][k].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
				ZTeam::LoadZSurface(i, open[ZTEAM_BASE_TEAM][j][k], open[i][j][k], filename_c);
			}

	for (j = 0; j < MAX_ANGLE_TYPES; j++) {
		sprintf(filename_c, "assets/units/vehicles/apc/top_r%03d.png", ROTATION[j]);
		top[j].LoadBaseImage(filename_c); // = IMG_Load_Error(filename_c);
	}

	for (i = 1; i < MAX_TEAM_TYPES; i++) {
		sprintf(filename_c, "assets/units/vehicles/apc/wasted_%s.png", team_type_string[i].c_str());
		//wasted[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
		ZTeam::LoadZSurface(i, wasted[ZTEAM_BASE_TEAM], wasted[i], filename_c);
	}
	wasted[0] = wasted[1];
}


int VAPC::Process() {
	double &the_time = ztime->ztime;
	int &x = loc.x;
	int &y = loc.y;

	if (moving && the_time >= next_move_time) {
		move_i++;
		if (move_i >= 3)
			move_i = 0;

		next_move_time = the_time + (VEHICLE_MOVE_ANIM_SPEED * SpeedOffsetPercentInv());
	}

	if (the_time >= next_turrent_time) {
		next_turrent_time = the_time + turrent_time_int;

		t_direction++;
		if (t_direction >= MAX_ANGLE_TYPES
			) t_direction = 0;
	}

	//attacking
	if (m_attacked_object && driver_info.size() && !damage_is_missile) {
		int tx, ty;
		int sx, sy;
		int w, h;

		m_attacked_object->GetDimensionsPixel(w, h);

		//determine direction
		sx = 0;
		sy = 0;

		for (vector<driver_info_s>::iterator i = driver_info.begin(); i != driver_info.end(); i++) {
			if (the_time < i->next_attack_time)
				continue;

			//set next time
			i->next_attack_time = the_time + damage_int_time + (0.012 * (rand() % 10));

			//get attack location
			m_attacked_object->GetCords(tx, ty);

			tx += (rand() % w);
			ty += (rand() % h);

			//do the effect
			switch (driver_type) {
			case GRUNT:
			case PSYCHO:
			case SNIPER:
				if (effect_list)
					effect_list->push_back((ZEffect*) (new EBullet(ztime, owner, x + 16, y + 16, tx, ty)));
				ZSoundEngine::PlayWavRestricted(RIFLE_FIRE_SND, x, y, width_pix, height_pix);
				break;
			case PYRO:
				if (effect_list)
					effect_list->push_back((ZEffect*) (new EFlame(ztime, x + 16 + sx, y + 16 + sy, tx, ty)));
				ZSoundEngine::PlayWavRestricted(PYRO_FIRE_SND, x, y, width_pix, height_pix);
				break;
			case LASER:
				if (effect_list)
					effect_list->push_back((ZEffect*) (new ELaser(ztime, x + 16 + sx, y + 16 + sy, tx, ty)));
				ZSoundEngine::PlayWavRestricted(LASER_FIRE_SND, x, y, width_pix, height_pix);
				break;
			}
		}
	}

	return 1;
}

void VAPC::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y) {
	int &x = loc.x;
	int &y = loc.y;
	const int turrent_x[MAX_ANGLE_TYPES] = { 1, 5, 9, 13, 15, 11, 8, 5 };
	const int turrent_y[MAX_ANGLE_TYPES] = { 5, 8, 5, 8, 5, 3, 3, 4 };
	ZSDL_Surface *base_surface;
	SDL_Rect from_rect, to_rect;
	int lx, ly;

	if (destroyed)
		base_surface = &wasted[owner];
	else
		base_surface = &base[owner][direction][move_i];

	the_map.RenderZSurface(base_surface, x, y, do_hit_effect);
	//if(the_map.GetBlitInfo(base_surface, x, y, from_rect, to_rect))
	//{
	//	to_rect.x += shift_x;
	//	to_rect.y += shift_y;

	//	ZSDL_BlitHitSurface( base_surface, &from_rect, dest, &to_rect, do_hit_effect);
	//}

	if (owner != NULL_TEAM && !destroyed) {
		lx = x + turrent_x[direction];
		ly = y + turrent_y[direction];

		the_map.RenderZSurface(&top[t_direction], lx, ly, do_hit_effect);
		//if(the_map.GetBlitInfo(top[t_direction], lx, ly, from_rect, to_rect))
		//{
		//	to_rect.x += shift_x;
		//	to_rect.y += shift_y;

		//	ZSDL_BlitHitSurface(top[t_direction], &from_rect, dest, &to_rect, do_hit_effect);
		//}
	}

	do_hit_effect = false;
}

void VAPC::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y) {
	//int &x = loc.x;
	//int &y = loc.y;
	//SDL_Rect from_rect, to_rect;
	//int lx, ly;
}

void VAPC::DoDeathEffect(bool do_fire_death, bool do_missile_death) {
	if (effect_list)
		effect_list->insert(effect_list->begin(), (ZEffect*) (new EDeath(ztime, loc.x, loc.y, EDEATH_APC)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

void VAPC::FireMissile(int x_, int y_) {
	int &x = loc.x;
	int &y = loc.y;

	int sx, sy;
	int w, h;

	//just switched to firing?
	if (m_attacked_object) {
		sx = 0;
		sy = 0;

		if (effect_list)
			effect_list->push_back((ZEffect*) (new EToughRocket(ztime, x + 16 + sx, y + 16 + sy, x_, y_)));
		ZSoundEngine::PlayWavRestricted(TOUGH_FIRE_SND, x, y, width_pix, height_pix);

		m_attacked_object->GetDimensionsPixel(w, h);

		//add some effect for the extra toughs in the apc
		for (int i = 0; i < driver_info.size() - 1; i++) {

			//get attack location:
			int tx, ty;
			m_attacked_object->GetCords(tx, ty);

			tx += (rand() % w);
			ty += (rand() % h);

			if (effect_list) {
				effect_list->push_back((ZEffect*) (new EToughRocket(ztime, x + 16 + sx, y + 16 + sy, tx, ty)));
			}
			//ZSoundEngine::PlayWavRestricted(TOUGH_FIRE_SND, x, y, width_pix, height_pix);
		}
	}
}

/*
bool VAPC::GetEjectable() {
	return true;
}
*/

// TODO 4: Get rid of this
void VAPC::ResetDamageInfo() {
	//clear the info
	attack_radius = 0;
	damage = 0;
	//damage_chance = 0;
	damage_int_time = 0;
	damage_is_missile = false;
	damage_radius = 0;
	missile_speed = 0;
	has_explosives = false;

	// Any drivers onboard?
	if (!driver_info.size()) {
		return;
	}

	ZUnit_Settings &unit_settings = zsettings->GetUnitSettings(ROBOT_OBJECT, driver_type);

	attack_radius = unit_settings.attack_radius;
	damage = unit_settings.attack_damage * MAX_UNIT_HEALTH;
	//damage_chance = unit_settings.attack_damage_chance;

	damage_int_time = unit_settings.attack_speed;
	damage_radius = unit_settings.attack_damage_radius;
	missile_speed = unit_settings.attack_missile_speed;

	if (driver_type == TOUGH) {
		has_explosives = true;
		damage_is_missile = true;
	}

	//multiplyer
	damage *= driver_info.size();
}
