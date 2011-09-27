#ifndef _ZCLIENT_H_
#define _ZCLIENT_H_

#include "zcore.h"
#include "client_socket.h"
#include <iostream>

class ZClient: public ZCore {
public:
	ZClient();

	void SetRemoteAddress(string ipaddress);
	void SetPlayerName(string player_name_);
	virtual void SetPlayerTeam(team_type player_team);
	virtual void SetDesiredTeam(team_type player_team);
protected:

	void ProcessSocketEvents();
	int ProcessMapDownload(char *data, int size);
	ZObject* ProcessNewObject(char *data, int size);
	void ProcessZoneInfo(char *data, int size);
	ZObject* ProcessObjectTeam(char *data, int size);
	ZObject* ProcessObjectAttackObject(char *data, int size);
	ZObject* ProcessDeleteObject(char *data, int size);
	ZObject* ProcessObjectHealthTeam(char *data, int size);
	ZObject* ProcessFireMissile(char *data, int size);
	ZObject* ProcessObjectLoc(char *data, int size);
	ZObject* ProcessBuildingState(char *data, int size);
	ZObject* ProcessBuildingQueueList(char *data, int size);
	ZObject* ProcessBuildingCannonList(char *data, int size);
	ZObject* ProcessObjectGroupInfo(char *data, int size);
	ZObject* ProcessObjectLidState(char *data, int size);
	ZObject* ProcessSetGrenadeState(char *data, int size);
	bool ProcessZSettings(char *data, int size);
	bool ProcessAddLPlayer(char *data, int size);
	bool ProcessDeleteLPlayer(char *data, int size);
	bool ProcessSetLPlayerName(char *data, int size);
	bool ProcessSetLPlayerTeam(char *data, int size);
	bool ProcessSetLPlayerMode(char *data, int size);
	bool ProcessSetLPlayerIgnored(char *data, int size);
	bool ProcessSetLPlayerLogInfo(char *data, int size);
	bool ProcessSetLPlayerVoteInfo(char *data, int size);
	bool ProcessUpdateGamePaused(char *data, int size);
	bool ProcessUpdateGameSpeed(char *data, int size);
	bool ProcessVoteInfo(char *data, int size);
	bool ProcessPlayerID(char *data, int size);
	bool ProcessSelectableMapList(char *data, int size);
	bool ProcessSetTeam(char *data, int size);
	void ProcessConnect();
	virtual void ProcessDisconnect();
	virtual void ProcessEndGame();
	virtual void ProcessResetGame();
	void RequestObjectList();
	void RequestZoneList();
	void SendPlayerInfo();
	void SendPlayerTeam(int new_team);
	void SendPlayerMode();
	void RequestPlayerList();
	void SendBotBypassData();
	p_info &OurPInfo();
	virtual void DeleteObjectCleanUp(ZObject *obj);

	virtual void SetupEHandler();

	EventHandler<ZClient> eventHandler;

	string remote_address;
	string player_name;
	team_type our_team;
	team_type desired_team;
	player_mode our_mode;
	int p_id;

	char *map_data;
	int map_data_size;
	ClientSocket client_socket;

	// ########### BEGIN Static TCP event handler wrappers ###############

	static void add_new_object_event(ZClient *p, char *data, int size, int dummy);
	static void add_player_event(ZClient *p, char *data, int size, int dummy);
	static void clear_player_list_event(ZClient *p, char *data, int size, int dummy);
	static void connect_event(ZClient *p, char *data, int size, int dummy);
	static void delete_object_event(ZClient *p, char *data, int size, int dummy);
	static void delete_player_event(ZClient *p, char *data, int size, int dummy);
	static void destroy_object_event(ZClient *p, char *data, int size, int dummy);
	static void disconnect_event(ZClient *p, char *data, int size, int dummy);
	static void display_login_event(ZClient *p, char *data, int size, int dummy);
	static void display_news_event(ZClient *p, char *data, int size, int dummy);
	static void do_crane_anim_event(ZClient *p, char *data, int size, int dummy);
	static void do_portrait_anim_event(ZClient *p, char *data, int size, int dummy);
	static void driver_hit_effect_event(ZClient *p, char *data, int size, int dummy);
	static void end_game_event(ZClient *p, char *data, int size, int dummy);
	static void fire_object_missile_event(ZClient *p, char *data, int size, int dummy);
	static void get_version_event(ZClient *p, char *data, int size, int dummy);
	static void pickup_grenade_event(ZClient *p, char *data, int size, int dummy);


	static void poll_buy_regkey_event(ZClient *p, char *data, int size, int dummy);
	static void request_version_event(ZClient *p, char *data, int size, int dummy);
	static void reset_game_event(ZClient *p, char *data, int size, int dummy);
	static void set_build_queue_list_event(ZClient *p, char *data, int size, int dummy);
	static void set_building_cannon_list_event(ZClient *p, char *data, int size, int dummy);
	static void set_building_state_event(ZClient *p, char *data, int size, int dummy);
	static void set_computer_message_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_experience_event(ZClient *p, char *data, int size, int dummy);
	static void set_grenade_amount_event(ZClient *p, char *data, int size, int dummy);
	//static void set_leader_event(ZClient *p, char *data, int size, int dummy);
	static void set_lid_open_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_attack_object_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_group_info_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_health_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_loc_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_rallypoints_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_team_event(ZClient *p, char *data, int size, int dummy);
	static void set_object_waypoints_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_id_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_ignored_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_loginfo_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_mode_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_name_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_team_event(ZClient *p, char *data, int size, int dummy);
	static void set_player_voteinfo_event(ZClient *p, char *data, int size, int dummy);
	static void set_regkey(ZClient *p, char *data, int size, int dummy);
	static void set_repair_building_anim_event(ZClient *p, char *data, int size, int dummy);
	static void set_selectable_map_list_event(ZClient *p, char *data, int size, int dummy);
	static void set_settings_event(ZClient *p, char *data, int size, int dummy);
	static void set_team_event(ZClient *p, char *data, int size, int dummy);
	static void set_vote_info_event(ZClient *p, char *data, int size, int dummy);
	static void set_zone_info_event(ZClient *p, char *data, int size, int dummy);
	static void snipe_object_event(ZClient *p, char *data, int size, int dummy);
	static void store_map_event(ZClient *p, char *data, int size, int dummy);
	static void team_ended_event(ZClient *p, char *data, int size, int dummy);
	static void test_event(ZClient *p, char *data, int size, int dummy);
	static void update_game_paused_event(ZClient *p, char *data, int size, int dummy);
	static void update_game_speed_event(ZClient *p, char *data, int size, int dummy);
	// ########### END Static TCP event handler wrappers ###############

	// ############# BEGIN Actual TCP event handlers ####################
	virtual void add_new_object_event(char *data, int size, int dummy);
	virtual void add_player_event(char *data, int size, int dummy);
	virtual void clear_player_list_event(char *data, int size, int dummy);
	virtual void connect_event(char *data, int size, int dummy);
	virtual void delete_object_event(char *data, int size, int dummy);
	virtual void delete_player_event(char *data, int size, int dummy);
	virtual void destroy_object_event(char *data, int size, int dummy);
	virtual void disconnect_event(char *data, int size, int dummy);
	virtual void display_login_event(char *data, int size, int dummy);
	virtual void display_news_event(char *data, int size, int dummy);
	virtual void do_crane_anim_event(char *data, int size, int dummy);
	virtual void do_portrait_anim_event(char *data, int size, int dummy);
	virtual void driver_hit_effect_event(char *data, int size, int dummy);
	virtual void end_game_event(char *data, int size, int dummy);
	virtual void fire_object_missile_event(char *data, int size, int dummy);
	virtual void get_version_event(char *data, int size, int dummy);
	virtual void pickup_grenade_event(char *data, int size, int dummy);
	virtual void poll_buy_regkey(char *data, int size, int dummy);
	virtual void request_version_event(char *data, int size, int dummy);
	virtual void reset_game_event(char *data, int size, int dummy);
	virtual void set_build_queue_list_event(char *data, int size, int dummy);
	virtual void set_building_cannon_list_event(char *data, int size, int dummy);
	virtual void set_building_state_event(char *data, int size, int dummy);
	virtual void set_computer_message_event(char *data, int size, int dummy);
	virtual void set_object_experience_event(char *data, int size, int dummy);
	virtual void set_grenade_amount_event(char *data, int size, int dummy);
	//virtual void set_leader_event(char *data, int size, int dummy);
	virtual void set_lid_open_event(char *data, int size, int dummy);
	virtual void set_object_attack_object_event(char *data, int size, int dummy);
	virtual void set_object_group_info_event(char *data, int size, int dummy);
	virtual void set_object_health_event(char *data, int size, int dummy);
	virtual void set_object_loc_event(char *data, int size, int dummy);
	virtual void set_object_rallypoints_event(char *data, int size, int dummy);
	virtual void set_object_team_event(char *data, int size, int dummy);
	virtual void set_object_waypoints_event(char *data, int size, int dummy);
	virtual void set_player_id_event(char *data, int size, int dummy);
	virtual void set_player_ignored_event(char *data, int size, int dummy);
	virtual void set_player_loginfo_event(char *data, int size, int dummy);
	virtual void set_player_mode_event(char *data, int size, int dummy);
	virtual void set_player_name_event(char *data, int size, int dummy);
	virtual void set_player_team_event(char *data, int size, int dummy);
	virtual void set_player_voteinfo_event(char *data, int size, int dummy);
	virtual void set_regkey(char *data, int size, int dummy);
	virtual void set_repair_building_anim_event(char *data, int size, int dummy);
	virtual void set_selectable_map_list_event(char *data, int size, int dummy);
	virtual void set_settings_event(char *data, int size, int dummy);
	virtual void set_team_event(char *data, int size, int dummy);
	virtual void set_vote_info_event(char *data, int size, int dummy);
	virtual void set_zone_info_event(char *data, int size, int dummy);
	virtual void snipe_object_event(char *data, int size, int dummy);
	virtual void store_map_event(char *data, int size, int dummy);
	virtual void team_ended_event(char *data, int size, int dummy);
	virtual void test_event(char *data, int size, int dummy);
	virtual void update_game_paused_event(char *data, int size, int dummy);
	virtual void update_game_speed_event(char *data, int size, int dummy);
	// ############# END Actual TCP event handlers ####################


	// ################# BEGIN Static player input event handler wrappers ##########################
	static void resize_event(ZClient *p, char *data, int size, int dummy);
	static void lclick_event(ZClient *p, char *data, int size, int dummy);
	static void lunclick_event(ZClient *p, char *data, int size, int dummy);
	static void rclick_event(ZClient *p, char *data, int size, int dummy);
	static void runclick_event(ZClient *p, char *data, int size, int dummy);
	static void mclick_event(ZClient *p, char *data, int size, int dummy);
	static void munclick_event(ZClient *p, char *data, int size, int dummy);
	static void wheelup_event(ZClient *p, char *data, int size, int dummy);
	static void wheeldown_event(ZClient *p, char *data, int size, int dummy);
	static void keydown_event(ZClient *p, char *data, int size, int dummy);
	static void keyup_event(ZClient *p, char *data, int size, int dummy);
	static void motion_event(ZClient *p, char *data, int size, int dummy);
	// ################# END Static player input event handler wrappers ##########################

	// ############# BEGIN Actual player input event handlers ####################
	virtual void resize_event(char* data, int size, int dummy) {}
	virtual void lclick_event(char* data, int size, int dummy) {}
	virtual void lunclick_event(char* data, int size, int dummy) {}
	virtual void rclick_event(char* data, int size, int dummy) {}
	virtual void runclick_event(char* data, int size, int dummy) {}
	virtual void mclick_event(char* data, int size, int dummy) {}
	virtual void munclick_event(char* data, int size, int dummy) {}
	virtual void wheelup_event(char* data, int size, int dummy) {}
	virtual void wheeldown_event(char* data, int size, int dummy) {}
	virtual void keydown_event(char* data, int size, int dummy) {}
	virtual void keyup_event(char* data, int size, int dummy) {}
	virtual void motion_event(char* data, int size, int dummy) {}
	// ############# END Actual player input event handlers ####################
};

#endif
