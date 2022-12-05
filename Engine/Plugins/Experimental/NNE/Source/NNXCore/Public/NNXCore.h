// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

NNXCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogNNX, Log, All);

namespace NNX
{
	class IRuntime;

	/// Register runtime
	NNXCORE_API bool RegisterRuntime(IRuntime* Runtime);
	
	/// Unregister runtime
	NNXCORE_API bool UnregisterRuntime(IRuntime* Runtime);

	/// Enumerate all available runtime modules
	NNXCORE_API TArray<IRuntime*> GetAllRuntimes();

	/// Get runtime by name
	NNXCORE_API IRuntime* GetRuntime(const FString& Name);
}
