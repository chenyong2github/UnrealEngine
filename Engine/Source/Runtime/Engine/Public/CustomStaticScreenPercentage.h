// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class FSceneViewFamily;

/** Used to deliver Screen percentage settings or other data. */
class ENGINE_API ICustomStaticScreenPercentageData
{
public:
	virtual ~ICustomStaticScreenPercentageData() {};
};

class ENGINE_API ICustomStaticScreenPercentage
{
public:
	virtual ~ICustomStaticScreenPercentage() {};

	virtual void SetupMainGameViewFamily(FSceneViewFamily& ViewFamily) = 0;

	/** For generic cases where view family isn't a game view family. Example: MovieRenderQueue. */
	virtual void SetupViewFamily(FSceneViewFamily& ViewFamily, TSharedPtr<ICustomStaticScreenPercentageData> InScreenPercentageDataInterface) {};

	virtual float GetMinUpsampleResolutionFraction() const = 0;
	virtual float GetMaxUpsampleResolutionFraction() const = 0;

};

extern ENGINE_API ICustomStaticScreenPercentage* GCustomStaticScreenPercentage;
