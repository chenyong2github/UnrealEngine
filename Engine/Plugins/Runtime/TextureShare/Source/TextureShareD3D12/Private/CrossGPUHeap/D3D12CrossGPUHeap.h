// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "ID3D12CrossGPUHeap.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)

#include "../Platform/TextureShareD3D12PlatformWindows.h"

#include "D3D12CrossGPUHeapRules.h"
#include "D3D12CrossGPUHeapProcessSync.h"
#include "D3D12CrossGPUHeapItem.h"
#include "D3D12CrossGPUHeapFence.h"

class FD3D12CrossGPUHeap
	: public FCrossGPUProcessSync
	, public ID3D12CrossGPUHeap
{
public:
	FD3D12CrossGPUHeap();
	virtual ~FD3D12CrossGPUHeap();

public:
	// DX12 Cross GPU heap resource API (experimental)
	virtual bool CreateCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect* SrcTextureRect) override;
	virtual bool OpenCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID) override;
	virtual bool SendCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect* SrcTextureRect) override;
	virtual bool ReceiveCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect* DstTextureRect) override;
	virtual bool BeginCrossGPUSession(FRHICommandListImmediate& RHICmdList) override;
	virtual bool EndCrossGPUSession(FRHICommandListImmediate& RHICmdList) override;

private:
	bool ReadTextureSyncData(const FString& ResourceID, FTextureSyncData& OutData);
	bool WriteTextureSyncData(const FString& ResourceID, const FTextureSyncData Data);
	bool GetTextureSlaveProcessHandle(const FString& ResourceID, HANDLE& OutHandle);

#if TE_ENABLE_FENCE
	bool ImplCreateCrossGPUFence(FRHICommandListImmediate& RHICmdList, const HANDLE ProcessHandle);
	bool ImplOpenCrossGPUFence(FRHICommandListImmediate& RHICmdList, const HANDLE ProcessHandle);

	bool ReadMasterFenceSyncData(const HANDLE MasterProcessHandle, FFenceSyncData& OutFenceData);
	bool WriteMasterFenceSyncData(const HANDLE MasterProcessHandle, const FFenceSyncData& InFenceData);
#endif

protected:
	FD3D12CrossGPUHeapSecurityAttributes SecurityAttributes;

	class FCrossGPUResource
	{
	public:
		bool IsProcessTexturesChanged(const HANDLE ProcessHandle, uint64 CurrentFenceValue) const;
		void Release();

#if TE_ENABLE_FENCE
		TMap<HANDLE, TSharedPtr<FD3D12CrossGPUFence>> Fences;
#endif
		TMap<FString, TSharedPtr<FD3D12CrossGPUItem>> Textures;
	};

	FCrossGPUResource Master;
	FCrossGPUResource Slave;

	bool bIsSessionStarted = false;

	HANDLE LocalProcessHandle = nullptr;
};
#endif
