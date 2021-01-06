// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "SynchronizedClock.h"


namespace Electra
{

	class FSynchronizedUTCTime : public ISynchronizedUTCTime
	{
	public:
		FSynchronizedUTCTime();
		virtual ~FSynchronizedUTCTime();

		virtual void SetTime(const FTimeValue& TimeNow) override;

		virtual FTimeValue GetTime() override;

	private:
		FTimeValue				BaseUTCTime;
		int64					BaseSystemTime;
		FMediaCriticalSection	Lock;
	};


	ISynchronizedUTCTime* ISynchronizedUTCTime::Create()
	{
		return new FSynchronizedUTCTime;
	}



	FSynchronizedUTCTime::FSynchronizedUTCTime()
	{
		BaseSystemTime = MEDIAutcTime::CurrentMSec();
	}

	FSynchronizedUTCTime::~FSynchronizedUTCTime()
	{
		Lock.Lock();
		Lock.Unlock();
	}

	void FSynchronizedUTCTime::SetTime(const FTimeValue& TimeNow)
	{
		int64 NowSystem = MEDIAutcTime::CurrentMSec();
		Lock.Lock();
		BaseUTCTime = TimeNow;
		BaseSystemTime = NowSystem;
		Lock.Unlock();
	}

	FTimeValue FSynchronizedUTCTime::GetTime()
	{
		int64 NowSystem = MEDIAutcTime::CurrentMSec();
		Lock.Lock();
		FTimeValue 	LastBaseUTC = BaseUTCTime;
		int64   	LastBaseSystem = BaseSystemTime;
		Lock.Unlock();

		FTimeValue Offset;
		Offset.SetFromMilliseconds(NowSystem - LastBaseSystem);
		return LastBaseUTC + Offset;
	}


} // namespace Electra


