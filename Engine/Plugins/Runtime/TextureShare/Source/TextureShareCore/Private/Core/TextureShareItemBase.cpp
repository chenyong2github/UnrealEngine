// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareItemBase.h"

#include "DirectX/TextureShareItemD3D11.h"
#include "DirectX/TextureShareItemD3D12.h"

#include "TextureShareCoreLog.h"
#include "Misc/ScopeLock.h"

namespace TextureShareItem
{
	FTextureShareSyncPolicySettings FTextureShareItemBase::GSyncPolicySettings[] =
	{
		FTextureShareSyncPolicySettings(ETextureShareProcess::Server),
		FTextureShareSyncPolicySettings(ETextureShareProcess::Client)
	};

	FCriticalSection FTextureShareItemBase::GSyncPolicySettingsDataGuard;

	const FTextureShareSyncPolicySettings& FTextureShareItemBase::GetSyncPolicySettings(ETextureShareProcess Process)
	{
		FScopeLock DataLock(&GSyncPolicySettingsDataGuard);

		return GSyncPolicySettings[(uint8)Process];
	}

	void FTextureShareItemBase::SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings)
	{
		FScopeLock DataLock(&GSyncPolicySettingsDataGuard);

		GSyncPolicySettings[(uint8)Process] = InSyncPolicySettings;
	}

	enum class ESyncProcessFailAction : uint8
	{
		None = 0,
		ConnectionLost
	};

	class FSyncProcess
	{
	public:
		FSyncProcess(FTextureShareItemBase& ShareItem, float InTimeOut, ESyncProcessFailAction InFailAction = ESyncProcessFailAction::None)
			: ShareItem(ShareItem)
			, FailAction(InFailAction)
			, Time0(FPlatformTime::Seconds())
			, TimeOut(InTimeOut)
		{}

		bool Tick()
		{
			if (ShareItem.CheckRemoteConnectionLost())
			{
				return false;
			}

			if (ShareItem.TryFrameSyncLost())
			{
				// remote process lost. break sync
				return false;
			}

			// If timeout is defined, checking elasped waiting time
			const double TotalWaitTime = FPlatformTime::Seconds() - Time0;
			const bool bIsTimeOut = (TimeOut>0) && (TotalWaitTime > TimeOut);
			if (!bIsTimeOut)
			{
				// default max timeout step in milliseconds (no timeout)
				const uint32 MaxWaitTime = 200;

				// TimeOut duration in milliseconds
				uint32 CustomWaitTime = MaxWaitTime;
				if (TimeOut > 0)
				{
					const double LostSeconds = TimeOut - TotalWaitTime;
					if (LostSeconds > 0)
					{
						CustomWaitTime = LostSeconds * 1000; // sec->msec
					}
				}

				// Use minimal wait time, to detect connection loss
				const uint32 WaitTime = FMath::Min(MaxWaitTime, CustomWaitTime);

				// Wait until the remote process data has changed or timed out
				ShareItem.WaitForRemoteProcessDataChanged(WaitTime);

				// Update data from remote process
				ShareItem.ReadRemoteProcessData();
				return true;
			}

			switch (FailAction)
			{
			case ESyncProcessFailAction::ConnectionLost:
				ShareItem.RemoteConnectionLost();
				break;
			default:
				break;
			}
			
			return false;
		}

	private:
		FTextureShareItemBase& ShareItem;
		ESyncProcessFailAction FailAction;
		double Time0;
		double TimeOut;
		float  WaitSeconds;
	};

	bool FTextureShareItemBase::WaitForRemoteProcessDataChanged(uint32 WaitTime, const bool bIgnoreThreadIdleStats)
	{
		return SharedResource && SharedResource->WaitReadDataEvent(WaitTime, bIgnoreThreadIdleStats);
	}

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

	int32 FTextureShareItemBase::FindTextureIndex(const FSharedResourceProcessData& Src, ESharedResourceTextureState TextureState, bool bNotEqual) const
	{
		for (int32 TextureIndex = 0; TextureIndex < MaxTextureShareItemTexturesCount; TextureIndex++)
		{
			if ((!bNotEqual && Src.Textures[TextureIndex].State == TextureState) || (bNotEqual && Src.Textures[TextureIndex].State != TextureState))
			{
				return TextureIndex;
			}
		}
		return -1;
	}

	int32 FTextureShareItemBase::FindTextureIndex(const FSharedResourceProcessData& Src, const FString& TextureName) const
	{
		for (int32 TextureIndex = 0; TextureIndex < MaxTextureShareItemTexturesCount; TextureIndex++)
		{
			// Case insensitive search
			if (!FPlatformString::Stricmp(Src.Textures[TextureIndex].Name, *TextureName))
			{
				return TextureIndex;
			}
		}
		return -1;
	}

	int32 FTextureShareItemBase::FindRemoteTextureIndex(const FSharedResourceTexture& LocalTextureData) const
	{
		if (IsConnectionValid())
		{
			if (LocalTextureData.IsUsed())
			{
				const FSharedResourceProcessData& Data = GetRemoteProcessData();
				int32 RemoteTextureIndex = FindTextureIndex(Data, LocalTextureData.Name);
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
			for (int32 TextureIndex = 0; TextureIndex < MaxTextureShareItemTexturesCount; TextureIndex++)
			{
				LocaData.Textures[TextureIndex].Index = TextureIndex;
			}
		}
	};

	bool FTextureShareItemBase::IsClient() const
	{
		FScopeLock DataLock(&DataLockGuard);
		return SharedResource && SharedResource->IsClient();
	}

	bool FTextureShareItemBase::IsValid() const
	{
		FScopeLock DataLock(&DataLockGuard);
		return SharedResource != nullptr;
	}

	bool FTextureShareItemBase::IsSessionValid() const
	{
		FScopeLock DataLock(&DataLockGuard);
		return IsValid() && bIsSessionStarted;
	}

	bool FTextureShareItemBase::IsLocalFrameLocked() const
	{
		FScopeLock DataLock(&DataLockGuard);
		return GetLocalProcessData().IsFrameLockedNow();
	}

	void FTextureShareItemBase::Release()
	{
		FScopeLock DataLock(&DataLockGuard);

		// Sync vs render thread frame lock/unlock
		FScopeLock FrameLock(&FrameLockGuard);

		// Finalize session
		EndSession();

		// Release from shared memory slot
		if (SharedResource)
		{
			const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
			SharedResource->Release(SyncSettings.TimeOut.ReleaseSync * 1000);
			delete SharedResource;
			SharedResource = nullptr;
		}
	}

	const FString& FTextureShareItemBase::GetName() const
	{
		FScopeLock DataLock(&DataLockGuard);

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
		FScopeLock DataLock(&DataLockGuard);

		if (!CheckTextureInfo(TextureName, InSize, InFormat, InFormatValue))
		{
			return false;
		}

		FSharedResourceProcessData& Data = GetLocalProcessData();
		int32 LocalTextureIndex = FindTextureIndex(Data, TextureName);
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
		FScopeLock DataLock(&DataLockGuard);

		FSharedResourceProcessData& Data = GetLocalProcessData();
		int32 LocalTextureIndex = FindTextureIndex(Data, TextureName);
		if (LocalTextureIndex >= 0)
		{
			FSharedResourceTexture& Dst = Data.Textures[LocalTextureIndex];
			Dst.SetConnectionMGPUIndex((int32)GPUIndex);

			return WriteLocalProcessData();
		}

		UE_LOG(LogTextureShareCore, Error, TEXT("Failed SetTextureGPUIndex('%s',%d): register texture first for share '%s'"), *TextureName, GPUIndex, *GetName());
		return false;
	}

	bool FTextureShareItemBase::SetDefaultGPUIndex(uint32 GPUIndex)
	{
		FScopeLock DataLock(&DataLockGuard);

		FSharedResourceProcessData& Data = GetLocalProcessData();
		Data.DefaultMGPUIndex = GPUIndex;
		return WriteLocalProcessData();
	}

	void FTextureShareItemBase::SetSyncWaitTime(float InSyncWaitTime)
	{
		// SyncWaitTime now is deprecated
	}

	bool FTextureShareItemBase::IsLocalTextureUsed(const FString& TextureName) const
	{
		FScopeLock DataLock(&DataLockGuard);
		return IsTextureUsed(true, TextureName);
	}

	bool FTextureShareItemBase::IsRemoteTextureUsed(const FString& TextureName) const
	{
		FScopeLock DataLock(&DataLockGuard);
		return IsTextureUsed(false, TextureName);
	}

	bool FTextureShareItemBase::GetRemoteTextureDesc(const FString& TextureName, FTextureShareSurfaceDesc& OutSharedTextureDesc) const
	{
		FScopeLock DataLock(&DataLockGuard);
		return GetResampledTextureDesc(false, TextureName, OutSharedTextureDesc);
	}

	bool FTextureShareItemBase::FindTextureData(const FString& TextureName, bool bIsLocal, FSharedResourceTexture& OutTextureData) const
	{
		const FSharedResourceProcessData& Data = bIsLocal ? GetLocalProcessData():GetRemoteProcessData();
		int32 TextureIndex = FindTextureIndex(Data, TextureName);
		if (TextureIndex >=0)
		{
			OutTextureData = Data.Textures[TextureIndex];
			return true;
		}
		return false;
	}

	bool FTextureShareItemBase::SetLocalAdditionalData(const FTextureShareAdditionalData& InAdditionalData)
	{
		FScopeLock DataLock(&DataLockGuard);

		FSharedResourceProcessData& LocalData = GetLocalProcessData();
		LocalData.AdditionalData = InAdditionalData;
		LocalData.bIsValidCustomProjectionData = false;

		return WriteLocalProcessData();
	}

	bool FTextureShareItemBase::SetCustomProjectionData(const FTextureShareCustomProjectionData& InCustomProjectionData)
	{
		FScopeLock DataLock(&DataLockGuard);

		FSharedResourceProcessData& LocalData = GetLocalProcessData();
		LocalData.CustomProjectionData = InCustomProjectionData;
		LocalData.bIsValidCustomProjectionData = true;

		return WriteLocalProcessData();
	}

	bool FTextureShareItemBase::GetRemoteAdditionalData(FTextureShareAdditionalData& OutAdditionalData)
	{
		FScopeLock DataLock(&DataLockGuard);

		if (ReadRemoteProcessData())
		{
			FSharedResourceProcessData& RemoteData = GetRemoteProcessData();
			OutAdditionalData = RemoteData.AdditionalData;

			if (RemoteData.bIsValidCustomProjectionData)
			{
				OutAdditionalData.PrjMatrix    = RemoteData.CustomProjectionData.PrjMatrix;
				OutAdditionalData.ViewLocation = RemoteData.CustomProjectionData.ViewLocation;
				OutAdditionalData.ViewRotation = RemoteData.CustomProjectionData.ViewRotation;
			}

			return true;
		}

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

	bool FTextureShareItemBase::TryTextureSync(FSharedResourceTexture& LocalTextureData, int32& RemoteTextureIndex)
	{
		if (!LocalTextureData.IsUsed())
		{
			return false;
		}

		// Update remote process data
		ReadRemoteProcessData();

		const FTextureShareSyncPolicySettings& SyncSettings = GetSyncSettings();
		RemoteTextureIndex = FindRemoteTextureIndex(LocalTextureData);

		ETextureShareSyncSurface TextureSyncMode = GetTextureSyncMode();

		/** [Required] Waiting until remote process register texture (Required texture pairing) */
		if(TextureSyncMode == ETextureShareSyncSurface::SyncPairingRead)
		{
			FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.TexturePairingSync, ESyncProcessFailAction::ConnectionLost);
			while (RemoteTextureIndex < 0)
			{
				if (!SyncProcess.Tick())
				{
					return false;
				}

				// Wait for texture pairing
				RemoteTextureIndex = FindRemoteTextureIndex(LocalTextureData);
			}
		}

		if (RemoteTextureIndex < 0)
		{
			// this texture not defined on remote process
			return false;
		}

		switch (TextureSyncMode)
		{
		/** [SyncReadWrite] - Waiting until remote process change texture (readOP is wait for writeOP from remote process completed) */
		case ETextureShareSyncSurface::SyncRead:
		case ETextureShareSyncSurface::SyncPairingRead:
		{
			// texture paired, check [SyncReadWrite] with remote process
			switch (LocalTextureData.OperationType)
			{
			/** Read operation wait for remote process write */
			case ETextureShareSurfaceOp::Read:
			{
#if TEXTURESHARECORE_RHI
				if(!IsClient() && !LocalTextureData.IsValidConnection())
				{
					// Server must create shared resource for client, before read op
					bool bIsTextureChanged;
					if(!LockServerRHITexture(LocalTextureData, bIsTextureChanged, RemoteTextureIndex))
					{
						return false;
					}

					// Publish local data
					WriteLocalProcessData();
				}
#endif
				FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.TextureSync, ESyncProcessFailAction::ConnectionLost);
				const FSharedResourceProcessData& LocalData = GetLocalProcessData();
				const FSharedResourceProcessData& RemoteData = GetRemoteProcessData();

				const uint64 LocalSyncFrame = LocalData.GetTextureAccessSyncFrame();
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
		break;
		}

		/** Ignore or skip texture update */
		default:
			break;
		}

		// Wait for shared texture handle, created by server
		if(IsClient())
		{
			switch (GetFrameSyncMode())
			{
			case ETextureShareSyncFrame::FrameSync:
			{
				// Wait for remote handle initialized
				FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.TextureResourceSync, ESyncProcessFailAction::ConnectionLost);
				while (!GetRemoteProcessData().Textures[RemoteTextureIndex].IsValidConnection())
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

			return GetRemoteProcessData().Textures[RemoteTextureIndex].IsValidConnection();
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

		if (TryFrameSyncLost())
		{
			// remote process lost. break sync
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
				FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.ConnectionSync, ESyncProcessFailAction::ConnectionLost);
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
			FSyncProcess SyncProcess(*this, SyncSettings.TimeOut.FrameSync, ESyncProcessFailAction::ConnectionLost);
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
		FScopeLock DataLock(&DataLockGuard);

		bool bIsTextureChanged;
		if (IsFrameValid())
		{
			FSharedResourceTexture TextureData;
			if (FindTextureData(TextureName, true, TextureData))
			{
				FSharedResourceTexture& LocalTextureData = GetLocalProcessData().Textures[TextureData.Index];
				if (BeginTextureOp(LocalTextureData))
				{
					int32 RemoteTextureIndex;
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
		FScopeLock DataLock(&DataLockGuard);

		return FTextureShareItemRHI::IsFormatResampleRequired(SharedTexture->GetFormat(), RHITexture->GetFormat());
	}

	bool FTextureShareItemBase::LockServerRHITexture(FSharedResourceTexture& LocalTextureData, bool& bIsTextureChanged, int32 RemoteTextureIndex)
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
			if (LocalTextureData.CreateConnection(SharedRHITexture->GetSharedHandle(), SharedRHITexture->GetSharedHandleGuid()))
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
			FScopeLock DataLock(&DataLockGuard);

			FSharedResourceTexture ServerTextureData, ClientTextureData;
			if (FindTextureData(TextureName, true, ServerTextureData) && FindTextureData(TextureName, false, ClientTextureData))
			{
				int32 ServerGPUIndex = ResourceData.ServerData.GetTextureMGPUIndex(ServerTextureData.Index);
				int32 ClientGPUIndex = ResourceData.ClientData.GetTextureMGPUIndex(ClientTextureData.Index);

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
							FTransferTextureParams Param(RHITexture, TextureRect, ServerGPUIndex, ClientGPUIndex, true, true);
							RHICmdList.TransferTextures(TArrayView<const FTransferTextureParams>(&Param, 1));
							break;
						}
						case ETextureShareSurfaceOp::Read:
						{
							FTransferTextureParams Param(RHITexture, TextureRect, ClientGPUIndex, ServerGPUIndex, true, true);
							RHICmdList.TransferTextures(TArrayView<const FTransferTextureParams>(&Param, 1));
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
		FScopeLock DataLock(&DataLockGuard);

		// Sync paired data at this point
		WriteLocalProcessData();
		ReadRemoteProcessData();

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
		FScopeLock DataLock(&DataLockGuard);

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
		FScopeLock DataLock(&DataLockGuard);

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
		FScopeLock DataLock(&DataLockGuard);

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
