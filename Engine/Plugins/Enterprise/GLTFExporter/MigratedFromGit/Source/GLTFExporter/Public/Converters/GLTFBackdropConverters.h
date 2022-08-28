// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonBackdrop*, const AActor*> IGLTFBackdropConverter;


class GLTFEXPORTER_API FGLTFBackdropConverter final : public FGLTFBuilderContext, public IGLTFBackdropConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonBackdrop* Convert(const AActor* BackdropActor) override;
};
