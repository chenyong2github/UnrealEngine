// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "ID3D12CrossGPUHeap.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)

#include "D3D12CrossGPUHeapRules.h"
#include "D3D12CrossGPUHeapProcessSync.h"
#include "D3D12CrossGPUHeapItem.h"

#if TE_ENABLE_FENCE
class FD3D12CrossGPUFence
{
public:
	FD3D12CrossGPUFence(HANDLE InFenceProcessHandle, const FD3D12CrossGPUSecurity& Security);
	~FD3D12CrossGPUFence();

public:
	enum class EFenceCmd : uint8
	{
		SendTexture = 0,
		SignalAllTexturesSended,
		ReceiveTexture,
		SignalAllTexturesReceived,
	};

	void Execute(FRHICommandListImmediate& RHICmdList, EFenceCmd Cmd, TSharedPtr<FD3D12CrossGPUItem>& Texture);
	
	inline uint64 GetFenceValueAvailableAt() const
	{ return FenceValueAvailableAt; }

public:
	bool Create(const HANDLE LocalProcessHandle, HANDLE& OutFenceHandle, uint64 InitialValue);
	bool Open(HANDLE FenceHandle);
	void Release();

private:
	inline ID3D12Fence* GetFence() const { return Fence.GetReference(); }
	//inline HANDLE GetCompletionEvent() const { return hFenceCompleteEvent; }
	//inline bool IsAvailable() const { return FenceValueAvailableAt <= Fence->GetCompletedValue(); }

protected:
	void ImplExecute(FRHICommandListImmediate& RHICmdList, EFenceCmd Cmd, TSharedPtr<FD3D12CrossGPUItem>& Texture);

	bool ImplWait(FRHICommandListImmediate& RHICmdList, uint64 NextFenceValue);
	bool ImplSignal(FRHICommandListImmediate& RHICmdList, uint64 NextFenceValue);

#if TE_USE_NAMEDFENCE
	FString GetSharedHeapName(const HANDLE MasterProcessHandle);
#endif

private:
	HANDLE FenceProcessHandle;
	uint64 FenceValue;
	uint64 FenceValueAvailableAt;

	TRefCountPtr<ID3D12Fence> Fence;
	//HANDLE hFenceCompleteEvent;
	const FD3D12CrossGPUSecurity& Security;
};
#endif

#endif
