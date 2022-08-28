// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonEpicVariantMaterial : IGLTFJsonObject
{
	FGLTFJsonMaterialIndex Material;
	int32                  Index;

	FGLTFJsonEpicVariantMaterial()
		: Material(INDEX_NONE)
		, Index(INDEX_NONE)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonEpicVariantNodeProperties : IGLTFJsonObject
{
	FGLTFJsonNodeIndex Node;
	TOptional<bool>    bIsVisible;

	TOptional<FGLTFJsonMeshIndex>        Mesh;
	TArray<FGLTFJsonEpicVariantMaterial> Materials;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonEpicVariant : IGLTFJsonObject
{
	FString Name;
	bool    bIsActive;

	FGLTFJsonTextureIndex Thumbnail;
	TMap<FGLTFJsonNodeIndex, FGLTFJsonEpicVariantNodeProperties> Nodes;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonEpicVariantSet : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonEpicVariant> Variants;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonEpicLevelVariantSets : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonEpicVariantSet> VariantSets;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
