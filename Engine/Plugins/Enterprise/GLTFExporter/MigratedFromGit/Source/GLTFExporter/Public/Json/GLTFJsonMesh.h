// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonKhrMaterialVariant.h"

struct GLTFEXPORTER_API FGLTFJsonAttributes : IGLTFJsonObject
{
	FGLTFJsonAccessorIndex Position;
	FGLTFJsonAccessorIndex Color0;
	FGLTFJsonAccessorIndex Normal;
	FGLTFJsonAccessorIndex Tangent;

	TArray<FGLTFJsonAccessorIndex> TexCoords;
	TArray<FGLTFJsonAccessorIndex> Joints;
	TArray<FGLTFJsonAccessorIndex> Weights;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonPrimitive : IGLTFJsonObject
{
	FGLTFJsonAccessorIndex Indices;
	FGLTFJsonMaterialIndex Material;
	EGLTFJsonPrimitiveMode Mode;
	FGLTFJsonAttributes    Attributes;

	TArray<FGLTFJsonKhrMaterialVariantMapping> KhrMaterialVariantMappings;

	FGLTFJsonPrimitive()
		: Mode(EGLTFJsonPrimitiveMode::Triangles)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonMesh : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonPrimitive> Primitives;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
