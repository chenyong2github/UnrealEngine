// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonSkin : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonAccessorIndex InverseBindMatrices;
	FGLTFJsonNodeIndex Skeleton;

	TArray<FGLTFJsonNodeIndex> Joints;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (InverseBindMatrices != INDEX_NONE)
		{
			Writer.Write(TEXT("inverseBindMatrices"), InverseBindMatrices);
		}

		if (Skeleton != INDEX_NONE)
		{
			Writer.Write(TEXT("skeleton"), Skeleton);
		}

		if (Joints.Num() > 0)
		{
			Writer.Write(TEXT("joints"), Joints);
		}
	}
};
