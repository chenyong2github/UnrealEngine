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
		if (IndexType* FoundIndex = IndexLookup.Find(Key))
		{
			return *FoundIndex;
		}

		IndexType NewIndex = Add(Builder, DesiredName, Forward<ArgTypes>(Args)...);

		IndexLookup.Add(Key, NewIndex);
		return NewIndex;
	}

private:

	virtual IndexType Add(FGLTFConvertBuilder& Builder, const FString& Name, ArgTypes... Args) = 0;

	TMap<KeyType, IndexType> IndexLookup;
};
