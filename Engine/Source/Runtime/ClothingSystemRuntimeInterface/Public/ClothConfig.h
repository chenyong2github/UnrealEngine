// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "ClothConfig.generated.h"

class UClothingAssetBase;

/**
 * Base class for simulator specific simulation controls.
 */
UCLASS(Abstract, DefaultToInstanced)
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UClothConfigBase : public UObject
{
	GENERATED_BODY()
public:
	UClothConfigBase();
	virtual ~UClothConfigBase();

	virtual bool HasSelfCollision() const
	{ unimplemented(); return false; }

#if WITH_EDITOR
	/** 
	 * Callback invoked by \c UClothingAssetBase::PostEditChangeChainProperty(). 
	 * Override to add custom handling of editor events.
	 */
	virtual bool PostEditChangeChainPropertyCallback(
		UClothingAssetBase* Asset,
		FPropertyChangedChainEvent& Event)
	{
		return false; 
	}

	/**
	 * Callback invoked by \c UClothingAssetBase::OnFinishedChangingClothPropertiesCallback().
	 * Override to add custom handling of editor events.
	 */
	virtual bool OnFinishedChangingClothingPropertiesCallback(
		UClothingAssetBase* Asset,
		const FPropertyChangedEvent& Event)
	{
		return false;
	}
#endif
};
