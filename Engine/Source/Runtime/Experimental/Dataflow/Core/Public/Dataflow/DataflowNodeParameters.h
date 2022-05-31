// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class  UDataflow;

namespace Dataflow
{
	class DATAFLOWCORE_API FContext
	{
	public:
		FContext(float InTime, FName InType = FName("Unknown"))
		: Timestamp(InTime) 
		, Type(InType){}

		float Timestamp = 0.f;
		FName Type = FName("Unknown");

		uint32 GetTypeHash() const
		{
			return ::GetTypeHash(Timestamp);
		}

		template<class T>
		const T* AsType(FName InType) const
		{
			if (Type.IsEqual(InType))
			{
				return (T*)this;
			}
			return nullptr;
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
