// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFBackdropConverter final : public TGLTFConverter<FGLTFJsonBackdropIndex, const AActor*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonBackdropIndex Convert(const AActor* Actor) override;
};
