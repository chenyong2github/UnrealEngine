// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerTime.h"

int64 MEDIAutcTime::CurrentMSec()
{
	FTimespan localTime(FDateTime::UtcNow().GetTicks());
	return (int64)localTime.GetTotalMilliseconds();
};

