// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

template <typename ElementType, typename ArrayType = TArray<ElementType>, ESPMode Mode = ESPMode::Fast>
class TGLTFSharedArray : public TSharedRef<ArrayType>
{
public:

	template <typename... ArgTypes>
	TGLTFSharedArray(ArgTypes&&... Args)
		: TSharedRef(MakeShared<ArrayType>(Forward<ArgTypes>(Args)...))
	{
	}

	TGLTFSharedArray(TGLTFSharedArray& SharedArray)
		: TSharedRef(SharedArray)
	{
	}

	TGLTFSharedArray(TGLTFSharedArray const& SharedArray)
		: TSharedRef(SharedArray)
	{
	}

	TGLTFSharedArray(TGLTFSharedArray&& SharedArray) noexcept
		: TSharedRef(SharedArray)
	{
	}

	TGLTFSharedArray& operator=(TGLTFSharedArray& Other)
	{
		TSharedRef<ArrayType>::operator=(Other);
		return *this;
	}

	TGLTFSharedArray& operator=(TGLTFSharedArray const& Other)
	{
		TSharedRef<ArrayType>::operator=(Other);
		return *this;
	}

	TGLTFSharedArray& operator=(TGLTFSharedArray&& Other) noexcept
	{
		TSharedRef<ArrayType>::operator=(Other);
		return *this;
	}

	bool operator==(const TGLTFSharedArray& Other) const
	{
		return this->Get() == Other.Get();
	}

	bool operator!=(const TGLTFSharedArray& Other) const
	{
		return this->Get() != Other.Get();
	}

	using TSharedRef<ArrayType>::TSharedRef;
	using TSharedRef<ArrayType>::operator=;

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
