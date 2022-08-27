// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Components/GLTFInteractionHotspotComponent.h"
#include "Engine.h"

class FGLTFHotspotComponentConverter final : public TGLTFConverter<FGLTFJsonHotspotIndex, const UGLTFInteractionHotspotComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonHotspotIndex Convert(const UGLTFInteractionHotspotComponent* HotspotComponent) override;
};
