// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UIFPlayerComponent.h"

#include "UIFPresenter.generated.h"

class FUIFrameworkModule;

/**
 * 
 */
 UCLASS(Abstract, Within=UIFrameworkPlayerComponent)
class UIFRAMEWORK_API UUIFrameworkPresenter : public UObject
{
	GENERATED_BODY()

public:
	virtual void AddToViewport(UWidget* UMGWidget, const FUIFrameworkGameLayerSlot& Slot)
	{

	}
};


/**
 *
 */
 UCLASS()
class UIFRAMEWORK_API UUIFrameworkGameViewportPresenter : public UUIFrameworkPresenter
 {
	 GENERATED_BODY()

public:
	virtual void AddToViewport(UWidget* UMGWidget, const FUIFrameworkGameLayerSlot& Slot) override;
};
