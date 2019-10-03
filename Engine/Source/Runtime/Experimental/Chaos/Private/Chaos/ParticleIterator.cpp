// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/ParticleIterator.h"

namespace Chaos
{
	CHAOS_API int32 ChaosParticleParallelFor = 1;
	FAutoConsoleVariableRef CVarChaosParticleParallelFor(TEXT("p.ChaosParticleParallelFor"), ChaosParticleParallelFor, TEXT("ParticleParallelFor function style for chaos"));
}
