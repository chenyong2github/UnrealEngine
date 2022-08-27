// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonArray.h"

struct FGLTFJsonColor3 : IGLTFJsonArray
{
	float R, G, B;

	static const FGLTFJsonColor3 Black;
	static const FGLTFJsonColor3 White;

	FGLTFJsonColor3(float R, float G, float B)
		: R(R), G(G), B(B)
	{
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(R);
		Writer.Write(G);
		Writer.Write(B);
	}

	bool operator==(const FGLTFJsonColor3& Other) const
	{
		return R == Other.R
			&& G == Other.G
			&& B == Other.B;
	}

	bool operator!=(const FGLTFJsonColor3& Other) const
	{
		return R != Other.R
			|| G != Other.G
			|| B != Other.B;
	}
};
