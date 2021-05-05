// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/**
 * Traits class which tests if a type is part of the core types included in CoreMinimal.h.
 */
template <typename T>
struct TIsUECoreType 
{ 
	enum { Value = false };
};
