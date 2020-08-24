// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class FSceneViewFamily;

class ENGINE_API ICustomStaticScreenPercentage
{
public:
	virtual ~ICustomStaticScreenPercentage() {};

	virtual void SetupMainGameViewFamily(FSceneViewFamily& ViewFamily) = 0;

	virtual float GetMinUpsampleResolutionFraction() const = 0;
	virtual float GetMaxUpsampleResolutionFraction() const = 0;

};

extern ENGINE_API ICustomStaticScreenPercentage* GCustomStaticScreenPercentage;
