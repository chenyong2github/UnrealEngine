// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingOutputComponent.h"
#include "DMXPixelMappingOutputDMXComponent.generated.h"


/**
 * Parent class for DMX sending components
 */
UCLASS(Abstract)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingOutputDMXComponent
	: public UDMXPixelMappingOutputComponent
{
	GENERATED_BODY()

public:
	/** Render input texture for downsample texture, donwsample and send DMX for this component */
	UE_DEPRECATED(5.3, "Deprecated for performance reasons. Instead use 'Get DMX Pixel Mapping Renderer Component' and Render only once each tick.")
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping", Meta = (DeprecatedFunction, DeprecationMessage = "Deprecated for performance reasons. Instead use 'Get DMX Pixel Mapping Renderer Component' and Render only once each tick"))
	virtual void RenderWithInputAndSendDMX() PURE_VIRTUAL(UDMXPixelMappingOutputDMXComponent::RenderWithInputAndSendDMX);

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override
	{
		return false;
	}
};
