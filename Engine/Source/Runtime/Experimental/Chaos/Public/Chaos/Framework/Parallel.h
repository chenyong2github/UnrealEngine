// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"

namespace Chaos
{
	void CHAOS_API PhysicsParallelFor(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded = false);
	//void CHAOS_API PhysicsParallelFor_RecursiveDivide(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded = false);


#if UE_BUILD_SHIPPING
	const bool bDisableParticleParallelFor = false;
	const bool bDisableCollisionParallelFor = false;
#else
	CHAOS_API extern bool bDisableParticleParallelFor;
	CHAOS_API extern bool bDisableCollisionParallelFor;
#endif
}