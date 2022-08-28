// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonSkin.h"

void FGLTFJsonSkin::WriteObject(IGLTFJsonWriter& Writer) const
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
