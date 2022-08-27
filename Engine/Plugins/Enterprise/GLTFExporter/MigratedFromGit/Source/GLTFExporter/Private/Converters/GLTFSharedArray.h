// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

template <typename ElementType, typename ArrayType = TArray<ElementType>, ESPMode Mode = ESPMode::Fast>
class TGLTFSharedArray : public TSharedRef<ArrayType, Mode>
{
public:

	TGLTFSharedArray()
		: TSharedRef(MakeShared<ArrayType>())
	{
	}

	using TSharedRef<ArrayType>::TSharedRef;
	using TSharedRef<ArrayType>::operator=;

	bool operator==(const TGLTFSharedArray& Other) const
	{
		return this->Get() == Other.Get();
	}

	bool operator!=(const TGLTFSharedArray& Other) const
	{
		return this->Get() != Other.Get();
	}

	friend uint32 GetTypeHash(const TGLTFSharedArray& SharedArray)
	{
		const ArrayType& Array = SharedArray.Get();
		uint32 Hash = GetTypeHash(Array.Num());

		for (const auto& Element : Array)
		{
			Hash = HashCombine(Hash, GetTypeHash(Element));
		}

		return Hash;
	}
};
