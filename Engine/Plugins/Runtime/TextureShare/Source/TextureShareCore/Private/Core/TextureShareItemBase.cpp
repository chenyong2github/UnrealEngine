// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareItemBase.h"

#include "DirectX/TextureShareItemD3D11.h"
#include "DirectX/TextureShareItemD3D12.h"

#include "TextureShareCoreLog.h"
#include "Misc/ScopeLock.h"

namespace TextureShareItem
{
	FTextureShareSyncPolicySettings FTextureShareItemBase::SyncPolicySettings[] =
	{
		FTextureShareSyncPolicySettings(ETextureShareProcess::Server),
		FTextureShareSyncPolicySettings(ETextureShareProcess::Client)
	};

	const FTextureShareSyncPolicySettings& FTextureShareItemBase::GetSyncPolicySettings(ETextureShareProcess Process)
	{
		return SyncPolicySettings[(int)Process];
	}

	void FTextureShareItemBase::SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings)
	{
		SyncPolicySettings[(int)Process] = InSyncPolicySettings;
	}

	enum class ESyncProcessFailAction : uint8
	{
		None = 0,
		ConnectionLost
	};

	class FSyncProcess
	{
	public:
		FSyncProcess(FTextureShareItemBase& ShareItem, float InTimeOut, ESyncProcessFailAction InFailAction = ESyncProcessFailAction::None, float InWaitSeconds= 1.f/200.f)
			: ShareItem(ShareItem)
			, FailAction(InFailAction)
			, Time0(FPlatformTime::Seconds())
			, TimeOut(InTimeOut)
			, WaitSeconds(InWaitSeconds)
		{}

		bool Tick()
		{
			if (ShareItem.CheckRemoteConnectionLost())
			{
				return false;
			}

			// If timeout is defined, checking elasped waiting time
			double TotalWaitTime = FPlatformTime::Seconds() - Time0;
			bool isTimeOut = (TimeOut>0) && (TotalWaitTime > TimeOut);

			if (ShareItem.TryFrameSyncLost())
			{
				// remote process lost. break sync
				return false;
			}

			switch (FailAction)
			{
			case ESyncProcessFailAction::ConnectionLost:
				if (isTimeOut)
				{
					ShareItem.RemoteConnectionLost();
					return false;
				}
				break;
			default:
				if (isTimeOut)
				{
					return false;
				}
				break;
			}

			// Wait for remote process data changes
			FPlatformProcess::SleepNoStats(WaitSeconds);

			// Update data from remote process
			ShareItem.ReadRemoteProcessData();

			return true;
		}

	private:
		FTextureShareItemBase& ShareItem;
		ESyncProcessFailAction FailAction;
		double Time0;
		double TimeOut;
		float  WaitSeconds;
	};

	bool FTextureShareItemBase::ReadRemoteProcessData()
	{
		const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
		return bIsSessionStarted && SharedResource && SharedResource->ReadRemoteData(GetRemoteProcessData(), SyncSettings.TimeOut.SharedMemorySync*1000);
	}

	bool FTextureShareItemBase::WriteLocalProcessData()
	{
		// Write data every call only inside session
		if (bIsSessionStarted && SharedResource)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			SharedResource->WriteLocalData(GetLocalProcessData(), SyncSettings.TimeOut.SharedMemorySync * 1000);
		}

		return SharedResource!=nullptr;
	}

	FSharedResourceProcessData& FTextureShareItemBase::GetLocalProcessData()
	{
		check(SharedResource)
		return IsClient() ? ResourceData.ClientData : ResourceData.ServerData;
	}

	FSharedResourceProcessData& FTextureShareItemBase::GetRemoteProcessData()
	{
		check(SharedResource);
		return !IsClient() ? ResourceData.ClientData : ResourceData.ServerData;
	}

	const FSharedResourceProcessData& FTextureShareItemBase::GetLocalProcessData() const
	{
		check(SharedResource)
			return IsClient() ? ResourceData.ClientData : ResourceData.ServerData;
	}

	const FSharedResourceProcessData& FTextureShareItemBase::GetRemoteProcessData() const
	{
		check(SharedResource);
		return !IsClient() ? ResourceData.ClientData : ResourceData.ServerData;
	}

	int FTextureShareItemBase::FindTextureIndex(const FSharedResourceProcessData& Src, ESharedResourceTextureState TextureState, bool bNotEqual) const
	{
		for (int i = 0; i < MaxTextureShareItemTexturesCount; i++)
		{
			if ((!bNotEqual && Src.Textures[i].State == TextureState) || (bNotEqual && Src.Textures[i].State != TextureState))
			{
				return i;
			}
		}
		return -1;
	}

	int FTextureShareItemBase::FindTextureIndex(const FSharedResourceProcessData& Src, const FString& TextureName) const
	{
		for (int i = 0; i < MaxTextureShareItemTexturesCount; i++)
		{
			// Case insensitive search
			if (!FPlatformString::Stricmp(Src.Textures[i].Name, *TextureName))
			{
				return i;
			}
		}
		return -1;
	}

	int FTextureShareItemBase::FindRemoteTextureIndex(const FSharedResourceTexture& LocalTextureData) const
	{
		if (IsConnectionValid())
		{
			if (LocalTextureData.IsUsed())
			{
				const FSharedResourceProcessData& Data = GetRemoteProcessData();
				int RemoteTextureIndex = FindTextureIndex(Data, LocalTextureData.Name);
				if (RemoteTextureIndex >= 0 && Data.Textures[RemoteTextureIndex].IsUsed())
				{
					return RemoteTextureIndex;
				}
			}
		}
		return -1;
	}

	FTextureShareItemBase::FTextureShareItemBase(const FString& ResourceName, FTextureShareSyncPolicy SyncMode, ETextureShareProcess InProcessType)
	{
		// Initialize shared memory for local process
		SharedResource = new FSharedResource(InProcessType, ResourceName);
		if (SharedResource)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			if (!SharedResource->Initialize(SyncSettings.TimeOut.InitializeSync * 1000))
			{
				Release();
				return;
			}

			// Reset local process data
			FSharedResourceProcessData& LocaData = GetLocalProcessData();
			LocaData.SyncMode = SyncMode;
			for (int i = 0; i < MaxTextureShareItemTexturesCount; i++)
			{
				LocaData.Textures[i].Index = i;
			}
		}
	};

	bool FTextureShareItemBase::IsClient() const
	{
		return SharedResource && SharedResource->IsClient();
	}

	void FTextureShareItemBase::Release()
	{
		// Sync vs render thread frame lock/unlock
		FScopeLock FrameLock(&FrameLockGuard);

		// Finalize session
		EndSession();

		// Release from shared memory slot
		if (SharedResource)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			SharedResource->Release(SyncSettings.TimeOut.ReleaseSync*1000);
			delete SharedResource;
			SharedResource = nullptr;
		}
	}

	const FString& FTextureShareItemBase::GetName() const
	{
		static FString InvalidShareName(TEXT("InvalidShareName"));
		return SharedResource ? SharedResource->GetName() : InvalidShareName;
	}

	bool FTextureShareItemBase::CheckTextureInfo(const FString& TextureName, const FIntPoint& InSize, ETextureShareFormat InFormat, uint32 InFormatValue) const
	{
		if (TextureName.IsEmpty())
		{
			UE_LOG(LogTextureShareCore, Error, TEXT("Required no-empty texture name for share '%s'"), *GetName());
			return false;
		}

		if (InSize.X < 0 || InSize.Y < 0)
		{
			UE_LOG(LogTextureShareCore, Error, TEXT("Invalid texture '%s' size '%s' for share '%s'"), *TextureName, *InSize.ToString(), *GetName());
			return false;
		}
		return true;
	}

	bool FTextureShareItemBase::RegisterTexture(const FString& TextureName, const FIntPoint& InSize, ETextureShareFormat InFormat, uint32 InFormatValue, ETextureShareSurfaceOp OperationType)
	{
		if (!CheckTextureInfo(TextureName, InSize, InFormat, InFormatValue))
		{
			return false;
		}

		FSharedResourceProcessData& Data = GetLocalProcessData();
		int LocalTextureIndex = FindTextureIndex(Data, TextureName);
		if (LocalTextureIndex < 0)
		{
			//Texture Not defined, add new
			LocalTextureIndex = FindTextureIndex(Data, ESharedResourceTextureState::Undefined);
		}

		if(LocalTextureIndex>=0)
		{
			FSharedResourceTexture& Dst = Data.Textures[LocalTextureIndex];
			if (SharedResource && SharedResource->InitializeTextureMutex(Dst.Index, TextureName))
			{
				FPlatformString::Strcpy(Dst.Name, MaxTextureShareItemNameLength, *TextureName);
				Dst.State = ESharedResourceTextureState::Ready;
				Dst.OperationType = OperationType;

				switch (InFormat)
				{
				case ETextureShareFormat::Format_DXGI:
					Dst.TextureDesc.PlatformFormat = InFormatValue;
					break;
#if TEXTURESHARECORE_RHI
				case ETextureShareFormat::Format_EPixel:
					// update from EPixelFormat to target device
					if (InFormatValue != EPixelFormat::PF_Unknown)
					{
						InitializeRHISharedTextureFormat(GetDeviceType(), (EPixelFormat)InFormatValue, Dst.TextureDesc);
					}
					break;
#endif
				default:
					break;
				}

				Dst.TextureDesc.Width  = InSize.X;
				Dst.TextureDesc.Height = InSize.Y;

				return WriteLocalProcessData();
			}
			//@todo: error semaphore create failed
		}

		UE_LOG(LogTextureShareCore, Error, TEXT("Failed register texture '%s' size '%s' for share '%s'"), *TextureName, *InSize.ToString(), *GetName());
		return false;
	}

	bool FTextureShareItemBase::SetTextureGPUIndex(const FString& TextureName, uint32 GPUIndex)
	{
		FSharedResourceProcessData& Data = GetLocalProcessData();
		int LocalTextureIndex = FindTextureIndex(Data, TextureName);
		if (LocalTextureIndex >= 0)
		{
			FSharedResourceTexture& Dst = Data.Textures[LocalTextureIndex];
			Dst.SharingData.MGPUIndex = GPUIndex;
			return WriteLocalProcessData();
		}

		UE_LOG(LogTextureShareCore, Error, TEXT("Failed SetTextureGPUIndex('%s',%d): register texture first for share '%s'"), *TextureName, GPUIndex, *GetName());
		return false;
	}

	bool FTextureShareItemBase::SetDefaultGPUIndex(uint32 GPUIndex)
	{
		FSharedResourceProcessData& Data = GetLocalProcessData();
		Data.DefaultMGPUIndex = GPUIndex;
		return WriteLocalProcessData();
	}

	void FTextureShareItemBase::SetSyncWaitTime(float InSyncWaitTime)
	{
		SyncWaitTime = InSyncWaitTime;
	}

	/*ESharedResourceTextureState FTextureShareItemBase::GetLocalTextureState(const FString& TextureName)
	{
		FSharedResourceProcessData& Data = GetLocalProcessData();
		int TextureIndex = FindTextureIndex(Data, TextureName);
		return (TextureIndex < 0) ? (ESharedResourceTextureState::INVALID) : (Data.Textures[TextureIndex].State);
	}*/

	bool FTextureShareItemBase::FindTextureData(const FString& TextureName, bool bIsLocal, FSharedResourceTexture& OutTextureData) const
	{
		const FSharedResourceProcessData& Data = bIsLocal ? GetLocalProcessData():GetRemoteProcessData();
		int TextureIndex = FindTextureIndex(Data, TextureName);
		if (TextureIndex >=0)
		{
			OutTextureData = Data.Textures[TextureIndex];
			return true;
		}
		return false;
	}

	bool FTextureShareItemBase::SetLocalAdditionalData(const FTextureShareAdditionalData& InAdditionalData)
	{
		FSharedResourceProcessData& LocalData = GetLocalProcessData();
		LocalData.AdditionalData = InAdditionalData;
		return WriteLocalProcessData();
	}

	bool FTextureShareItemBase::GetRemoteAdditionalData(FTextureShareAdditionalData& OutAdditionalData)
	{
		if (ReadRemoteProcessData())
		{
			FSharedResourceProcessData& RemoteData = GetRemoteProcessData();
			OutAdditionalData = RemoteData.AdditionalData;
			return true;
		}
		return false;
	}

	bool FTextureShareItemBase::SetCustomProjectionData(const FTextureShareCustomProjectionData& InCustomProjectionData)
	{
		// NOT IMPLEMETED
		return false;
	}


	bool FTextureShareItemBase::BeginTextureOp(FSharedResourceTexture& LocalTextureData)
	{
		if (IsClient())
		{
			switch (LocalTextureData.OperationType)
			{
			case ETextureShareSurfaceOp::Read:
				// All Read ops must be before write on client process
				return !ResourceData.ClientData.IsFrameOpLockedNow();

			case ETextureShareSurfaceOp::Write:
				// After Write op on the client process, read op must be disabled inside current frame lock
				ResourceData.ClientData.FrameState = ETextureShareFrameState::LockedOp;
				return WriteLocalProcessData();
			}

			UE_LOG(LogTextureShareCore, Error, TEXT("Client: Invalid ops order for share '%s'. The Read is not allowed after Write op"), *GetName());
			return false;
		}
		else
		{
			switch (LocalTextureData.OperationType)
			{
			case ETextureShareSurfaceOp::Write:
				// All Write ops must be before read on server process
				return !ResourceData.ServerData.IsFrameOpLockedNow();

			case ETextureShareSurfaceOp::Read:
				// After Read op on the server process, write op must be disabled inside current frame lock
				ResourceData.ServerData.FrameState = ETextureShareFrameState::LockedOp;
				return WriteLocalProcessData();
			}

			UE_LOG(LogTextureShareCore, Error, TEXT("Server: Invalid ops order for share '%s'. The Write is not allowed after Read op"), *GetName());
			return false;
		}
	}

	bool FTextureShareItemBase::TryTextureSync(FSharedResourceTexture& LocalTextureData, int& RemoteTextureIndex)
	{
		if (!LocalTextureData.IsUsed())
		{
			return false;
		}

		// Update remote process data
		ReadRemoteProcessData();
		RemoteTextureIndex = FindRemoteTextureIndex(LocalTextureData);

		const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();

		switch (GetTextureSyncMode())
		{
		/** [Required] Waiting until remote process register texture (Required texture pairing) */
		case ETextureShareSyncSurface::SyncPairingRead:
		{
			FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.TexturePairingSync, ESyncProcessFailAction::ConnectionLost, SyncWaitTime);
			while (RemoteTextureIndex < 0)
			{
				if (!SyncProcess.Tick())
				{
					return false;
				}

				// Wait for texture pairing
				RemoteTextureIndex = FindRemoteTextureIndex(LocalTextureData);
			}
			// don't break, continue
		}

		/** [SyncReadWrite] - Waiting until remote process change texture (readOP is wait for writeOP from remote process completed) */
		case ETextureShareSyncSurface::SyncRead:
		{
			if (RemoteTextureIndex >= 0)
			{
				// texture paired, check [SyncReadWrite] with remote process
				switch (LocalTextureData.OperationType)
				{
				/** Read operation wait for remote process write */
				case ETextureShareSurfaceOp::Read:
				{
#if TEXTURESHARECORE_RHI
					if(!IsClient() && !LocalTextureData.SharingData.IsValid())
					{
						// Server must create shared resource for client, before read op
						bool bIsTextureChanged;
						if(!LockServerRHITexture(LocalTextureData, bIsTextureChanged, RemoteTextureIndex))
						{
							return false;
						}
					}
#endif
					FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.TextureSync, ESyncProcessFailAction::ConnectionLost, SyncWaitTime);
					FSharedResourceProcessData& LocalData = GetLocalProcessData();
					FSharedResourceProcessData& RemoteData = GetRemoteProcessData();

					uint64 LocalSyncFrame = LocalData.GetTextureAccessSyncFrame();
					while (RemoteData.Textures[RemoteTextureIndex].AccessSyncFrame != LocalSyncFrame)
					{
						// Failed R/W on any side
						if (!RemoteData.IsFrameLockedNow())
						{
							if (IsClient())
							{
								// Server finished current frame, ignore this texture
								return false;
							}
							else
							{
								if (RemoteData.SyncFrame != LocalData.SyncFrame)
								{
									// Client inside frame, and wait for server. Ignore this frame
									return false;
								}
							}
						}

						if (!SyncProcess.Tick())
						{
							return false;
						}
					}
					break;
				}

				/** Write operation not require sync*/
				default:
					break;
				}
			}
			break;
		}

		/** Ignore or skip texture update */
		default:
			break;
		}

		if (RemoteTextureIndex < 0)
		{
			// this texture not defined on remote process
			return false;
		}

		// Wait for shared texture handle, created by server
		if(IsClient())
		{
			switch (GetFrameSyncMode())
			{
			case ETextureShareSyncFrame::FrameSync:
			{
				// Wait for remote handle initialized
				FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.TextureResourceSync, ESyncProcessFailAction::ConnectionLost, SyncWaitTime);
				while (!GetRemoteProcessData().Textures[RemoteTextureIndex].SharingData.IsValid())
				{
					if (!SyncProcess.Tick())
					{
						return false;
					}
				}
				break;
			}
			default:
				break;
			}

			return GetRemoteProcessData().Textures[RemoteTextureIndex].SharingData.IsValid();
		}

		// On the server side just use local texture, and share to client
		return true;
	}

	bool FTextureShareItemBase::TryBeginFrame()
	{
		// Update remote process data
		ReadRemoteProcessData();

		if (CheckRemoteConnectionLost())
		{
			return false;
		}

		const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();

		if (!bRemoteConnectionValid)
		{
			// check _ConnectionSync_ (TextureShare objects begin sessions)
			switch (GetConnectionSyncMode())
			{
			case ETextureShareSyncConnect::SyncSession:
			{
				// Wait for other process
				FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.ConnectionSync, ESyncProcessFailAction::ConnectionLost, SyncWaitTime);
				while (!IsConnectionValid())
				{
					if (!SyncProcess.Tick())
					{
						return false;
					}
				}
				// processes connected now
				break;
			}
			default:
				if (!IsConnectionValid())
				{
					// Reset connection for local
					RemoteConnectionLost();
					return false;
				}
				// processes connected now
				break;
			}

			// Remote side found, begin connection
			BeginRemoteConnection();
		}

		// Synchronize _SyncFrame_ (Frame numbers is equal)
		switch (GetFrameSyncMode())
		{
		case ETextureShareSyncFrame::FrameSync:
		{
			FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.FrameSync, ESyncProcessFailAction::ConnectionLost, SyncWaitTime);
			while (!ResourceData.IsSyncFrameValid(IsClient()))
			{
				if (!SyncProcess.Tick())
				{
					return false;
				}
			}
			return true;
		}
		default:
			// Ignore frame sync
			break;
		}

		// just skip frames
		return ResourceData.IsSyncFrameValid(IsClient());
	}

	bool FTextureShareItemBase::LockTextureMutex(FSharedResourceTexture& LocalTextureData)
	{
		const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();

		if (IsFrameValid() && SharedResource)
		{
			if (!SharedResource->LockTextureMutex(LocalTextureData.Index, SyncSettings.TimeOut.TextureLockMutex * 1000))
			{
				// Timeout. Mutex deadlock? re-create mutex
				SharedResource->ReleaseTextureMutex(LocalTextureData.Index, true);
				SharedResource->InitializeTextureMutex(LocalTextureData.Index, FString(LocalTextureData.Name));
				return false;
			}
			return true;
		}
		return false;
	}

	void FTextureShareItemBase::UnlockTextureMutex(FSharedResourceTexture& LocalTextureData, bool bIsTextureChanged)
	{
		if (bIsTextureChanged)
		{
			// Update texture access frame (sync read purpose)
			LocalTextureData.AccessSyncFrame = GetLocalProcessData().GetTextureAccessSyncFrame();
			WriteLocalProcessData();
		}
		SharedResource->UnlockTextureMutex(LocalTextureData.Index);
	}

#if TEXTURESHARECORE_RHI
	bool FTextureShareItemBase::LockRHITexture_RenderThread(const FString& TextureName, FTexture2DRHIRef& OutRHITexture)
	{
		bool bIsTextureChanged;
		if (IsFrameValid())
		{
			FSharedResourceTexture TextureData;
			if (FindTextureData(TextureName, true, TextureData))
			{
				FSharedResourceTexture& LocalTextureData = GetLocalProcessData().Textures[TextureData.Index];
				if (BeginTextureOp(LocalTextureData))
				{
					int RemoteTextureIndex;
					if (TryTextureSync(LocalTextureData,  RemoteTextureIndex) && LockTextureMutex(LocalTextureData))
					{
						if (!IsClient())
						{
							if (LockServerRHITexture(LocalTextureData, bIsTextureChanged, RemoteTextureIndex))
							{
								OutRHITexture = GetSharedRHITexture(LocalTextureData)->GetSharedResource();
							}
						}
						else
						{
							if (LockClientRHITexture(LocalTextureData, bIsTextureChanged))
							{
								OutRHITexture = GetSharedRHITexture(LocalTextureData)->GetOpenedResource();
							}
						}
						// Publish local data
						WriteLocalProcessData();
					}
				}

				if (OutRHITexture.IsValid() && OutRHITexture->IsValid())
				{
					return true;
				}

				// Remove lock scope
				UnlockTextureMutex(LocalTextureData, false);
			}
		}
		return false;
	}

	bool FTextureShareItemBase::IsFormatResampleRequired(const FRHITexture* SharedTexture, const FRHITexture* RHITexture)
	{
		return FTextureShareItemRHI::IsFormatResampleRequired(SharedTexture->GetFormat(), RHITexture->GetFormat());
	}

	bool FTextureShareItemBase::LockServerRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged, int RemoteTextureIndex)
	{
		// Server, only for UE4. implement throught RHI:
		if (!IsFrameValid() || IsClient())
		{
			return false;
		}

		bIsTextureChanged = false;
		FSharedRHITexture* SharedRHITexture = GetSharedRHITexture(LocalTextureData);

		// Get fresh shared texture info
		FTextureShareSurfaceDesc SharedTextureDesc;
		if (LocalTextureData.IsUsed() && !GetResampledTextureDesc(IsClient(), LocalTextureData.Name, SharedTextureDesc))
		{
			// Now this texture disconnected
			SharedTextureDesc = FTextureShareSurfaceDesc();
		}

		// Update shared RHI texture
		if (SharedRHITexture->Update_RenderThread(*this, SharedTextureDesc, LocalTextureData))
		{
			bIsTextureChanged = true;

			// Save new shared handle & guid
			LocalTextureData.SharingData.SharedHandle = SharedRHITexture->GetSharedHandle();
			LocalTextureData.SharingData.SharedHandleGuid = SharedRHITexture->GetSharedHandleGuid();

			if (LocalTextureData.SharingData.IsValid())
			{
				LocalTextureData.State = ESharedResourceTextureState::Enabled;

				// Update Texture format info
				LocalTextureData.TextureDesc.PixelFormat = SharedRHITexture->GetSharedResource()->GetFormat();
				return true;
			}
			// Failed, disable this texture [1]
		}
		else
		{
			// Use exist shared texture
			// Close NT handle, if client already connected to shared handle
			if (LocalTextureData.IsSharedHandleConnected(GetRemoteProcessData().Textures[RemoteTextureIndex]))
			{
				SharedRHITexture->ReleaseHandle();
			}
			return true;
		}

		// [1] Failed to share resource, disable this texture slot
		LocalTextureData.State = ESharedResourceTextureState::Disabled;
		return false;
	}

	bool FTextureShareItemBase::TransferTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName)
	{
#if WITH_MGPU
		// Server, only for UE4. implement throught RHI and MGPU
		if (!IsClient())
		{
			FSharedResourceTexture ServerTextureData, ClientTextureData;
			if (FindTextureData(TextureName, true, ServerTextureData) && FindTextureData(TextureName, false, ClientTextureData))
			{
				int ServerGPUIndex = ResourceData.ServerData.GetTextureMGPUIndex(ServerTextureData.Index);
				int ClientGPUIndex = ResourceData.ClientData.GetTextureMGPUIndex(ClientTextureData.Index);

				if(ServerGPUIndex != ClientGPUIndex)
				{
					FSharedRHITexture* SharedRHITexture = GetSharedRHITexture(ServerTextureData);
					FTexture2DRHIRef& RHITexture = SharedRHITexture->GetSharedResource();
					
					if (RHITexture.IsValid() && RHITexture->IsValid())
					{
						FIntRect TextureRect(FIntPoint(0, 0), RHITexture->GetSizeXY());

						switch (ServerTextureData.OperationType)
						{
						case ETextureShareSurfaceOp::Write:
						{
							FTransferResourceParams Param(RHITexture, TextureRect, ServerGPUIndex, ClientGPUIndex, true, true);
							RHICmdList.TransferResources(TArrayView<const FTransferResourceParams>(&Param, 1));
							break;
						}
						case ETextureShareSurfaceOp::Read:
						{
							FTransferResourceParams Param(RHITexture, TextureRect, ClientGPUIndex, ServerGPUIndex, true, true);
							RHICmdList.TransferResources(TArrayView<const FTransferResourceParams>(&Param, 1));
							break;
						}
						}
						return true;
					}
				}
			}
		}
#endif
		return false;
	}
#endif

	bool FTextureShareItemBase::BeginFrame_RenderThread()
	{
		WriteLocalProcessData();

		FSharedResourceProcessData& LocalData = GetLocalProcessData();
		if (!LocalData.IsFrameLockedNow())
		{
			// multithread purpose. Game vs Render threads
			// Wait until EndFrame before remove this share object
			FrameLockGuard.Lock();

			if (bIsSessionStarted)
			{
				// Reset connection
				if (!IsConnectionValid())
				{
					RemoteConnectionLost();
				}

				// begin frame
				if (TryBeginFrame())
				{
					// Update local frame index
					LocalData.SyncFrame++;

					// Update frame lock state
					LocalData.FrameState = ETextureShareFrameState::Locked;

					// publish local data
					return WriteLocalProcessData();
				}
			}

			// Lock is failed. remove mutex
			FrameLockGuard.Unlock();
		}
		else
		{
			UE_LOG(LogTextureShareCore, Error, TEXT("Double time frame lock for share '%s'"), *GetName());
		}
		return false;
	}

	bool FTextureShareItemBase::EndFrame_RenderThread()
	{
		FSharedResourceProcessData& LocalData = GetLocalProcessData();
		if (LocalData.IsFrameLockedNow())
		{
			// Ulock all locked textures mutex
			if (SharedResource)
			{
				SharedResource->UnlockTexturesMutex();
			}

			// Update frame lock state
			LocalData.FrameState = ETextureShareFrameState::None;

			// publish local data
			WriteLocalProcessData();

			FrameLockGuard.Unlock();
		}
		return bIsSessionStarted;
	}

	bool FTextureShareItemBase::BeginSession()
	{
		// Update remote process data
		ReadRemoteProcessData();

		if (!bIsSessionStarted)
		{
			// Begin session
			FSharedResourceProcessData& LocalData = GetLocalProcessData();
			LocalData.DeviceType = GetDeviceType();
#if TEXTURESHARECORE_RHI
			LocalData.Source = ETextureShareSource::Unreal;
#else
			LocalData.Source = ETextureShareSource::SDK;
#endif
			bIsSessionStarted = true;

			// publish local data
			return WriteLocalProcessData();
		}
		return false;
	}

	void FTextureShareItemBase::EndSession()
	{
		if (bIsSessionStarted)
		{
			// Stop current connection
			EndRemoteConnection();
		}

		//Release texture mutex
		if(SharedResource)
		{
			// Force delete ISO when server die
			SharedResource->ReleaseTexturesMutex(!IsClient());
		}

		// Clear local process data
		ResourceData = FSharedResourceSessionData();
		WriteLocalProcessData();

		// Stop session, and disable access to local IPC data
		bIsSessionStarted = false;
	}

	bool FTextureShareItemBase::TryFrameSyncLost()
	{
		if (ResourceData.IsFrameSyncLost())
		{
			GetLocalProcessData().ResetSyncFrame();
			return WriteLocalProcessData();
		}
		return false;
	}

	void FTextureShareItemBase::BeginRemoteConnection()
	{
		bRemoteConnectionValid = true;
	}

	void FTextureShareItemBase::EndRemoteConnection()
	{
		if (bRemoteConnectionValid)
		{
			// Break current frame
			EndFrame_RenderThread();

			// Release device shared resources
			DeviceReleaseTextures();

			// Reset frame sync
			GetLocalProcessData().ResetSyncFrame();

			WriteLocalProcessData();

			bRemoteConnectionValid = false;
		}
	}

	void FTextureShareItemBase::RemoteConnectionLost()
	{
		//@todo: handle error

		// Stop current connection
		EndRemoteConnection();
	}

	bool FTextureShareItemBase::CheckRemoteConnectionLost()
	{
		if (bRemoteConnectionValid && !IsConnectionValid())
		{
			RemoteConnectionLost();
			return true;
		}
		return false;
	}

	// Helpers:
	const FTextureShareSyncPolicySettings& FTextureShareItemBase::GetSyncSettings() const
	{
		return GetSyncPolicySettings(IsClient() ? ETextureShareProcess::Client : ETextureShareProcess::Server);
	}

	ETextureShareSyncConnect FTextureShareItemBase::GetConnectionSyncMode() const
	{
		ETextureShareSyncConnect Result = GetLocalProcessData().SyncMode.ConnectionSync;

		if (Result == ETextureShareSyncConnect::Default)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			Result = SyncSettings.DefaultSyncPolicy.ConnectionSync;
		}

		return Result;
	}

	ETextureShareSyncFrame FTextureShareItemBase::GetFrameSyncMode() const
	{
		ETextureShareSyncFrame Result = GetLocalProcessData().SyncMode.FrameSync;

		if (Result == ETextureShareSyncFrame::Default)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			Result = SyncSettings.DefaultSyncPolicy.FrameSync;
		}

		return Result;
	}

	ETextureShareSyncSurface FTextureShareItemBase::GetTextureSyncMode() const
	{
		ETextureShareSyncSurface Result = GetLocalProcessData().SyncMode.TextureSync;
		if (Result == ETextureShareSyncSurface::Default)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			Result = SyncSettings.DefaultSyncPolicy.TextureSync;
		}
		return Result;
	}
	
	bool FTextureShareItemBase::CompleteTextureDesc(FTextureShareSurfaceDesc& InOutTextureDesc, const FTextureShareSurfaceDesc& InFillerTextureDesc) const
	{
		if (!InOutTextureDesc.IsSizeValid())
		{
			if (!InFillerTextureDesc.IsSizeValid())
			{
				// Texture size must be defined at least from one side
				return false;
			}
			InOutTextureDesc.SetSize(InFillerTextureDesc);
		}

		if (!InOutTextureDesc.IsFormatValid())
		{
			if (!InFillerTextureDesc.IsFormatValid())
			{
				// Texture format must be defined at least from one side
				return false;
			}
			InOutTextureDesc.SetFormat(InFillerTextureDesc);
		}
		return true;
	}

	bool FTextureShareItemBase::IsTextureUsed(bool bIsLocal, const FString& TextureName) const
	{
		FSharedResourceTexture TextureData;
		return FindTextureData(TextureName, bIsLocal, TextureData) && TextureData.IsUsed();
	}

	bool FTextureShareItemBase::GetResampledTextureDesc(bool bToLocal, const FString& TextureName, FTextureShareSurfaceDesc& OutSharedTextureDesc) const
	{
		FSharedResourceTexture LocalTextureData, RemoteTextureData;
		if (FindTextureData(TextureName, bToLocal, LocalTextureData)
			&& LocalTextureData.IsUsed()
			&& FindTextureData(TextureName, !bToLocal, RemoteTextureData)
			&& RemoteTextureData.IsUsed()
			)
		{
			OutSharedTextureDesc = LocalTextureData.TextureDesc;
			return CompleteTextureDesc(OutSharedTextureDesc, RemoteTextureData.TextureDesc);
		}
		return false;
	}
};
