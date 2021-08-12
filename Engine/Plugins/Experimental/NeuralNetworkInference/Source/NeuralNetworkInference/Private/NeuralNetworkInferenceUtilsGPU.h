// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RHI.h"

class FNeuralNetworkInferenceUtilsGPU
{
public:
	/**
	 * If OutReadBufferis nullptr, it creates a new FReadBuffer pointer into OutReadBuffer and copies the data from InArrayData.
	 * @param InDebugName Input name for FReadBuffer::Initialize().
	 */
	static void CreateAndLoadSRVBuffer(TSharedPtr<FReadBuffer>& OutReadBuffer, const TArray<uint32>& InArrayData, const TCHAR* InDebugName);

	/**
	 * Sanity checks when running the forward operators or their related GPU functions.
	 */
	static bool GPUSanityChecks(const FRDGBuilder* const InGraphBuilder);
	static bool GPUSanityChecks(const FRDGBuilder* const InGraphBuilder, const bool bInIsLoaded);
};
