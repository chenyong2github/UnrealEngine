// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IConcertSyncUIModule.h"

class FConcertSyncUIModule : public IConcertSyncUIModule
{
public:
	FConcertSyncUIModule() = default;
	virtual ~FConcertSyncUIModule() {}

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};


IMPLEMENT_MODULE(FConcertSyncUIModule, ConcertSyncUI);

