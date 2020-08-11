// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
	const bool bChaos_Joint_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
	const bool bChaos_Joint_ISPC_Enabled = true;
#else
	extern bool bChaos_Joint_ISPC_Enabled;
#endif

extern bool bChaos_Joint_EarlyOut_Enabled;
extern bool bChaos_Joint_Batching;
extern int32 bChaos_Joint_MaxBatchSize;

extern float Chaos_Joint_DegenerateRotationLimit;

extern float Chaos_Joint_VelProjectionAlpha;

extern bool bChaos_Joint_DisableSoftLimits;

extern bool bChaos_Joint_EnableMatrixSolve;