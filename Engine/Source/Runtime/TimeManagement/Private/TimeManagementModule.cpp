// Copyright Epic Games, Inc. All Rights Reserved.

#include "ITimeManagementModule.h"
#include "TimedDataInputCollection.h"

class FTimeManagementModule : public ITimeManagementModule
{
public:
	virtual FTimedDataInputCollection& GetTimedDataInputCollection() { return Collection; }

private:
	FTimedDataInputCollection Collection;
};

IMPLEMENT_MODULE(FTimeManagementModule, TimeManagement);