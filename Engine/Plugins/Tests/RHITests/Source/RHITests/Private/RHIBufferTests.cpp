// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIBufferTests.h"

// Copies data in the specified vertex buffer back to the CPU, and passes a pointer to that data to the provided verification lambda.
bool FRHIBufferTests::VerifyBufferContents(const TCHAR* TestName, FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, TFunctionRef<bool(void* Ptr, uint32 NumBytes)> VerifyCallback)
{
	bool Result;
	{
		uint32 NumBytes = Buffer->GetSize();

		FStagingBufferRHIRef StagingBuffer = RHICreateStagingBuffer();
		RHICmdList.CopyToStagingBuffer(Buffer, StagingBuffer, 0, NumBytes);

		// @todo - readback API is inconsistent across RHIs
		RHICmdList.SubmitCommandsAndFlushGPU();
		RHICmdList.BlockUntilGPUIdle();

		void* Memory = RHILockStagingBuffer(StagingBuffer, 0, NumBytes);
		Result = VerifyCallback(Memory, NumBytes);
		RHIUnlockStagingBuffer(StagingBuffer);
	}

	// Immediate flush to clean up the staging buffer / other resources
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

	if (!Result)
	{
		UE_LOG(LogRHIUnitTestCommandlet, Error, TEXT("Test failed. \"%s\""), TestName);
	}
	else
	{
		UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Test passed. \"%s\""), TestName);
	}

	return Result;
}
