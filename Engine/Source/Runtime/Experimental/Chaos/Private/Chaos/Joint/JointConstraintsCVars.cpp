// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Joint/JointConstraintsCVars.h"
#include "HAL/IConsoleManager.h"

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_Joint_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosJointISPCEnabled(TEXT("p.Chaos.Joint.ISPC"), bChaos_Joint_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in the Joint Solver"));
#endif

bool bChaos_Joint_EarlyOut_Enabled = true;
FAutoConsoleVariableRef CVarChaosJointEarlyOutEnabled(TEXT("p.Chaos.Joint.EarlyOut"), bChaos_Joint_EarlyOut_Enabled, TEXT("Whether to iterating when joints report being solved"));

bool bChaos_Joint_Batching = false;
FAutoConsoleVariableRef CVarChaosJointBatching(TEXT("p.Chaos.Joint.Batching"), bChaos_Joint_Batching, TEXT(""));

int32 bChaos_Joint_MaxBatchSize = 1000;
FAutoConsoleVariableRef CVarChaosJointBatchSize(TEXT("p.Chaos.Joint.MaxBatchSize"), bChaos_Joint_MaxBatchSize, TEXT(""));

float Chaos_Joint_DegenerateRotationLimit = -0.998f;	// Cos(176deg)
FAutoConsoleVariableRef CVarChaosJointDegenerateRotationLimit(TEXT("p.Chaos.Joint.DegenerateRotationLimit"), Chaos_Joint_DegenerateRotationLimit, TEXT("Cosine of the swing angle that is considered degerenerate (default Cos(176deg))"));

bool bChaos_Joint_EllipticalFix = true;
FAutoConsoleVariableRef CVarChaosJointEllipticalFix(TEXT("p.Chaos.Joint.EllipticalFix"), bChaos_Joint_EllipticalFix, TEXT("Enable the proper elliptical joint axis/error calculation"));
