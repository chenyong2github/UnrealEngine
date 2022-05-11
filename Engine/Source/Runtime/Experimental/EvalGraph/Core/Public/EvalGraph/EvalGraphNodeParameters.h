// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphConnectionTypes.h"

class  UEvalGraph;
class  UEdGraphPin;

namespace Eg
{
	struct EVALGRAPHCORE_API FContext
	{
		float Timestamp = 0.f;

		uint32 GetTypeHash() const
		{
			return ::GetTypeHash(Timestamp);
		}
	};


	struct FCacheValueBase {
		FCacheValueBase(EGraphConnectionType InType) : Type(InType) {}
		EGraphConnectionType Type;
	};

	template<class T>
	struct TCacheValue : public FCacheValueBase {
		TCacheValue(T InData = T())
			: FCacheValueBase(GraphConnectionType<T>())
			, Data(InData) {}
		T Data;
	};
}

FORCEINLINE uint32 GetTypeHash(const Eg::FContext& Context)
{
	return ::GetTypeHash(Context.Timestamp);
}
