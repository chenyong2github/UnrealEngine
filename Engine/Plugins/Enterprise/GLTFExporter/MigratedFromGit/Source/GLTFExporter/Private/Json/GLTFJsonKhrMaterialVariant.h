// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonKhrMaterialVariantMapping : IGLTFJsonObject
{
	FGLTFJsonMaterialIndex                   Material;
	TArray<FGLTFJsonKhrMaterialVariantIndex> Variants;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("material"), Material);
		Writer.Write(TEXT("variants"), Variants);
	}
};

struct FGLTFJsonKhrMaterialVariant : IGLTFJsonObject
{
	FString Name;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("name"), Name);
	}
};
