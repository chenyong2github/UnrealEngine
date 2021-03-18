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
	// ~Begin UDMXPixelMappingBaseComponent Interface
	virtual void ResetDMX() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::ResetDMX);
	virtual void SendDMX() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::SendDMX);
	virtual void Render() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::Render);
	virtual void RenderAndSendDMX() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::RenderAndSendDMX);
	// ~End UDMXPixelMappingBaseComponent Interface

	/** Render input texture for downsample texture, donwsample and send DMX for this component */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void RenderWithInputAndSendDMX() PURE_VIRTUAL(UDMXPixelMappingOutputComponent::RenderWithInputAndSendDMX);

	/** Render the texture for output and send it */
	virtual void RendererOutputTexture() {}

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override
	{
		return false;
	}
};
