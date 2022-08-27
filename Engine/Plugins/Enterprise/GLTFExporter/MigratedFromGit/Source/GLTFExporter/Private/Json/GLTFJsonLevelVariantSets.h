// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Misc/Optional.h"

struct FGLTFJsonVariantMaterial : IGLTFJsonObject
{
	FGLTFJsonMaterialIndex Material;
	int32                  Index;

	FGLTFJsonVariantMaterial()
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

struct FGLTFJsonVariantNodeProperties : IGLTFJsonObject
{
	FGLTFJsonNodeIndex Node;
	TOptional<bool>    bIsVisible;

	TOptional<FGLTFJsonMeshIndex>    Mesh;
	TArray<FGLTFJsonVariantMaterial> Materials;

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

struct FGLTFJsonVariant : IGLTFJsonObject
{
	FString Name;
	bool    bIsActive;

	FGLTFJsonTextureIndex Thumbnail;
	TMap<FGLTFJsonNodeIndex, FGLTFJsonVariantNodeProperties> Nodes;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("name"), Name);
		Writer.Write(TEXT("active"), bIsActive);

		if (Thumbnail != INDEX_NONE)
		{
			Writer.Write(TEXT("thumbnail"), Thumbnail);
		}

		TArray<FGLTFJsonVariantNodeProperties> NodeValues;
		Nodes.GenerateValueArray(NodeValues);
		Writer.Write(TEXT("nodes"), NodeValues);
	}
};

struct FGLTFJsonVariantSet : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonVariant> Variants;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("variants"), Variants);
	}
};

struct FGLTFJsonLevelVariantSets : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonVariantSet> VariantSets;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("variantSets"), VariantSets);
	}
};
