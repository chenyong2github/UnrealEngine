// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12CrossGPUHeap.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)

#include "TextureShareD3D12Log.h"

FD3D12CrossGPUHeap::FD3D12CrossGPUHeap()
{
	LocalProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, false, GetCurrentProcessId());
}

FD3D12CrossGPUHeap::~FD3D12CrossGPUHeap()
{
	Slave.Release();
	Master.Release();
}

bool FD3D12CrossGPUHeap::CreateCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect* SrcTextureRect)
{
	if (Master.Textures.Contains(ResourceID))
	{
		UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("CrossGPU master '%s' already exist"), *ResourceID);
		return false;
	}

	ID3D12Resource* ResolvedTexture = (ID3D12Resource*)SrcResource->GetTexture2D()->GetNativeResource();
	if (ResolvedTexture)
	{
		FIntPoint Size = SrcTextureRect ? SrcTextureRect->Size() : SrcResource->GetSizeXY();
		EPixelFormat Format = SrcResource->GetFormat();
		
		HANDLE ResourceHandle;

		TSharedPtr<FD3D12CrossGPUItem> ShareItem = MakeShareable(new FD3D12CrossGPUItem(ResourceID, SecurityAttributes));

		bool bSharedTextureValid =
#if TE_CUDA_TEXTURECOPY
			ShareItem->CreateCudaResource(ResolvedTexture, Size, Format, ResourceHandle);
#else
			ShareItem->CreateSharingHeapResource(ResolvedTexture, Size, Format, ResourceHandle);
#endif
		if(bSharedTextureValid)
		{
			// OK, sync with slave
			FTextureSyncData SyncData;
			SyncData.MasterProcessHandle = LocalProcessHandle;
			SyncData.ResourceHandle = ResourceHandle;
			SyncData.Format = Format;

			WriteTextureSyncData(ResourceID, SyncData);

			Master.Textures.Add(ResourceID, ShareItem);
			UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Created shared texture '%s'"), *ResourceID);
			return true;
		}

		UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed create crossGPU share for '%s'"), *ResourceID);
		return false;
	}

	UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Required DX12 resource for '%s'"), *ResourceID);
	return false;

}

bool FD3D12CrossGPUHeap::OpenCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID)
{
	if (Slave.Textures.Contains(ResourceID))
	{
		UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("CrossGPU slave '%s' already exist"), *ResourceID);
		return false;
	}

	FTextureSyncData SyncData;
	if (!ReadTextureSyncData(ResourceID, SyncData))
	{
		UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Can't sync crossGPU share for '%s'"), *ResourceID);
		return false;
	}
	
	TSharedPtr<FD3D12CrossGPUItem> ShareItem = MakeShareable(new FD3D12CrossGPUItem(ResourceID, SecurityAttributes));
	if(ShareItem->OpenSharingHeapResource(SyncData.ResourceHandle, SyncData.Format))
	{
		// OK, sync with slave
		ShareItem->SetTextureProcessHandle(SyncData.MasterProcessHandle);

		Slave.Textures.Add(ResourceID, ShareItem);
		UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Opened shared texture '%s'"), *ResourceID);
		return true;
	}

	UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed open slave crossGPU share for '%s'"), *ResourceID);
	return false;
}

bool FD3D12CrossGPUHeap::BeginCrossGPUSession(FRHICommandListImmediate& RHICmdList)
{
	// Pair sender with receivers
	for (auto& Texture : Master.Textures)
	{
		// Get paired remote process handle
		if (Texture.Value->GetTextureProcessHandle() == 0)
		{
			HANDLE RemoteProcessHandle = nullptr;
			if (!GetTextureSlaveProcessHandle(Texture.Key, RemoteProcessHandle))
			{
				SetErrorIPCSignal();
				UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed pair crossGPU texture '%s' with receiver"), *Texture.Key);
				return false;
			}

			Texture.Value->SetTextureProcessHandle(RemoteProcessHandle);
		}
	}
	

#if TE_ENABLE_FENCE
	// Create master fences:
	for (auto& Texture : Master.Textures)
	{
		const HANDLE RemoteProcessHandle = Texture.Value->GetTextureProcessHandle();
		if (!Master.Fences.Contains(RemoteProcessHandle))
		{
			if (!ImplCreateCrossGPUFence(RHICmdList, RemoteProcessHandle))
			{
				SetErrorIPCSignal();
				UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed create fence for crossGPU share for '%s'"), *Texture.Key);
				return false;
			}
		}
	}

	// Open slave Fences
	for (auto& Texture : Slave.Textures)
	{
		const HANDLE RemoteProcessHandle = Texture.Value->GetTextureProcessHandle();
		if (!Slave.Fences.Contains(RemoteProcessHandle))
		{
			if (!ImplOpenCrossGPUFence(RHICmdList, RemoteProcessHandle))
			{
				SetErrorIPCSignal();
				UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("Failed open fence for crossGPU share for '%s'"), *Texture.Key);
				return false;
			}
		}
	}

	// wait until all fences stay connected
	FPublicCrossGPUSyncData CrossGPUSyncData;
	while (ReadSyncData(CrossGPUSyncData) && !CrossGPUSyncData.IsFenceConnected())
	{
		// Wait for remote process data changes
		FPlatformProcess::SleepNoStats(0.01f);

		//@todo Add timeout

		if (GetErrorIPCSignal())
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("BeginCrossGPUSession Failed: attemp process error on remote node"));
			return false;
		}
	}

#endif

	bIsSessionStarted = true;
	UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Begin D3D12 CrossGPU session"));
	return true;
}

bool FD3D12CrossGPUHeap::EndCrossGPUSession(FRHICommandListImmediate& RHICmdList)
{
	//@todo release fences & interprocess links

	bIsSessionStarted = false;
	UE_LOG(LogD3D12CrossGPUHeap, Log, TEXT("Finish D3D12 CrossGPU session"));
	return true;
}

static void CopyDirectTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect* SrcTextureRect, const FIntRect* DstTextureRect)
{
	// Copy direct, format identical
	FResolveParams Params;
	if (SrcTextureRect || DstTextureRect)
	{
		FIntVector SrcSizeXYZ = SrcTexture->GetSizeXYZ();
		FIntVector DstSizeXYZ = DstTexture->GetSizeXYZ();

		FIntPoint SrcSize(SrcSizeXYZ.X, SrcSizeXYZ.Y);
		FIntPoint DstSize(DstSizeXYZ.X, DstSizeXYZ.Y);

		FIntRect SrcRect = SrcTextureRect ? (*SrcTextureRect) : (FIntRect(FIntPoint(0, 0), SrcSize));
		FIntRect DstRect = DstTextureRect ? (*DstTextureRect) : (FIntRect(FIntPoint(0, 0), DstSize));

		Params.DestArrayIndex = 0;
		Params.SourceArrayIndex = 0;

		Params.Rect.X1 = SrcRect.Min.X;
		Params.Rect.X2 = SrcRect.Max.X;

		Params.Rect.Y1 = SrcRect.Min.Y;
		Params.Rect.Y2 = SrcRect.Max.Y;

		Params.DestRect.X1 = DstRect.Min.X;
		Params.DestRect.X2 = DstRect.Max.X;

		Params.DestRect.Y1 = DstRect.Min.Y;
		Params.DestRect.Y2 = DstRect.Max.Y;
	}

	RHICmdList.CopyToResolveTarget(SrcTexture, DstTexture, Params);
}

bool FD3D12CrossGPUHeap::SendCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect* SrcTextureRect)
{
	if (bIsSessionStarted)
	{
		TSharedPtr<FD3D12CrossGPUItem>* TexturePtr = Master.Textures.Find(ResourceID);
		if (TexturePtr && TexturePtr->IsValid())
		{
			TSharedPtr<FD3D12CrossGPUItem>& Texture = *TexturePtr;

#if TE_ENABLE_FENCE
			TSharedPtr<FD3D12CrossGPUFence> Fence = Master.Fences[Texture->GetTextureProcessHandle()];

			Fence->Execute(RHICmdList, FD3D12CrossGPUFence::EFenceCmd::SendTexture, Texture);
#endif

			CopyDirectTextureImpl_RenderThread(RHICmdList, SrcResource, Texture->GetRHITexture2D(), SrcTextureRect, nullptr);

#if TE_ENABLE_FENCE
			if (Master.IsProcessTexturesChanged(Texture->GetTextureProcessHandle(), Fence->GetFenceValueAvailableAt()))
			{
				// All process testures sended
				Fence->Execute(RHICmdList, FD3D12CrossGPUFence::EFenceCmd::SignalAllTexturesSended, Texture);
			}
#endif
			return true;
		}
		else
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("SendCrossGPUResource(%s): texture not found"),*ResourceID);
		}
	}
	return false;
}

bool FD3D12CrossGPUHeap::ReceiveCrossGPUResource(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect* DstTextureRect)
{
	if (bIsSessionStarted)
	{

		TSharedPtr<FD3D12CrossGPUItem>* TexturePtr = Slave.Textures.Find(ResourceID);
		if (TexturePtr && TexturePtr->IsValid())
		{
			TSharedPtr<FD3D12CrossGPUItem>& Texture = *TexturePtr;

#if TE_ENABLE_FENCE
			TSharedPtr<FD3D12CrossGPUFence> Fence = Slave.Fences[Texture->GetTextureProcessHandle()];

			Fence->Execute(RHICmdList, FD3D12CrossGPUFence::EFenceCmd::ReceiveTexture, Texture);
#endif

			CopyDirectTextureImpl_RenderThread(RHICmdList, Texture->GetRHITexture2D(), DstResource, nullptr, DstTextureRect);

#if TE_ENABLE_FENCE
			if (Slave.IsProcessTexturesChanged(Texture->GetTextureProcessHandle(), Fence->GetFenceValueAvailableAt()))
			{
				// All process testures sended
				Fence->Execute(RHICmdList, FD3D12CrossGPUFence::EFenceCmd::SignalAllTexturesReceived, Texture);
			}
#endif
			return true;
		}
		else
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("ReceiveCrossGPUResource(%s): texture not found"), *ResourceID);
		}
	}
	return false;
}

bool FD3D12CrossGPUHeap::GetTextureSlaveProcessHandle(const FString& ResourceID, HANDLE& OutHandle)
{
	FPublicCrossGPUSyncData PublicData;
	int Index = -1;

	while (Index < 0 && ReadSyncData(PublicData))
	{
		Index = PublicData.FindTexture(ResourceID.ToLower());
		if (Index >= 0)
		{
			// Wait for remote process
			FTextureSyncData Data = PublicData.GetTexture(Index);
			while (Data.SlaveProcessHandle == nullptr && ReadTextureData(Index, Data))
			{
				// Wait for remote process data changes
				FPlatformProcess::SleepNoStats(0.01f);

				if (GetErrorIPCSignal())
				{
					UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("GetTextureSlaveProcessHandle Failed: attemp process error on remote node"));
					return false;
				}
			}

			OutHandle = Data.SlaveProcessHandle;
			return true;
		}

		// Wait for remote process data changes
		FPlatformProcess::SleepNoStats(0.01f);

		if (GetErrorIPCSignal())
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("GetTextureSlaveProcessHandle(%s) Failed: attemp process error on remote node"), *ResourceID);
			return false;
		}
	}

	return false;
}

bool FD3D12CrossGPUHeap::ReadTextureSyncData(const FString& ResourceID, FTextureSyncData& OutData)
{
	FPublicCrossGPUSyncData PublicData;
	int Index = -1;

	while (Index<0 && ReadSyncData(PublicData))
	{
		Index = PublicData.FindTexture(ResourceID.ToLower());
		if (Index >= 0)
		{
			// Remote process read, send back local process handle
			OutData = PublicData.GetTexture(Index);
			if (OutData.SlaveProcessHandle != 0)
			{
				//@todo: this texture already paired
				return false;
			}

			// Send process handle back to master
			OutData.SlaveProcessHandle = LocalProcessHandle;
			return WriteTextureData(Index, OutData);
		}

		// Wait for remote process data changes
		FPlatformProcess::SleepNoStats(0.01f);

		if (GetErrorIPCSignal())
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("ReadTextureSyncData(%s) Failed: attemp process error on remote node"),*ResourceID);
			return false;
		}

	}

	return false;
}

bool FD3D12CrossGPUHeap::WriteTextureSyncData(const FString& ResourceID, const FTextureSyncData InData)
{
	FPublicCrossGPUSyncData PublicData;
	if (ReadSyncData(PublicData))
	{
		FString TextureName = ResourceID.ToLower();

		int Index = PublicData.FindFreeTexture();
		if (Index >= 0)
		{
			FTextureSyncData Dst = InData;
			FPlatformString::Strcpy(Dst.Name, CrossGPUProcessSync::MaxTextureNameLength, *TextureName);

			// Publish changes
			return WriteTextureData(Index, Dst);
		}
	}
	return false;
}

#if TE_ENABLE_FENCE

bool FD3D12CrossGPUHeap::ReadMasterFenceSyncData(const HANDLE MasterProcessHandle, FFenceSyncData& OutFenceData)
{
	FPublicCrossGPUSyncData PublicData;
	int Index = -1;

	while (Index < 0 && ReadSyncData(PublicData))
	{
		Index = PublicData.FindMasterFence(MasterProcessHandle);
		if (Index >= 0)
		{
			// Remote process read, send back local process handle
			OutFenceData = PublicData.GetFence(Index);
			return true;
		}

		// Wait for remote process data changes
		FPlatformProcess::SleepNoStats(0.01f);

		if (GetErrorIPCSignal())
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("ReadMasterFenceSyncData(%u) Failed: attemp process error on remote node"), MasterProcessHandle);
			return false;
		}
	}

	return false;
}

bool FD3D12CrossGPUHeap::WriteMasterFenceSyncData(const HANDLE MasterProcessHandle, const FFenceSyncData& InFenceData)
{
	FPublicCrossGPUSyncData PublicData;
	if (ReadSyncData(PublicData))
	{
		if (PublicData.FindMasterFence(MasterProcessHandle) >= 0)
		{
			// fence already exist
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("WriteMasterFenceSyncData MasterProcessHandle '%u' already exist"), MasterProcessHandle);
			return false;
		}

		int Index = PublicData.FindFreeMasterFence();
		if (Index >= 0)
		{
			// Publish changes
			WriteFenceData(Index, InFenceData);
			return true;
		}
	}
	return false;
}

bool FD3D12CrossGPUHeap::ImplCreateCrossGPUFence(FRHICommandListImmediate& RHICmdList, const HANDLE ProcessHandle)
{
	if (Master.Fences.Contains(ProcessHandle))
	{
		return true;
	}

	// Create new fence
	TSharedPtr<FD3D12CrossGPUFence> Fence = MakeShareable(new FD3D12CrossGPUFence(ProcessHandle, Security));

	uint64 InitialValue = 0;
	FFenceSyncData FenceData;
	if (Fence->Create(LocalProcessHandle, FenceData.FenceHandle, InitialValue))
	{
		// Save master process handle (connected from this side)
		FenceData.MasterProcessHandle = LocalProcessHandle;

		if (WriteMasterFenceSyncData(ProcessHandle, FenceData))
		{
			Master.Fences.Emplace(ProcessHandle, Fence);
			return true;
		}
	}

	UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("CrossGPU master fence not created"));
	return false;
}

bool FD3D12CrossGPUHeap::ImplOpenCrossGPUFence(FRHICommandListImmediate& RHICmdList, const HANDLE ProcessHandle)
{
	if (Slave.Fences.Contains(ProcessHandle))
	{
		return true;
	}

	// Open new fence
	TSharedPtr<FD3D12CrossGPUFence> Fence = MakeShareable(new FD3D12CrossGPUFence(ProcessHandle, Security));

	FFenceSyncData FenceData;
	if (ReadMasterFenceSyncData(ProcessHandle, FenceData))
	{
		if (Fence->Open(FenceData.FenceHandle))
		{
			// Save slave process handle (connected from this side)
			FenceData.SlaveProcessHandle = LocalProcessHandle;
			if (WriteMasterFenceSyncData(ProcessHandle, FenceData))
			{
				Slave.Fences.Emplace(ProcessHandle, Fence);
				return true;
			}
		}
	}

	UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("CrossGPU slave fence not opened"));
	return false;
}
#endif

/*
 * FD3D12CrossGPUHeap::FCrossGPUResource
 */
bool FD3D12CrossGPUHeap::FCrossGPUResource::IsProcessTexturesChanged(const HANDLE ProcessHandle, uint64 CurrentFenceValue) const
{
	for (const auto& It : Textures)
	{
		if (It.Value->IsTextureProcessHandle(ProcessHandle))
		{
			if (!It.Value->IsFenceChanged(CurrentFenceValue))
			{
				// Wait for this texture change, before fence signal for ths process
				return false;
			}
		}
	}
	return true;
}

void FD3D12CrossGPUHeap::FCrossGPUResource::Release()
{
	//@todo: check TMap destructor
	for (auto& It : Textures)
	{
		It.Value->Release();
		It.Value.Reset();
	}
	Textures.Empty();

#if TE_ENABLE_FENCE
	for (auto& It : Fences)
	{
		It.Value->Release();
		It.Value.Reset();
	}
	Fences.Empty();
#endif
}

#endif
