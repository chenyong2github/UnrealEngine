// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class  UDataflow;

namespace Dataflow
{
	class DATAFLOWCORE_API FContext
	{
	public:
		FContext(float InTime, FString InType = FString(""))
		: Timestamp(InTime)
		, Type(StaticType().Append(InType))
		{}

		float Timestamp = 0.f;
		FString Type;
		static FString StaticType() { return "FContext"; }

		uint32 GetTypeHash() const
		{
			return ::GetTypeHash(Timestamp);
		}

		template<class T>
		const T* AsType() const
		{
			if (Type.Contains(T::StaticType()))
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
