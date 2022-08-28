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

	bool operator==(const BaseType& Other) const
	{
		for (uint32 i = 0; i < GetNum(BaseType::Elements); ++i)
		{
			if (BaseType::Elements[i] != Other.Elements[i])
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
};

typedef TGLTFJsonMatrix<FGLTFMatrix2> FGLTFJsonMatrix2;
typedef TGLTFJsonMatrix<FGLTFMatrix3> FGLTFJsonMatrix3;
typedef TGLTFJsonMatrix<FGLTFMatrix4> FGLTFJsonMatrix4;
