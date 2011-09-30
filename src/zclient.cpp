#include "zclient.h"
#include "common.h"

using namespace COMMON;

ZClient::ZClient() : ZCore() {
	remote_address = "localhost";
	player_name = "nameless_player";
	our_team = NULL_TEAM;
	our_mode = NOBODY_MODE;
	p_id = -1;

	map_data = 0;
	map_data_size = 0;

	//setup events
	SetupEHandler();
	//give the tcp socket the event list so it can cram in events
	client_socket.SetEventList(&eventHandler.GetEventList());
}

void ZClient::SetRemoteAddress(string ipaddress) {
	remote_address = ipaddress;
}

void ZClient::ProcessConnect() {
	//ask for the version
	client_socket.SendMessage(REQUEST_VERSION, NULL, 0);

	//ask for game paused
	client_socket.SendMessage(GET_GAME_PAUSED, NULL, 0);

	//ask for game speed
	client_socket.SendMessage(GET_GAME_SPEED, NULL, 0);

	//ask for settings
	client_socket.SendMessage(REQUEST_SETTINGS, NULL, 0);

	//give our name and mode
	//client_socket.SendMessageAscii(GIVE_PLAYER_NAME, player_name.c_str());
	SendPlayerInfo();

	//ask for player list
	RequestPlayerList();

	//ask for the selectable map list
	client_socket.SendMessage(REQUEST_SELECTABLE_MAP_LIST, NULL, 0);

	//ask for the map
	client_socket.SendMessage(REQUEST_MAP, NULL, 0);
}

void ZClient::ProcessDisconnect() {
	printf("ZClient::disconnected from the game server...\n");
}

void ZClient::SetPlayerName(string player_name_) {
	player_name = player_name_;
}

void ZClient::SetPlayerTeam(team_type player_team) {
	our_team = player_team;
}

void ZClient::SetDesiredTeam(team_type player_team) {
	desired_team = player_team;
}

int ZClient::ProcessMapDownload(char *data, int size) {
	//static char *map_data = 0;
	//static int map_data_size;
	int pack_num;
	int data_size;

	data_size = size - 4;

	//1 = done, 0 still going

	if (size < 4)
		return 1;

	memcpy(&pack_num, data, 4);

	//last packet?
	if (pack_num == -1) {
		if (map_data && map_data_size > 0) {
			//load map
			printf("map_data_size:%d\n", map_data_size);
			zmap.Read(map_data, map_data_size);

			//free mem
			free(map_data);
			map_data = 0;
			map_data_size = 0;
		}

		//SendPlayerInfo();
		RequestObjectList();
		RequestZoneList();

		//return "loaded"
		return 1;
	}

// 	printf("pushing map data:%d:%d\n", pack_num, data_size);

	//first packet?
	if (!pack_num) {
		if (map_data) {
			free(map_data);
			map_data = 0;
			map_data_size = 0;
		}

		//stuff that data in our buffer
		map_data = (char*) malloc(data_size);
		memcpy(map_data, data + 4, data_size);
		map_data_size += data_size;
	} else {
		//resize and stuff that data in our buffer
		map_data = (char*) realloc(map_data, map_data_size + data_size);
		memcpy(map_data + map_data_size, data + 4, data_size);
		map_data_size += data_size;
	}

	return 0;
}

void ZClient::RequestObjectList() {
	client_socket.SendMessage(REQUEST_OBJECTS, NULL, 0);
}

void ZClient::RequestZoneList() {
	client_socket.SendMessage(REQUEST_ZONES, NULL, 0);
}

void ZClient::SendPlayerInfo() {
	int the_team;

	the_team = desired_team;

	client_socket.SendMessageAscii(SET_NAME, player_name.c_str());
	SendPlayerTeam(the_team);
	//client_socket.SendMessage(SET_TEAM, (char*)&the_team, 4);

	SendPlayerMode();
}

void ZClient::SendPlayerTeam(int new_team) {
	client_socket.SendMessage(SET_TEAM, (char*) &new_team, 4);
}

void ZClient::SendPlayerMode() {
	player_mode_packet packet;

	packet.mode = our_mode;

	client_socket.SendMessage(SET_PLAYER_MODE, (char*) &packet, sizeof(player_mode_packet));
}

void ZClient::RequestPlayerList() {
	client_socket.SendMessage(REQUEST_PLAYER_ID, NULL, 0);
	client_socket.SendMessage(REQUEST_PLAYER_LIST, NULL, 0);
}

void ZClient::SendBotBypassData() {
	if (bot_bypass_size < 1 || bot_bypass_size > MAX_BOT_BYPASS_SIZE) {
		printf("ZClient::SendBotBypassData:: invalid bot_bypass_size\n");
		return;
	}

	client_socket.SendMessage(SEND_BOT_BYPASS_DATA, bot_bypass_data, bot_bypass_size);
}

p_info &ZClient::OurPInfo() {
	static p_info not_found;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (i->p_id == p_id)
			return *i;

	return not_found;
}

void ZClient::DeleteObjectCleanUp(ZObject *obj) {

}

ZObject* ZClient::ProcessNewObject(char *data, int size) {

	object_init_packet *o = (object_init_packet*) data;

	//good packet?
	if (size != sizeof(object_init_packet)) {
		return NULL;
	}

	//this a ok creation?
	if (!CreateObjectOk(o->object_type, o->object_id, o->x, o->y, o->owner, o->blevel, o->extra_links)) {
		return NULL;
	}

	bool setOwner = (our_team == o->owner);

	return MakeNewObject(100, o->blevel, o->ref_id, o->x, o->y, o->owner, o->object_type, o->object_id, o->extra_links, setOwner, NULL);
}


void ZClient::ProcessZoneInfo(char *data, int size) {
	zone_info_packet *pi = (zone_info_packet*) data;

	//good packet?
	if (size != sizeof(zone_info_packet))
		return;

	//will this data crash us?
	if (pi->owner < 0)
		return;
	if (pi->owner >= MAX_TEAM_TYPES)
		return;
	if (pi->zone_number < 0)
		return;
	if (pi->zone_number >= zmap.GetZoneInfoList().size())
		return;

	//printf("ProcessZoneInfo::id:%d owner:%d\n", pi->zone_number, pi->owner);

	//set the zone info
	zmap.GetZoneInfoList()[pi->zone_number].owner = (team_type) pi->owner;

	//for the buildings
	ResetZoneOwnagePercentages();
}

ZObject* ZClient::ProcessObjectTeam(char *data, int size) {

	//got the header?
	if (size < sizeof(object_team_packet)) {
		return NULL;
	}

	object_team_packet *pi = (object_team_packet*) data;

	//packet good?
	if (size != sizeof(object_team_packet) + (pi->driver_amount * sizeof(driver_info_s))) {
		return NULL;
	}

	//will this data crash us?
	if (pi->owner < 0) {
		return NULL;
	}

	if (pi->owner >= MAX_TEAM_TYPES) {
		return NULL;
	}

	ZObject* obj = GetObjectFromID(pi->ref_id);

	if (!obj) {
		return NULL;
	}

	obj->SetOwner((team_type) pi->owner);
	//obj->SetDriver(pi->driver_type, 1);

	ZMannedObject* manned = dynamic_cast<ZMannedObject*>(obj);

	if (manned) {
		manned->SetDriverType(pi->driver_type);
		manned->ClearDrivers();

		int shift_amt = sizeof(object_team_packet);

		for (int i = 0; i < pi->driver_amount; i++) {

			manned->AddDriver(*(driver_info_s*) (data + shift_amt));
			shift_amt += sizeof(driver_info_s);
		}

	}

	//printf("ProcessObjectTeam:obj driver set to %s\n", robot_type_string[obj->GetDriver()].c_str());

	return obj;
}

ZObject* ZClient::ProcessObjectAttackObject(char *data, int size) {
	attack_object_packet *pi = (attack_object_packet*) data;
	ZObject *obj;
	ZObject *attack_obj;

	//good packet?
	if (size != sizeof(attack_object_packet))
		return NULL;

	obj = GetObjectFromID(pi->ref_id);
	attack_obj = GetObjectFromID(pi->attack_object_ref_id);

	if (!obj)
		return NULL;

	obj->SetAttackObject(attack_obj);

	return obj;
}

ZObject* ZClient::ProcessDeleteObject(char *data, int size) {
	int ref_id;
	ZObject *obj;

	if (size != sizeof(int))
		return NULL;

	ref_id = *(int*) data;

	obj = GetObjectFromID(ref_id);

	if (!obj)
		return NULL;

	//clean up while it still exists
	DeleteObjectCleanUp(obj);

	//vaporize it
	ols.DeleteObject(obj);

	return obj;
}

ZObject* ZClient::ProcessObjectHealthTeam(char *data, int size) {
	object_health_packet *pi = (object_health_packet*) data;

	//good packet?
	if (size != sizeof(object_health_packet))
		return NULL;

	ZObject *obj = GetObjectFromID(pi->ref_id);

	if (!obj)
		return NULL;

	obj->SetHealth(pi->health, zmap);

	return obj;
}

ZObject* ZClient::ProcessFireMissile(char *data, int size) {
	fire_missile_packet *pi = (fire_missile_packet*) data;
	ZObject *obj;

	//good packet?
	if (size != sizeof(fire_missile_packet))
		return NULL;

	obj = GetObjectFromID(pi->ref_id);

	if (!obj)
		return NULL;

	obj->FireMissile(pi->x, pi->y);

	return obj;
}

ZObject* ZClient::ProcessObjectLoc(char *data, int size) {
	ZObject *obj;
	int ref_id;
	object_location new_loc;

	if (size != 4 + sizeof(object_location))
		return NULL;

	ref_id = ((int*) data)[0];
	memcpy(&new_loc, data + 4, sizeof(object_location));

	obj = GetObjectFromID(ref_id);

	if (!obj)
		return NULL;

	obj->SetLoc(new_loc);

	return obj;
}

ZObject* ZClient::ProcessBuildingState(char *data, int size) {
	set_building_state_packet *pi = (set_building_state_packet*) data;
	ZObject *obj;

	//good packet?
	if (size != sizeof(set_building_state_packet))
		return NULL;

	obj = GetObjectFromID(pi->ref_id);

	if (!obj)
		return NULL;

	obj->ProcessSetBuildingStateData(data, size);

	return obj;
}

ZObject* ZClient::ProcessBuildingQueueList(char *data, int size) {
	ZObject *obj;
	int ref_id;

	//does it hold the header info?
	if (size < 8)
		return NULL;

	//get header
	ref_id = ((int*) data)[0];

	obj = GetObjectFromID(ref_id);

	if (!obj)
		return NULL;

	obj->ProcessBuildingQueueData(data, size);

	return obj;
}

ZObject* ZClient::ProcessBuildingCannonList(char *data, int size) {
	int ref_id;
	ZObject *obj;

	//good packet?
	if (size < 8)
		return NULL;

	ref_id = *(int*) (data);

	obj = GetObjectFromID(ref_id);

	if (!obj)
		return NULL;

	obj->ProcessSetBuiltCannonData(data, size);

	return obj;
}

ZObject* ZClient::ProcessObjectGroupInfo(char *data, int size) {

	//needs to atleast hold the ref_id at this point
	if (size < 4)
		return NULL;

	int ref_id = *(int*) data;

	ZObject* obj = GetObjectFromID(ref_id);

	if (!obj)
		return NULL;

	obj->ProcessGroupInfoData(data, size, object_list);

	return obj;
}

ZObject* ZClient::ProcessObjectLidState(char *data, int size) {
	set_lid_state_packet *pi = (set_lid_state_packet*) data;
	ZObject *obj;

	//good packet?
	if (size != sizeof(set_lid_state_packet))
		return NULL;

	obj = GetObjectFromID(pi->ref_id);

	if (!obj)
		return NULL;

	ZVehicle* vehicle = dynamic_cast<ZVehicle*>(obj);

	if (vehicle) {
		vehicle->SetLidState(pi->lid_open);
	}

	return obj;
}

ZObject* ZClient::ProcessSetGrenadeState(char *data, int size) {
	obj_grenade_amount_packet *pi = (obj_grenade_amount_packet*) data;
	ZObject *obj;

	//good packet?
	if (size != sizeof(obj_grenade_amount_packet))
		return NULL;

	obj = GetObjectFromID(pi->ref_id);

	if (!obj)
		return NULL;

	obj->SetGrenadeAmount(pi->grenade_amount);

	return obj;
}

bool ZClient::ProcessZSettings(char *data, int size) {
	//good packet?
	if (size != sizeof(ZSettings))
		return false;

	//stuff it
	memcpy(&zsettings, data, sizeof(ZSettings));

	return true;
}

bool ZClient::ProcessAddLPlayer(char *data, int size) {
	add_remove_player_packet *pi = (add_remove_player_packet*) data;

	//good packet?
	if (size != sizeof(add_remove_player_packet))
		return false;

	player_info.push_back(p_info(pi->p_id));

	return true;
}

bool ZClient::ProcessDeleteLPlayer(char *data, int size) {
	add_remove_player_packet *pi = (add_remove_player_packet*) data;

	//good packet?
	if (size != sizeof(add_remove_player_packet))
		return false;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end();) {
		if (pi->p_id == i->p_id)
			i = player_info.erase(i);
		else
			i++;
	}

	return true;
}

bool ZClient::ProcessSetLPlayerName(char *data, int size) {
	int p_id;

	//good packet?
	if (size < 5)
		return false;

	//last char must be null
	if (data[size - 1])
		return false;

	memcpy(&p_id, data, sizeof(int));

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (p_id == i->p_id)
			i->name = (data + sizeof(int));

	return true;
}

bool ZClient::ProcessSetLPlayerTeam(char *data, int size) {
	set_player_int_packet *pi = (set_player_int_packet*) data;

	//good packet?
	if (size != sizeof(set_player_int_packet))
		return false;

	if (pi->value < 0)
		return false;
	if (pi->value >= MAX_TEAM_TYPES)
		return false;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (pi->p_id == i->p_id)
			i->team = (team_type) pi->value;

	return true;
}

bool ZClient::ProcessSetLPlayerMode(char *data, int size) {
	set_player_int_packet *pi = (set_player_int_packet*) data;

	//good packet?
	if (size != sizeof(set_player_int_packet))
		return false;

	if (pi->value < 0)
		return false;
	if (pi->value >= MAX_PLAYER_MODES)
		return false;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (pi->p_id == i->p_id)
			i->mode = (player_mode) pi->value;

	return true;
}

bool ZClient::ProcessSetLPlayerIgnored(char *data, int size) {
	set_player_int_packet *pi = (set_player_int_packet*) data;

	//good packet?
	if (size != sizeof(set_player_int_packet))
		return false;

	if (pi->value < 0)
		return false;
	if (pi->value >= MAX_PLAYER_MODES)
		return false;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (pi->p_id == i->p_id)
			i->ignored = (player_mode) pi->value;

	return true;
}

bool ZClient::ProcessSetLPlayerLogInfo(char *data, int size) {
	set_player_loginfo_packet *pi = (set_player_loginfo_packet*) data;

	//good packet?
	if (size != sizeof(set_player_loginfo_packet))
		return false;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (pi->p_id == i->p_id) {
			i->db_id = pi->db_id;
			i->activated = pi->activated;
			i->logged_in = pi->logged_in;
			i->bot_logged_in = pi->bot_logged_in;
			i->voting_power = pi->voting_power;
			i->total_games = pi->total_games;
		}

	return true;
}

bool ZClient::ProcessSetLPlayerVoteInfo(char *data, int size) {
	set_player_int_packet *pi = (set_player_int_packet*) data;

	//good packet?
	if (size != sizeof(set_player_int_packet))
		return false;

	if (pi->value < 0)
		return false;
	if (pi->value >= P_MAX_VOTE_CHOICES)
		return false;

	for (vector<p_info>::iterator i = player_info.begin(); i != player_info.end(); i++)
		if (pi->p_id == i->p_id)
			i->vote_choice = pi->value;

	return true;
}

bool ZClient::ProcessUpdateGamePaused(char *data, int size) {
	update_game_paused_packet *pi = (update_game_paused_packet*) data;

	//good packet?
	if (size != sizeof(update_game_paused_packet))
		return false;

	if (pi->game_paused)
		ztime.Pause();
	else
		ztime.Resume();

	return true;
}

bool ZClient::ProcessUpdateGameSpeed(char *data, int size) {
	float_packet *pi = (float_packet*) data;

	//good packet?
	if (size != sizeof(float_packet))
		return false;

	ztime.SetGameSpeed(pi->game_speed);

	return true;
}

bool ZClient::ProcessVoteInfo(char *data, int size) {
	vote_info_packet *pi = (vote_info_packet*) data;

	//good packet?
	if (size != sizeof(vote_info_packet))
		return false;

	zvote.SetVoteInProgress(pi->in_progress);
	zvote.SetVoteType(pi->vote_type);
	zvote.SetVoteValue(pi->value);

	return true;
}

bool ZClient::ProcessPlayerID(char *data, int size) {
	player_id_packet *pi = (player_id_packet*) data;

	//good packet?
	if (size != sizeof(player_id_packet))
		return false;

	p_id = pi->p_id;

	return true;
}

bool ZClient::ProcessSelectableMapList(char *data, int size) {
	selectable_map_list.clear();

	if (size <= 1)
		return true;

	const int buf_size = 500;
	int len = size - 1;
	char buf[500];
	int i = 0;

	while (i < len) {
		split(buf, data, ',', &i, buf_size, len);

		selectable_map_list.push_back(buf);
	}

	//blah
	//{
	//	printf("loaded selectable map list:\n");

	//	for(i=0; i<selectable_map_list.size(); i++)
	//		printf("\t%d) '%s'\n", i, selectable_map_list[i].c_str());
	//}

	return true;
}

bool ZClient::ProcessSetTeam(char *data, int size) {
	int_packet *pi = (int_packet*) data;

	//good packet?
	if (size != sizeof(int_packet))
		return false;

	if (pi->team < 0)
		return true;
	if (pi->team >= MAX_TEAM_TYPES)
		return true;

	SetPlayerTeam((team_type) pi->team);

	return true;
}

void ZClient::ProcessEndGame() {

}

void ZClient::ProcessResetGame() {
	//clear stuff out
	zmap.ClearMap();

	//clear object list
	ols.DeleteAllObjects();
	//for(vector<ZObject*>::iterator obj=object_list.begin(); obj!=object_list.end(); obj++)
	//	delete *obj;

	//object_list.clear();

	//ask for the map
	client_socket.SendMessage(REQUEST_MAP, NULL, 0);
}

void ZClient::ProcessSocketEvents() {
	char *message;
	int size;
	int pack_id;
	SocketHandler* shandler;
	int packets_processed = 0;
	double time_took;

	shandler = client_socket.GetHandler();

	if (!shandler)
		return;

	//not connected, free it up
	if (!shandler->Connected()) {
		client_socket.ClearConnection();
		eventHandler.ProcessEvent(OTHER_EVENT, DISCONNECT_EVENT, NULL, 0, 0);
	} else if (shandler->DoRecv()) {
		while (shandler->DoFastProcess(&message, &size, &pack_id)) {
			eventHandler.ProcessEvent(TCP_EVENT, pack_id, message, size, 0);

			// This is from ZPlayer:
			//	eventHandler.ProcessEvent(TCP_EVENT, pack_id, message, size, 0);

			// This is from ZBot:
			//	eventHandler.ProcessEvent(OTHER_EVENT, DISCONNECT_EVENT, NULL, 0, 0);

		}
		shandler->ResetFastProcess();
	}
}

void ZClient::SetupEHandler() {

	eventHandler.SetParent(this);

	// TCP Events:
	eventHandler.AddFunction(TCP_EVENT, DEBUG_EVENT_, handle_test_event);
	eventHandler.AddFunction(TCP_EVENT, STORE_MAP, handle_store_map_event);
	eventHandler.AddFunction(TCP_EVENT, ADD_NEW_OBJECT, handle_add_new_object_event);
	eventHandler.AddFunction(TCP_EVENT, SET_ZONE_INFO, handle_set_zone_info_event);
	eventHandler.AddFunction(TCP_EVENT, NEWS_EVENT, handle_display_news_event);
	eventHandler.AddFunction(TCP_EVENT, SEND_WAYPOINTS, handle_set_object_waypoints_event);
	eventHandler.AddFunction(TCP_EVENT, SEND_RALLYPOINTS, handle_set_object_rallypoints_event);
	eventHandler.AddFunction(TCP_EVENT, SEND_LOC, handle_set_object_loc_event);
	eventHandler.AddFunction(TCP_EVENT, SET_OBJECT_TEAM, handle_set_object_team_event);
	eventHandler.AddFunction(TCP_EVENT, SET_ATTACK_OBJECT, handle_set_object_attack_object_event);
	eventHandler.AddFunction(TCP_EVENT, DELETE_OBJECT, handle_delete_object_event);
	eventHandler.AddFunction(TCP_EVENT, UPDATE_HEALTH, handle_set_object_health_event);
	eventHandler.AddFunction(TCP_EVENT, END_GAME, handle_end_game_event);
	eventHandler.AddFunction(TCP_EVENT, RESET_GAME, handle_reset_game_event);
	eventHandler.AddFunction(TCP_EVENT, FIRE_MISSILE, handle_fire_object_missile_event);
	eventHandler.AddFunction(TCP_EVENT, SET_EXPERIENCE, handle_set_object_experience_event);
//	eventHandler.AddFunction(TCP_EVENT, SET_LEADER, set_leader_event);
	eventHandler.AddFunction(TCP_EVENT, DESTROY_OBJECT, handle_destroy_object_event);
	eventHandler.AddFunction(TCP_EVENT, SET_BUILDING_STATE, handle_set_building_state_event);
	eventHandler.AddFunction(TCP_EVENT, SET_BUILT_CANNON_AMOUNT, handle_set_building_cannon_list_event);
	eventHandler.AddFunction(TCP_EVENT, COMP_MSG, handle_set_computer_message_event);
	eventHandler.AddFunction(TCP_EVENT, OBJECT_GROUP_INFO, handle_set_object_group_info_event);
	eventHandler.AddFunction(TCP_EVENT, DO_CRANE_ANIM, handle_do_crane_anim_event);
	eventHandler.AddFunction(TCP_EVENT, SET_REPAIR_ANIM, handle_set_repair_building_anim_event);
	eventHandler.AddFunction(TCP_EVENT, SET_SETTINGS, handle_set_settings_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LID_OPEN, handle_set_lid_open_event);
	eventHandler.AddFunction(TCP_EVENT, SNIPE_OBJECT, handle_snipe_object_event);
	eventHandler.AddFunction(TCP_EVENT, DRIVER_HIT_EFFECT, handle_driver_hit_effect_event);
	eventHandler.AddFunction(TCP_EVENT, CLEAR_PLAYER_LIST, handle_clear_player_list_event);
	eventHandler.AddFunction(TCP_EVENT, ADD_LPLAYER, handle_add_player_event);
	eventHandler.AddFunction(TCP_EVENT, DELETE_LPLAYER, handle_delete_player_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LPLAYER_NAME, handle_set_player_name_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LPLAYER_TEAM, handle_set_player_team_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LPLAYER_MODE, handle_set_player_mode_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LPLAYER_IGNORED, handle_set_player_ignored_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LPLAYER_LOGINFO, handle_set_player_loginfo_event);
	eventHandler.AddFunction(TCP_EVENT, SET_LPLAYER_VOTEINFO, handle_set_player_voteinfo_event);
	eventHandler.AddFunction(TCP_EVENT, UPDATE_GAME_PAUSED, handle_update_game_paused_event);
	eventHandler.AddFunction(TCP_EVENT, UPDATE_GAME_SPEED, handle_update_game_speed_event);
	eventHandler.AddFunction(TCP_EVENT, VOTE_INFO, handle_set_vote_info_event);
	eventHandler.AddFunction(TCP_EVENT, GIVE_PLAYER_ID, handle_set_player_id_event);
	eventHandler.AddFunction(TCP_EVENT, GIVE_SELECTABLE_MAP_LIST, handle_set_selectable_map_list_event);
	eventHandler.AddFunction(TCP_EVENT, GIVE_LOGINOFF, handle_display_login_event);
	eventHandler.AddFunction(TCP_EVENT, SET_GRENADE_AMOUNT, handle_set_grenade_amount_event);
	eventHandler.AddFunction(TCP_EVENT, PICKUP_GRENADE_ANIM, handle_pickup_grenade_event);
	eventHandler.AddFunction(TCP_EVENT, DO_PORTRAIT_ANIM, handle_do_portrait_anim_event);
	eventHandler.AddFunction(TCP_EVENT, TEAM_ENDED, handle_team_ended_event);
	eventHandler.AddFunction(TCP_EVENT, SET_TEAM, handle_set_team_event);
	eventHandler.AddFunction(TCP_EVENT, POLL_BUY_REGKEY, handle_poll_buy_regkey_event);
	eventHandler.AddFunction(TCP_EVENT, RETURN_REGKEY, handle_set_regkey_event);
	eventHandler.AddFunction(TCP_EVENT, SET_BUILDING_QUEUE_LIST, handle_set_build_queue_list_event);
	eventHandler.AddFunction(TCP_EVENT, REQUEST_VERSION, handle_request_version_event);
	eventHandler.AddFunction(TCP_EVENT, GIVE_VERSION, handle_get_version_event);

	// Other Events:
	eventHandler.AddFunction(OTHER_EVENT, CONNECT_EVENT, handle_connect_event);
	eventHandler.AddFunction(OTHER_EVENT, DISCONNECT_EVENT, handle_disconnect_event);

	// SDL Events:
	eventHandler.AddFunction(SDL_EVENT, RESIZE_EVENT, resize_event);
	eventHandler.AddFunction(SDL_EVENT, LCLICK_EVENT, lclick_event);
	eventHandler.AddFunction(SDL_EVENT, LUNCLICK_EVENT, lunclick_event);
	eventHandler.AddFunction(SDL_EVENT, RCLICK_EVENT, rclick_event);
	eventHandler.AddFunction(SDL_EVENT, RUNCLICK_EVENT, runclick_event);
	eventHandler.AddFunction(SDL_EVENT, MCLICK_EVENT, mclick_event);
	eventHandler.AddFunction(SDL_EVENT, MUNCLICK_EVENT, munclick_event);
	eventHandler.AddFunction(SDL_EVENT, WHEELUP_EVENT, wheelup_event);
	eventHandler.AddFunction(SDL_EVENT, WHEELDOWN_EVENT, wheeldown_event);
	eventHandler.AddFunction(SDL_EVENT, KEYDOWN_EVENT_, keydown_event);
	eventHandler.AddFunction(SDL_EVENT, KEYUP_EVENT_, keyup_event);
	eventHandler.AddFunction(SDL_EVENT, MOTION_EVENT, motion_event);

}

// ################# BEGIN Static SDL Event handler wrappers ##########################

void ZClient::resize_event(ZClient *p, char *data, int size, int dummy) {
	p->resize_event(data, size, dummy);
}
void ZClient::lclick_event(ZClient *p, char *data, int size, int dummy) {
	p->lclick_event(data, size, dummy);
}
void ZClient::lunclick_event(ZClient *p, char *data, int size, int dummy) {
	p->lunclick_event(data, size, dummy);
}
void ZClient::rclick_event(ZClient *p, char *data, int size, int dummy) {
	p->rclick_event(data, size, dummy);
}
void ZClient::runclick_event(ZClient *p, char *data, int size, int dummy) {
	p->runclick_event(data, size, dummy);
}
void ZClient::mclick_event(ZClient *p, char *data, int size, int dummy) {
	p->mclick_event(data, size, dummy);
}
void ZClient::munclick_event(ZClient *p, char *data, int size, int dummy) {
	p->munclick_event(data, size, dummy);
}
void ZClient::wheelup_event(ZClient *p, char *data, int size, int dummy) {
	p->wheelup_event(data, size, dummy);
}
void ZClient::wheeldown_event(ZClient *p, char *data, int size, int dummy) {
	p->wheeldown_event(data, size, dummy);
}
void ZClient::keydown_event(ZClient *p, char *data, int size, int dummy) {
	p->keydown_event(data, size, dummy);
}
void ZClient::keyup_event(ZClient *p, char *data, int size, int dummy) {
	p->keyup_event(data, size, dummy);
}
void ZClient::motion_event(ZClient *p, char *data, int size, int dummy) {
	p->motion_event(data, size, dummy);
}
// ################# END Static SDL Event handler wrappers ##########################


//################# BEGIN Static TCP Event handler wrappers ########################

void ZClient::handle_add_new_object_event(ZClient *p, char *data, int size, int dummy) {
	p->add_new_object_event(data, size, dummy);
}

void ZClient::handle_add_player_event(ZClient *p, char *data, int size, int dummy) {
	p->add_player_event(data, size, dummy);
}

void ZClient::handle_clear_player_list_event(ZClient *p, char *data, int size, int dummy) {
	p->clear_player_list_event(data, size, dummy);
}

void ZClient::handle_connect_event(ZClient *p, char *data, int size, int dummy) {
	p->connect_event(data, size, dummy);
}

void ZClient::handle_delete_object_event(ZClient *p, char *data, int size, int dummy) {
	p->delete_object_event(data, size, dummy);
}

void ZClient::handle_delete_player_event(ZClient *p, char *data, int size, int dummy) {
	p->delete_player_event(data, size, dummy);
}

void ZClient::handle_destroy_object_event(ZClient *p, char *data, int size, int dummy) {
	p->destroy_object_event(data, size, dummy);
}

void ZClient::handle_disconnect_event(ZClient *p, char *data, int size, int dummy) {
	p->disconnect_event(data, size, dummy);
}

void ZClient::handle_display_login_event(ZClient *p, char *data, int size, int dummy) {
	p->display_login_event(data, size, dummy);
}

void ZClient::handle_display_news_event(ZClient *p, char *data, int size, int dummy) {
	p->display_news_event(data, size, dummy);
}

void ZClient::handle_do_crane_anim_event(ZClient *p, char *data, int size, int dummy) {
	p->do_crane_anim_event(data, size, dummy);
}

void ZClient::handle_do_portrait_anim_event(ZClient *p, char *data, int size, int dummy) {
	p->do_portrait_anim_event(data, size, dummy);
}

void ZClient::handle_driver_hit_effect_event(ZClient *p, char *data, int size, int dummy) {
	p->driver_hit_effect_event(data, size, dummy);
}

void ZClient::handle_end_game_event(ZClient *p, char *data, int size, int dummy) {
	p->end_game_event(data, size, dummy);
}

void ZClient::handle_fire_object_missile_event(ZClient *p, char *data, int size, int dummy) {
	p->fire_object_missile_event(data, size, dummy);
}

void ZClient::handle_get_version_event(ZClient *p, char *data, int size, int dummy) {
	p->get_version_event(data, size, dummy);
}


void ZClient::handle_pickup_grenade_event(ZClient *p, char *data, int size, int dummy) {
	p->pickup_grenade_event(data, size, dummy);
}


void ZClient::handle_poll_buy_regkey_event(ZClient *p, char *data, int size, int dummy) {
	p->poll_buy_regkey(data, size, dummy);
}

void ZClient::handle_request_version_event(ZClient *p, char *data, int size, int dummy) {
	p->request_version_event(data, size, dummy);
}

void ZClient::handle_reset_game_event(ZClient *p, char *data, int size, int dummy) {
	p->reset_game_event(data, size, dummy);
}

void ZClient::handle_set_build_queue_list_event(ZClient *p, char *data, int size, int dummy) {
	p->set_build_queue_list_event(data, size, dummy);
}

void ZClient::handle_set_building_cannon_list_event(ZClient *p, char *data, int size, int dummy) {
	p->set_building_cannon_list_event(data, size, dummy);
}

void ZClient::handle_set_building_state_event(ZClient *p, char *data, int size, int dummy) {
	p->set_building_state_event(data, size, dummy);
}

void ZClient::handle_set_computer_message_event(ZClient *p, char *data, int size, int dummy) {
	p->set_computer_message_event(data, size, dummy);
}

void ZClient::handle_set_object_experience_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_experience_event(data, size, dummy);
}

void ZClient::handle_set_grenade_amount_event(ZClient *p, char *data, int size, int dummy) {
	p->set_grenade_amount_event(data, size, dummy);
}

void ZClient::handle_set_lid_open_event(ZClient *p, char *data, int size, int dummy) {
	p->set_lid_open_event(data, size, dummy);
}

void ZClient::handle_set_object_attack_object_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_attack_object_event(data, size, dummy);
}

void ZClient::handle_set_object_group_info_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_group_info_event(data, size, dummy);
}

void ZClient::handle_set_object_health_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_health_event(data, size, dummy);
}

void ZClient::handle_set_object_loc_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_loc_event(data, size, dummy);
}

void ZClient::handle_set_object_rallypoints_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_rallypoints_event(data, size, dummy);
}

void ZClient::handle_set_object_team_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_team_event(data, size, dummy);
}

void ZClient::handle_set_object_waypoints_event(ZClient *p, char *data, int size, int dummy) {
	p->set_object_waypoints_event(data, size, dummy);
}

void ZClient::handle_set_player_id_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_id_event(data, size, dummy);
}

void ZClient::handle_set_player_ignored_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_ignored_event(data, size, dummy);
}

void ZClient::handle_set_player_loginfo_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_loginfo_event(data, size, dummy);
}

void ZClient::handle_set_player_mode_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_mode_event(data, size, dummy);
}

void ZClient::handle_set_player_name_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_name_event(data, size, dummy);
}

void ZClient::handle_set_player_team_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_team_event(data, size, dummy);
}

void ZClient::handle_set_player_voteinfo_event(ZClient *p, char *data, int size, int dummy) {
	p->set_player_voteinfo_event(data, size, dummy);
}

void ZClient::handle_set_regkey_event(ZClient *p, char *data, int size, int dummy) {
	p->set_regkey(data, size, dummy);
}

void ZClient::handle_set_repair_building_anim_event(ZClient *p, char *data, int size, int dummy) {
	p->set_repair_building_anim_event(data, size, dummy);
}

void ZClient::handle_set_selectable_map_list_event(ZClient *p, char *data, int size, int dummy) {
	p->set_selectable_map_list_event(data, size, dummy);
}

void ZClient::handle_set_settings_event(ZClient *p, char *data, int size, int dummy) {
	p->set_settings_event(data, size, dummy);
}

void ZClient::handle_set_team_event(ZClient *p, char *data, int size, int dummy) {
	p->set_team_event(data, size, dummy);
}

void ZClient::handle_set_vote_info_event(ZClient *p, char *data, int size, int dummy) {
	p->set_vote_info_event(data, size, dummy);
}

void ZClient::handle_set_zone_info_event(ZClient *p, char *data, int size, int dummy) {
	p->set_zone_info_event(data, size, dummy);
}

void ZClient::handle_snipe_object_event(ZClient *p, char *data, int size, int dummy) {
	p->snipe_object_event(data, size, dummy);
}

void ZClient::handle_store_map_event(ZClient *p, char *data, int size, int dummy) {
	p->store_map_event(data, size, dummy);
}

void ZClient::handle_team_ended_event(ZClient *p, char *data, int size, int dummy) {
	p->team_ended_event(data, size, dummy);
}

void ZClient::handle_test_event(ZClient *p, char *data, int size, int dummy) {
	p->test_event(data, size, dummy);
}

void ZClient::handle_update_game_paused_event(ZClient *p, char *data, int size, int dummy) {
	p->update_game_paused_event(data, size, dummy);
}

void ZClient::handle_update_game_speed_event(ZClient *p, char *data, int size, int dummy) {
	p->update_game_speed_event(data, size, dummy);
}

//################# END Static TCP Event handler wrappers ########################



//############## BEGIN Actual TCP Event Handlers ####################
void ZClient::add_new_object_event(char *data, int size, int dummy) {
}

void ZClient::add_player_event(char *data, int size, int dummy) {
	ProcessAddLPlayer(data, size);
}

void ZClient::clear_player_list_event(char *data, int size, int dummy) {
	player_info.clear();
}

void ZClient::connect_event(char *data, int size, int dummy) {
}

void ZClient::delete_object_event(char *data, int size, int dummy) {
	ZObject *obj = ProcessDeleteObject(data, size);

		if (!obj) {
			return;
		}

		// TODO 1: This loop was originally in ZBot::delete_object_event only. Is is correct for ZPlayer too?
		// (This method was merged from ZBot and ZPlayer. The only difference was this loop)
		for (vector<ZObject*>::iterator i = object_list.begin(); i != object_list.end(); i++) {
			(*i)->RemoveObject(obj);
		}
}

void ZClient::delete_player_event(char *data, int size, int dummy) {
	ProcessDeleteLPlayer(data, size);
}

void ZClient::destroy_object_event(char *data, int size, int dummy) {
}

void ZClient::disconnect_event(char *data, int size, int dummy) {
	ProcessDisconnect();
}

void ZClient::display_login_event(char *data, int size, int dummy) {
}

void ZClient::display_news_event(char *data, int size, int dummy) {
}

void ZClient::do_crane_anim_event(char *data, int size, int dummy) {
}

void ZClient::do_portrait_anim_event(char *data, int size, int dummy) {
}

void ZClient::driver_hit_effect_event(char *data, int size, int dummy) {
}

void ZClient::end_game_event(char *data, int size, int dummy) {
	ProcessEndGame();
}

void ZClient::fire_object_missile_event(char *data, int size, int dummy) {
}

void ZClient::get_version_event(char *data, int size, int dummy) {
}

void ZClient::pickup_grenade_event(char *data, int size, int dummy) {
}

void ZClient::poll_buy_regkey(char *data, int size, int dummy) {
}

void ZClient::request_version_event(char *data, int size, int dummy) {
}

void ZClient::reset_game_event(char *data, int size, int dummy) {
}

void ZClient::set_build_queue_list_event(char *data, int size, int dummy) {
	ZObject *obj = ProcessBuildingQueueList(data, size);
}

void ZClient::set_building_cannon_list_event(char *data, int size, int dummy) {
	ZObject *obj = ProcessBuildingCannonList(data, size);
}

void ZClient::set_building_state_event(char *data, int size, int dummy) {
	ProcessBuildingState(data, size);
}

void ZClient::set_computer_message_event(char *data, int size, int dummy) {
}

void ZClient::set_object_experience_event(char *data, int size, int dummy) {

	set_experience_packet *pi = (set_experience_packet*) data;

	//good packet?
	if (size < sizeof(set_experience_packet)) {
		return;
	}

	ZObject* obj = GetObjectFromID(pi->ref_id);

	if (!obj) {
		return;
	}

	obj->SetGroupExperience(pi->experience);
	obj->ForceHoverNameImageUpdate();
}

void ZClient::set_grenade_amount_event(char *data, int size, int dummy) {
}

void ZClient::set_lid_open_event(char *data, int size, int dummy) {
	ProcessObjectLidState(data, size);
}

void ZClient::set_object_attack_object_event(char *data, int size, int dummy) {
}

void ZClient::set_object_group_info_event(char *data, int size, int dummy) {
	ProcessObjectGroupInfo(data, size);
}

void ZClient::set_object_health_event(char *data, int size, int dummy) {
}

void ZClient::set_object_loc_event(char *data, int size, int dummy) {
	ZObject *obj = ProcessObjectLoc(data, size);
}

void ZClient::set_object_rallypoints_event(char *data, int size, int dummy) {
	ZObject *our_object = ProcessRallypointData(data, size);
}

void ZClient::set_object_team_event(char *data, int size, int dummy) {
}

void ZClient::set_object_waypoints_event(char *data, int size, int dummy) {
}

void ZClient::set_player_id_event(char *data, int size, int dummy) {
	ProcessPlayerID(data, size);
}

void ZClient::set_player_ignored_event(char *data, int size, int dummy) {
	ProcessSetLPlayerIgnored(data, size);
}

void ZClient::set_player_loginfo_event(char *data, int size, int dummy) {
	ProcessSetLPlayerLogInfo(data, size);
}

void ZClient::set_player_mode_event(char *data, int size, int dummy) {
	ProcessSetLPlayerMode(data, size);
}

void ZClient::set_player_name_event(char *data, int size, int dummy) {
	ProcessSetLPlayerName(data, size);
}

void ZClient::set_player_team_event(char *data, int size, int dummy) {
	ProcessSetLPlayerTeam(data, size);
}

void ZClient::set_player_voteinfo_event(char *data, int size, int dummy) {
}

void ZClient::set_regkey(char *data, int size, int dummy) {
}

void ZClient::set_repair_building_anim_event(char *data, int size, int dummy) {
}

void ZClient::set_selectable_map_list_event(char *data, int size, int dummy) {
	ProcessSelectableMapList(data, size);
}

void ZClient::set_settings_event(char *data, int size, int dummy) {
	ProcessZSettings(data, size);
}

void ZClient::set_team_event(char *data, int size, int dummy) {
	ProcessSetTeam(data, size);
}

void ZClient::set_vote_info_event(char *data, int size, int dummy) {
}

void ZClient::set_zone_info_event(char *data, int size, int dummy) {
	ProcessZoneInfo(data, size);
}

void ZClient::snipe_object_event(char *data, int size, int dummy) {
}

void ZClient::store_map_event(char *data, int size, int dummy) {
}

void ZClient::team_ended_event(char *data, int size, int dummy) {
}

void ZClient::test_event(char *data, int size, int dummy) {
}

void ZClient::update_game_paused_event(char *data, int size, int dummy) {
	ProcessUpdateGamePaused(data, size);
}

void ZClient::update_game_speed_event(char *data, int size, int dummy) {
	ProcessUpdateGameSpeed(data, size);
}

//############## END Actual TCP Event Handlers ####################
