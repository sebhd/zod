#ifndef _ZCANNON_H_
#define _ZCANNON_H_

#include "zobject.h"
#include "ZMannedObject.h"

class ZCannon : public ZMannedObject
{
	public:
		ZCannon(ZTime *ztime_, ZSettings *zsettings_ = NULL);
		
		static void Init();
		virtual bool CanBeSniped();
		
	protected:
		static ZSDL_Surface init_place[3];
};

#endif
