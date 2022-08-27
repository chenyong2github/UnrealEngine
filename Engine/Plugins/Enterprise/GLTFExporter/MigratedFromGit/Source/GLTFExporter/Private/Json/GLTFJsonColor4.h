// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonArray.h"
#include "Json/GLTFJsonColor3.h"

struct FGLTFJsonColor4 : IGLTFJsonArray
{
	float R, G, B, A;

	static const FGLTFJsonColor4 Black;
	static const FGLTFJsonColor4 White;

	FGLTFJsonColor4(float R, float G, float B, float A = 1.0f)
		: R(R), G(G), B(B), A(A)
	{
	}

	FGLTFJsonColor4(const FGLTFJsonColor3& Color3, float A = 1.0f)
		: R(Color3.R), G(Color3.G), B(Color3.B), A(A)
	{
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(R);
		Writer.Write(G);
		Writer.Write(B);
		Writer.Write(A);
	}

	operator FGLTFJsonColor3() const
	{
		return { R, G, B };
	}

	bool operator==(const FGLTFJsonColor4& Other) const
	{
		return R == Other.R
			&& G == Other.G
			&& B == Other.B
			&& A == Other.A;
	}

	bool operator!=(const FGLTFJsonColor4& Other) const
	{
		return R != Other.R
			|| G != Other.G
			|| B != Other.B
			|| A != Other.A;
	}
};
