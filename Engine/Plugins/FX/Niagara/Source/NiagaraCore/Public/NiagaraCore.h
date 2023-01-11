// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "NiagaraCore.generated.h"

typedef uint64 FNiagaraSystemInstanceID;

UENUM()
enum class ENiagaraIterationSource : uint8
{
	/** Iterate over all active particles. */
	Particles = 0,
	/** Iterate over all elements in the data interface. */
	DataInterface,
	/** Iterate over a user provided number of elements. */
	DirectSet,
};
