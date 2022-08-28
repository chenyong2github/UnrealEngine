// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonAttributes : IGLTFJsonObject
{
	FGLTFJsonAccessorIndex Position; // always required
	FGLTFJsonAccessorIndex Color0;
	FGLTFJsonAccessorIndex Normal;
	FGLTFJsonAccessorIndex Tangent;
	TArray<FGLTFJsonAccessorIndex> TexCoords;

	// skeletal mesh attributes
	TArray<FGLTFJsonAccessorIndex> Joints;
	TArray<FGLTFJsonAccessorIndex> Weights;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("POSITION"), Position);

		if (Color0 != INDEX_NONE) Writer.Write(TEXT("COLOR_0"), Color0);
		if (Normal != INDEX_NONE) Writer.Write(TEXT("NORMAL"), Normal);
		if (Tangent != INDEX_NONE) Writer.Write(TEXT("TANGENT"), Tangent);

		for (int32 Index = 0; Index < TexCoords.Num(); ++Index)
		{
			Writer.Write(TEXT("TEXCOORD_") + FString::FromInt(Index), TexCoords[Index]);
		}

		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			Writer.Write(TEXT("JOINTS_") + FString::FromInt(Index), Joints[Index]);
		}

		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			Writer.Write(TEXT("WEIGHTS_") + FString::FromInt(Index), Weights[Index]);
		}
	}
};

struct FGLTFJsonPrimitive : IGLTFJsonObject
{
	FGLTFJsonAccessorIndex Indices;
	FGLTFJsonMaterialIndex Material;
	EGLTFJsonPrimitiveMode Mode;
	FGLTFJsonAttributes    Attributes;

	FGLTFJsonPrimitive()
		: Mode(EGLTFJsonPrimitiveMode::Triangles)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("attributes"), Attributes);

		if (Indices != INDEX_NONE)
		{
			Writer.Write(TEXT("indices"), Indices);
		}

		if (Material != INDEX_NONE)
		{
			Writer.Write(TEXT("material"), Material);
		}

		if (Mode != EGLTFJsonPrimitiveMode::Triangles)
		{
			Writer.Write(TEXT("mode"), Mode);
		}
	}
};

struct FGLTFJsonMesh : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonPrimitive> Primitives;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("primitives"), Primitives);
	}
};
