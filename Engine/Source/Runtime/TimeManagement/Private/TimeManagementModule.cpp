// Copyright Epic Games, Inc. All Rights Reserved.

#include "ITimeManagementModule.h"
#include "TimedDataInputCollection.h"
#include "CommonFrameRates.h"

class FTimeManagementModule : public ITimeManagementModule
{
public:
	virtual FTimedDataInputCollection& GetTimedDataInputCollection() { return Collection; }
	virtual TArrayView<const struct FCommonFrameRateInfo> GetAllCommonFrameRates() { return FCommonFrameRates::GetAll(); }

private:
	FTimedDataInputCollection Collection;
};

IMPLEMENT_MODULE(FTimeManagementModule, TimeManagement);