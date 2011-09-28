#include "zserver.h"
#include "zvehicle.h"
#include <iostream>

using namespace COMMON;

ZServer::ZServer() : ZCore() {
	int i;

	//setup randomizer
	srand(time(0));

	current_map_i = -1;
	next_ref_id = 0;
	game_on = false;
	next_end_game_check_time = 0;
	next_reset_game_time = 0;
	do_reset_game = false;
	next_scuffle_time = 0;
	next_make_suggestions_time = 0;

	//bots
	for (int i = 0; i < MAX_TEAM_TYPES; i++) {
		init_bot[i] = false;
		bot_thread[i] = NULL;
	}

	SetupEHandler();

	server_socket.SetEventHandler(&ehandler);
	server_socket.SetEventList(&ehandler.GetEventList());

	ZObject::SetDamageMissileList(&damage_missile_list);
}

ZServer::~ZServer() {
	KillAllBots();
}

void ZServer::CheckDestroyedBridge(ZObject *obj) {
	unsigned char ot, oid;
	double &the_time = ztime.ztime;

	if (!obj)
		return;

	obj->GetObjectID(ot, oid);

	if (!(ot == BUILDING_OBJECT && (oid == BRIDGE_VERT || oid == BRIDGE_HORZ)))
		return;

	//kill all the robots and vehicles on the bridge right now
	for (vector<ZObject*>::iterator i = object_list.begin(); i != object_list.end(); i++) {
		unsigned char iot, ioid;

		//some checks
		if ((*i)->IsDestroyed())
			continue;

		(*i)->GetObjectID(iot, ioid);

		//is it a robot or vehicle?
		if (!(iot == VEHICLE_OBJECT || iot == ROBOT_OBJECT))
			continue;

		//does it intersect with this bridge?
		if (!(*i)->IntersectsObject(*obj))
			continue;

		//destroy it then
		(*i)->SetHealth(0, zmap);

		RelayObjectDeath(*i);
	}
}

void ZServer::CheckEndGame() {
	double &the_time = ztime.ztime;

	if (!game_on)
		return;

	if (the_time >= next_end_game_check_time) {
		next_end_game_check_time = the_time + 1.0;

		if (EndGameRequirementsMet())
			ProcessEndGame();
	}
}

bool ZServer::CheckMapObject(map_object &m_obj) {
	//some checks
	if (m_obj.object_type < 0 || m_obj.object_type >= MAX_MAP_OBJECT_TYPES) {
		printf("ZServer::CheckMapObject:bad type\n");
		return false;
	}

	if (m_obj.object_id < 0) {
		printf("ZServer::CheckMapObject:unused id\n");
		return false;
	}

	switch (m_obj.object_type) {
	case BUILDING_OBJECT:
		if (m_obj.object_id >= MAX_BUILDING_TYPES) {
			printf("ZServer::CheckMapObject:bad id\n");
			return false;
		}
		break;
	case CANNON_OBJECT:
		if (m_obj.object_id >= MAX_CANNON_TYPES) {
			printf("ZServer::CheckMapObject:bad id\n");
			return false;
		}
		break;
	case VEHICLE_OBJECT:
		if (m_obj.object_id >= MAX_VEHICLE_TYPES) {
			printf("ZServer::CheckMapObject:bad id\n");
			return false;
		}
		break;
	case ROBOT_OBJECT:
		if (m_obj.object_id >= MAX_ROBOT_TYPES) {
			printf("ZServer::CheckMapObject:bad id\n");
			return false;
		}
		break;
	case MAP_ITEM_OBJECT:
		if (m_obj.object_id >= MAX_ITEM_TYPES) {
			printf("ZServer::CheckMapObject:bad id\n");
			return false;
		}
		break;
	default:
		printf("ZServer::CheckMapObject:unused id\n");
		break;
	}

	if (m_obj.owner < 0 || m_obj.owner >= MAX_TEAM_TYPES) {
		printf("ZServer::CheckMapObject:bad team\n");
		return false;
	}

	if (m_obj.x < 0 || m_obj.y < 0) {
		printf("ZServer::CheckMapObject:bad coordinate\n");
		return false;
	}

	if (m_obj.object_type == BUILDING_OBJECT && (m_obj.blevel < 0 || m_obj.blevel >= MAX_BUILDING_LEVELS)) {
		printf("ZServer::CheckMapObject:bad blevel\n");
		return false;
	}

	return true;
}

void ZServer::CheckResetGame() {
	if (game_on)
		return;
	if (!map_list.size())
		return;
	if (current_time() < next_reset_game_time)
		return;

	DoResetGame();
}

ZObject *ZServer::CreateObject(unsigned char ot, unsigned char oid, int x, int y, int owner, vector<ZObject*> *obj_list,
		int blevel, unsigned short extra_links, int health_percent) {

	int refId = next_ref_id++;

	//checks
	if (!CreateObjectOk(ot, oid, x, y, owner, blevel, extra_links)) {
		return NULL;
	}

	bool setOwner = false;

	ZObject *new_object_ptr = MakeNewObject(health_percent, blevel, refId, x, y, owner, ot, oid, extra_links, setOwner,
			obj_list);

	//should work here
	ProcessChangeObjectAmount();

	return new_object_ptr;
}

ZObject *ZServer::CreateRobotGroup(unsigned char oid, int x, int y, int owner, vector<ZObject*> *obj_list,
		int robot_amount, int health_percent) {
	//int robot_amount;
	int i;
	ZObject *leader_obj;
	ZObject *minion_obj;

	//simple check
	if (oid >= MAX_ROBOT_TYPES)
		return NULL;

	if (robot_amount == 0)
		return NULL;

	if (robot_amount < 0)
		robot_amount = zsettings.GetUnitSettings(ROBOT_OBJECT, oid).group_amount;

	//create leader
	leader_obj = CreateObject(ROBOT_OBJECT, oid, x, y, owner, obj_list, 0, 0, health_percent);
	robot_amount--;

	if (!leader_obj)
		return NULL;

	//create minions
	for (i = 0; i < robot_amount; i++) {
		minion_obj = CreateObject(ROBOT_OBJECT, oid, x, y, owner, obj_list, 0, 0, health_percent);

		if (minion_obj) {
			leader_obj->AddGroupMinion(minion_obj);
			minion_obj->SetGroupLeader(leader_obj);
		}
	}

	return leader_obj;
}

void ZServer::DoResetGame(string map_name) {
	//load next map
	LoadNextMap(map_name);

	//tell everyone the game has reset
	BroadCastNews("A new game has started");

	//tell them to reset
	server_socket.SendMessageAll(RESET_GAME, NULL, 0);
}

bool ZServer::EndGameRequirementsMet() {
	int i;
	bool team_available[MAX_TEAM_TYPES];
	int teams_available;

	//clear
	for (i = 0; i < MAX_TEAM_TYPES; i++)
		team_available[i] = false;

	for (vector<ZObject*>::iterator obj = ols.passive_engagable_olist.begin(); obj != ols.passive_engagable_olist.end();
			obj++) {
		unsigned char ot, oid;

		(*obj)->GetObjectID(ot, oid);

		if (!(ot == ROBOT_OBJECT || ot == VEHICLE_OBJECT || ot == CANNON_OBJECT))
			continue;

		team_available[(*obj)->GetOwner()] = true;
	}

	//how many teams exist?
	teams_available = 0;
	for (i = 1; i < MAX_TEAM_TYPES; i++)
		teams_available += team_available[i];

	return (teams_available <= 1);
}

void ZServer::InitObjects() {
	for (vector<map_object>::iterator i = zmap.GetObjectList().begin(); i != zmap.GetObjectList().end(); i++) {
		if (!CheckMapObject(*i))
			continue;

		if (i->object_type == ROBOT_OBJECT)
			CreateRobotGroup(i->object_id, i->x * 16, i->y * 16, i->owner, NULL, -1, i->health_percent);
		else
			CreateObject(i->object_type, i->object_id, i->x * 16, i->y * 16, i->owner, NULL, i->blevel, i->extra_links,
					i->health_percent);
	}

	//if we have a new object_list then we need a new flag list
	//ols.SetupFlagList();

	//make sure you can not eject fort turret cannons
	MakeAllFortTurretsUnEjectable();

	////as with all creation / deletions
	//ProcessChangeObjectAmount();
}

void ZServer::InitPerpetualServerSettings() {
	if (p_settings_filename.length()) {
		if (!psettings.LoadSettings(p_settings_filename))
			psettings.SaveSettings(p_settings_filename);
	}

	if (psettings.use_database && psettings.use_mysql) {
		string err_msg;

		if (zmysql.LoadDetails(err_msg, psettings)) {
			if (zmysql.Connect(err_msg)) {
				//make the initial users table if needed
				if (!zmysql.CreateUserTable(err_msg))
					printf("mysql error: %s\n", err_msg.c_str());

				if (!zmysql.CreateOnlineHistoryTable(err_msg))
					printf("mysql error: %s\n", err_msg.c_str());

				if (!zmysql.CreateAffiliateTable(err_msg))
					printf("mysql error: %s\n", err_msg.c_str());

				if (0) {
					ZMysqlUser new_user;
					bool added_user;

					new_user.username = "5";
					new_user.password = "5";
					new_user.loginname = "5";
					new_user.email = "5";
					new_user.voting_power = 5;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());

					new_user.username = "10";
					new_user.password = "10";
					new_user.loginname = "10";
					new_user.email = "10";
					new_user.voting_power = 10;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());

					new_user.username = "15";
					new_user.password = "15";
					new_user.loginname = "15";
					new_user.email = "15";
					new_user.voting_power = 15;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());

					new_user.username = "20";
					new_user.password = "20";
					new_user.loginname = "20";
					new_user.email = "20";
					new_user.voting_power = 20;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());

					new_user.username = "25";
					new_user.password = "25";
					new_user.loginname = "25";
					new_user.email = "25";
					new_user.voting_power = 25;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());

					new_user.username = "30";
					new_user.password = "30";
					new_user.loginname = "30";
					new_user.email = "30";
					new_user.voting_power = 30;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());

					new_user.username = "35";
					new_user.password = "35";
					new_user.loginname = "35";
					new_user.email = "35";
					new_user.voting_power = 35;

					if (!zmysql.AddUser(err_msg, new_user, added_user))
						printf("mysql error: %s\n", err_msg.c_str());
				}
			} else
				printf("mysql error: %s\n", err_msg.c_str());
		} else
			printf("mysql error: %s\n", err_msg.c_str());
	}

	InitOnlineHistory();
	InitSelectableMapList();
}

void ZServer::InitSelectableMapList() {
	//if we can't read in the official list
	//and we can't make one
	//then just use the regular map list
	if (!ReadSelectableMapList() && !ReadSelectableMapListFromFolder())
		selectable_map_list = map_list;
}

void ZServer::InitZones() {
	//this function sets all the buildings / zones to their correct initial owners
	//and links them all accordingly
	for (vector<ZObject*>::iterator i = object_list.begin(); i != object_list.end(); i++) {
		map_zone_info *the_zone;
		unsigned char iot;
		unsigned char ioid;
		int x, y;

		(*i)->GetObjectID(iot, ioid);
		(*i)->GetCords(x, y);

		//is this a fort, then just set the zone to the forts owner
		if (iot == BUILDING_OBJECT && (ioid == FORT_FRONT || ioid == FORT_BACK)) {
			the_zone = zmap.GetZone(x, y);

			if (the_zone) {
				the_zone->owner = (*i)->GetOwner();

				//init any other buildings in this zone
				for (vector<ZObject*>::iterator j = object_list.begin(); j != object_list.end(); j++) {
					unsigned char ot, oid;
					int ox, oy;

					(*j)->GetObjectID(ot, oid);
					(*j)->GetCords(ox, oy);

					if (ot == BUILDING_OBJECT)
						if (the_zone == zmap.GetZone(ox, oy))
							(*j)->SetOwner((*i)->GetOwner());
				}
			}
		}

		//is this a flag, then let it know its zone
		else if (iot == MAP_ITEM_OBJECT && ioid == FLAG_ITEM) {
			the_zone = zmap.GetZone(x, y);

			if (the_zone)
				((OFlag*) (*i))->SetZone(the_zone, zmap, object_list);
		}
	}

	//now that all the buildings have their correct initial teams, set default productions
	for (vector<ZObject*>::iterator i = object_list.begin(); i != object_list.end(); i++)
		(*i)->SetBuildingDefaultProduction();

	//do this...
	ResetZoneOwnagePercentages();
}

void ZServer::Setup() {
	//randomizer
	SetupRandomizer();

	//registered?
	CheckRegistration();

	//load base settings
	if (settings_filename.length())
		zsettings.LoadSettings(settings_filename);
	else {
		//if we can't load the default filename, then make it
		if (!zsettings.LoadSettings("default_settings.txt"))
			zsettings.SaveSettings("default_settings.txt");
	}

	//load the map list
	if (map_list_name.size() && !ReadMapList())
		printf("ZServer::Setup:could not load map list '%s'\n", map_list_name.c_str());

	//init main server settings (if there are any)
	InitPerpetualServerSettings();

	//use selectable map list as map list?
	if (!map_name.size() && !map_list.size()) {
		load_maps_randomly = false;
		map_list = selectable_map_list;

		printf("ZServer::Setup:using selectable map list as the map list\n");
	}

	//needed for pathfinding
	ZMap::ServerInit();

	LoadNextMap();

	//start listen socket
	if (!server_socket.Start())
		printf("ZServer::Setup:socket not setup\n");

	//init bots
	InitBots();
}

void ZServer::InitBot(int bot_team, bool do_init) {
	if (bot_team < 0)
		return;
	if (bot_team >= MAX_TEAM_TYPES)
		return;
	if (bot_team == NULL_TEAM)
		return;

	init_bot[bot_team] = do_init;
}

void ZServer::InitBots() {
	for (int i = 0; i < MAX_TEAM_TYPES; i++) {
		bot[i].SetDesiredTeam((team_type) i);
		bot[i].SetRemoteAddress("localhost");
		bot[i].SetBotBypassData(bot_bypass_data, bot_bypass_size);

		if (init_bot[i])
			StartBot(i);
	}
}

int StartBotThreadFunc(void *vbot_ptr) {
	ZBot *zbot_ptr = (ZBot *) vbot_ptr;

	//incase we set this previously to close it
	zbot_ptr->AllowRun();

	zbot_ptr->Setup();
	zbot_ptr->Run();

	return 0;
}

void ZServer::StartBot(int bot_team) {
	if (bot_team < 0)
		return;
	if (bot_team >= MAX_TEAM_TYPES)
		return;
	if (bot_team == NULL_TEAM)
		return;

	if (bot_thread[bot_team]) {
		printf("ZServer::StartBot: bot already started: %s\n", team_type_string[bot_team].c_str());
		return;
	}

	bot_thread[bot_team] = SDL_CreateThread(StartBotThreadFunc, (void*) &bot[bot_team]);

	printf("ZServer::StartBot: bot started: %s\n", team_type_string[bot_team].c_str());
}

void ZServer::KillBot(int bot_team) {
	if (bot_team < 0)
		return;
	if (bot_team >= MAX_TEAM_TYPES)
		return;
	if (bot_team == NULL_TEAM)
		return;

	if (!bot_thread[bot_team])
		return;

	bot[bot_team].AllowRun(false);

	int wait_status;
	SDL_WaitThread(bot_thread[bot_team], &wait_status);
	bot_thread[bot_team] = NULL;

	printf("ZServer::StartBot: bot stopped: %s\n", team_type_string[bot_team].c_str());
}

void ZServer::KillAllBots() {
	for (int i = 0; i < MAX_TEAM_TYPES; i++)
		if (bot_thread[i])
			KillBot(i);
}

void ZServer::ProcessEndGame() {
	//needed for end animations
	RelayTeamsWonEnded();
	//for(int i=RED_TEAM;i<MAX_TEAM_TYPES;i++)
	//	if(team_units_available[i] >= 0)
	//		RelayTeamEnded(i, true);

	//update the dbs
	UpdateUsersWinLoses();

	game_on = false;

	//set reset time?
	if (map_list.size()) {
		do_reset_game = true;
		next_reset_game_time = current_time() + 10.0;
	}

	//give clients the game over message
	BroadCastNews("The game has ended");

	//tell clients game over
	server_socket.SendMessageAll(END_GAME, NULL, 0);
}

void ZServer::ResetGame() {
	//clear stuff out
	zmap.ClearMap();

	//no missiles... no!
	damage_missile_list.clear();

	//clear out the object lists
	ols.DeleteAllObjects();

	////clear object list
	//for(vector<ZObject*>::iterator obj=object_list.begin(); obj!=object_list.end(); obj++)
	//	delete *obj;

	//object_list.clear();

	////clear specific object lists
	//flag_object_list.clear();
}

void ZServer::LoadNextMap(string override_map_name) {
	//clear stuff
	ResetGame();

	//load map list
	if (override_map_name.size()) {
		map_name = override_map_name;
	} else if (map_list.size()) {
		NextInMapList();

		map_name = map_list[current_map_i];
	}

	//load map
	if (map_name.size()) {
		if (!zmap.Read(map_name.c_str()))
			printf("ZServer::LoadNextMap:could not load map '%s'\n", map_name.c_str());
	} else
		printf("ZServer::LoadNextMap:no map set to load\n");

	InitObjects();
	InitZones();

	if (psettings.start_map_paused)
		PauseGame();

	game_on = true;
}

int ZServer::NextInMapList() {
	if (!map_list.size()) {
		printf("NextInMapList::error, no maps in the map list\n");
		return -1;
	}

	if (load_maps_randomly) {
		current_map_i = rand() % map_list.size();
	} else {
		current_map_i++;
		if (current_map_i >= map_list.size())
			current_map_i = 0;
	}

	if (current_map_i != -1)
		printf("NextInMapList::returning %d '%s'\n", current_map_i, map_list[current_map_i].c_str());

	return current_map_i;
}

bool ZServer::ReadMapList() {
	FILE *fp;
	char cur_line[500];
	bool loaded = false;

	map_list.clear();

	fp = fopen(map_list_name.c_str(), "r");

	if (!fp) {
		printf("ReadMapList::could not load '%s'\n", map_list_name.c_str());
		return false;
	}

	//random?
	if (fgets(cur_line, 500, fp)) {
		clean_newline(cur_line, 500);
		load_maps_randomly = atoi(cur_line);
	}

	//all the maps
	while (fgets(cur_line, 500, fp)) {
		clean_newline(cur_line, 500);
		if (strlen(cur_line)) {
			map_list.push_back(cur_line);
			loaded = true;
		}
	}

	fclose(fp);

	return loaded;
}

bool ZServer::ReadSelectableMapList() {
	FILE *fp;
	char cur_line[500];
	bool loaded = false;

	//is there even a file set?
	if (!psettings.selectable_map_list.length())
		return false;

	selectable_map_list.clear();

	fp = fopen(psettings.selectable_map_list.c_str(), "r");

	if (!fp) {
		printf("ReadSelectableMapList::could not load '%s'\n", psettings.selectable_map_list.c_str());
		return false;
	}

	//load the maps
	while (fgets(cur_line, 500, fp)) {
		clean_newline(cur_line, 500);
		if (strlen(cur_line)) {
			selectable_map_list.push_back(cur_line);
			loaded = true;
		}
	}

	fclose(fp);

	return loaded;
}

bool ZServer::ReadSelectableMapListFromFolder(string foldername) {
	vector<string> mlist;

	//read list
	mlist = directory_filelist(foldername);

	//find only .maps
	parse_filelist(mlist, ".map");

	//sort list
	sort(mlist.begin(), mlist.end(), sort_string_func);

	//debug
	//for(int i=0;i<mlist.size(); i++) printf("ReadSelectableMapListFromFolder::map found:%s\n", mlist[i].c_str());

	if (mlist.size()) {
		selectable_map_list = mlist;
		return true;
	} else {
		printf("ReadSelectableMapListFromFolder::no *.map files found in '%s'\n", foldername.c_str());
		return false;
	}
}

//moved to zserverolists
//void ZServer::SetupFlagList()
//{
//	flag_object_list.clear();
//
//	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
//	{
//		unsigned char ot, oid;
//		(*i)->GetObjectID(ot, oid);
//
//		if(ot == MAP_ITEM_OBJECT && oid == FLAG_ITEM)
//			flag_object_list.push_back(*i);
//	}
//}

/**
 * Create a new group of robots and return a pointer to the leader.
 */
void ZServer::Run() {
	double whole_time;
	double process_objects_time;

	while (allow_run) {
		whole_time = current_time();

		ztime.UpdateTime();
// 		printf("zserver\n");

		//check for tcp events
		server_socket.Process();

		//process events
		ehandler.ProcessEvents();

		//suggest something?
		CheckPlayerSuggestions();

		//vote expired?
		CheckVoteExpired();

		//scuffle
		ScuffleUnits();

		//run stuff
		process_objects_time = current_time();
		ProcessObjects();
		process_objects_time = current_time() - process_objects_time;

		//process missiles
		ProcessMissiles();

		//check for endgame
		CheckEndGame();
		CheckResetGame();

		//insert history
		CheckUpdateOnlineHistory();

		whole_time = current_time() - whole_time;
		//printf("server:: whole_time:%lf \t process_objects:%lf\n", whole_time, process_objects_time);

		//pause
		uni_pause(10);
	}
}

void ZServer::ScuffleUnits() {
	double &the_time = ztime.ztime;

	if (the_time >= next_scuffle_time) {
		vector<ZObject*>::iterator i, j;

		next_scuffle_time = the_time + 1.0;

		for (i = ols.mobile_olist.begin(); i != ols.mobile_olist.end(); i++) {
			int x, y;
			int w, h;
			unsigned char ot, oid;

			(*i)->GetObjectID(ot, oid);

			if (!(ot == VEHICLE_OBJECT || ot == ROBOT_OBJECT))
				continue;
			if ((*i)->GetOwner() == NULL_TEAM)
				continue;
			if ((*i)->GetWayPointList().size())
				continue;

			(*i)->GetCenterCords(x, y);
			(*i)->GetDimensionsPixel(w, h);

			//ok does it overlap any other moveable units?
			for (j = ols.mobile_olist.begin(); j != ols.mobile_olist.end(); j++)
				if (*j != *i && (*i)->GetOwner() == (*j)->GetOwner()) {
					int jx, jy;
					int jw, jh;
					int shift, offset;
					unsigned char jot, joid;

					(*j)->GetObjectID(jot, joid);

					if (!(jot == VEHICLE_OBJECT || jot == ROBOT_OBJECT))
						continue;
					if ((*j)->GetOwner() == NULL_TEAM)
						continue;
					if ((*j)->GetWayPointList().size())
						continue;

					(*j)->GetCenterCords(jx, jy);
					(*j)->GetDimensionsPixel(jw, jh);

					//are their centers roughly within 2 pixels away?
					if (abs(x - jx) >= 6)
						continue;
					if (abs(y - jy) >= 6)
						continue;

					//scuffle..
					waypoint init_movepoint;

					init_movepoint.mode = MOVE_WP;
					init_movepoint.ref_id = -1;

					shift = rand() % ((jw >> 1) + (w >> 1));
					if (rand() % 2)
						init_movepoint.x = x - shift;
					else
						init_movepoint.x = x + shift;

					shift = rand() % ((jh >> 1) + (h >> 1));
					if (rand() % 2)
						init_movepoint.y = y - shift;
					else
						init_movepoint.y = y + shift;

					(*i)->GetWayPointList().push_back(init_movepoint);

					break;
				}
		}
	}
}

void ZServer::ProcessMissiles() {
	double &the_time = ztime.ztime;

	for (vector<damage_missile>::iterator i = damage_missile_list.begin(); i != damage_missile_list.end();) {
		if (the_time >= i->explode_time) {
			ProcessMissileDamage(*i);
			i = damage_missile_list.erase(i);
		} else
			i++;
	}

	//the objects push new objects into this queue, so we need to move it to the real one
	//also do CheckUnitLimitReached if required
	for (vector<damage_missile>::iterator i = new_damage_missile_list.begin(); i != new_damage_missile_list.end(); i++)
		damage_missile_list.push_back(*i);
	new_damage_missile_list.clear();
}

void ZServer::ProcessMissileDamage(damage_missile &the_missile) {
	double &the_time = ztime.ztime;
	int &x = the_missile.x;
	int &y = the_missile.y;
	int &radius = the_missile.radius;
	int &team = the_missile.team;
	int &damage = the_missile.damage;
	vector<ZObject*>::iterator obj;

	for (obj = object_list.begin(); obj != object_list.end(); obj++) {
		if (!(*obj)->IsDestroyed() && (team == NULL_TEAM || (*obj)->GetOwner() != team)) {
			int tx, ty;
			int dx, dy;
			float mag;

			(*obj)->GetCenterCords(tx, ty);

			dx = x - tx;
			dy = y - ty;

			//quick check
			if (abs(dx) > radius)
				continue;
			if (abs(dy) > radius)
				continue;

			mag = sqrt((float) ((dx * dx) + (dy * dy)));

			//slow check
			if (mag >= radius)
				continue;

			//printf("missile hit %s dist:%lf\n", (*obj)->GetObjectName().c_str(), mag);

			(*obj)->DamageHealth(damage * (1.0 - (mag / radius)), zmap);

			(*obj)->SetDamagedByMissileTime(the_time);

			if (the_missile.attacker_ref_id != -1 && the_missile.attack_player_given
					&& the_missile.target_ref_id == (*obj)->GetRefID() && (*obj)->IsDestroyed()) {
				RelayPortraitAnim(the_missile.attacker_ref_id, TARGET_DESTROYED_ANIM);
			}

			UpdateObjectHealth(*obj, the_missile.attacker_ref_id);
		}
	}
}

void ZServer::ProcessPathFindingResults() {
	zmap.GetPathFinder().Lock_List();

	vector<ZPath_Finding_Response*>::iterator pf;
	for (pf = zmap.GetPathFinder().GetList().begin(); pf != zmap.GetPathFinder().GetList().end(); pf++) {
		ZObject *obj;

		obj = GetObjectFromID((*pf)->obj_ref_id);
		if (obj)
			obj->PostPathFindingResult(*pf);
	}

	zmap.GetPathFinder().Clear_Response_List();

	zmap.GetPathFinder().Unlock_List();
}

void ZServer::ProcessObjects() {

	char *data;
	int size;
	double &the_time = ztime.ztime;
	vector<ZObject*>::iterator obj;
	double path_time;
	double delete_time;
	double process_time;
	double update_time;
	double whole_time;

	whole_time = current_time();

	delete_time = current_time();
	//kill all those that need to die

	for (obj = object_list.begin(); obj != object_list.end();) {
		if ((*obj)->KillMe(the_time)) {
			obj = DeleteObject(obj);
		} else {
			obj++;
		}
	}

	delete_time = current_time() - delete_time;

	process_time = current_time();

	//process all the objects

	for (obj = object_list.begin(); obj != object_list.end(); obj++) {
		(*obj)->ProcessServer(zmap, ols);
	}

	process_time = current_time() - process_time;

	//see if any pathfinding responses have come back
	//and process them
	path_time = current_time();
	ProcessPathFindingResults();
	path_time = current_time() - path_time;

	update_time = current_time();
	//check to see what updated and needs to be sent to the clients

	// ATTENTION: there is more then one way how the 'obj' iterator can be incremented.
	// Because of this, it is incremented at the end of the for loop block and not in
	// the 'for' statement as usual!
	for (obj = object_list.begin(); obj != object_list.end(); /* don't increment here */) {

		ZObject *tobj = NULL;

		const server_flag sflags = (*obj)->GetSFlags();

		// this case can cut us early because it will delete this object entirely
		if ((*obj)->GetSFlags().entered_target_ref_id != -1) {

			tobj = GetObjectFromID((*obj)->GetSFlags().entered_target_ref_id);

			ZRobot* robot = dynamic_cast<ZRobot*>(*obj);

			if (robot) {
				RobotEnterObject(robot, tobj);
			}
		}

		//might want to revive it to the client first
		if ((*obj)->GetSFlags().auto_repair) {
			(*obj)->SetHasProcessedDeath(false);
			UpdateObjectHealth((*obj));
		}

		if (/*(*obj)->GetSFlags().updated_location ||*/(*obj)->GetSFlags().updated_velocity) {
			RelayObjectLoc(*obj);
		}

		if ((*obj)->GetSFlags().updated_waypoints) {
			RelayObjectWayPoints(*obj);

			//also fix minion waypoints
			(*obj)->CloneMinionWayPoints();
		}

		if ((*obj)->GetSFlags().updated_attack_object) {
			RelayObjectAttackObject(*obj);
		}

		if ((*obj)->GetSFlags().updated_attack_object_health && (*obj)->GetAttackObject()) {
			UpdateObjectHealth((*obj)->GetAttackObject(), (*obj)->GetRefID());
			//RelayBuildingState((*obj)->GetAttackObject());
		}

		if ((*obj)->GetSFlags().updated_attack_object_driver_health && (*obj)->GetAttackObject()) {

			ZObject* attackObj = (*obj)->GetAttackObject();

			ZMannedObject* manned = dynamic_cast<ZMannedObject*>(attackObj);

			if (manned) {
				UpdateObjectDriverHealth(manned);
			}
		}

		if ((*obj)->GetSFlags().destroy_fort_building_ref_id != -1) {

			tobj = GetObjectFromID((*obj)->GetSFlags().destroy_fort_building_ref_id);

			if (tobj) {
				tobj->SetHealth(0, zmap);
				UpdateObjectHealth(tobj);
			}
			//(*obj)->GetSFlags().destroy_fort_building->SetHealth(0, zmap);
			//UpdateObjectHealth((*obj)->GetSFlags().destroy_fort_building);
			//RelayBuildingState((*obj)->GetSFlags().destroy_fort_building);
		}

		if ((*obj)->GetSFlags().fired_missile) {
			fire_missile_packet the_data;

			the_data.ref_id = (*obj)->GetRefID();
			the_data.x = (*obj)->GetSFlags().missile_x;
			the_data.y = (*obj)->GetSFlags().missile_y;

			server_socket.SendMessageAll(FIRE_MISSILE, (const char*) &the_data, sizeof(fire_missile_packet));
		}

		// ########## BEGIN Handle changed experience ###############
		if ((*obj)->GetSFlags().updated_experience) {
			UpdateObjectExperience((*obj));
		}
		// ########## END Handle changed experience ###############

		// ########## BEGIN Handle 'build unit' ###############
		if ((*obj)->GetSFlags().build_unit) {

			ZObject *new_obj = BuildingCreateUnit(*obj, (*obj)->GetSFlags().bot, (*obj)->GetSFlags().boid);
			(*obj)->ResetProduction();
			RelayBuildingState(*obj);
			RelayObjectBuildingQueue(*obj);
			RelayObjectManufacturedSound(*obj, new_obj);

			//append rally points
			if (new_obj) {
				for (vector<waypoint>::iterator i = (*obj)->GetRallyPointList().begin();
						i != (*obj)->GetRallyPointList().end(); i++) {
					new_obj->GetWayPointList().push_back(*i);
				}
			}
		}
		// ########## BEGIN END 'build unit' ###############

		if ((*obj)->GetSFlags().repair_unit) {
			ZObject *new_obj = BuildingRepairUnit(*obj, (*obj)->GetSFlags().rot, (*obj)->GetSFlags().roid,
					(*obj)->GetSFlags().rdriver_type, (*obj)->GetSFlags().rdriver_info,
					(*obj)->GetSFlags().rwaypoint_list);

			RelayBuildingState(*obj);
		}

		if ((*obj)->GetSFlags().set_crane_anim) {
			crane_anim_packet the_data;

			the_data.ref_id = (*obj)->GetRefID();
			the_data.on = (*obj)->GetSFlags().crane_anim_on;
			the_data.rep_ref_id = (*obj)->GetSFlags().crane_rep_ref_id;

			server_socket.SendMessageAll(DO_CRANE_ANIM, (const char*) &the_data, sizeof(crane_anim_packet));
		}

		if ((*obj)->GetSFlags().entered_repair_building_ref_id != -1) {

			tobj = GetObjectFromID((*obj)->GetSFlags().entered_repair_building_ref_id);
			UnitEnterRepairBuilding((*obj), tobj);
		}

		/*
		 if ((*obj)->GetSFlags().updated_open_lid) {
		 set_lid_state_packet the_data;

		 the_data.ref_id = (*obj)->GetRefID();
		 the_data.lid_open = (*obj)->GetLidState();

		 server_socket.SendMessageAll(SET_LID_OPEN, (const char*) &the_data, sizeof(set_lid_state_packet));
		 }
		 */

		ZVehicle* vehicle = dynamic_cast<ZVehicle*>(*obj);

		if (vehicle) {

			if ((*obj)->GetSFlags().updated_open_lid) {
				set_lid_state_packet the_data;

				the_data.ref_id = (*obj)->GetRefID();
				the_data.lid_open = vehicle->GetLidState();

				server_socket.SendMessageAll(SET_LID_OPEN, (const char*) &the_data, sizeof(set_lid_state_packet));
			}
		}

		if ((*obj)->GetSFlags().updated_grenade_amount) {
			RelayObjectGrenadeAmount(*obj);
		}

		if ((*obj)->GetSFlags().do_pickup_grenade_anim) {
			int_packet the_data;

			the_data.ref_id = (*obj)->GetRefID();

			server_socket.SendMessageAll(PICKUP_GRENADE_ANIM, (const char*) &the_data, sizeof(int_packet));
		}

		if ((*obj)->GetSFlags().updated_leader_grenade_amount && (*obj)->GetGroupLeader()) {
			RelayObjectGrenadeAmount((*obj)->GetGroupLeader());
		}

		if ((*obj)->GetSFlags().delete_grenade_box_ref_id != -1) {
			tobj = GetObjectFromID((*obj)->GetSFlags().delete_grenade_box_ref_id);
			if (tobj) {
				tobj->SetHealth(0, zmap);
				UpdateObjectHealth(tobj);
			}
			//(*obj)->GetSFlags().delete_grenade_box->SetHealth(0, zmap);
			//UpdateObjectHealth((*obj)->GetSFlags().delete_grenade_box);
		}

		if ((*obj)->GetSFlags().portrait_anim_ref_id != -1) {
			RelayPortraitAnim((*obj)->GetSFlags().portrait_anim_ref_id, (*obj)->GetSFlags().portrait_anim_value);
		}

		//ATTENTION: there is more then one way this can increment
		//so we need it here and not in the for
		obj++;
	}

	update_time = current_time() - update_time;

	//the objects push new objects into this queue, so we need to move it to the real one
	//also do CheckUnitLimitReached if required
	for (vector<ZObject*>::iterator i = new_object_list.begin(); i != new_object_list.end(); i++) {
		ols.AddObject(*i);
	}

	//object_list.push_back(*i);
	if (new_object_list.size()) {
		CheckUnitLimitReached();
	}

	new_object_list.clear();

	//check for flag captures
	CheckFlagCaptures();

	whole_time = current_time() - whole_time;

	//if(whole_time > 0.1) printf("server process objects:: whole_time:%lf \t path_time:%lf \t delete_time:%lf \t process_time:%lf \t update_time:%lf\n", whole_time, path_time, delete_time, process_time, update_time);
}

void ZServer::UpdateObjectExperience(ZObject* obj) {

	if (!obj)
		return;

	set_experience_packet the_data;

	the_data.ref_id = obj->GetRefID();
	the_data.experience = obj->GetExperience();

	server_socket.SendMessageAll(SET_EXPERIENCE, (const char*) &the_data, sizeof(set_experience_packet));

}

void ZServer::UnitEnterRepairBuilding(ZObject *unit_obj, ZObject *rep_building_obj) {
	char *data;
	int size;

	//checks
	if (!unit_obj)
		return;
	if (!rep_building_obj)
		return;

	if (rep_building_obj->SetRepairUnit(unit_obj)) {
		//kill the entie
		unit_obj->DoKillMe(0);

		//anounce the animation
		rep_building_obj->CreateRepairAnimData(data, size);

		if (data) {
			server_socket.SendMessageAll(SET_REPAIR_ANIM, (const char*) data, size);
			free(data);
		}
	}
}

/**
 * Let robots board a manned object (vehicle or cannon)
 */
void ZServer::RobotEnterObject(ZRobot* robot_obj, ZObject *dest_obj) {

	//checks
	if (!robot_obj || !dest_obj) {
		return;
	}

	ZMannedObject* manned = dynamic_cast<ZMannedObject*>(dest_obj);

	if (!manned) {
		return;
	}

	//can someone enter the target?
	if (!manned->CanBeEntered()) {
		return;
	}

	// Set the target's driver type:
	unsigned char ot, oid;
	robot_obj->GetObjectID(ot, oid);
	manned->SetDriverType(oid);

	// Put driver into vehicle and remove it from the game:
	manned->AddDriver(robot_obj->GetHealth(), robot_obj->GetExperience());
	robot_obj->DoKillMe(0);

	//is this an apc that will take the minions too?
	dest_obj->GetObjectID(ot, oid);

	if (ot == VEHICLE_OBJECT && oid == APC) {

		// Make minions board the vehicle, too:
		for (vector<ZObject*>::iterator i = robot_obj->GetMinionList().begin(); i != robot_obj->GetMinionList().end();
				i++) {

			ZObject *minion_obj = *i;

			// Put driver into vehicle and remove it from the game:
			manned->AddDriver(minion_obj->GetHealth(), minion_obj->GetExperience());
			minion_obj->DoKillMe(0);
		}
	}

	//change team of target
	SetObjectTeam(dest_obj, robot_obj->GetOwner());

	//play portrait anim
	if (ot == VEHICLE_OBJECT) {
		RelayPortraitAnim(dest_obj->GetRefID(), VEHICLE_CAPTURED_ANIM);
	} else if (ot == CANNON_OBJECT) {
		RelayPortraitAnim(dest_obj->GetRefID(), GUN_CAPTURED_ANIM);
	}
}

void ZServer::RelayObjectManufacturedSound(ZObject *building_obj, ZObject *obj) {
	unsigned char ot, oid;

	if (!obj)
		return;
	if (!building_obj)
		return;

	obj->GetObjectID(ot, oid);

	computer_msg_packet send_data;
	send_data.ref_id = obj->GetRefID();

	switch (ot) {
	case VEHICLE_OBJECT:
		send_data.sound = COMP_VEHICLE_SND;
		break;
	case ROBOT_OBJECT:
		send_data.sound = COMP_ROBOT_SND;
		break;
	case CANNON_OBJECT:
		send_data.sound = COMP_GUN_SND;
		send_data.ref_id = building_obj->GetRefID();
		break;
	default:
		return;
	}

	RelayTeamMessage(obj->GetOwner(), COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
}

void ZServer::RelayObjectLoc(ZObject *obj) {
	char *data;
	int size;

	obj->CreateLocationData(data, size);

	//send to all
	server_socket.SendMessageAll(SEND_LOC, data, size);

	free(data);
}

void ZServer::BuildingCreateCannon(ZObject *obj, unsigned char oid) {
	//if it stores the cannon for placement, let the people know
	if (obj->StoreBuiltCannon(oid)) {
		RelayBuildingBuiltCannons(obj);

		//have them play the tune too
		computer_msg_packet send_data;
		send_data.ref_id = obj->GetRefID();
		send_data.sound = COMP_GUN_SND;
		RelayTeamMessage(obj->GetOwner(), COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
	}
}

void ZServer::RelayBuildingBuiltCannons(ZObject *obj) {
	char *data;
	int size;

	obj->CreateBuiltCannonData(data, size);

	if (!data)
		return;

	server_socket.SendMessageAll(SET_BUILT_CANNON_AMOUNT, (const char*) data, size);

	free(data);
}

void ZServer::ProcessChangeObjectAmount() {
	CheckUnitLimitReached();
}

ZObject *ZServer::BuildingRepairUnit(ZObject *obj, unsigned char ot, unsigned char oid, int driver_type,
		vector<driver_info_s> &driver_info, vector<waypoint> &rwaypoint_list) {
	int x, y;
	int w, h;
	int new_x, new_y;
	ZObject *new_obj;

	//get the starting cords for the new unit
	obj->GetRepairCenter(x, y);

	//make the unit
	if (ot == ROBOT_OBJECT)
		new_obj = CreateRobotGroup(oid, x, y, obj->GetOwner(), &new_object_list);
	else
		new_obj = CreateObject(ot, oid, x, y, obj->GetOwner(), &new_object_list);

	//wasn't made?
	if (!new_obj)
		return NULL;

	ZMannedObject* manned = dynamic_cast<ZMannedObject*>(new_obj);

	if (manned) {
		//does it have drivers?
		if (driver_info.size()) {

			manned->SetDriverType(driver_type);
			manned->GetDrivers() = driver_info;
		}
	}

	//shift its location
	new_obj->GetDimensionsPixel(w, h);
	new_x = (x - (w >> 1));
	new_y = (y - (h >> 1));
	new_obj->SetCords(new_x, new_y);

	//shift its minions
	for (vector<ZObject*>::iterator i = new_obj->GetMinionList().begin(); i != new_obj->GetMinionList().end(); i++)
		(*i)->SetCords(new_x, new_y);

	//set the initial waypoint
	waypoint init_movepoint;

	obj->GetRepairEntrance(x, y);

	init_movepoint.mode = FORCE_MOVE_WP;
	init_movepoint.ref_id = -1;
	init_movepoint.x = x;
	init_movepoint.y = y;

	new_obj->GetWayPointList().push_back(init_movepoint);

	//append old waypoints too
	//new_obj->GetWayPointList() += rwaypoint_list;
	//sprintf("BuildingRepairUnit::rwaypoint_list.size():%d\n", rwaypoint_list.size());
	if (rwaypoint_list.size() > 1) {
		for (vector<waypoint>::iterator wp = rwaypoint_list.begin() + 1; wp != rwaypoint_list.end(); wp++)
			new_obj->GetWayPointList().push_back(*wp);
	}

	//clone dem waypoints
	new_obj->CloneMinionWayPoints();

	//tell everyone it exists
	RelayNewObject(new_obj);

	//and the minions...
	for (vector<ZObject*>::iterator i = new_obj->GetMinionList().begin(); i != new_obj->GetMinionList().end(); i++)
		RelayNewObject(*i);

	//tell them where it is going
	RelayObjectWayPoints(new_obj);

	//tell them who is in it
	SetObjectTeam(new_obj, new_obj->GetOwner());

	return new_obj;
}

ZObject *ZServer::BuildingCreateUnit(ZObject *obj, unsigned char ot, unsigned char oid) {
	int x, y;
	int w, h;
	int new_x, new_y;
	ZObject *new_obj;

	//cannons are built a little differently
	if (ot == CANNON_OBJECT) {
		//printf("CannonsInZone:%d\n", obj->CannonsInZone(object_list));
		if (obj->CannonsInZone(ols) < MAX_STORED_CANNONS)
			BuildingCreateCannon(obj, oid);
		return NULL;
	}

	//get the starting cords for the new unit
	obj->GetBuildingCreationPoint(x, y);

	//make the unit
	if (ot == ROBOT_OBJECT) {
		new_obj = CreateRobotGroup(oid, x, y, obj->GetOwner(), &new_object_list);
	} else {
		new_obj = CreateObject(ot, oid, x, y, obj->GetOwner(), &new_object_list);
	}

	//wasn't made?
	if (!new_obj)
		return NULL;

	//shift its location
	new_obj->GetDimensionsPixel(w, h);
	new_x = x - (w >> 1);
	new_y = y - (h >> 1);
	new_obj->SetCords(new_x, new_y);

	//shift its minions
	for (vector<ZObject*>::iterator i = new_obj->GetMinionList().begin(); i != new_obj->GetMinionList().end(); i++)
		(*i)->SetCords(new_x, new_y);

	//set the initial waypoint
	waypoint init_movepoint;

	obj->GetBuildingCreationMovePoint(x, y);

	init_movepoint.mode = FORCE_MOVE_WP;
	init_movepoint.ref_id = -1;
	init_movepoint.x = x;
	init_movepoint.y = y;

	new_obj->GetWayPointList().push_back(init_movepoint);

	//clone dem waypoints
	new_obj->CloneMinionWayPoints();

	//tell everyone it exists
	RelayNewObject(new_obj);

	//and the minions...
	for (vector<ZObject*>::iterator i = new_obj->GetMinionList().begin(); i != new_obj->GetMinionList().end(); i++)
		RelayNewObject(*i);

	//tell them where it is going
	RelayObjectWayPoints(new_obj);

	return new_obj;
}

void ZServer::CheckDestroyedFort(ZObject *obj) {
	unsigned char ot, oid;
	int team;
	double &the_time = ztime.ztime;

	if (!obj)
		return;

	obj->GetObjectID(ot, oid);

	if (!(ot == BUILDING_OBJECT && (oid == FORT_FRONT || oid == FORT_BACK)))
		return;

	team = obj->GetOwner();
	if (team == NULL_TEAM)
		return;

	//needed for all the end animations
	RelayTeamEnded(team, false);

	//destroy all the other objects in this list
	for (vector<ZObject*>::iterator i = object_list.begin(); i != object_list.end(); i++) {
		unsigned char iot, ioid;

		if ((*i)->GetOwner() != team)
			continue;
		if ((*i)->IsDestroyed())
			continue;
		if ((*i) == obj)
			continue;

		(*i)->GetObjectID(iot, ioid);

		//no flags.
		if (iot == MAP_ITEM_OBJECT)
			continue;

		//destroy it then
		(*i)->SetHealth(0, zmap);

		//stop auto repairs of factories in fort territory
		//...this is fixed in DoAutoRepair()
		//if(obj->GetConnectedZone() && obj->GetConnectedZone() == (*i)->GetConnectedZone())
		//	(*i)->StopAutoRepair();

		RelayObjectDeath(*i);
	}

	//lose all the zones
	for (vector<ZObject*>::iterator i = ols.flag_olist.begin(); i != ols.flag_olist.end(); i++) {
		if ((*i)->GetOwner() != team)
			continue;

		AwardZone((OFlag*) *i, NULL_TEAM);
	}

	//annouce the news
	BroadCastNews(string("The " + team_type_string[team] + " team has been eliminated").c_str());
}

void ZServer::RelayObjectDeath(ZObject *obj, int killer_ref_id) {
	double &the_time = ztime.ztime;
	int missile_radius, missile_damage;
	destroy_object_packet send_data_base;
	char *send_data;
	int size;
	int i;
	vector<fire_missile_info> missile_list;

	send_data_base.ref_id = obj->GetRefID();
	send_data_base.killer_ref_id = killer_ref_id;
	//send_data_base.fire_missile = obj->ServerFireTurrentMissile(send_data.missile_x, send_data.missile_y, missile_damage, missile_radius, send_data.missile_offset_time);
	missile_list = obj->ServerFireTurrentMissile(missile_damage, missile_radius);
	send_data_base.fire_missile_amount = missile_list.size();
	send_data_base.destroy_object = false;
	send_data_base.do_fire_death = (the_time - obj->GetDamagedByFireTime() < 1.5);
	send_data_base.do_missile_death = (the_time - obj->GetDamagedByMissileTime() < 1.5);

	//make data
	size = sizeof(destroy_object_packet) + (sizeof(fire_missile_info) * send_data_base.fire_missile_amount);
	send_data = (char*) malloc(size);
	memcpy(send_data, (const char*) &send_data_base, sizeof(destroy_object_packet));

	//turrent damage
	//if(send_data_base.fire_missile_amount)
	for (i = 0; i < send_data_base.fire_missile_amount; i++) {
		//push into the send packet
		memcpy(send_data + sizeof(destroy_object_packet) + (sizeof(fire_missile_info) * i),
				(const char*) &missile_list[i], sizeof(fire_missile_info));

		//push into the server missile list
		damage_missile new_missile;

		new_missile.x = missile_list[i].missile_x;
		new_missile.y = missile_list[i].missile_y;
		new_missile.damage = missile_damage;
		new_missile.radius = missile_radius;
		new_missile.team = NULL_TEAM;
		new_missile.attacker_ref_id = -1;
		new_missile.attack_player_given = false;
		new_missile.target_ref_id = -1;
		//new_missile.CalcExplodeTimeTo(loc.x + (width_pix / 2), loc.y + (height_pix / 2), missile_speed);
		new_missile.explode_time = the_time + missile_list[i].missile_offset_time;

		new_damage_missile_list.push_back(new_missile);
	}

	if (obj->CanBeDestroyed()) {
		obj->DoKillMe(the_time + LIFE_AFTER_DEATH_TIME);
		send_data_base.destroy_object = true;
	}

	//server_socket.SendMessageAll(DESTROY_OBJECT, (const char*)&send_data_base, sizeof(destroy_object_packet));
	server_socket.SendMessageAll(DESTROY_OBJECT, (const char*) send_data, size);

	//clear data
	if (send_data)
		free(send_data);

	//destroying an object stops it's production
	//so announce this too

	ZBuilding* building = dynamic_cast<ZBuilding*>(obj);

	if (building) {

		if (building->ProducesUnits())
			RelayBuildingState(obj);
	}

}

void ZServer::RelayObjectAttackObject(ZObject *obj) {
	char *data;
	int size;

	if (!obj)
		return;

	obj->CreateAttackObjectData(data, size);
	server_socket.SendMessageAll(SET_ATTACK_OBJECT, data, size);
	free(data);
}

void ZServer::UpdateObjectDriverHealth(ZMannedObject *obj) {
	if (obj->GetDrivers().size() && obj->GetDrivers().begin()->health > 0) {
		//tell the clients to do the driver hit effect
		//printf("UpdateObjectDriverHealth::%d\n", obj->GetDrivers().begin()->driver_health);

		driver_hit_packet send_data;
		send_data.ref_id = obj->GetRefID();
		server_socket.SendMessageAll(DRIVER_HIT_EFFECT, (char*) &send_data, sizeof(driver_hit_packet));
	} else {
		//printf("UpdateObjectDriverHealth::dead\n");

		//say it has been sniped
		snipe_object_packet send_data;
		send_data.ref_id = obj->GetRefID();
		server_socket.SendMessageAll(SNIPE_OBJECT, (char*) &send_data, sizeof(snipe_object_packet));

		//disengage
		if (obj->Disengage())
			RelayObjectAttackObject(obj);

		//driver doesn't exist anymore
		SetObjectTeam(obj, NULL_TEAM);

		//clear waypoints if there are any
		if (obj->GetWayPointList().size()) {
			obj->GetWayPointList().clear();

			//let folks know about the waypoint updates
			RelayObjectWayPoints(obj);

			//stop movement
			if (obj->StopMove())
				RelayObjectLoc(obj);
		}

		//all objects attacking this obj must quit
		vector<ZObject*>::iterator o;
		for (o = object_list.begin(); o != object_list.end(); o++) {
			//are we currently attacking this object? then stop
			if ((*o)->GetAttackObject() == obj) {
				(*o)->Disengage();
				RelayObjectAttackObject((*o));
			}

			//do we have a waypoint looking to kill just this target?
			if ((*o)->GetWayPointList().size()
					&& ((*o)->GetWayPointList().begin()->mode == ATTACK_WP
							|| (*o)->GetWayPointList().begin()->mode == AGRO_WP)
					&& (*o)->GetWayPointList().begin()->ref_id == obj->GetRefID()) {
				//kill the waypoint
				(*o)->GetWayPointList().erase((*o)->GetWayPointList().begin());

				//let folks know about the waypoint updates
				RelayObjectWayPoints((*o));

				//stop movement
				if ((*o)->StopMove())
					RelayObjectLoc(*o);
			}
		}
	}
}

void ZServer::UpdateObjectHealth(ZObject *obj, int attacker_ref_id) {

	if (!obj)
		return;

	if (obj->IsDestroyed() && !obj->HasProcessedDeath()) {
		obj->SetHasProcessedDeath(true);

		ZObject* killer = GetObjectFromID(attacker_ref_id);

		// Give the killer experience for the kill:
		if (killer) {
			killer->SetGroupExperience(killer->GetExperience() + 1);
		}

		RelayObjectDeath(obj, attacker_ref_id);

		CheckDestroyedFort(obj);
		CheckDestroyedBridge(obj);
		CheckNoUnitsDestroyFort(obj->GetOwner());

	} else {
		RelayObjectHealth(obj);
	}

//if it is a building, then new state info needs to go out
	RelayBuildingState(obj);
}

void ZServer::RelayObjectHealth(ZObject *obj, int player) {

	if (!obj) {
		return;
	}

	object_health_packet send_data;

	send_data.ref_id = obj->GetRefID();
	send_data.health = obj->GetHealth();

//send data
	if (player == -1) {
		server_socket.SendMessageAll(UPDATE_HEALTH, (const char*) &send_data, sizeof(object_health_packet));
	} else {
		server_socket.SendMessage(player, UPDATE_HEALTH, (const char*) &send_data, sizeof(object_health_packet));
	}
}

void ZServer::CheckNoUnitsDestroyFort(int team) {
	double &the_time = ztime.ztime;

	if (team == NULL_TEAM)
		return;

//leave if we find a unit in this team that is alive
	for (vector<ZObject*>::iterator obj = ols.passive_engagable_olist.begin(); obj != ols.passive_engagable_olist.end();
			obj++) {
		unsigned char ot, oid;

		(*obj)->GetObjectID(ot, oid);

		//only leave the function
		//if we find an alive unit on our team
		if ((*obj)->GetOwner() != team)
			continue;
		if (!(ot == ROBOT_OBJECT || ot == VEHICLE_OBJECT || ot == CANNON_OBJECT))
			continue;
		if ((*obj)->IsDestroyed())
			continue;

		return;
	}

//ok destroy all the forts
	for (vector<ZObject*>::iterator obj = object_list.begin(); obj != object_list.end(); obj++) {
		unsigned char ot, oid;

		(*obj)->GetObjectID(ot, oid);

		if ((*obj)->GetOwner() != team)
			continue;
		if (!(ot == BUILDING_OBJECT && (oid == FORT_FRONT || oid == FORT_BACK)))
			continue;
		if ((*obj)->IsDestroyed())
			continue;

		//destroy it then
		(*obj)->SetHealth(0, zmap);

		CheckDestroyedFort(*obj);

		RelayObjectDeath(*obj);
	}
}

void ZServer::CheckFlagCaptures() {
	static double next_time = 0;
	double &the_time = ztime.ztime;

	if (the_time > next_time) {
		vector<ZObject*>::iterator f, o;

		next_time = the_time + 0.2;

		for (f = ols.flag_olist.begin(); f != ols.flag_olist.end(); f++)
			for (o = ols.mobile_olist.begin(); o != ols.mobile_olist.end(); o++) {
				unsigned char ot, oid;

				if ((*o)->GetOwner() == NULL_TEAM)
					continue;
				if ((*f)->GetOwner() == (*o)->GetOwner())
					continue;

				(*o)->GetObjectID(ot, oid);

				//only get mobile units
				if (!(ot == VEHICLE_OBJECT || ot == ROBOT_OBJECT))
					continue;

				//they intersect?
				if (!(*f)->IntersectsObject(**o))
					continue;

				//ok they intersect so lets give the team the zone
				AwardZone((OFlag*) *f, *o);

				//escape looking for objects that could capture this flag
				break;
			}
	}
}

void ZServer::AwardZone(OFlag *flag, ZObject *conquerer) {
	RelayPortraitAnim(conquerer->GetRefID(), TERRITORY_TAKEN_ANIM);
	AwardZone(flag, conquerer->GetOwner());
}

void ZServer::AwardZone(OFlag *flag, team_type new_team) {
	team_type old_team;
	vector<ZObject*>::iterator i;

	old_team = flag->GetLinkedZone()->owner;
//new_team = conquerer->GetOwner();

//set the flags team
	SetObjectTeam(flag, new_team);

//set all its buildings teams
	for (i = flag->GetLinkedObjects().begin(); i != flag->GetLinkedObjects().end(); i++) {
		SetObjectTeam(*i, new_team);

		//start default production
		if ((*i)->SetBuildingDefaultProduction())
			RelayBuildingState(*i);
	}

//set the zones team
	flag->GetLinkedZone()->owner = new_team;

//send the zone info to everyone
	{
		zone_info_packet packet_info;

		packet_info.zone_number = flag->GetLinkedZone()->id;
		packet_info.owner = flag->GetLinkedZone()->owner;

		server_socket.SendMessageAll(SET_ZONE_INFO, (char*) &packet_info, sizeof(zone_info_packet));
	}

//tell them to play a tune
	if (old_team != NULL_TEAM) {
		computer_msg_packet send_data;
		send_data.ref_id = -1;
		send_data.sound = COMP_TERRITORY_LOST_SND;

		RelayTeamMessage(old_team, COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
	}

	if (flag->HasRadar()) {
		computer_msg_packet send_data;
		send_data.ref_id = -1;
		send_data.sound = COMP_RADAR_ACTIVATED_SND;
		RelayTeamMessage(new_team, COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
	}

//ok do this
	ResetZoneOwnagePercentages(true);
}

// TODO 4: Try to simplify this a bit
void ZServer::RemoveObjectFromGroup(ZObject *obj) {
	char *data;
	int size;

	if (!obj->IsGroupMember())
		return;

// If the object is a minion...
	if (obj->GetGroupLeader()) {
		//it has a group leader so remove itself from the leaders list
		obj->GetGroupLeader()->RemoveGroupMinion(obj);

		//update all on the leaders new group info
		RelayObjectGroupInfo(obj->GetGroupLeader());
		//obj->GetGroupLeader()->CreateGroupInfoData(data, size);
		//if(data)
		//{
		//	server_socket.SendMessageAll(OBJECT_GROUP_INFO, data, size);
		//	free(data);
		//}

		//update all on this objects info
	}

// Otherwise, if it's a leader and has minions:
	else if (obj->GetMinionList().size()) {

		//it is a group leader
		ZObject *new_leader = NULL;

		//pick the new leader
		for (vector<ZObject*>::iterator i = obj->GetMinionList().begin(); i != obj->GetMinionList().end(); i++) {
			if (*i) {
				new_leader = *i;
				break;
			}
		}

		if (new_leader) {
			//tell everyone the new leader
			for (vector<ZObject*>::iterator i = obj->GetMinionList().begin(); i != obj->GetMinionList().end(); i++) {
				if (*i) {
					(*i)->SetGroupLeader(new_leader);
				}
			}

			//clear out new leader info
			new_leader->ClearGroupInfo();

			//set up new_leader's minion list
			for (vector<ZObject*>::iterator i = obj->GetMinionList().begin(); i != obj->GetMinionList().end(); i++) {
				if (*i) {
					new_leader->AddGroupMinion(*i);
				}
			}

			//send out new leaders info
			RelayObjectGroupInfo(new_leader);
			//new_leader->CreateGroupInfoData(data, size);
			//if(data)
			//{
			//	server_socket.SendMessageAll(OBJECT_GROUP_INFO, data, size);
			//	free(data);
			//}

			//send out minions new info
			for (vector<ZObject*>::iterator i = new_leader->GetMinionList().begin();
					i != new_leader->GetMinionList().end(); i++) {
				if (*i) {
					RelayObjectGroupInfo(*i);
					//(*i)->CreateGroupInfoData(data, size);
					//if(data)
					//{
					//	server_socket.SendMessageAll(OBJECT_GROUP_INFO, data, size);
					//	free(data);
					//}
				}
			}

			//give new leader the grenades
			if (obj->GetGrenadeAmount()) {
				new_leader->SetGrenadeAmount(obj->GetGrenadeAmount());
				RelayObjectGrenadeAmount(new_leader);
			}
		}
	}

//now do its stuff
	obj->ClearGroupInfo();
	RelayObjectGroupInfo(obj);
//obj->CreateGroupInfoData(data, size);
//if(data)
//{
//	server_socket.SendMessageAll(OBJECT_GROUP_INFO, data, size);
//	free(data);
//}
}

vector<ZObject*>::iterator ZServer::DeleteObject(vector<ZObject*>::iterator obj) {
	vector<ZObject*>::iterator ret;
	int ref_id;
	ZObject *dobj;

	dobj = *obj;

//clean up group stuff?
	RemoveObjectFromGroup(dobj);

//clean up map
	dobj->UnSetMapImpassables(zmap);

	ref_id = dobj->GetRefID();

//now make sure no other part of the server is still pointing to this object
	for (vector<ZObject*>::iterator o = object_list.begin(); o != object_list.end(); o++)
		(*o)->RemoveObject(dobj);

//make the deletion
	delete *obj;
	ret = object_list.erase(obj);

//clean up server olists
	ols.RemoveObject(dobj);

//do this (mainly so production of units can start again)
	CheckUnitLimitReached();

//tell everyone it is deleted
	server_socket.SendMessageAll(DELETE_OBJECT, (char*) &ref_id, sizeof(int));

	return ret;
}

void ZServer::DeleteObject(ZObject *obj) {
	vector<ZObject*>::iterator i;

	for (i = object_list.begin(); i != object_list.end(); i++)
		if (obj == *i) {
			DeleteObject(i);
			break;
		}
}

void ZServer::SetObjectTeam(ZObject *obj, team_type new_team) {
//object_team_packet packet_info;
//char *send_data;
	char *data = NULL;
	int size = 0;

	obj->SetOwner(new_team);

//packet_info.ref_id = obj->GetRefID();
//packet_info.owner = obj->GetOwner();
//packet_info.driver_type = obj->GetDriver();

	obj->CreateTeamData(data, size);

	server_socket.SendMessageAll(SET_OBJECT_TEAM, data, size);
//server_socket.SendMessageAll(SET_OBJECT_TEAM, (char*)&packet_info, sizeof(object_team_packet));

	if (data)
		free(data);

//should need to be here
	ProcessChangeObjectAmount();
}

void ZServer::SetMapName(string map_name_) {
	map_name = map_name_;
}

void ZServer::SetMapList(string map_list_name_) {
	map_list_name = map_list_name_;
}

void ZServer::SetSettingsFilename(string settings_filename_) {
	settings_filename = settings_filename_;
}

void ZServer::SetPerpetualSettingsFilename(string p_settings_filename_) {
	p_settings_filename = p_settings_filename_;
}

void ZServer::SendNews(int player, const char* message, char r, char g, char b) {
	char *data;
	int message_size = strlen(message) + 1;

	if (message_size <= 1)
		return;

	data = (char*) malloc(message_size + 3);

	memcpy(data, &r, 1);
	memcpy(data + 1, &g, 1);
	memcpy(data + 2, &b, 1);
	memcpy(data + 3, message, message_size);

	server_socket.SendMessage(player, NEWS_EVENT, data, message_size + 3);

	free(data);
}

void ZServer::BroadCastNews(const char* message, char r, char g, char b) {
	int i, max;

	max = server_socket.PlayersConnected();
	for (i = 0; i < max; i++)
		SendNews(i, message, r, g, b);
}

void ZServer::RelayObjectRallyPoints(ZObject *obj, int player) {
	char *data;
	int size;

//sanity
	if (!obj)
		return;

//it even do rally points?
	if (!obj->CanSetRallypoints())
		return;

//get data
	CreateWaypointSendData(obj->GetRefID(), obj->GetRallyPointList(), data, size);

//any data made?
	if (!data)
		return;

//send data
	if (player == -1)
		server_socket.SendMessageAll(SEND_RALLYPOINTS, data, size);
	else
		server_socket.SendMessage(player, SEND_RALLYPOINTS, data, size);

//createdwaypoint data must be freed
	free(data);
}

void ZServer::RelayObjectBuildingQueue(ZObject *obj, int player) {
	char *data;
	int size;

	if (!obj)
		return;

//building queue info packet
	obj->CreateBuildingQueueData(data, size);

//not made?
	if (!data)
		return;

//send data
	if (player == -1)
		server_socket.SendMessageAll(SET_BUILDING_QUEUE_LIST, data, size);
	else
		server_socket.SendMessage(player, SET_BUILDING_QUEUE_LIST, data, size);

//data must be freed
	free(data);
}

void ZServer::RelayObjectGroupInfo(ZObject *obj, int player) {
	char *data;
	int size;

	if (!obj)
		return;

	obj->CreateGroupInfoData(data, size);

//not made?
	if (!data)
		return;

//send data
	if (player == -1)
		server_socket.SendMessageAll(OBJECT_GROUP_INFO, data, size);
	else
		server_socket.SendMessage(player, OBJECT_GROUP_INFO, data, size);

//data must be freed
	free(data);
}

void ZServer::RelayVersion(int player) {
	version_packet packet;

//check
	if (strlen(GAME_VERSION) + 1 >= MAX_VERSION_PACKET_CHARS) {
		printf("ZServer::RelayVersion: '%s' too large to fit in a %d char array\n", GAME_VERSION,
				MAX_VERSION_PACKET_CHARS);
		return;
	}

	strcpy(packet.version, GAME_VERSION);

	if (player == -1)
		server_socket.SendMessageAll(GIVE_VERSION, (char*) &packet, sizeof(version_packet));
	else
		server_socket.SendMessage(player, GIVE_VERSION, (char*) &packet, sizeof(version_packet));
}

void ZServer::RelayObjectWayPoints(ZObject *obj) {
	char *data;
	int size;

//sanity
	if (!obj)
		return;

//get data
	CreateWaypointSendData(obj->GetRefID(), obj->GetWayPointList(), data, size);

//any data made?
	if (!data)
		return;

//send data
	RelayTeamMessage(obj->GetOwner(), SEND_WAYPOINTS, data, size);

//createdwaypoint data must be freed
	free(data);
}

void ZServer::RelayTeamMessage(team_type the_team, int pack_id, const char *data, int size) {
	int i, max;

	max = server_socket.PlayersConnected();
	for (i = 0; i < max; i++)
		if (player_info[i].team == the_team)
			server_socket.SendMessage(i, pack_id, data, size);
}

void ZServer::RelayBuildingState(ZObject *obj, int player) {
	char *data;
	int size;

//building info packet
	obj->CreateBuildingStateData(data, size);
	if (data) {
		if (player == -1)
			server_socket.SendMessageAll(SET_BUILDING_STATE, data, size);
		else
			server_socket.SendMessage(player, SET_BUILDING_STATE, data, size);

		free(data);
	}

//repair anim info packet
	obj->CreateRepairAnimData(data, size);
	if (data) {
		if (player == -1)
			server_socket.SendMessageAll(SET_REPAIR_ANIM, data, size);
		else
			server_socket.SendMessage(player, SET_REPAIR_ANIM, data, size);

		free(data);
	}

//send queue info
	RelayObjectBuildingQueue(obj, player);
}

void ZServer::RelayNewObject(ZObject *obj, int player) {
	object_init_packet object_info;

	if (!obj)
		return;

//pack the info
	obj->GetCords(object_info.x, object_info.y);
	obj->GetObjectID(object_info.object_type, object_info.object_id);
	object_info.owner = obj->GetOwner();
	object_info.ref_id = obj->GetRefID();
	object_info.blevel = obj->GetLevel();
	object_info.extra_links = obj->GetExtraLinks();
	object_info.health = obj->GetHealth();

//send it
	if (player == -1)
		server_socket.SendMessageAll(ADD_NEW_OBJECT, (char*) &object_info, sizeof(object_init_packet));
	else
		server_socket.SendMessage(player, ADD_NEW_OBJECT, (char*) &object_info, sizeof(object_init_packet));

//send minion info
	RelayObjectGroupInfo(obj, player);
//{
//	char *data;
//	int size;

//	obj->CreateGroupInfoData(data, size);

//	if(data)
//	{
//		if(player == -1)
//			server_socket.SendMessageAll(OBJECT_GROUP_INFO, data, size);
//		else
//			server_socket.SendMessage(player, OBJECT_GROUP_INFO, data, size);

//		free(data);
//	}
//}
}

void ZServer::CheckObjectWaypoints(ZObject *obj) {
	unsigned char ot, oid;
	ZObject *aobj;

	obj->GetObjectID(ot, oid);

	for (vector<waypoint>::iterator wp = obj->GetWayPointList().begin(); wp != obj->GetWayPointList().end();) {
		//clients are not allowed to set forcemove waypoints
		if (wp->mode == FORCE_MOVE_WP)
			wp->mode = MOVE_WP;

		//clients are not allowed to set agro waypoints
		if (wp->mode == AGRO_WP)
			wp->mode = ATTACK_WP;

		//attacking an object that can only be attacked by explosions?
		if (wp->mode == ATTACK_WP) {
			aobj = GetObjectFromID(wp->ref_id);

			if (!aobj) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			if (!obj->HasExplosives() && aobj->AttackedOnlyByExplosives()) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			if (!obj->CanAttack()) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

		}

		//entering ok?
		if (wp->mode == ENTER_WP) {
			//this a robot?
			if (ot != ROBOT_OBJECT) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			//target exist?
			aobj = GetObjectFromID(wp->ref_id);
			if (!aobj) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			//can the target be entered?
			ZMannedObject* manned = dynamic_cast<ZMannedObject*>(aobj);

			if (manned) {
				if (!manned->CanBeEntered()) {
					wp = obj->GetWayPointList().erase(wp);
					continue;
				}
			} else {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}
		}

		if (wp->mode == CRANE_REPAIR_WP) {
			//it a crane?
			if (!(ot == VEHICLE_OBJECT && oid == CRANE)) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			//target exist?
			aobj = GetObjectFromID(wp->ref_id);
			if (!aobj) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			//can it repair that target?
			if (!aobj->CanBeRepairedByCrane(obj->GetOwner())) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}
		}

		if (wp->mode == UNIT_REPAIR_WP) {
			//can it be repaired?
			if (!obj->CanBeRepaired()) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			//target exist?
			aobj = GetObjectFromID(wp->ref_id);
			if (!aobj) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}

			//can the target repair it?
			if (!aobj->CanRepairUnit(obj->GetOwner())) {
				wp = obj->GetWayPointList().erase(wp);
				continue;
			}
		}

		//bad mode?
		if (wp->mode < 0 || wp->mode >= MAX_WAYPOINT_MODES) {
			wp = obj->GetWayPointList().erase(wp);
			continue;
		}

		wp++;
	}
}

void ZServer::ResetZoneOwnagePercentages(bool notify_players) {
	int i;
	int zone_ownage[MAX_TEAM_TYPES];

//special case
	if (!ols.flag_olist.size()) {
		for (i = 0; i < MAX_TEAM_TYPES; i++)
			team_zone_percentage[i] = 0;

		return;
	}

//clear
	for (i = 0; i < MAX_TEAM_TYPES; i++)
		zone_ownage[i] = 0;

//tally
	for (vector<ZObject*>::iterator of = ols.flag_olist.begin(); of != ols.flag_olist.end(); of++)
		zone_ownage[(*of)->GetOwner()]++;

//percentage
	for (i = 0; i < MAX_TEAM_TYPES; i++)
		team_zone_percentage[i] = 1.0 * zone_ownage[i] / ols.flag_olist.size();

//tell all the buildings to redo their build orders
	for (vector<ZObject*>::iterator obj = object_list.begin(); obj != object_list.end(); obj++) {
		if ((*obj)->ResetBuildTime(team_zone_percentage[(*obj)->GetOwner()]) && notify_players)
			RelayBuildingState(*obj);
		;
	}
}

bool ZServer::LoginPlayer(int player, ZMysqlUser &user) {
	if (!psettings.use_database)
		return false;

	if (player_info[player].logged_in) {
		SendNews(player, "login error: please log off first", 0, 0, 0);
		return false;
	}

//logout others
	LogoutOthers(user.tabID);

	player_info[player].name = user.username;
	player_info[player].db_id = user.tabID;
	player_info[player].activated = user.activated;
	player_info[player].voting_power = user.voting_power;
	player_info[player].total_games = user.total_games;
	player_info[player].logged_in = true;
	player_info[player].last_time = time(0);

	if (psettings.use_mysql) {
		string err_msg;

		if (!zmysql.UpdateUserVariable(err_msg, user.tabID, "last_time", time(0)))
			printf("mysql error: %s\n", err_msg.c_str());

		if (!zmysql.UpdateUserVariable(err_msg, user.tabID, "last_ip", player_info[player].ip))
			printf("mysql error: %s\n", err_msg.c_str());
	}

//SendNews(player, string("Welcome " + player_info[player].name).c_str(), 0, 0, 0);

	BroadCastNews(string(player_info[player].name + " logged in").c_str());

//tell everyone
	RelayLPlayerName(player);
	RelayLPlayerLoginInfo(player);
	RelayLPlayerVoteChoice(player);

//check history stuff
	CheckOnlineHistoryPlayerCount();

//check vote stuff
	CheckVote();

//tell them to (or not) display the login menu
	SendPlayerLoginRequired(player);

	return true;
}

bool ZServer::LogoutPlayer(int player, bool connection_exists) {
	if (!psettings.use_database)
		return false;

	if (player_info[player].logged_in) {
		//append total_time
		{
			string err_msg;

			if (!zmysql.IncreaseUserVariable(err_msg, player_info[player].db_id, "total_time",
					time(0) - player_info[player].last_time))
				printf("mysql error: %s\n", err_msg.c_str());
		}

		//SendNews(player, string("Goodbye " + player_info[player].name).c_str(), 0, 0, 0);

		BroadCastNews(string(player_info[player].name + " logged out").c_str());

		player_info[player].logout();

		//tell everyone
		RelayLPlayerName(player);
		RelayLPlayerLoginInfo(player);
		RelayLPlayerVoteChoice(player);

		//check vote stuff
		CheckVote();

		//tell them to (or not) display the login menu
		if (connection_exists)
			SendPlayerLoginRequired(player);
	}
//else
//	SendNews(player, "logout error: not logged in", 0, 0, 0);

	return true;
}

void ZServer::LogoutOthers(int db_id) {
	for (int i = 0; i < player_info.size(); i++)
		if (player_info[i].logged_in && player_info[i].db_id == db_id) {
			SendNews(i, "warning: someone else has logged in with your user", 0, 0, 0);
			LogoutPlayer(i);
		}

}

void ZServer::RelayLAdd(int player, int to_player) {
	add_remove_player_packet packet;

	packet.p_id = player_info[player].p_id;

//add it
	if (to_player == -1)
		server_socket.SendMessageAll(ADD_LPLAYER, (char*) &packet, sizeof(add_remove_player_packet));
	else
		server_socket.SendMessage(to_player, ADD_LPLAYER, (char*) &packet, sizeof(add_remove_player_packet));
}

void ZServer::RelayLPlayerName(int player, int to_player) {
	char *data;
	int size;

	size = sizeof(int) + player_info[player].name.size() + 1;
	data = (char*) malloc(size);

	memcpy(data, &(player_info[player].p_id), sizeof(int));
	memcpy(data + sizeof(int), player_info[player].name.c_str(), player_info[player].name.size());
	*(data + sizeof(int) + player_info[player].name.size()) = 0;

	if (to_player == -1)
		server_socket.SendMessageAll(SET_LPLAYER_NAME, data, size);
	else
		server_socket.SendMessage(to_player, SET_LPLAYER_NAME, data, size);

	free(data);
}

void ZServer::RelayLPlayerTeam(int player, int to_player) {
	set_player_int_packet packet;

	packet.p_id = player_info[player].p_id;
	packet.value = player_info[player].team;

	if (to_player == -1)
		server_socket.SendMessageAll(SET_LPLAYER_TEAM, (char*) &packet, sizeof(set_player_int_packet));
	else
		server_socket.SendMessage(to_player, SET_LPLAYER_TEAM, (char*) &packet, sizeof(set_player_int_packet));
}

void ZServer::RelayLPlayerMode(int player, int to_player) {
	set_player_int_packet packet;

	packet.p_id = player_info[player].p_id;
	packet.value = player_info[player].mode;

	if (to_player == -1)
		server_socket.SendMessageAll(SET_LPLAYER_MODE, (char*) &packet, sizeof(set_player_int_packet));
	else
		server_socket.SendMessage(to_player, SET_LPLAYER_MODE, (char*) &packet, sizeof(set_player_int_packet));
}

void ZServer::RelayLPlayerIgnored(int player, int to_player) {
	set_player_int_packet packet;

	packet.p_id = player_info[player].p_id;
	packet.value = player_info[player].ignored;

	if (to_player == -1)
		server_socket.SendMessageAll(SET_LPLAYER_IGNORED, (char*) &packet, sizeof(set_player_int_packet));
	else
		server_socket.SendMessage(to_player, SET_LPLAYER_IGNORED, (char*) &packet, sizeof(set_player_int_packet));
}

void ZServer::RelayLPlayerLoginInfo(int player, int to_player) {
	set_player_loginfo_packet packet;

	packet.p_id = player_info[player].p_id;
	packet.db_id = player_info[player].db_id;
	packet.voting_power = player_info[player].voting_power;
	packet.total_games = player_info[player].total_games;
	packet.activated = player_info[player].activated;
	packet.logged_in = player_info[player].logged_in;
	packet.bot_logged_in = player_info[player].bot_logged_in;

	if (to_player == -1)
		server_socket.SendMessageAll(SET_LPLAYER_LOGINFO, (char*) &packet, sizeof(set_player_loginfo_packet));
	else
		server_socket.SendMessage(to_player, SET_LPLAYER_LOGINFO, (char*) &packet, sizeof(set_player_loginfo_packet));
}

void ZServer::RelayLPlayerVoteChoice(int player, int to_player) {
	set_player_int_packet packet;

	packet.p_id = player_info[player].p_id;
	packet.value = player_info[player].vote_choice;

	if (to_player == -1)
		server_socket.SendMessageAll(SET_LPLAYER_VOTEINFO, (char*) &packet, sizeof(set_player_int_packet));
	else
		server_socket.SendMessage(to_player, SET_LPLAYER_VOTEINFO, (char*) &packet, sizeof(set_player_int_packet));
}

bool ZServer::LoginCheckDenied(int player) {
	return (psettings.require_login && !player_info[player].logged_in && !player_info[player].bot_logged_in);
}

bool ZServer::ActivationCheckPassed(int player) {
	if (!psettings.require_login)
		return true;
	if (psettings.ignore_activation)
		return true;
	if (player_info[player].bot_logged_in)
		return true;
	if (player_info[player].logged_in && player_info[player].activated)
		return true;

	return false;
}

void ZServer::RelayTeamsWonEnded() {
	bool team_won[MAX_TEAM_TYPES];

//clear
	for (int i = 0; i < MAX_TEAM_TYPES; i++)
		team_won[i] = false;

//populate team_won[]
	for (vector<ZObject*>::iterator obj = object_list.begin(); obj != object_list.end(); obj++) {
		unsigned char ot, oid;

		(*obj)->GetObjectID(ot, oid);

		if (!(ot == ROBOT_OBJECT || ot == VEHICLE_OBJECT || ot == CANNON_OBJECT))
			continue;

		team_won[(*obj)->GetOwner()] = true;
	}

	for (int i = RED_TEAM; i < MAX_TEAM_TYPES; i++)
		if (team_won[i])
			RelayTeamEnded(i, true);
}

void ZServer::UpdateUsersWinLoses() {
	bool team_won[MAX_TEAM_TYPES];

	if (!psettings.use_database)
		return;
	if (!psettings.use_mysql)
		return;

//clear
	for (int i = 0; i < MAX_TEAM_TYPES; i++)
		team_won[i] = false;

//populate team_won[]
	for (vector<ZObject*>::iterator obj = object_list.begin(); obj != object_list.end(); obj++) {
		unsigned char ot, oid;

		(*obj)->GetObjectID(ot, oid);

		if (!(ot == ROBOT_OBJECT || ot == VEHICLE_OBJECT || ot == CANNON_OBJECT))
			continue;

		team_won[(*obj)->GetOwner()] = true;
	}

//update users
	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (i->logged_in && i->team != NULL_TEAM) {
			string err_msg;

			i->total_games++;

			if (!zmysql.IncreaseUserVariable(err_msg, i->db_id, "total_games", 1))
				printf("mysql error: %s\n", err_msg.c_str());

			if (team_won[i->team]) {
				if (!zmysql.IncreaseUserVariable(err_msg, i->db_id, "wins", 1))
					printf("mysql error: %s\n", err_msg.c_str());
			} else {
				if (!zmysql.IncreaseUserVariable(err_msg, i->db_id, "loses", 1))
					printf("mysql error: %s\n", err_msg.c_str());
			}
		}

//send out updates
	for (int i = 0; i < player_info.size(); i++)
		RelayLPlayerLoginInfo(i);
}

void ZServer::InitOnlineHistory() {
	if (!psettings.use_mysql)
		return;

	next_online_history_time = current_time() + (60 * 15);
	next_online_history_player_count = 0;
	next_online_history_tray_player_count = 0;
}

void ZServer::CheckUpdateOnlineHistory() {
	double the_time = current_time();

	if (!psettings.use_mysql)
		return;

	if (the_time >= next_online_history_time) {
		string err_msg;

		//last double check
		CheckOnlineHistoryPlayerCount();
		CheckOnlineHistoryTrayPlayerCount();

		//insert
		if (!zmysql.InsertOnlineHistoryEntry(err_msg, time(0), next_online_history_player_count,
				next_online_history_tray_player_count))
			printf("mysql error: %s\n", err_msg.c_str());

		//clear
		next_online_history_time = the_time + (60 * 15);
		next_online_history_player_count = 0;
		next_online_history_tray_player_count = 0;
	}
}

void ZServer::CheckOnlineHistoryPlayerCount() {
	if (!psettings.use_mysql)
		return;

	int current_player_count = 0;

//get
	if (psettings.require_login) {
		for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
			if (i->logged_in)
				current_player_count++;
	} else {
		for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
			if (i->mode == PLAYER_MODE)
				current_player_count++;
	}

//set
	if (current_player_count > next_online_history_player_count)
		next_online_history_player_count = current_player_count;
}

void ZServer::CheckOnlineHistoryTrayPlayerCount() {
	if (!psettings.use_mysql)
		return;

	int current_player_count = 0;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (i->mode == TRAY_MODE)
			current_player_count++;

//set
	if (current_player_count > next_online_history_tray_player_count)
		next_online_history_tray_player_count = current_player_count;
}

void ZServer::AwardAffiliateCreation(string ip) {
	string err_msg;
	int users_found;

	if (!zmysql.IPInUserTable(err_msg, ip, users_found)) {
		printf("mysql error: %s\n", err_msg.c_str());
		return;
	}

//only award if this is the first creation with this ip
	if (users_found == 1) {
		int aff_id;
		bool ip_found;

		if (!zmysql.GetAffiliateId(err_msg, ip, aff_id, ip_found)) {
			printf("mysql error: %s\n", err_msg.c_str());
			return;
		}

		//the ip is in the affiliate table
		//and it was brought in bought another user
		if (ip_found && aff_id != -1) {
			//reward the user
			if (!zmysql.IncreaseUserVariable(err_msg, aff_id, "aff_creates", 1))
				printf("mysql error: %s\n", err_msg.c_str());
		}
	}
}

void ZServer::PauseGame() {
	if (ztime.IsPaused())
		return;

	ztime.Pause();

	RelayGamePaused();
}

void ZServer::ResumeGame() {
	if (!ztime.IsPaused())
		return;

	ztime.Resume();

	RelayGamePaused();
}

void ZServer::RelayObjectGrenadeAmount(ZObject *obj, int player) {
	obj_grenade_amount_packet packet; //RelayObjectGrenadeAmount

	if (!obj->CanHaveGrenades())
		return;

	packet.ref_id = obj->GetRefID();
	packet.grenade_amount = obj->GetGrenadeAmount();

	if (player == -1)
		server_socket.SendMessageAll(SET_GRENADE_AMOUNT, (const char*) &packet, sizeof(obj_grenade_amount_packet));
	else
		server_socket.SendMessage(player, SET_GRENADE_AMOUNT, (const char*) &packet, sizeof(obj_grenade_amount_packet));
}

void ZServer::RelayPortraitAnim(int ref_id, int anim_id) {
	do_portrait_anim_packet the_data;

	the_data.ref_id = ref_id;
	the_data.anim_id = anim_id;

	server_socket.SendMessageAll(DO_PORTRAIT_ANIM, (const char*) &the_data, sizeof(do_portrait_anim_packet));
}

void ZServer::RelayTeamEnded(int team, bool won) {
	team_ended_packet the_data;

	the_data.team = team;
	the_data.won = won;

	server_socket.SendMessageAll(TEAM_ENDED, (const char*) &the_data, sizeof(team_ended_packet));
}

void ZServer::RelayGamePaused(int player) {
	update_game_paused_packet packet;

	packet.game_paused = ztime.IsPaused();

	if (player == -1)
		server_socket.SendMessageAll(UPDATE_GAME_PAUSED, (const char*) &packet, sizeof(update_game_paused_packet));
	else
		server_socket.SendMessage(player, UPDATE_GAME_PAUSED, (const char*) &packet, sizeof(update_game_paused_packet));
}

void ZServer::RelayGameSpeed(int player) {
	float_packet packet;

	packet.game_speed = ztime.GameSpeed();

	if (player == -1)
		server_socket.SendMessageAll(UPDATE_GAME_SPEED, (const char*) &packet, sizeof(float_packet));
	else
		server_socket.SendMessage(player, UPDATE_GAME_SPEED, (const char*) &packet, sizeof(float_packet));
}

bool ZServer::VoteRequired() {
	int player_count = 0;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (i->mode == PLAYER_MODE) {
			player_count++;

			if (player_count >= 2)
				return true;
		}

	return false;
}

bool ZServer::StartVote(int vote_type, int value, int player) {
	if (vote_type < 0)
		return false;
	if (vote_type >= MAX_VOTE_TYPES)
		return false;

//other restrictions
	switch (vote_type) {
	case CHANGE_MAP_VOTE:
		if (value < 0 || value >= selectable_map_list.size()) {
			if (player != -1)
				SendNews(player, "invalid map choice, please type /listmaps", 0, 0, 0);
			return false;
		}
		break;
	case START_BOT:
	case STOP_BOT:
		if (value < 0)
			return false;
		if (value >= MAX_TEAM_TYPES)
			return false;
		if (value == NULL_TEAM)
			return false;
		break;
	case CHANGE_GAME_SPEED:
		if (!psettings.allow_game_speed_change) {
			if (player != -1)
				SendNews(player, "changing the game speed is not allowed on this server", 0, 0, 0);
			return false;
		}
		if (!value || value < 0) {
			if (player != -1)
				SendNews(player, "new game speed must be above zero", 0, 0, 0);
			return false;
		}
		break;
	}

	if (player != -1 && !CanVote(player)) {
		SendNews(player, "you must be logged in to start a vote, please type /help", 0, 0, 0);
		return false;
	}

	if (zvote.VoteInProgress()) {
		//it the same vote?
		if (vote_type == zvote.GetVoteType() && value == zvote.GetVoteValue())
			VoteYes(player);

		return false;
	}

//if(!VoteRequired() || (player != -1 && player_info[player].voting_power >= VotesNeeded()))
	if (!VoteRequired() || (player != -1 && player_info[player].real_voting_power() >= VotesNeeded())) {
		ProcessVote(vote_type, value);
		return false;
	}

	if (zvote.StartVote(vote_type, value)) {
		if (player != -1) {
			char message[500];
			string append_description;

			append_description = VoteAppendDescription();

			if (append_description.length())
				sprintf(message, "vote started by %s to %s: %s", player_info[player].name.c_str(),
						vote_type_string[vote_type].c_str(), append_description.c_str());
			else
				sprintf(message, "vote started by %s to %s", player_info[player].name.c_str(),
						vote_type_string[vote_type].c_str());

			//switch(vote_type)
			//{
			//case CHANGE_MAP_VOTE:
			//	sprintf(message, "vote started by %s to %s: %d. %s", player_info[player].name.c_str(), vote_type_string[vote_type].c_str(), value, selectable_map_list[value].c_str());
			//	break;
			//default:
			//	sprintf(message, "vote started by %s to %s", player_info[player].name.c_str(), vote_type_string[vote_type].c_str());
			//	break;
			//}

			BroadCastNews(message, 0, 0, 0);
		}

		ClearPlayerVotes();

		VoteYes(player);

		RelayVoteInfo();
		return true;
	} else {
		return false;
	}
}

bool ZServer::VoteYes(int player) {
	if (!zvote.VoteInProgress())
		return false;

	if (player_info[player].vote_choice != P_NULL_VOTE) {
		SendNews(player, "you have already voted", 0, 0, 0);
		return false;
	}

	if (!CanVote(player)) {
		SendNews(player, "you must be logged in to vote, please type /help", 0, 0, 0);
		return false;
	}

	player_info[player].vote_choice = P_YES_VOTE;

	RelayLPlayerVoteChoice(player);

	CheckVote();

	SendNews(player, "you have voted yes", 0, 0, 0);

	return true;
}

bool ZServer::VoteNo(int player) {
	if (!zvote.VoteInProgress())
		return false;

	if (player_info[player].vote_choice != P_NULL_VOTE) {
		SendNews(player, "you have already voted", 0, 0, 0);
		return false;
	}

	if (!CanVote(player)) {
		SendNews(player, "you must be logged in to vote, please type /help", 0, 0, 0);
		return false;
	}

	player_info[player].vote_choice = P_NO_VOTE;

	RelayLPlayerVoteChoice(player);

	CheckVote();

	SendNews(player, "you have voted no", 0, 0, 0);

	return true;
}

bool ZServer::VotePass(int player) {
	if (!zvote.VoteInProgress())
		return false;

	if (player_info[player].vote_choice != P_NULL_VOTE) {
		SendNews(player, "you have already voted", 0, 0, 0);
		return false;
	}

	if (!CanVote(player)) {
		SendNews(player, "you must be logged in to vote, please type /help", 0, 0, 0);
		return false;
	}

	player_info[player].vote_choice = P_PASS_VOTE;

	RelayLPlayerVoteChoice(player);

	CheckVote();

	SendNews(player, "you have passed on voting", 0, 0, 0);

	return true;
}

void ZServer::CheckVoteExpired() {
	if (!zvote.VoteInProgress())
		return;

	if (zvote.TimeExpired()) {
		KillVote();
		BroadCastNews("vote has expired", 0, 0, 0);
	}
}

void ZServer::KillVote() {
	if (!zvote.VoteInProgress())
		return;

	ClearPlayerVotes();
	zvote.ResetVote();
	RelayVoteInfo();
}

void ZServer::CheckVote() {
	if (!zvote.VoteInProgress())
		return;

	int votes_needed = VotesNeeded();

	if (VotesFor() >= votes_needed) {
		//process vote
		ProcessVote(zvote.GetVoteType(), zvote.GetVoteValue());

		//clean up
		KillVote();
	} else if (VotesAgainst() >= votes_needed) {
		KillVote();
	}
}

void ZServer::ProcessVote(int vote_type, int value) {
	switch (vote_type) {
	case PAUSE_VOTE:
		PauseGame();
		break;
	case RESUME_VOTE:
		ResumeGame();
		break;
	case CHANGE_MAP_VOTE:
		if (value < 0)
			break;
		if (value >= selectable_map_list.size())
			break;

		DoResetGame(selectable_map_list[value]);
		break;
	case START_BOT:
		if (!bot_thread[value])
			StartBot(value);
		SetTeamsBotsIgnored(value, false);
		break;
	case STOP_BOT:
		SetTeamsBotsIgnored(value, true);
		break;
	case RESET_GAME_VOTE:
		DoResetGame(map_name);
		break;
	case RESHUFFLE_TEAMS_VOTE:
		ReshuffleTeams();
		break;
	case CHANGE_GAME_SPEED:
		if (value <= 0)
			break;
		ChangeGameSpeed(value / 100.0);
		break;
	}
}

bool ZServer::CanVote(int player) {
	if (psettings.require_login && !player_info[player].logged_in)
		return false;

	return true;
}

void ZServer::RelayVoteInfo(int player) {
	vote_info_packet packet;

	packet.in_progress = zvote.VoteInProgress();
	packet.vote_type = zvote.GetVoteType();
	packet.value = zvote.GetVoteValue();

	if (player == -1)
		server_socket.SendMessageAll(VOTE_INFO, (const char*) &packet, sizeof(vote_info_packet));
	else
		server_socket.SendMessage(player, VOTE_INFO, (const char*) &packet, sizeof(vote_info_packet));
}

void ZServer::ClearPlayerVotes() {
	for (int i = 0; i < player_info.size(); i++) {
		player_info[i].vote_choice = P_NULL_VOTE;

		RelayLPlayerVoteChoice(i);
	}
}

void ZServer::GivePlayerID(int player) {
	player_id_packet packet;

	packet.p_id = player_info[player].p_id;

	server_socket.SendMessage(player, GIVE_PLAYER_ID, (const char*) &packet, sizeof(player_id_packet));
}

void ZServer::GivePlayerSelectableMapList(int player) {
	string send_str;

//populate send_str
	for (vector<string>::iterator i = selectable_map_list.begin(); i != selectable_map_list.end(); i++) {
		if (!send_str.length())
			send_str += *i;
		else
			send_str += "," + *i;
	}

//send
	if (send_str.length())
		server_socket.SendMessage(player, GIVE_SELECTABLE_MAP_LIST, send_str.c_str(), send_str.length() + 1);
	else
		server_socket.SendMessage(player, GIVE_SELECTABLE_MAP_LIST, NULL, 0);
}

bool ZServer::TeamHasBot(int team, bool active_only) {
	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (i->team == team && i->mode == BOT_MODE) {
			if (active_only && !i->ignored)
				return true;
			else if (!active_only)
				return true;
		}

	return false;
}

void ZServer::SetTeamsBotsIgnored(int team, bool ignored) {
	bool change_happened = false;

	if (team < 0)
		return;
	if (team >= MAX_TEAM_TYPES)
		return;

	for (int i = 0; i < player_info.size(); i++) {
		p_info &p = player_info[i];

		if (p.team == team && p.mode == BOT_MODE && p.ignored != ignored) {
			player_info[i].ignored = ignored;
			RelayLPlayerIgnored(i);

			change_happened = true;
		}
	}

	if (change_happened) {
		string send_str;

		if (ignored)
			send_str = "the " + team_type_string[team] + " team bot has been stopped";
		else
			send_str = "the " + team_type_string[team] + " team bot has been started";

		BroadCastNews(send_str.c_str());
	}
}

void ZServer::AttemptPlayerLogin(int player, string loginname, string password) {
//we using a database?
	if (!psettings.use_database) {
		SendNews(player, "login error: no database used", 0, 0, 0);
		return;
	}

//player already logged in?
	if (player_info[player].logged_in) {
		SendNews(player, "login error: please log off first", 0, 0, 0);
		return;
	}

	if (!good_user_string(loginname.c_str()) || !good_user_string(password.c_str())) {
		char message[500];

		sprintf(message, "login error: only alphanumeric characters and entries under %d characters long allowed",
				MAX_PLAYER_NAME_SIZE);
		SendNews(player, message, 0, 0, 0);

		return;
	}

//if using mysql, then attempt to log in
	if (psettings.use_mysql) {
		ZMysqlUser user;
		string err_msg;

		bool user_found;
		if (!zmysql.LoginUser(err_msg, loginname, password, user, user_found)) {
			printf("mysql error: %s\n", err_msg.c_str());

			SendNews(player, "login error: error with the user database", 0, 0, 0);
		} else {
			if (user_found) {
				user.debug();

				LoginPlayer(player, user);
			} else {
				SendNews(player, "login error: invalid login details", 0, 0, 0);
			}
		}
	}
}

void ZServer::AttemptCreateUser(int player, string username, string loginname, string password, string email) {
	if (!username.length() || !loginname.length() || !password.length() || !email.length()) {
		SendNews(player, "create user error: all entries must be filled", 0, 0, 0);
		return;
	}

	if (!psettings.use_database) {
		SendNews(player, "create user error: no database used", 0, 0, 0);
		return;
	}

//player already logged in?
	if (player_info[player].logged_in) {
		SendNews(player, "create user error: please log off first", 0, 0, 0);
		return;
	}

	if (psettings.use_mysql) {
		ZMysqlUser user;
		string err_msg;

		user.username = username;
		user.loginname = loginname;
		user.password = password;
		user.email = email;

		user.creation_time = time(0);
		user.creation_ip = player_info[player].ip;

		if (!user.format_okay()) {
			char message[500];

			sprintf(message,
					"create user error: only alphanumeric characters and entries under %d characters long allowed",
					MAX_PLAYER_NAME_SIZE);
			SendNews(player, message, 0, 0, 0);

			return;
		}

		bool user_exists;
		if (!zmysql.CheckUserExistance(err_msg, user, user_exists)) {
			printf("mysql error: %s\n", err_msg.c_str());

			SendNews(player, "create user error: error with the user database", 0, 0, 0);
		} else {
			if (user_exists) {
				SendNews(player, "create user error: user already exists", 0, 0, 0);
			} else {
				bool user_added;

				if (!zmysql.AddUser(err_msg, user, user_added)) {
					printf("mysql error: %s\n", err_msg.c_str());

					SendNews(player, "create user error: error with the user database", 0, 0, 0);
				} else {
					if (user_added) {
						AwardAffiliateCreation(user.creation_ip);

						SendNews(player, string("user " + user.username + " created").c_str(), 0, 0, 0);

						//log them in
						AttemptPlayerLogin(player, loginname, password);
					} else
						SendNews(player, "create user error: unknown error", 0, 0, 0);
				}
			}
		}
	}
}

void ZServer::SendPlayerLoginRequired(int player) {
	loginoff_packet packet;

	if (!psettings.require_login)
		packet.show_login = false;
	else
		packet.show_login = !player_info[player].logged_in;

	server_socket.SendMessage(player, GIVE_LOGINOFF, (const char*) &packet, sizeof(loginoff_packet));
}

void ZServer::MakeAllFortTurretsUnEjectable() {

	for (vector<ZObject*>::iterator i = object_list.begin(); i != object_list.end(); i++) {
		unsigned char ot, oid;

		(*i)->GetObjectID(ot, oid);

		ZCannon* cannon = dynamic_cast<ZCannon*>(*i);

		if (!cannon) {
			continue;
		}

		int x, y;

		cannon->GetCords(x, y);

		if (AreaIsFortTurret(x / 16, y / 16)) {
			cannon->SetEjectable(false);
		}

		/*
		 if (ot != CANNON_OBJECT)
		 continue;

		 int x, y;

		 (*i)->GetCords(x, y);

		 if (AreaIsFortTurret(x / 16, y / 16)) {
		 (*i)->SetEjectableCannon(false);
		 }
		 */
	}
}

bool ZServer::ChangePlayerTeam(int player, int team) {
	string send_str;

	if (team < 0)
		return false;
	if (team >= MAX_TEAM_TYPES)
		return false;

	player_info[player].team = (team_type) team;

//update player lists
	RelayLPlayerTeam(player);

	int_packet packet;

	packet.team = team;

	server_socket.SendMessage(player, SET_TEAM, (const char*) &packet, sizeof(int_packet));

	send_str = "you have been set to the " + team_type_string[player_info[player].team] + " team";
	SendNews(player, send_str.c_str(), 0, 0, 0);

	return true;
}

void ZServer::ReshuffleTeams() {
	vector<int> loggedin_to_be_changed;
	vector<int> players_to_be_changed;
	vector<int> teams_available;
	vector<int> orig_teams_available;

//collect players that need changed
	for (int i = 0; i < player_info.size(); i++)
		if (player_info[i].mode == PLAYER_MODE) {
			if (player_info[i].logged_in)
				loggedin_to_be_changed.push_back(i);
			else
				players_to_be_changed.push_back(i);
		}

//collect available teams
	{
		bool team_found[MAX_TEAM_TYPES];

		//clear
		for (int i = 0; i < MAX_TEAM_TYPES; i++)
			team_found[i] = false;

		//populate team_won[]
		for (vector<ZObject*>::iterator obj = object_list.begin(); obj != object_list.end(); obj++) {
			unsigned char ot, oid;

			(*obj)->GetObjectID(ot, oid);

			if (!(ot == ROBOT_OBJECT || ot == VEHICLE_OBJECT || ot == CANNON_OBJECT))
				continue;

			team_found[(*obj)->GetOwner()] = true;
		}

		for (int i = RED_TEAM; i < MAX_TEAM_TYPES; i++)
			if (team_found[i] && !TeamHasBot(i, true))
				teams_available.push_back(i);
	}

	if (!loggedin_to_be_changed.size() && !players_to_be_changed.size()) {
		BroadCastNews("reshuffle teams error: no players to shuffle");
		return;
	}

	if (!teams_available.size()) {
		BroadCastNews("reshuffle teams error: no available teams to shuffle to");
		return;
	}

//set this
	orig_teams_available = teams_available;

//logged in players first
	for (vector<int>::iterator i = loggedin_to_be_changed.begin(); i != loggedin_to_be_changed.end(); i++) {
		player_info[*i].team = NULL_TEAM;

		if (!teams_available.size())
			teams_available = orig_teams_available;
		if (!teams_available.size())
			return;

		int random_c;

		random_c = rand() % teams_available.size();

		ChangePlayerTeam(*i, teams_available[random_c]);

		teams_available.erase(teams_available.begin() + random_c);
	}

//other players
	for (vector<int>::iterator i = players_to_be_changed.begin(); i != players_to_be_changed.end(); i++) {
		player_info[*i].team = NULL_TEAM;

		if (!teams_available.size())
			teams_available = orig_teams_available;
		if (!teams_available.size())
			return;

		int random_c;

		random_c = rand() % teams_available.size();

		ChangePlayerTeam(*i, teams_available[random_c]);

		teams_available.erase(teams_available.begin() + random_c);
	}

	BroadCastNews("the teams have been reshuffled");
}

void ZServer::CheckPlayerSuggestions() {
	double the_time = current_time();

	if (the_time >= next_make_suggestions_time) {
		next_make_suggestions_time = the_time + 30;

		SuggestReshuffleTeams();
	}
}

bool ZServer::SuggestReshuffleTeams() {
	int players_on_team[MAX_TEAM_TYPES];
	int bots_on_team[MAX_TEAM_TYPES];
	int only_player_team = -1;

//clear
	for (int i = 0; i < MAX_TEAM_TYPES; i++) {
		players_on_team[i] = 0;
		bots_on_team[i] = 0;
	}

	for (int i = 0; i < player_info.size(); i++) {
		if (player_info[i].ignored)
			continue;

		switch (player_info[i].mode) {
		case PLAYER_MODE:
			if (psettings.require_login && player_info[i].logged_in)
				players_on_team[player_info[i].team]++;
			else if (!psettings.require_login)
				players_on_team[player_info[i].team]++;
			break;
		case BOT_MODE:
			bots_on_team[player_info[i].team]++;
			break;
		}
	}

//find the only player team
	for (int i = RED_TEAM; i < MAX_TEAM_TYPES; i++)
		if (players_on_team[i]) {
			if (only_player_team == -1)
				only_player_team = i;
			else
				return false;
		}

//any players on a team?
	if (only_player_team == -1)
		return false;

//any bots on other teams?
	for (int i = RED_TEAM; i < MAX_TEAM_TYPES; i++)
		if (bots_on_team[i] && i != only_player_team)
			return false;

//ok we need to suggest either to reshuffle or turn on bots
	if (players_on_team[only_player_team] > 1)
		BroadCastNews("please type in /help to learn about the commands /changeteam, /reshuffleteams, and /startbot");
	else
		BroadCastNews("please type in /help to learn about the command /startbot");

	return true;
}

void ZServer::ChangeGameSpeed(float new_speed) {
	char send_str[1500];

//set
	ztime.SetGameSpeed(new_speed);

//tell them all the news
	RelayGameSpeed();

	sprintf(send_str, "game speed changed to %d%%", (int) (ztime.GameSpeed() * 100));
	BroadCastNews(send_str);
}

void ZServer::SetupEHandler() {
	ehandler.SetParent(this);
	ehandler.AddFunction(TCP_EVENT, DEBUG_EVENT_, handle_test_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_MAP, handle_request_map_event);
	ehandler.AddFunction(TCP_EVENT, GIVE_PLAYER_NAME, handle_set_name_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_OBJECTS, handle_send_object_list_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_ZONES, handle_send_zone_info_list_event);
	ehandler.AddFunction(TCP_EVENT, SET_NAME, handle_set_player_name_event);
	ehandler.AddFunction(TCP_EVENT, SET_TEAM, handle_set_player_team_event);
	ehandler.AddFunction(TCP_EVENT, SEND_WAYPOINTS, handle_rcv_object_waypoints_event);
	ehandler.AddFunction(TCP_EVENT, SEND_RALLYPOINTS, handle_rcv_object_rallypoints_event);
	ehandler.AddFunction(TCP_EVENT, START_BUILDING, handle_start_building_event);
	ehandler.AddFunction(TCP_EVENT, STOP_BUILDING, handle_stop_building_event);
	ehandler.AddFunction(TCP_EVENT, REPLACE_BUILDING, handle_replace_building_event);
	ehandler.AddFunction(TCP_EVENT, PLACE_CANNON, handle_place_cannon_event);
	ehandler.AddFunction(TCP_EVENT, SEND_CHAT, handle_relay_chat_event);
	ehandler.AddFunction(TCP_EVENT, EJECT_VEHICLE, handle_exit_vehicle_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_SETTINGS, handle_request_settings_event);
	ehandler.AddFunction(TCP_EVENT, SET_PLAYER_MODE, handle_set_player_mode_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_PLAYER_LIST, handle_request_player_list_event);
	ehandler.AddFunction(TCP_EVENT, SEND_BOT_BYPASS_DATA, handle_bot_login_bypass_event);
	ehandler.AddFunction(TCP_EVENT, GET_GAME_PAUSED, handle_get_pause_game_event);
	ehandler.AddFunction(TCP_EVENT, SET_GAME_PAUSED, handle_set_pause_game_event);
	ehandler.AddFunction(TCP_EVENT, START_VOTE, handle_start_vote_event);
	ehandler.AddFunction(TCP_EVENT, VOTE_YES, handle_vote_yes_event);
	ehandler.AddFunction(TCP_EVENT, VOTE_NO, handle_vote_no_event);
	ehandler.AddFunction(TCP_EVENT, VOTE_PASS, handle_vote_pass_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_PLAYER_ID, handle_request_player_id_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_SELECTABLE_MAP_LIST, handle_request_selectable_map_list_event);
	ehandler.AddFunction(TCP_EVENT, SEND_LOGIN, handle_player_login_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_LOGINOFF, handle_request_login_required_event);
	ehandler.AddFunction(TCP_EVENT, CREATE_USER, handle_create_user_event);
	ehandler.AddFunction(TCP_EVENT, BUY_REGKEY, handle_buy_regkey_event);
	ehandler.AddFunction(TCP_EVENT, GET_GAME_SPEED, handle_get_game_speed_event);
	ehandler.AddFunction(TCP_EVENT, SET_GAME_SPEED, handle_set_game_speed_event);
	ehandler.AddFunction(TCP_EVENT, ADD_BUILDING_QUEUE, handle_add_building_queue_event);
	ehandler.AddFunction(TCP_EVENT, CANCEL_BUILDING_QUEUE, handle_cancel_building_queue_event);
	ehandler.AddFunction(TCP_EVENT, RESHUFFLE_TEAMS, handle_reshuffle_teams_event);
	ehandler.AddFunction(TCP_EVENT, START_BOT_EVENT, handle_start_bot_event);
	ehandler.AddFunction(TCP_EVENT, STOP_BOT_EVENT, handle_stop_bot_event);
	ehandler.AddFunction(TCP_EVENT, SELECT_MAP, handle_select_map_event);
	ehandler.AddFunction(TCP_EVENT, RESET_MAP, handle_reset_map_event);
	ehandler.AddFunction(TCP_EVENT, REQUEST_VERSION, handle_request_version_event);

	ehandler.AddFunction(OTHER_EVENT, CONNECT_EVENT, handle_connect_event);
	ehandler.AddFunction(OTHER_EVENT, DISCONNECT_EVENT, handle_disconnect_event);
}

void ZServer::handle_test_event(ZServer *p, char *data, int size, int player) {
	char message[500];

	if (size)
		printf("ZServer::test_event:%s... from player %d\n", data, player);

	sprintf(message, "hello player %d", player);
	p->server_socket.SendMessageAscii(player, DEBUG_EVENT_, message);
}

void ZServer::handle_connect_event(ZServer *p, char *data, int size, int player) {
	p_info new_entry;

	p->BroadCastNews("a player connected");

	if (player < p->player_info.size()) {
		printf("ZServer::connect_event: tried to add a player that is already on the list [p:%d] vs [size:%s]\n",
				player, (char*) p->player_info.size());
		return;
	}

//set ip
	if (size)
		new_entry.ip = data;

	p->player_info.push_back(new_entry);

	{
		add_remove_player_packet p1;

		p1.p_id = new_entry.p_id;

		//add it
		p->server_socket.SendMessageAll(ADD_LPLAYER, (char*) &p1, sizeof(add_remove_player_packet));
	}
}

void ZServer::handle_disconnect_event(ZServer *p, char *data, int size, int player) {
	p_info new_entry;

	p->BroadCastNews("a player disconnected");

	if (player >= p->player_info.size()) {
		printf("ZServer::disconnect_event: tried to remove a player that is not on the list [p:%d] vs [size:%s]\n",
				player, (char*) p->player_info.size());
		return;
	}

//logout player
	p->LogoutPlayer(player, false);

//announce delete
	{
		add_remove_player_packet packet;

		packet.p_id = p->player_info[player].p_id;

		p->server_socket.SendMessageAll(DELETE_LPLAYER, (char*) &packet, sizeof(add_remove_player_packet));
	}

	p->player_info.erase(p->player_info.begin() + player);
}

void ZServer::handle_request_map_event(ZServer *p, char *data, int size, int player) {

	const int buf_size = 1024 * 4;
	char buf[buf_size + 4];
	int pack_num = -1;
	int read_in;
	int read_point = 0;

	if (p->zmap.Loaded()) {
		char *map_data;
		int map_data_size;

		p->zmap.GetMapData(map_data, map_data_size);

		while (map_data_size > 0) {
			pack_num++;

			if (map_data_size >= buf_size) {
				memcpy(buf + 4, map_data + read_point, buf_size);
				read_in = buf_size;
				read_point += read_in;
				map_data_size -= read_in;
			} else {
				memcpy(buf + 4, map_data + read_point, map_data_size);
				read_in = map_data_size;
				read_point += read_in;
				map_data_size = 0;
			}

			memcpy(buf, &pack_num, 4);
			p->server_socket.SendMessage(player, STORE_MAP, buf, 4 + read_in);
		}
	}

	pack_num = -1;
	memcpy(buf, &pack_num, 4);
	p->server_socket.SendMessage(player, STORE_MAP, buf, 4);

	/*
	 //send the map file
	 if(p->zmap.Loaded())
	 {
	 FILE *fp;

	 fp = fopen(p->map_name.c_str(), "rb");

	 if(fp)
	 {
	 pack_num = 0;

	 while(1)
	 {
	 read_in = fread(buf + 4, 1, buf_size, fp);

	 // 				printf("read_in:%d\n", read_in);

	 if(read_in > 0)
	 {
	 memcpy(buf, &pack_num, 4);
	 p->server_socket.SendMessage(player, STORE_MAP, buf, 4 + read_in);

	 pack_num++;
	 }
	 else
	 break;
	 }

	 pack_num = -1;

	 fclose(fp);
	 }
	 }

	 memcpy(buf, &pack_num, 4);
	 p->server_socket.SendMessage(player, STORE_MAP, buf, 4);
	 */
}

void ZServer::handle_set_name_event(ZServer *p, char *data, int size, int player) {
//if(!size) return;
//
////is the last char a null character?
//if(data[size]) return;
//
//printf("p->player_info.size():%d\n", p->player_info.size());

//p->player_info[player].name = data;
}

void ZServer::handle_send_object_list_event(ZServer *p, char *data, int size, int player) {

	for (vector<ZObject*>::iterator i = p->object_list.begin(); i != p->object_list.end(); i++) {
		object_init_packet object_info;

		//pack the object info
		p->RelayNewObject(*i, player);

		//send health
		p->RelayObjectHealth(*i, player);

		//send building state and repair anim info
		p->RelayBuildingState(*i, player);

		//send building queue list - sent with state
		//p->RelayObjectBuildingQueue(*i, player);

		//send group information - sent with RelayNewObject
		//p->RelayObjectGroupInfo(*i, player);

		//send grenade amount
		p->RelayObjectGrenadeAmount(*i, player);

		//send rally points
		p->RelayObjectRallyPoints(*i, player);
	}
}

void ZServer::handle_send_zone_info_list_event(ZServer *p, char *data, int size, int player) {
	int i;

	for (i = 0; i < p->zmap.GetZoneInfoList().size(); i++) {
		zone_info_packet packet_info;

		packet_info.zone_number = i;
		packet_info.owner = p->zmap.GetZoneInfoList()[i].owner;

		p->server_socket.SendMessage(player, SET_ZONE_INFO, (char*) &packet_info, sizeof(zone_info_packet));
	}
}

void ZServer::handle_set_player_name_event(ZServer *p, char *data, int size, int player) {
	int i;
	char message[500];

//checks
	if (size <= 0)
		return;

//it got a null character?
	for (i = 0; i < size; i++)
		if (!data[i])
			break;

	if (i == size)
		return;

//max size?
	if (size > MAX_PLAYER_NAME_SIZE) {
		data[MAX_PLAYER_NAME_SIZE] = 0;
	}

	if (p->psettings.require_login) {
		p->SendNews(player, "set name error: login required, please type /help", 0, 0, 0);
		return;
	}

	sprintf(message, "player '%s' set their name to '%s'", p->player_info[player].name.c_str(), data);

	p->player_info[player].name = data;

	p->BroadCastNews(message);

//update player lists
	p->RelayLPlayerName(player);
//{
//	char *data;
//	int size;

//	size = sizeof(int) + p->player_info[player].name.size() + 1;
//	data = (char*)malloc(size);

//	memcpy(data, &(p->player_info[player].p_id), sizeof(int));
//	memcpy(data+sizeof(int), p->player_info[player].name.c_str(), p->player_info[player].name.size());
//	*(data+sizeof(int)+p->player_info[player].name.size()) = 0;

//	p->server_socket.SendMessageAll(SET_LPLAYER_NAME, data, size);

//	free(data);
//}
}

void ZServer::handle_set_player_team_event(ZServer *p, char *data, int size, int player) {
	int the_team;
	char message[500];

	if (size != 4)
		return;

	the_team = *(int*) data;

	if (the_team < NULL_TEAM)
		return;
	if (the_team >= MAX_TEAM_TYPES)
		return;

	p->ChangePlayerTeam(player, the_team);

	p->player_info[player].team = (team_type) the_team;

//sprintf(message, "player '%s' set their team to the %s team", p->player_info[player].name.c_str(), team_type_string[the_team].c_str());
//p->BroadCastNews(message);

//update player lists
	p->RelayLPlayerTeam(player);
//{
//	set_player_int_packet packet;

//	packet.p_id = p->player_info[player].p_id;
//	packet.value = p->player_info[player].team;

//	p->server_socket.SendMessageAll(SET_LPLAYER_TEAM, (char*)&packet, sizeof(set_player_int_packet));
//}
}

void ZServer::handle_rcv_object_waypoints_event(ZServer *p, char *data, int size, int player) {
	ZObject *our_object;

//logged in?
//if(p->psettings.require_login && !p->player_info[player].logged_in)
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "move unit error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "move unit error: player currently ignored", 0, 0, 0);
		return;
	}

//do server tests
	{
		int expected_packet_size;
		int waypoint_amount;
		int ref_id;
		ZObject *our_object;

		//does it hold the header info?
		if (size < 8)
			return;

		//get header
		ref_id = ((int*) data)[0];
		waypoint_amount = ((int*) data)[1];

		expected_packet_size = 8 + (waypoint_amount * sizeof(waypoint));

		//should we toss this packet for bad data?
		if (size != expected_packet_size)
			return;

		//find our object
		our_object = p->GetObjectFromID(ref_id);

		//not found?
		if (!our_object)
			return;

		//passes activation check?
		unsigned char ot, oid;
		our_object->GetObjectID(ot, oid);
		if (!p->ActivationCheckPassed(player) && UnitRequiresActivation(ot, oid)) {
			p->SendNews(player, "move unit error: activation required, please visit www.nighsoft.com", 0, 0, 0);
			return;
		}
	}

	our_object = p->ProcessWaypointData(data, size, true, p->player_info[player].team);

//did any objects get their waypoint list updated?
	if (!our_object)
		return;

//check to make sure this isn't a bunk list
//p->CheckObjectWaypoints(our_object);

//just left cannon = false
	our_object->SetJustDisembarked(false);

//clone
	our_object->CloneMinionWayPoints();

//now relay
	p->RelayObjectWayPoints(our_object);

//check to see if we need to force it to stop
	if (!our_object->GetWayPointList().size()) {
		our_object->StopMove();
		p->RelayObjectLoc(our_object);

		for (vector<ZObject*>::iterator i = our_object->GetMinionList().begin(); i != our_object->GetMinionList().end();
				i++) {
			(*i)->StopMove();
			p->RelayObjectLoc((*i));
		}
	}
}

void ZServer::handle_rcv_object_rallypoints_event(ZServer *p, char *data, int size, int player) {
	ZObject *our_object;

//logged in?
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "set rally points error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "set rally points error: player currently ignored", 0, 0, 0);
		return;
	}

	our_object = p->ProcessRallypointData(data, size, true, p->player_info[player].team);

//did any objects get their waypoint list updated?
	if (!our_object)
		return;

//now relay
	p->RelayObjectRallyPoints(our_object);
}

void ZServer::handle_start_building_event(ZServer *p, char *data, int size, int player) {
	start_building_packet *pi = (start_building_packet*) data;
	ZObject *obj;

//good packet?
	if (size != sizeof(start_building_packet))
		return;

	obj = p->GetObjectFromID(pi->ref_id);

	if (!obj)
		return;

//is this a building that can build?
	if (!obj->ProducesUnits())
		return;
//unsigned char ot, oid;
//obj->GetObjectID(ot, oid);
//if(ot != BUILDING_OBJECT) return;
//if(!(oid == FORT_FRONT || oid == FORT_BACK || oid == ROBOT_FACTORY || oid == VEHICLE_FACTORY)) return;

//is the team business kosher?
	if (obj->GetOwner() == NULL_TEAM)
		return;
	if (p->player_info[player].team != obj->GetOwner())
		return;

//logged in?
//if(p->psettings.require_login && !p->player_info[player].logged_in)
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "start production error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "start production error: player currently ignored", 0, 0, 0);
		return;
	}

//ok it is a building that can build so lets set its new production
	if (obj->SetBuildingProduction(pi->ot, pi->oid)) {
		//now let us tell everyone the building's new state
		p->RelayBuildingState(obj);

		//tell the person who did it
		computer_msg_packet send_data;
		send_data.ref_id = obj->GetRefID();
		send_data.sound = COMP_STARTING_MANUFACTURE_SND;
		p->server_socket.SendMessage(player, COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
	}
}

void ZServer::handle_stop_building_event(ZServer *p, char *data, int size, int player) {
	int ref_id;
	ZObject *obj;

//good packet?
	if (size != sizeof(int))
		return;

	ref_id = *(int*) data;

	obj = p->GetObjectFromID(ref_id);

	if (!obj)
		return;

//is this a building that can build?
	unsigned char ot, oid;

	obj->GetObjectID(ot, oid);

	if (ot != BUILDING_OBJECT)
		return;
	if (!(oid == FORT_FRONT || oid == FORT_BACK || oid == ROBOT_FACTORY || oid == VEHICLE_FACTORY))
		return;

//is the team business kosher?
	if (obj->GetOwner() == NULL_TEAM)
		return;
	if (p->player_info[player].team != obj->GetOwner())
		return;

//logged in?
//if(p->psettings.require_login && !p->player_info[player].logged_in)
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "stop production error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "stop production error: player currently ignored", 0, 0, 0);
		return;
	}

//stop it!!!
	if (obj->StopBuildingProduction()) {
		//now let us tell everyone the building's new state
		p->RelayBuildingState(obj);

		//tell the person who did it
		computer_msg_packet send_data;
		send_data.ref_id = obj->GetRefID();
		send_data.sound = COMP_MANUFACTURING_CANCELED_SND;
		p->server_socket.SendMessage(player, COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
	}
}

void ZServer::handle_replace_building_event(ZServer *p, char *data, int size, int player) {
	start_building_packet *pi = (start_building_packet*) data;
	ZObject *obj;

//good packet?
	if (size != sizeof(start_building_packet))
		return;

	obj = p->GetObjectFromID(pi->ref_id);

	if (!obj)
		return;

//is this a building that can build?
	if (!obj->ProducesUnits())
		return;
//unsigned char ot, oid;
//obj->GetObjectID(ot, oid);
//if(ot != BUILDING_OBJECT) return;
//if(!(oid == FORT_FRONT || oid == FORT_BACK || oid == ROBOT_FACTORY || oid == VEHICLE_FACTORY)) return;

//is the team business kosher?
	if (obj->GetOwner() == NULL_TEAM)
		return;
	if (p->player_info[player].team != obj->GetOwner())
		return;

//logged in?
//if(p->psettings.require_login && !p->player_info[player].logged_in)
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "start production error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "start production error: player currently ignored", 0, 0, 0);
		return;
	}

// Cancel current production:
	obj->StopBuildingProduction();

//ok it is a building that can build so lets set its new production
	if (obj->SetBuildingProduction(pi->ot, pi->oid)) {
		//now let us tell everyone the building's new state
		p->RelayBuildingState(obj);

		//tell the person who did it
		computer_msg_packet send_data;
		send_data.ref_id = obj->GetRefID();
		send_data.sound = COMP_STARTING_MANUFACTURE_SND;
		p->server_socket.SendMessage(player, COMP_MSG, (char*) &send_data, sizeof(computer_msg_packet));
	}
}

void ZServer::handle_place_cannon_event(ZServer *p, char *data, int size, int player) {

	place_cannon_packet *pi = (place_cannon_packet*) data;
	ZObject *obj, *new_obj;

//good packet?
	if (size != sizeof(place_cannon_packet))
		return;

	obj = p->GetObjectFromID(pi->ref_id);

	if (!obj)
		return;

	if (!obj->HaveStoredCannon(pi->oid))
		return;

//this object belong to a zone?
	if (!obj->GetConnectedZone())
		return;

//this player can place this cannon?
	if (obj->GetOwner() == NULL_TEAM)
		return;
	if (p->player_info[player].team != obj->GetOwner())
		return;

//this placement place ok?
	if (!p->CannonPlacable(obj, pi->tx, pi->ty))
		return;

//remove it
	obj->RemoveStoredCannon(pi->oid);

//place it
	new_obj = p->CreateObject(CANNON_OBJECT, pi->oid, pi->tx * 16, pi->ty * 16, obj->GetOwner());

//is this area a fort turret?
	/*
	 if (p->AreaIsFortTurret(pi->tx, pi->ty))
	 new_obj->SetEjectableCannon(false);
	 */
	p->MakeAllFortTurretsUnEjectable();

//announce new object
	p->RelayNewObject(new_obj);

//announce amount of cannons in building
	p->RelayBuildingBuiltCannons(obj);

//as with all creation / deletions
	p->ProcessChangeObjectAmount();
}

void ZServer::handle_relay_chat_event(ZServer *p, char *data, int size, int player) {
	string out_message;
	string in_message;

//we are not relaying nothing
	if (size <= 1)
		return;

//is there a null terminator?
	if (data[size - 1])
		return;

//is rest of it ascii?

//is it a command?
	in_message = data;
	if (!in_message.length())
		return;
	if (in_message[0] == '/') {
		p->ProcessPlayerCommand(player, in_message.substr(1));
		return;
	}

//prep message
	out_message = p->player_info[player].name + ":: " + in_message;

	int t;
	t = p->player_info[player].team;

	p->BroadCastNews(out_message.c_str(), team_color[t].r * 0.3, team_color[t].g * 0.3, team_color[t].b * 0.3);
}

/**
 * Handler for the event that is raised when units leave a vehicle.
 */
void ZServer::handle_exit_vehicle_event(ZServer *p, char *data, int size, int player) {

	eject_vehicle_packet *pi = (eject_vehicle_packet*) data;
	ZObject *obj, *leader;

//good packet?
	if (size != sizeof(eject_vehicle_packet)) {
		return;
	}

	obj = p->GetObjectFromID(pi->ref_id);

	if (!obj) {
		return;
	}

//same team?
	if (obj->GetOwner() == NULL_TEAM) {
		return;
	}

	if (p->player_info[player].team != obj->GetOwner()) {
		return;
	}

//an object that can eject?

	ZMannedObject* manned = dynamic_cast<ZMannedObject*>(obj);

	if (manned) {

		if (!manned->GetEjectable()) {
			return;
		}

		//create robot(s)

		if (manned->GetDrivers().size()) {

			if (manned->GetDriverType() >= 0 && manned->GetDriverType() < MAX_ROBOT_TYPES) {

				// Get vehicle coordinates.
				int x, y;
				manned->GetCords(x, y);

				vector<driver_info_s>::iterator driver_i = manned->GetDrivers().begin();

				// Create a new group of robots with so many units as there were drivers in the vehicle:
				leader = p->CreateRobotGroup(manned->GetDriverType(), x, y, manned->GetOwner(), NULL,
						manned->GetDrivers().size());

				//annouce leader
				{
					p->RelayNewObject(leader);

					//health
					leader->SetHealth(driver_i->health, p->zmap);
					p->UpdateObjectHealth(leader);

					// Set experience:
					leader->SetExperience(driver_i->experience);
					p->UpdateObjectExperience(leader);

				}

				//just left a cannon?
				unsigned char ot, oid;
				manned->GetObjectID(ot, oid);

				if (ot == CANNON_OBJECT || true) {
					leader->SetJustDisembarked(true);
				}

				//and the minions...
				driver_i++;

				for (vector<ZObject*>::iterator iter_minion = leader->GetMinionList().begin();
						iter_minion != leader->GetMinionList().end() && driver_i != manned->GetDrivers().end();
						iter_minion++) {

					p->RelayNewObject((*iter_minion));

					// Set health:
					(*iter_minion)->SetHealth(driver_i->health, p->zmap);
					p->UpdateObjectHealth((*iter_minion));

					// Set experience:
					(*iter_minion)->SetExperience(driver_i->experience);
					p->UpdateObjectExperience((*iter_minion));

					//just left cannon?
					if (ot == CANNON_OBJECT || true) {
						(*iter_minion)->SetJustDisembarked(true);
					}

					driver_i++;
				}

				// Finally, clear out the drivers from the vehicle:
				manned->ClearDrivers();
			}

			//we need to kill the waypoints too
			{
				char *data;
				int size;

				//clear
				obj->GetWayPointList().clear();

				//now relay
				p->RelayObjectWayPoints(obj);

				//force it to stop moving
				if (obj->StopMove())
					p->RelayObjectLoc(obj);

				//force it to stop attacking
				obj->SetAttackObject(NULL);
				p->RelayObjectAttackObject(obj);
			}
		}

		//new team
		p->SetObjectTeam(obj, NULL_TEAM);

	}
}

void ZServer::handle_request_settings_event(ZServer *p, char *data, int size, int player) {
	p->server_socket.SendMessage(player, SET_SETTINGS, (char*) &p->zsettings, sizeof(ZSettings));
}

void ZServer::handle_set_player_mode_event(ZServer *p, char *data, int size, int player) {
	player_mode_packet *pi = (player_mode_packet*) data;

//good packet?
	if (size != sizeof(player_mode_packet))
		return;

//good setting
	if (pi->mode < 0)
		return;
	if (pi->mode >= MAX_PLAYER_MODES)
		return;

	p->player_info[player].mode = (player_mode) pi->mode;

//ignore bot?
	if (p->player_info[player].mode == BOT_MODE && p->psettings.bots_start_ignored && !p->player_info[player].ignored) {
		p->player_info[player].ignored = true;
		p->RelayLPlayerIgnored(player);
	}

//update player lists
	p->RelayLPlayerMode(player);

//set history stuff
	p->CheckOnlineHistoryPlayerCount();
	p->CheckOnlineHistoryTrayPlayerCount();

//printf("ZServer::set_player_mode_event:p:%d m:%s\n", player, player_mode_string[p->player_info[player].mode].c_str());
}

void ZServer::handle_request_player_list_event(ZServer *p, char *data, int size, int player) {
//clear list
	p->server_socket.SendMessage(player, CLEAR_PLAYER_LIST, NULL, 0);

//send the list
//for(vector<p_info>::iterator i=p->player_info.begin(); i!=p->player_info.end(); i++)
	for (int i = 0; i < p->player_info.size(); i++) {
		p->RelayLAdd(i, player);

		p->RelayLPlayerName(i, player);
		p->RelayLPlayerTeam(i, player);
		p->RelayLPlayerMode(i, player);
		p->RelayLPlayerIgnored(i, player);
		p->RelayLPlayerLoginInfo(i, player);
		p->RelayLPlayerVoteChoice(i, player);
	}
}

void ZServer::handle_bot_login_bypass_event(ZServer *p, char *data, int size, int player) {
	if (p->bot_bypass_size < 1 || p->bot_bypass_size > MAX_BOT_BYPASS_SIZE) {
		printf("ZServer::bot_login_bypass_event:: invalid bot_bypass_size\n");
		return;
	}

//size ok?
	if (size != p->bot_bypass_size)
		return;

//is it a match?
	for (int i = 0; i < size; i++)
		if (data[i] != p->bot_bypass_data[i])
			return;

//do a bot bypass

//clear up
	p->player_info[player].logout();

//set
	p->player_info[player].name = "Bot";
	p->player_info[player].bot_logged_in = true;

//tell everyone
	p->RelayLPlayerName(player);
	p->RelayLPlayerLoginInfo(player);
}

void ZServer::handle_get_pause_game_event(ZServer *p, char *data, int size, int player) {
	p->RelayGamePaused(player);
}

void ZServer::handle_set_pause_game_event(ZServer *p, char *data, int size, int player) {
	update_game_paused_packet *pi = (update_game_paused_packet*) data;

//good packet?
	if (size != sizeof(update_game_paused_packet))
		return;

	if (pi->game_paused && p->ztime.IsPaused())
		return;
	if (!pi->game_paused && !p->ztime.IsPaused())
		return;

	if (pi->game_paused)
		p->StartVote(PAUSE_VOTE, -1, player);
	else
		p->StartVote(RESUME_VOTE, -1, player);

//if(pi->game_paused)
//	p->PauseGame();
//else
//	p->ResumeGame();
}

void ZServer::handle_start_vote_event(ZServer *p, char *data, int size, int player) {
	vote_info_packet *pi = (vote_info_packet*) data;

//good packet?
	if (size != sizeof(vote_info_packet))
		return;

	if (p->StartVote(pi->vote_type, pi->value))
		p->VoteYes(player);
}

void ZServer::handle_vote_yes_event(ZServer *p, char *data, int size, int player) {
	p->VoteYes(player);
}

void ZServer::handle_vote_no_event(ZServer *p, char *data, int size, int player) {
	p->VoteNo(player);
}

void ZServer::handle_vote_pass_event(ZServer *p, char *data, int size, int player) {
	p->VotePass(player);
}

void ZServer::handle_request_player_id_event(ZServer *p, char *data, int size, int player) {
	p->GivePlayerID(player);
}

void ZServer::handle_request_selectable_map_list_event(ZServer *p, char *data, int size, int player) {
	p->GivePlayerSelectableMapList(player);
}

void ZServer::handle_player_login_event(ZServer *p, char *data, int size, int player) {
	if (size <= 1)
		return;

	char loginname[500];
	char password[500];
	int i = 0;

	split(loginname, data, ',', &i, 500, size - 1);
	split(password, data, ',', &i, 500, size - 1);

	if (!loginname[0])
		return;
	if (!password[0])
		return;

	if (!good_user_string(loginname))
		return;
	if (!good_user_string(password))
		return;

	p->AttemptPlayerLogin(player, loginname, password);
}

void ZServer::handle_request_login_required_event(ZServer *p, char *data, int size, int player) {
	p->SendPlayerLoginRequired(player);
}

void ZServer::handle_create_user_event(ZServer *p, char *data, int size, int player) {
	if (size <= 1)
		return;

	char username[500];
	char loginname[500];
	char password[500];
	char email[500];
	int i = 0;

	split(username, data, ',', &i, 500, size - 1);
	split(loginname, data, ',', &i, 500, size - 1);
	split(password, data, ',', &i, 500, size - 1);
	split(email, data, ',', &i, 500, size - 1);

	if (!username[0])
		return;
	if (!loginname[0])
		return;
	if (!password[0])
		return;
	if (!email[0])
		return;

	if (!good_user_string(username))
		return;
	if (!good_user_string(loginname))
		return;
	if (!good_user_string(password))
		return;
	if (!good_user_string(email))
		return;

	p->AttemptCreateUser(player, username, loginname, password, email);
}

void ZServer::handle_buy_regkey_event(ZServer *p, char *data, int size, int player) {
	buy_registration_packet *pi = (buy_registration_packet*) data;

//good packet?
	if (size != sizeof(buy_registration_packet))
		return;

#ifdef DISABLE_BUYREG
	p->SendNews(player, "buy reg key error: please visit www.nighsoft.com", 0, 0, 0);
	return;
#else
//using db etc?
	if (!p->psettings.use_database || !p->psettings.use_mysql) {
		p->SendNews(player, "buy reg key error: server not configured for selling registration keys", 0, 0, 0);
		return;
	}

//user logged in?
	if (!p->player_info[player].logged_in) {
		p->SendNews(player, "buy reg key error: you must be logged in", 0, 0, 0);
		return;
	}

//user activated?
	if (!p->player_info[player].activated) {
		p->SendNews(player, "buy reg key error: you must be activated, please visit www.nighsoft.com", 0, 0, 0);
		return;
	}

//user got 1 VP?
	if (p->player_info[player].voting_power - REGISTRATION_COST < 0) {
		p->SendNews(player, "buy reg key error: more voting power to spend required, please visit www.nighsoft.com", 0,
				0, 0);
		return;
	}

//kill some voting power
	p->player_info[player].voting_power -= REGISTRATION_COST;
	p->RelayLPlayerLoginInfo(player);

//mysql
	{
		string err_msg;

		if (!p->zmysql.IncreaseUserVariable(err_msg, p->player_info[player].db_id, "voting_power", -REGISTRATION_COST))
			printf("mysql error: %s\n", err_msg.c_str());
	}

//send the key
	buy_registration_packet packet;

	p->zencrypt.AES_Encrypt(pi->buf, 16, packet.buf);

	p->server_socket.SendMessage(player, RETURN_REGKEY, (const char*) &packet, sizeof(buy_registration_packet));

//log the transaction
	printd_reg(
			(char*) string(
					p->player_info[player].name + " bought a registration key for "
							+ data_to_hex_string((unsigned char*) pi->buf, 16)).c_str());

#endif
}

void ZServer::handle_get_game_speed_event(ZServer *p, char *data, int size, int player) {
	p->RelayGameSpeed(player);
}

void ZServer::handle_set_game_speed_event(ZServer *p, char *data, int size, int player) {
	float_packet *pi = (float_packet*) data;
	int new_speed;

//good packet?
	if (size != sizeof(float_packet))
		return;

	new_speed = pi->game_speed * 100;

	p->StartVote(CHANGE_GAME_SPEED, new_speed, player);
}

void ZServer::handle_add_building_queue_event(ZServer *p, char *data, int size, int player) {
	add_building_queue_packet *pi = (add_building_queue_packet*) data;
	ZObject *obj;

//good packet?
	if (size != sizeof(add_building_queue_packet))
		return;

	obj = p->GetObjectFromID(pi->ref_id);

	if (!obj)
		return;

//printf("add queue %d %d\n", pi->ot, pi->oid);

//is the team business kosher?
	if (obj->GetOwner() == NULL_TEAM)
		return;
	if (p->player_info[player].team != obj->GetOwner())
		return;

//produces units?
	if (!obj->ProducesUnits())
		return;

//logged in?
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "add queue error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "add queue error: player currently ignored", 0, 0, 0);
		return;
	}

//are we starting production or adding to the queue?
	unsigned char bot, boid;

	ZBuilding* building = dynamic_cast<ZBuilding*>(obj);

	if (building) {

		if (building->GetBuildUnit(bot, boid) && bot == (unsigned char) -1 && boid == (unsigned char) -1) {
			if (building->SetBuildingProduction(pi->ot, pi->oid))
				p->RelayBuildingState(obj);
		} else {
			if (building->AddBuildingQueue(pi->ot, pi->oid))
				p->RelayObjectBuildingQueue(obj);
		}
	}
}

void ZServer::handle_cancel_building_queue_event(ZServer *p, char *data, int size, int player) {
	cancel_building_queue_packet *pi = (cancel_building_queue_packet*) data;
	ZObject *obj;

//good packet?
	if (size != sizeof(cancel_building_queue_packet))
		return;

	obj = p->GetObjectFromID(pi->ref_id);

	if (!obj)
		return;

//is the team business kosher?
	if (obj->GetOwner() == NULL_TEAM)
		return;
	if (p->player_info[player].team != obj->GetOwner())
		return;

//produces units?
	if (!obj->ProducesUnits())
		return;

//logged in?
	if (p->LoginCheckDenied(player)) {
		p->SendNews(player, "cancel queue error: login required, please type /help", 0, 0, 0);
		return;
	}

//ignored?
	if (p->player_info[player].ignored) {
		p->SendNews(player, "cancel queue error: player currently ignored", 0, 0, 0);
		return;
	}

	ZBuilding* building = dynamic_cast<ZBuilding*>(obj);

	if (building) {

	if (building->CancelBuildingQueue(pi->list_i, pi->ot, pi->oid))
		p->RelayObjectBuildingQueue(obj);
	}
}

void ZServer::handle_reshuffle_teams_event(ZServer *p, char *data, int size, int player) {
	p->StartVote(RESHUFFLE_TEAMS_VOTE, -1, player);
}

void ZServer::handle_start_bot_event(ZServer *p, char *data, int size, int player) {
	int_packet *pi = (int_packet*) data;

//good packet?
	if (size != sizeof(int_packet))
		return;

//team ok?
	if (pi->team < 0)
		return;
	if (pi->team >= MAX_TEAM_TYPES)
		return;
	if (pi->team == NULL_TEAM)
		return;

	p->StartVote(START_BOT, pi->team, player);
}

void ZServer::handle_stop_bot_event(ZServer *p, char *data, int size, int player) {
	int_packet *pi = (int_packet*) data;

//good packet?
	if (size != sizeof(int_packet))
		return;

//team ok?
	if (pi->team < 0)
		return;
	if (pi->team >= MAX_TEAM_TYPES)
		return;
	if (pi->team == NULL_TEAM)
		return;

	p->StartVote(STOP_BOT, pi->team, player);
}

void ZServer::handle_select_map_event(ZServer *p, char *data, int size, int player) {
	int_packet *pi = (int_packet*) data;

//good packet?
	if (size != sizeof(int_packet))
		return;

	p->StartVote(CHANGE_MAP_VOTE, pi->map_num, player);
}

void ZServer::handle_reset_map_event(ZServer *p, char *data, int size, int player) {
	p->StartVote(RESET_GAME_VOTE, -1, player);
}

void ZServer::handle_request_version_event(ZServer *p, char *data, int size, int player) {
	p->RelayVersion(player);
}

