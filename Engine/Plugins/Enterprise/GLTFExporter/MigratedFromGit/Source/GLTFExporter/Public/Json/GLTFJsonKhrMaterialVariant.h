// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct GLTFEXPORTER_API FGLTFJsonKhrMaterialVariantMapping : IGLTFJsonObject
{
	FGLTFJsonMaterialIndex                   Material;
	TArray<FGLTFJsonKhrMaterialVariantIndex> Variants;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonKhrMaterialVariant : IGLTFJsonObject
{
	FString Name;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
