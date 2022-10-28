// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataInterfaceContext.h"

namespace UE::DataInterface
{

// Call the interface with the provided context (which should contain the parameters and result for the interface)
template<typename InterfaceType>
static bool GetDataSafe(const TScriptInterface<InterfaceType> DataInterface, const FContext& InContext)
{
	if(DataInterface.GetInterface() != nullptr)
	{
		return DataInterface.GetInterface()->GetData(InContext);
	}

	return false;
}

//// Set the Result into the context and call the interface
//template<typename InterfaceType, typename ValueType>
//static bool GetDataSafe(const TScriptInterface<InterfaceType> DataInterface, FContext& InContext, ValueType& Result)
//{
//	if (DataInterface.GetInterface() != nullptr)
//	{
//		if constexpr (TIsDerivedFrom<ValueType, FParam>::Value)
//		{
//			const FContext ResultHolder = InContext.WithResult(Result);
//			return DataInterface.GetInterface()->GetData(ResultHolder);
//		}
//		else
//		{
//			// Support containers with Num() and GetData()
//			if constexpr (TModels<Private::CSizedContainerWithAccessibleDataAsRawPtr, ValueType>::Value)
//			{
//				TWrapParam<ValueType::ElementType> WrappedResult(Result.GetData(), Result.Num());
//				const FContext ResultHolder = InContext.WithResult(WrappedResult);
//				return DataInterface.GetInterface()->GetData(ResultHolder);
//			}
//			else
//			{
//				TWrapParam<ValueType> WrappedResult(&Result);
//				const FContext ResultHolder = InContext.WithResult(WrappedResult);
//				return DataInterface.GetInterface()->GetData(ResultHolder);
//			}
//		}
//	}
//
//	return false;
//}

// This version accepts a const Context, but it crates a sub context in order to be able to pass the Result
// Using the non const version is a better option if possible
template<typename InterfaceType, typename ValueType>
static bool GetDataSafe(const TScriptInterface<InterfaceType> DataInterface, const FContext& InContext, ValueType& Result)
{
	if(DataInterface.GetInterface() != nullptr)
	{
		if constexpr (TIsDerivedFrom<ValueType, FParam>::Value)
		{
			return DataInterface.GetInterface()->GetData(InContext, Result);
		}
		else
		{
			// Support containers with Num() and GetData()
			if constexpr (TModels<Private::CSizedContainerWithAccessibleDataAsRawPtr, ValueType>::Value)
			{
				TWrapParam<typename ValueType::ElementType> WrappedResult(Result.GetData(), Result.Num());
				return DataInterface.GetInterface()->GetData(InContext, WrappedResult);
			}
			else
			{
				TWrapParam<ValueType> WrappedResult(&Result);
				return DataInterface.GetInterface()->GetData(InContext, WrappedResult);
			}
		}
	}

	return false;
}

template<typename ValueType>
static bool GetParameter(const FContext& InContext, FName InKey, ValueType& Result)
{
	if constexpr (TIsDerivedFrom<ValueType, FParam>::Value)
	{
		return InContext.GetParameter(InKey, Result);
	}
	else
	{
		TWrapParam<ValueType> WrappedResult(&Result);
		return InContext.GetParameter(InKey, WrappedResult);
	}
}

}