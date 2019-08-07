// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if INCLUDE_CHAOS
#include "Chaos/PBDRigidParticles.h"
#endif

#include "PhysicsCoreTypes.generated.h"

UENUM()
enum class EChaosSolverTickMode : uint8
{
	Fixed,
	Variable,
	VariableCapped,
	VariableCappedWithTarget,
};

UENUM()
enum class EChaosThreadingMode : uint8
{
	DedicatedThread,
	TaskGraph,
	SingleThread,
	Num UMETA(Hidden),
	Invalid UMETA(Hidden)
};

UENUM()
enum class EChaosBufferMode : uint8
{
	Double,
	Triple,
	Num UMETA(Hidden),
	Invalid UMETA(Hidden)
};

#if INCLUDE_CHAOS
typedef Chaos::TPBDRigidParticles<float, 3> FParticlesType;
#endif
