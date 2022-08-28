// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Components/GLTFInteractionHotspotComponent.h"

class FGLTFHotspotConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonHotspotIndex, const UGLTFInteractionHotspotComponent*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonHotspotIndex Convert(const UGLTFInteractionHotspotComponent* HotspotComponent) override;
};
