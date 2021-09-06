// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/NetworkPredictionAsyncInstanceData.h"
#include "Templates/UnrealTemplate.h"

namespace UE_NP {

template<typename InAsyncServiceInterface, int32 NumInlineServices=4>
struct TAsyncServiceStorage
{
	using AsyncServiceInterface = InAsyncServiceInterface;
	TArray<TUniquePtr<AsyncServiceInterface>, TInlineAllocator<NumInlineServices>> Array;

	template<typename AsyncModelDef, typename ServiceType, typename... ArgsType>
	void Instantiate(ArgsType&&... Args)
	{
		npCheck(AsyncModelDef::ID >= 0);

		// Resize array for this ModelDef if necessary
		if (Array.IsValidIndex(AsyncModelDef::ID) == false)
		{
			Array.SetNum(AsyncModelDef::ID+1);
		}

		// Allocate instance on the UniquePtr if necessary
		auto& Ptr = Array[AsyncModelDef::ID];
		if (Ptr.IsValid() == false)
		{
			Ptr = MakeUnique<ServiceType>(Forward<ArgsType>(Args)...);
		}
	}
};

} // namespace UE_NP