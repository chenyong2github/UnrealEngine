// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include <time.h>

#if !defined(UE_HAS_EPOCH_TIME_OFFSET) || !UE_HAS_EPOCH_TIME_OFFSET

time_t UE_epoch_time_offset()
{
	// There is no offset by default
	return 0;
}

#endif // !defined(UE_HAS_EPOCH_TIME_OFFSET) || !UE_HAS_EPOCH_TIME_OFFSET
