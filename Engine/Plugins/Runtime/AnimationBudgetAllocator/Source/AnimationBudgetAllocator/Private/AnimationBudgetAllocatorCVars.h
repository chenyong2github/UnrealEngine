// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DrawDebugHelpers.h"

struct FAnimationBudgetAllocatorParameters;

/** Enabled/disabled flag */
extern int32 GAnimationBudgetEnabled;

#if ENABLE_DRAW_DEBUG
/** Debug rendering flag */
extern int32 GAnimationBudgetDebugEnabled;

/** Controls whether debug rendering shows addresses of component data for debugging */
extern int32 GAnimationBudgetDebugShowAddresses;
#endif

/** CVar-driven parameter block */
extern FAnimationBudgetAllocatorParameters GBudgetParameters;

/** Delegate broadcast when parameter block changes */
extern FSimpleMulticastDelegate GOnCVarParametersChanged;