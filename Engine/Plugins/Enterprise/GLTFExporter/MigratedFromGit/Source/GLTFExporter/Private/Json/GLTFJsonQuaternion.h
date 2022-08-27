// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/GLTFQuaternion.h"
#include "Json/GLTFJsonArray.h"

struct FGLTFJsonQuaternion final : TGLTFQuaternion<float>, IGLTFJsonArray
{
	static const TGLTFQuaternion Identity;

	FGLTFJsonQuaternion(const TGLTFQuaternion& Other)
		: TGLTFQuaternion(Other)
	{
	}

	FGLTFJsonQuaternion& operator=(const TGLTFQuaternion& Other)
	{
		*static_cast<TGLTFQuaternion*>(this) = Other;
		return *this;
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		for (int32 i = 0; i < 4; ++i)
		{
			Writer.Write(Components[i]);
		}
	}

	bool operator==(const TGLTFQuaternion& Other) const
	{
		for (uint32 i = 0; i < GetNum(Components); ++i)
		{
			if (Components[i] != Other.Components[i])
			{
				return false;
			}
		}

		return true;
	}

	bool operator!=(const TGLTFQuaternion& Other) const
	{
		return !(*this == Other);
	}
};
