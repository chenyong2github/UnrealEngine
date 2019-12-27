// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"


struct FTransform;
struct FQuat;

class DATASMITHIMPORTER_API FDatasmithTransformUtils
{
public:

	/**
	 * Get rotation from FTransform and fix floating point precision issues with quaternion
	 */
	static void GetRotation(const FTransform& InTransform, FQuat& OutRotation);
};