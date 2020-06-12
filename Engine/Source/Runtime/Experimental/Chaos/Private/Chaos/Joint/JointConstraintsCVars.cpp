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

float Chaos_Joint_VelProjectionAlpha = 0.1f;
FAutoConsoleVariableRef CVarChaosJointVelProjectionScale(TEXT("p.Chaos.Joint.VelProjectionAlpha"), Chaos_Joint_VelProjectionAlpha, TEXT("How much of the velocity correction to apply during projection. Equivalent to (1-damping) for projection velocity delta"));

bool bChaos_Joint_DisableSoftLimits = false;
FAutoConsoleVariableRef CVarChaosJointDisableSoftLimits(TEXT("p.Chaos.Joint.DisableSoftLimits"), bChaos_Joint_DisableSoftLimits, TEXT("Disable soft limits (for debugging only)"));

bool bChaos_Joint_EnableMatrixSolve = false;
FAutoConsoleVariableRef CVarChaosJointEnableMatrixSolve(TEXT("p.Chaos.Joint.EnableMatrixSolve"), bChaos_Joint_EnableMatrixSolve, TEXT(""));
