#ifndef _ZVEHICLE_H_
#define _ZVEHICLE_H_

#include "ZMannedObject.h"

class ZVehicle: public ZMannedObject {
public:
	ZVehicle(ZTime *ztime_, ZSettings *zsettings_);


	static void Init();
	void DoDriverHitEffect();

	virtual bool CanBeRepaired();
	virtual void SetLidState(bool lid_open_);
	virtual bool GetLidState();
	virtual void ProcessServerLid();
	virtual void SignalLidShouldOpen();
	virtual void SignalLidShouldClose();
	virtual bool CanBeSniped();
	virtual void TryDropTracks();

protected:
	virtual void RecalcDirection();
	bool ShowDamaged();
	bool ShowPartiallyDamaged();
	virtual void ProcessLid();
	virtual void RenderLid(ZMap &the_map, SDL_Surface *dest, int &lx, int &ly, int &shift_x, int &shift_y);

	static ZSDL_Surface lid[MAX_ANGLE_TYPES][3];
	static ZSDL_Surface tank_robot[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][2];


	bool do_driver_hit_effect;
	bool moving;
	bool lid_open;
	bool has_lid;
	bool show_robot;
	bool do_open_lid;
	bool do_close_lid;

	int t_direction;
	int lid_i;
	int robot_i;
	int track_type;

	double next_lid_time;
	double next_open_lid_time;
	double next_close_lid_time;
};

#endif
