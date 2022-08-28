// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/GLTFVector.h"
#include "Json/GLTFJsonArray.h"

template <typename BaseType>
struct TGLTFJsonVector final : BaseType, IGLTFJsonArray
{
	static const TGLTFJsonVector Zero;
	static const TGLTFJsonVector One;

	TGLTFJsonVector(const BaseType& Other)
		: BaseType(Other)
	{
	}

	TGLTFJsonVector& operator=(const BaseType& Other)
	{
		*static_cast<BaseType*>(this) = Other;
		return *this;
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		for (int32 i = 0; i < GetNum(BaseType::Components); ++i)
		{
			Writer.Write(BaseType::Components[i]);
		}
	}

	bool IsNearlyEqual(const BaseType& Other, float Tolerance = KINDA_SMALL_NUMBER) const
	{
		for (int32 i = 0; i < GetNum(BaseType::Components); ++i)
		{
			if (!FMath::IsNearlyEqual(BaseType::Components[i], Other.Components[i], Tolerance))
			{
				return false;
			}
		}

		return true;
	}
};

typedef TGLTFJsonVector<FGLTFVector2> FGLTFJsonVector2;
typedef TGLTFJsonVector<FGLTFVector3> FGLTFJsonVector3;
typedef TGLTFJsonVector<FGLTFVector4> FGLTFJsonVector4;
