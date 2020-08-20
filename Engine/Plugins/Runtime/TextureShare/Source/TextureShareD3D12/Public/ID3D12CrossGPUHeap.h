// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"

struct ID3D12Resource;

class ID3D12CrossGPUHeap
{
public:
	// DX12 Cross GPU heap resource API (experimental)
	virtual bool CreateCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect* SrcTextureRect) = 0;
	virtual bool OpenCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID) = 0;
	virtual bool SendCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect* SrcTextureRect) = 0;
	virtual bool ReceiveCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect* DstTextureRect) = 0;
	virtual bool BeginCrossGPUSession(FRHICommandListImmediate& RHICmdList) = 0;
	virtual bool EndCrossGPUSession(FRHICommandListImmediate& RHICmdList) = 0;
};
