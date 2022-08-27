// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/GLTFColor.h"
#include "Json/GLTFJsonArray.h"

template <typename BaseType>
struct TGLTFJsonColor final : BaseType, IGLTFJsonArray
{
	static const TGLTFJsonColor Black;
	static const TGLTFJsonColor White;

	TGLTFJsonColor(const BaseType& Other)
		: BaseType(Other)
	{
	}

	TGLTFJsonColor& operator=(const BaseType& Other)
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

typedef TGLTFJsonColor<FGLTFColor3> FGLTFJsonColor3;
typedef TGLTFJsonColor<FGLTFColor4> FGLTFJsonColor4;
