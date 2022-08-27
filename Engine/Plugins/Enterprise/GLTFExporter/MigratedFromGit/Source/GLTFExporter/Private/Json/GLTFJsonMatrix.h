// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/GLTFMatrix.h"
#include "Json/GLTFJsonArray.h"

template <typename BaseType>
struct TGLTFJsonMatrix : BaseType, IGLTFJsonArray
{
	static const TGLTFJsonMatrix Identity;

	TGLTFJsonMatrix(const BaseType& Other)
		: BaseType(Other)
	{
	}

	TGLTFJsonMatrix& operator=(const BaseType& Other)
	{
		*static_cast<BaseType*>(this) = Other;
		return *this;
	}

	bool operator==(const BaseType& Other) const
	{
		for (SIZE_T i = 0; i < GetNum(BaseType::Components); ++i)
		{
			if (BaseType::Components[i] != Other.Components[i])
			{
				return false;
			}
		}

		return true;
	}

	bool operator!=(const BaseType& Other) const
	{
		return !(*this == Other);
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		for (SIZE_T i = 0; i < GetNum(BaseType::Elements); ++i)
		{
			Writer.Write(BaseType::Elements[i]);
		}
	}

	bool IsNearlyEqual(const BaseType& Other, float Tolerance = KINDA_SMALL_NUMBER) const
	{
		for (SIZE_T i = 0; i < GetNum(BaseType::Elements); ++i)
		{
			if (!FMath::IsNearlyEqual(BaseType::Elements[i], Other.Elements[i], Tolerance))
			{
				return false;
			}
		}

		return true;
	}
};

struct FGLTFJsonMatrix2 : TGLTFJsonMatrix<FGLTFMatrix2>
{
	static const FGLTFJsonMatrix2 Identity;

	using TGLTFJsonMatrix::TGLTFJsonMatrix;
	using TGLTFJsonMatrix::operator=;
};

struct FGLTFJsonMatrix3 : TGLTFJsonMatrix<FGLTFMatrix3>
{
	static const FGLTFJsonMatrix3 Identity;

	using TGLTFJsonMatrix::TGLTFJsonMatrix;
	using TGLTFJsonMatrix::operator=;
};

struct FGLTFJsonMatrix4 : TGLTFJsonMatrix<FGLTFMatrix4>
{
	static const FGLTFJsonMatrix4 Identity;

	using TGLTFJsonMatrix::TGLTFJsonMatrix;
	using TGLTFJsonMatrix::operator=;
};
