// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/GLTFMatrix.h"
#include "Json/GLTFJsonArray.h"

template <typename BaseType>
struct TGLTFJsonMatrix final : BaseType, IGLTFJsonArray
{
	static const TGLTFJsonMatrix Identity;

	TGLTFJsonMatrix(const BaseType& Other)
		: TGLTFMatrix(Other)
	{
	}

	TGLTFJsonMatrix& operator=(const BaseType& Other)
	{
		*static_cast<BaseType*>(this) = Other;
		return *this;
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		for (int32 i = 0; i < GetNum(BaseType::Elements); ++i)
		{
			Writer.Write(BaseType::Elements[i]);
		}
	}

	bool IsNearlyEqual(const BaseType& Other, float Tolerance = KINDA_SMALL_NUMBER) const
	{
		for (int32 i = 0; i < GetNum(BaseType::Elements); ++i)
		{
			if (!FMath::IsNearlyEqual(BaseType::Elements[i], Other.Elements[i], Tolerance))
			{
				return false;
			}
		}

		return true;
	}
};

typedef TGLTFJsonMatrix<FGLTFMatrix2> FGLTFJsonMatrix2;
typedef TGLTFJsonMatrix<FGLTFMatrix3> FGLTFJsonMatrix3;
typedef TGLTFJsonMatrix<FGLTFMatrix4> FGLTFJsonMatrix4;
