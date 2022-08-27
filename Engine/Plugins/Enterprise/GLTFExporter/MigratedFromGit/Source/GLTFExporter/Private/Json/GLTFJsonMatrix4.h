// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonArray.h"
#include "Converters/GLTFRawTypes.h"

struct FGLTFJsonMatrix4 : IGLTFJsonArray, FGLTFRawMatrix4
{
	static const FGLTFJsonMatrix4 Identity;

	FGLTFJsonMatrix4(const FGLTFRawMatrix4& Raw)
	{
		*static_cast<FGLTFRawMatrix4*>(this) = Raw;
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		for (int32 i = 0; i < 16; ++i)
		{
			Writer.Write(Cells[i]);
		}
	}

	bool operator==(const FGLTFJsonMatrix4& Other) const
	{
		for (int32 i = 0; i < 16; ++i)
		{
			if (Cells[i] != Other.Cells[i])
			{
				return false;
			}
		}

		return true;
	}

	bool operator!=(const FGLTFJsonMatrix4& Other) const
	{
		return !(*this == Other);
	}
};
