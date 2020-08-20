// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12CrossGPUHeapFence.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)
#include "TextureShareD3D12Log.h"
#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"

#if TE_ENABLE_FENCE

// macro to deal with COM calls inside a function that returns `{}` on error
#define CHECK_HR_DEFAULT(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res)); \
			return {};\
		}\
	}


FD3D12CrossGPUFence::FD3D12CrossGPUFence(HANDLE InFenceProcessHandle, const FD3D12CrossGPUSecurity& InSecurity)
	: FenceProcessHandle(InFenceProcessHandle)
	, FenceValue(0)
	, FenceValueAvailableAt(0)
	//, hFenceCompleteEvent(INVALID_HANDLE_VALUE)
	, Security(InSecurity)
{
	//hFenceCompleteEvent = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
	//check(INVALID_HANDLE_VALUE != hFenceCompleteEvent);
}

FD3D12CrossGPUFence::~FD3D12CrossGPUFence()
{
	/*
	if (hFenceCompleteEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFenceCompleteEvent);
		hFenceCompleteEvent = INVALID_HANDLE_VALUE;
	}
	*/

	Release();
}

void FD3D12CrossGPUFence::Release()
{
	Fence.SafeRelease();
}

void FD3D12CrossGPUFence::Execute(FRHICommandListImmediate& RHICmdList, EFenceCmd Cmd, TSharedPtr<FD3D12CrossGPUItem>& Texture)
{
	//@todo: move inside RHICmdList
	ImplExecute(RHICmdList, Cmd, Texture);
}

void FD3D12CrossGPUFence::ImplExecute(FRHICommandListImmediate& RHICmdList, EFenceCmd Cmd, TSharedPtr<FD3D12CrossGPUItem>& Texture)
{
	switch (Cmd)
	{
	
	case EFenceCmd::SendTexture:
	{
#if TE_FENCE_DEBUG_LOG
		UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Fence[%u] SendTexture(%s) %u,%u"), FenceProcessHandle, *Texture->GetName(), FenceValue, FenceValueAvailableAt);
#endif

		if (FenceValue == FenceValueAvailableAt)
		{
			if (!ImplWait(RHICmdList, FenceValue + 1))
			{
				UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed Fence Wait"));
				return;
			}
			FenceValueAvailableAt++;
		}

		Texture->ChangeFence(FenceValueAvailableAt);
		break;
	}

	case EFenceCmd::SignalAllTexturesSended:
	case EFenceCmd::SignalAllTexturesReceived:
	{
		ImplSignal(RHICmdList, FenceValue);

		FenceValue = FenceValueAvailableAt;
		break;
	}

	case EFenceCmd::ReceiveTexture:
	{
#if TE_FENCE_DEBUG_LOG
		UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Fence[%u] ReceiveTexture(%s) %u,%u"), FenceProcessHandle, *Texture->GetName(), FenceValue, FenceValueAvailableAt);
#endif

		if (FenceValue == FenceValueAvailableAt)
		{
			if (!ImplWait(RHICmdList, FenceValue + 1))
			{
				UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed Fence Wait"));
				return;
			}
		}

		Texture->ChangeFence(FenceValueAvailableAt);
		break;
	}

	}
}

bool FD3D12CrossGPUFence::ImplWait(FRHICommandListImmediate& RHICmdList, uint64 NextFenceValue)
{
#if TE_FENCE_DEBUG_LOG
	UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Fence[%u] Wait(%u)"), FenceProcessHandle, NextFenceValue);
#endif

	// Wait until the GPU has completed commands up to this fence point.
	if (GetFence()->GetCompletedValue() < NextFenceValue)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		CHECK_HR_DEFAULT(GetFence()->SetEventOnCompletion(NextFenceValue, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	FenceValueAvailableAt = NextFenceValue;
	return true;
}

bool FD3D12CrossGPUFence::ImplSignal(FRHICommandListImmediate& RHICmdList, uint64 NextFenceValue)
{
#if TE_FENCE_DEBUG_LOG
	UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Fence[%u] Signal(%u)"), FenceProcessHandle, NextFenceValue);
#endif

	//@todo integrate into RHICmd. Now just flush
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

	ID3D12CommandQueue* D3D12CommandQueue = DynamicRHI->RHIGetD3DCommandQueue();

	// Schedule a signal in the command queue
	HRESULT hr = D3D12CommandQueue->Signal(GetFence(), NextFenceValue);
	if (!SUCCEEDED(hr))
	{
		UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed Fence[%u] Signal(%u)"), FenceProcessHandle, NextFenceValue);
	}

	// Save signal value
	FenceValueAvailableAt = NextFenceValue;
	return true;
}

#if TE_USE_NAMEDFENCE
FString FD3D12CrossGPUFence::GetSharedHeapName(const HANDLE MasterProcessHandle)
{
	return FString::Printf(TEXT("Global\\CrossGPUFence_%u"), MasterProcessHandle);
}
#endif

bool FD3D12CrossGPUFence::Create(const HANDLE LocalProcessHandle, HANDLE& OutFenceHandle, uint64 InitialValue)
{
	auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());

	// Create fence for cross adapter resources
	CHECK_HR_DEFAULT(UE4D3DDevice->CreateFence(
		InitialValue,
		D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER,
		IID_PPV_ARGS(Fence.GetInitReference())));

	OutFenceHandle = nullptr;
#if TE_USE_NAMEDFENCE
	FString FenceHeapName = GetSharedHeapName(LocalProcessHandle);
	CHECK_HR_DEFAULT(UE4D3DDevice->CreateSharedHandle(Fence, *Security, GENERIC_ALL, *FenceHeapName, &OutFenceHandle));
#else
	CHECK_HR_DEFAULT(UE4D3DDevice->CreateSharedHandle(Fence, *Security, GENERIC_ALL, nullptr, &OutFenceHandle));
#endif

	return true;
}


bool FD3D12CrossGPUFence::Open(HANDLE FenceHandle)
{
	auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());

	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

	// Open shared handle on secondaryDevice device
#if TE_USE_NAMEDFENCE
	HANDLE NamedResourceHandle;
	FString FenceHeapName = GetSharedHeapName(FenceProcessHandle);
	CHECK_HR_DEFAULT(UE4D3DDevice->OpenSharedHandleByName(*FenceHeapName, GENERIC_ALL, &NamedResourceHandle));
	CHECK_HR_DEFAULT(UE4D3DDevice->OpenSharedHandle(NamedResourceHandle, IID_PPV_ARGS(Fence.GetInitReference())));
	CloseHandle(NamedResourceHandle);
#else
	CHECK_HR_DEFAULT(UE4D3DDevice->OpenSharedHandle(FenceHandle, IID_PPV_ARGS(Fence.GetInitReference())));
	CloseHandle(FenceHandle);
#endif

	return true;
}
/*
bool FD3D12CrossGPUFence::SetupFence()
{
	auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

	ID3D12CommandQueue* D3D12CommandQueue = DynamicRHI->RHIGetD3DCommandQueue();

	// Schedule a signal in the command queue
	CHECK_HR_DEFAULT(D3D12CommandQueue->Signal(GetFence(), mCurrentFenceValue));
	mFenceValues[0] = mCurrentFenceValue;
	mCurrentFenceValue++;

	// Wait until fence has been processed
	ThrowIfFailed(GetFence()->SetEventOnCompletion(mFenceValues[0], mFenceEvents[Device_Primary]));
	WaitForSingleObjectEx(mFenceEvents[Device_Primary], INFINITE, FALSE);

	return true;
}*/


/*
// Sync with RHICmdList

FRHICOMMAND_MACRO(FD3D12CrossGPUFenceCmd)
{
	FD3D12CrossGPUFence* Fence;
	FD3D12CrossGPUFence::EFenceCmd            Cmd;

	FORCEINLINE_DEBUGGABLE FD3D12CrossGPUFenceCmd(FD3D12CrossGPUFence * InFence, FD3D12CrossGPUFence::EFenceCmd InCmd)
		: Fence(InFence)
		, Cmd(InCmd)
	{
		ensure(Fence);
	}
	RHI_API void Execute(FRHICommandListBase & CmdList);
};

void FD3D12CrossGPUFenceCmd::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(D3D12CrossGPUFenceCmd);
	Fence->ImplExecuteCmd(CmdList, Cmd);
}

#define RHIALLOC_COMMAND(...) new ( RHICmdList.AllocCommand(sizeof(__VA_ARGS__), alignof(__VA_ARGS__)) ) __VA_ARGS__

void FD3D12CrossGPUFence::ExecuteCmd(FRHICommandListImmediate& RHICmdList, EFenceCmd Cmd)
{
	//check(IsOutsideRenderPass());
	if (RHICmdList.Bypass())
	{
		ImplExecuteCmd(RHICmdList, Cmd);
		return;
	}
	RHIALLOC_COMMAND(FD3D12CrossGPUFenceCmd)(this, Cmd);
}

void FD3D12CrossGPUFence::ImplExecuteCmd(FRHICommandListBase& RHICmdList, EFenceCmd Cmd)
{
	switch (Cmd)
	{
	case EFenceCmd::Signal:
		ImplSignal(RHICmdList);
		break;
	case EFenceCmd::Wait:
		ImplWait(RHICmdList);
		break;
	}
}
*/

#endif

#endif
