// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonSkinIndex, FGLTFJsonNodeIndex, const USkeletalMesh*> IGLTFSkinConverter;

class FGLTFSkinConverter final : public FGLTFBuilderContext, public IGLTFSkinConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSkinIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh) override;
};
