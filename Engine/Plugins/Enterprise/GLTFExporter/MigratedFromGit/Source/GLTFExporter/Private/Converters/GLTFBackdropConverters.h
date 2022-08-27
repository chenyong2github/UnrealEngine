// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFBackdropConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonBackdropIndex, const AActor*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonBackdropIndex Convert(const AActor* Actor) override final;
};
