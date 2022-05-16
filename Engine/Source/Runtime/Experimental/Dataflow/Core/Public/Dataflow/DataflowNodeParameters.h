// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class  UDataflow;

namespace Dataflow
{
	struct DATAFLOWCORE_API FContext
	{
		float Timestamp = 0.f;

		uint32 GetTypeHash() const
		{
			return ::GetTypeHash(Timestamp);
		}
	};


	template<class T>
	struct TCacheValue  {
		TCacheValue(T InData = T())
			: Data(InData) {}
		T Data;
	};
}

FORCEINLINE uint32 GetTypeHash(const Dataflow::FContext& Context)
{
	return ::GetTypeHash(Context.Timestamp);
}
