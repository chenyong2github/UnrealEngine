// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceLegacyUtils.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderingThread.h"
#include <atomic>



/* FNeuralNetworkInferenceLegacyUtils public functions
 *****************************************************************************/

void FNeuralNetworkInferenceLegacyUtils::WaitUntilRHIFinished()
{
	std::atomic<bool> bDidGPUFinish(false);
	ENQUEUE_RENDER_COMMAND(ForwardGPU_Gemm_RenderThread)(
		[&bDidGPUFinish](FRHICommandListImmediate& RHICmdList)
		{
			bDidGPUFinish = true;
		}
	);
	while (!bDidGPUFinish)
	{
		FPlatformProcess::Sleep(0.1e-3);
	}
}
