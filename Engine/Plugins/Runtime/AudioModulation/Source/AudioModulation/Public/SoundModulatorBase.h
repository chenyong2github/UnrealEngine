// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"

#include "SoundModulatorBase.generated.h"


// Forward Declarations
class ISoundModulatable;


/**
 * Base class for all modulators
 */
UCLASS(hideCategories = Object, abstract, MinimalAPI)
class USoundModulatorBase : public UObject
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};

/**
 * Base class for modulators that manipulate control bus values
 */
UCLASS(hideCategories = Object, abstract, MinimalAPI)
class USoundBusModulatorBase : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()
};
