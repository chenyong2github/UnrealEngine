// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonKhrMaterialVariantMapping : IGLTFJsonObject
{
	FGLTFJsonMaterial* Material;
	TArray<FGLTFJsonKhrMaterialVariant*> Variants;

	FGLTFJsonKhrMaterialVariantMapping()
		: Material(nullptr)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonKhrMaterialVariant : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonKhrMaterialVariant(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
