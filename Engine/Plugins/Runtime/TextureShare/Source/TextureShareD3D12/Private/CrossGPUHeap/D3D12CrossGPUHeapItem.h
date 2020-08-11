// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "ID3D12CrossGPUHeap.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)
#include "D3D12CrossGPUHeapRules.h"
#include "D3D12CrossGPUHeapProcessSync.h"

class FD3D12CrossGPUItem
{
public:
	FD3D12CrossGPUItem(const FString& ResourceID, const FTextureShareD3D12CrossGPUSecurity& Security);
	~FD3D12CrossGPUItem()
	{
		Release();
	}

	bool CreateSharingHeapResource(ID3D12Resource* SrcResource, FIntPoint& Size, EPixelFormat Format, HANDLE& OutResourceHandle);
	bool OpenSharingHeapResource(HANDLE ResourceHandle, EPixelFormat Format);

#if TE_CUDA_TEXTURECOPY
	bool CreateCudaResource(ID3D12Resource* SrcResource, FIntPoint& Size, EPixelFormat Format, HANDLE& OutResourceHandle);
	void InitCuda();
#endif

	FRHITexture2D* GetRHITexture2D() const
	{ return Resource; }
	
	FString GetName() const
	{ return ResourceID; }

	void Release();

	inline bool IsTextureProcessHandle(HANDLE ProcessHandle) const
	{
		return ProcessHandle == RemoteProcessHandle;
	}

	inline void SetTextureProcessHandle(HANDLE ProcessHandle)
	{
		RemoteProcessHandle = ProcessHandle;
	}

	inline HANDLE GetTextureProcessHandle() const
	{
		return RemoteProcessHandle;
	}

	inline bool IsFenceChanged(uint64 CurrentFenceValue) const
	{
		return FenceValue >= CurrentFenceValue;
	}

	inline void ChangeFence(uint64 CurrentFenceValue)
	{
		FenceValue = CurrentFenceValue;
	}


private:
	HANDLE RemoteProcessHandle = nullptr;
	uint64 FenceValue = 0;

protected:
	FString GetSharedHeapName();

private:
	FString          ResourceID;
	FTexture2DRHIRef Resource;

	TRefCountPtr<ID3D12Resource> D3D12Resource;
	const FTextureShareD3D12CrossGPUSecurity& Security;
};
#endif
