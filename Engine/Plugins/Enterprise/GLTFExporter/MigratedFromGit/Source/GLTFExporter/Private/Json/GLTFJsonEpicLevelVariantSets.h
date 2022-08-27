// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Misc/Optional.h"

struct FGLTFJsonEpicVariantMaterial : IGLTFJsonObject
{
	FGLTFJsonMaterialIndex Material;
	int32                  Index;

	FGLTFJsonEpicVariantMaterial()
		: Material(INDEX_NONE)
		, Index(INDEX_NONE)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("material"), Material);

		if (Index != INDEX_NONE)
		{
			Writer.Write(TEXT("index"), Index);
		}
	}
};

struct FGLTFJsonEpicVariantNodeProperties : IGLTFJsonObject
{
	FGLTFJsonNodeIndex Node;
	TOptional<bool>    bIsVisible;

	TOptional<FGLTFJsonMeshIndex>        Mesh;
	TArray<FGLTFJsonEpicVariantMaterial> Materials;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (Node != INDEX_NONE)
		{
			Writer.Write(TEXT("node"), Node);
		}

		Writer.StartObject(TEXT("properties"));

		if (bIsVisible.IsSet())
		{
			Writer.Write(TEXT("visible"), bIsVisible.GetValue());
		}

		if (Mesh.IsSet())
		{
			Writer.Write(TEXT("mesh"), Mesh.GetValue());
		}

		if (Materials.Num() > 0)
		{
			Writer.Write(TEXT("materials"), Materials);
		}

		Writer.EndObject();
	}
};

struct FGLTFJsonEpicVariant : IGLTFJsonObject
{
	FString Name;
	bool    bIsActive;

	FGLTFJsonTextureIndex Thumbnail;
	TMap<FGLTFJsonNodeIndex, FGLTFJsonEpicVariantNodeProperties> Nodes;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("name"), Name);
		Writer.Write(TEXT("active"), bIsActive);

		if (Thumbnail != INDEX_NONE)
		{
			Writer.Write(TEXT("thumbnail"), Thumbnail);
		}

		TArray<FGLTFJsonEpicVariantNodeProperties> NodeValues;
		Nodes.GenerateValueArray(NodeValues);
		Writer.Write(TEXT("nodes"), NodeValues);
	}
};

struct FGLTFJsonEpicVariantSet : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonEpicVariant> Variants;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("variants"), Variants);
	}
};

struct FGLTFJsonEpicLevelVariantSets : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonEpicVariantSet> VariantSets;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("variantSets"), VariantSets);
	}
};
