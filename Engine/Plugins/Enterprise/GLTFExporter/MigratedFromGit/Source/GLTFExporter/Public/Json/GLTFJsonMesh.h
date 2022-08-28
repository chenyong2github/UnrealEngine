// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonKhrMaterialVariant.h"

struct GLTFEXPORTER_API FGLTFJsonAttributes : IGLTFJsonObject
{
	FGLTFJsonAccessor* Position;
	FGLTFJsonAccessor* Color0;
	FGLTFJsonAccessor* Normal;
	FGLTFJsonAccessor* Tangent;

	TArray<FGLTFJsonAccessor*> TexCoords;
	TArray<FGLTFJsonAccessor*> Joints;
	TArray<FGLTFJsonAccessor*> Weights;

	FGLTFJsonAttributes()
		: Position(nullptr)
		, Color0(nullptr)
		, Normal(nullptr)
		, Tangent(nullptr)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonPrimitive : IGLTFJsonObject
{
	FGLTFJsonAttributes Attributes;
	FGLTFJsonAccessor* Indices;
	FGLTFJsonMaterial* Material;
	EGLTFJsonPrimitiveMode Mode;

	TArray<FGLTFJsonKhrMaterialVariantMapping> KhrMaterialVariantMappings;

	FGLTFJsonPrimitive()
		: Indices(nullptr)
		, Material(nullptr)
		, Mode(EGLTFJsonPrimitiveMode::Triangles)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonMesh : IGLTFJsonIndexedObject
{
	FString Name;

	TArray<FGLTFJsonPrimitive> Primitives;

	FGLTFJsonMesh(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
