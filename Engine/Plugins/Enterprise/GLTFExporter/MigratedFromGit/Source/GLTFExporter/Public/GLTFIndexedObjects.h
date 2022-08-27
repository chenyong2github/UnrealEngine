// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFIndexedBuilder;

template <class IndexType, class KeyType, class AdderType>
struct GLTFEXPORTER_API TGLTFIndexedObjects
{
	TMap<KeyType, IndexType> IndexLookup;

	template <typename... ArgsType>
	FORCEINLINE IndexType Get(ArgsType&&... Args) const
	{
		const KeyType Key(Forward<ArgsType>(Args)...);
		return IndexLookup.FindRef(Key);
	}

	template <typename... ArgsType>
	FORCEINLINE IndexType GetOrAdd(FGLTFIndexedBuilder& Builder, const FString& DesiredName, ArgsType&&... Args)
	{
		const KeyType Key(Forward<ArgsType>(Args)...);

		IndexType Index = IndexLookup.FindRef(Key);
		if (Index == INDEX_NONE)
		{
			Index = AdderType::Add(Builder, DesiredName, Forward<ArgsType>(Args)...);
			IndexLookup.Add(Key, Index);
		}

		return Index;
	}
};
