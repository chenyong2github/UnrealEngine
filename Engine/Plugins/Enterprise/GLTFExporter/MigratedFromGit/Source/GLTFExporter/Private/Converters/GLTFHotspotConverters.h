// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Components/GLTFInteractionHotspotComponent.h"
#include "Engine.h"

class FGLTFHotspotComponentConverter final : public TGLTFConverter<FGLTFJsonHotspotIndex, const UGLTFInteractionHotspotComponent*>
{
	FGLTFJsonHotspotIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const UGLTFInteractionHotspotComponent* HotspotComponent) override;
};
