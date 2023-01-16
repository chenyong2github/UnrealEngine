// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNECoreRuntime.h"
#include "UObject/WeakInterfacePtr.h"

NNXCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogNNE, Log, All);

namespace UE::NNECore
{
	NNXCORE_API bool RegisterRuntime(TWeakInterfacePtr<INNERuntime> Runtime);

	NNXCORE_API bool UnregisterRuntime(TWeakInterfacePtr<INNERuntime> Runtime);
	
	// Enumerate all available runtime modules
	NNXCORE_API TArrayView<TWeakInterfacePtr<INNERuntime>> GetAllRuntimes();

	/// Get runtime by name
	template<class T> TWeakInterfacePtr<T> GetRuntime(const FString& Name)
	{
		for (TWeakInterfacePtr<INNERuntime> Runtime : GetAllRuntimes())
		{
			if (Runtime->GetRuntimeName() == Name)
			{
				T* RuntimePtr = Cast<T>(Runtime.Get());
				return TWeakInterfacePtr<T>(RuntimePtr);
			}
				
		}
		return TWeakInterfacePtr<T>(nullptr);
	}
	
}
