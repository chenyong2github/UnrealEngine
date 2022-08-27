// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFConvertBuilder;

template <class IndexType, class KeyType>
class GLTFEXPORTER_API TGLTFConverter
{
public:
	virtual ~TGLTFConverter() = default;

	template <typename... ArgsType>
    FORCEINLINE IndexType Get(ArgsType&&... Args) const
	{
		const KeyType Key(Forward<ArgsType>(Args)...);
		return IndexLookup.FindRef(Key);
	}

	template <typename... ArgsType>
    FORCEINLINE IndexType GetOrAdd(FGLTFConvertBuilder& Builder, const FString& DesiredName, ArgsType&&... Args)
	{
		const KeyType Key(Forward<ArgsType>(Args)...);

		IndexType Index = IndexLookup.FindRef(Key);
		if (Index == INDEX_NONE)
		{
			Index = Add(Builder, DesiredName, Key);
			IndexLookup.Add(Key, Index);
		}

		return Index;
	}

private:

	TMap<KeyType, IndexType> IndexLookup;

	virtual IndexType Add(FGLTFConvertBuilder& Builder, const FString& Name, KeyType Key) = 0;
};
