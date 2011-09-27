#ifndef ZMANNEDOBJECT_H_
#define ZMANNEDOBJECT_H_

#include "zobject.h"
#include "zrobot.h"

class ZMannedObject: public ZObject {
public:

	ZMannedObject(ZTime *ztime_, ZSettings *zsettings_);
	virtual ~ZMannedObject();
	virtual void AddDriver(int health, int experience);
	virtual void AddDriver(driver_info_s new_driver);
	//virtual void NewAddDriver(ZObject driver);

	virtual bool CanBeSniped();
	virtual bool CanBeEntered();

	virtual bool CanSetWaypoints() {
		return true;
	}

	virtual void ClearDrivers();

	virtual float GetDamageChance();
	virtual float GetSnipeChance();


	virtual unsigned char GetExperience();
	virtual void SetExperience(unsigned char);

	int DamageDriverHealth(int damage_amount);
	//int NewDamageDriverHealth(int damage_amount);

	virtual vector<driver_info_s> &GetDrivers();
	//virtual vector<ZObject>& NewGetDrivers();

	virtual int GetDriverType();

	virtual int GetDriverHealth();
	//virtual int NewGetDriverHealth();

	virtual bool GetEjectable();

	virtual int GetSmartness();

	virtual void SetAttackObject(ZObject *obj);
	virtual void SetDriverType(int driver_type_);
	void SetEjectable(bool ejectable_);
	virtual void SetInitialDrivers();

protected:
	bool can_be_sniped;
	bool ejectable;
	//vector<ZObject> newDriverInfo;
};

#endif /* ZMANNEDOBJECT_H_ */
