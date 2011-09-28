#ifndef _ZBOT_H_
#define _ZBOT_H_

#include "zclient.h"

class PreferredUnit {
public:
	PreferredUnit();
	PreferredUnit(unsigned char ot_, unsigned char oid_) {
		ot = ot_;
		oid = oid_;
	}
	unsigned char ot, oid;
};

class ZBot: public ZClient {
public:
	ZBot();

	void Setup();
	void Run();
private:
	//void SetupEHandler();

	//virtual void ProcessSocketEvents();

	void ProcessAI();

	//AI 1
	bool Stage1AI();
	bool Stage2AI();

	//AI 2
	bool Stage1AI_2();
	void GiveOutOrders_2(vector<ZObject*> unit_list, vector<ZObject*> target_list,
			vector<ZObject*> &repair_building_list, vector<ZObject*> &grenade_box_list);

	//AI 3
	bool Stage1AI_3();
	void CollectOurUnits_3(vector<ZObject*> &units_list, vector<ZObject*> &targeted_list);
	void CollectOurTargets_3(vector<ZObject*> &targets_list, bool all_out);
	void ReduceUnitsToPercent(vector<ZObject*> &units_list, double max_percent);
	void RemoveTargetedFromTargets(vector<ZObject*> &targets_list, vector<ZObject*> &targeted_list);
	void MatchTargets_3(vector<ZObject*> &units_list, vector<ZObject*> &targets_list);
	void GiveOutOrders_3(vector<ZObject*> &units_list, vector<ZObject*> &targets_list);
	bool GoAllOut_3(double &percent_to_order, double &order_delay);

	void ChooseBuildOrders();

	void SendBotDevWaypointList(ZObject *obj);

	EventHandler<ZBot> ehandler;

	// TCP Events:
	virtual void add_new_object_event(char *data, int size, int dummy);
	virtual void connect_event(char *data, int size, int dummy);
	//virtual void delete_object_event(char *data, int size, int dummy);
	virtual void destroy_object_event(char *data, int size, int dummy);
	virtual void reset_game_event(char *data, int size, int dummy);
	virtual void set_grenade_amount_event(char *data, int size, int dummy);
	virtual void set_object_attack_object_event(char *data, int size, int dummy);
	virtual void set_object_health_event(char *data, int size, int dummy);
	virtual void set_object_team_event(char *data, int size, int dummy);
	virtual void set_player_voteinfo_event(char *data, int size, int dummy);
	//virtual void set_settings_event(char *data, int size, int dummy);
	virtual void set_team_event(char *data, int size, int dummy);
	virtual void set_vote_info_event(char *data, int size, int dummy);
	virtual void store_map_event(char *data, int size, int dummy);
	virtual void test_event(char *data, int size, int dummy);

	vector<ZObject*> flag_object_list;
	vector<PreferredUnit> preferred_build_list;

	double next_ai_time;
	double last_order_time;
};

#endif
