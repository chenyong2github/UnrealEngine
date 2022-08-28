// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FGLTFConvertBuilder;

template <typename IndexType, typename... ArgTypes>
class TGLTFConverter
{
	typedef TTuple<ArgTypes...> KeyType;

public:

	virtual ~TGLTFConverter() = default;

	IndexType Get(ArgTypes&&... Args) const
	{
		const KeyType Key(Forward<ArgTypes>(Args)...);
		return IndexLookup.FindRef(Key);
	}

	IndexType GetOrAdd(FGLTFConvertBuilder& Builder, const FString& DesiredName, ArgTypes... Args)
	{
		const KeyType Key(Forward<ArgTypes>(Args)...);

		IndexType Index = IndexLookup.FindRef(Key);
		if (Index == INDEX_NONE)
		{
			Index = Add(Builder, DesiredName, Forward<ArgTypes>(Args)...);
			IndexLookup.Add(Key, Index);
		}

		return Index;
	}

private:

	virtual IndexType Add(FGLTFConvertBuilder& Builder, const FString& Name, ArgTypes... Args) = 0;

	TMap<KeyType, IndexType> IndexLookup;
};
