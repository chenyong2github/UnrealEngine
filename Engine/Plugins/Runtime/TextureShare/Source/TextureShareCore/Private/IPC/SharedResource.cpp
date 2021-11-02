// Copyright Epic Games, Inc. All Rights Reserved.

#include "SharedResource.h"
#include "SharedResourceMemory.h"

#include "Logging/LogScopedVerbosityOverride.h"

namespace TextureShareItem
{
	/**
	FSharedResourceMemory
	*/
	static const FString GlobalProcessMemoryShareName_GUID(TEXT("{0256554c-7637-46e3-9514-53ee0bec92b8}")); // use GUIDGen
	static const FString GlobalProcessMemoryMutexName(TEXT("TextureShareItem_MemoryMutex"));

	FPlatformProcess::FSemaphore *         FSharedResourceMemory::ProcessMutex = nullptr;
	FPlatformMemory::FSharedMemoryRegion * FSharedResourceMemory::ProcessMemory = nullptr;

	// Initialize one library memory share per process (multi session usage)
	bool FSharedResourceMemory::InitializeProcessMemory()
	{
		if (ProcessMutex || ProcessMemory)
		{
			//! multi initialize, ignore
			return false;
		}

		bool bCreateNew = false;
		uint32 AccessMode = FPlatformMemory::ESharedMemoryAccess::Read | FPlatformMemory::ESharedMemoryAccess::Write;
		SIZE_T Size = sizeof(FSharedResourcePublicData);

		// Try open existing share memory:
		{
			LOG_SCOPE_VERBOSITY_OVERRIDE(LogHAL, ELogVerbosity::Error);
			ProcessMemory = FPlatformMemory::MapNamedSharedMemoryRegion(*GlobalProcessMemoryShareName_GUID, false, AccessMode, Size);
		}

		if (!ProcessMemory)
		{
			// Try Open new:

			ProcessMemory = FPlatformMemory::MapNamedSharedMemoryRegion(*GlobalProcessMemoryShareName_GUID, true, AccessMode, Size);

			if (ProcessMemory)
			{
				bCreateNew = true;

				// initialize memory with zeroes
				check(ProcessMemory->GetAddress());
				FMemory::Memzero(ProcessMemory->GetAddress(), ProcessMemory->GetSize());

				// Release old mutex (leak fix??)

				{
					LOG_SCOPE_VERBOSITY_OVERRIDE(LogHAL, ELogVerbosity::Error);
					ProcessMutex = FPlatformProcess::NewInterprocessSynchObject(*GlobalProcessMemoryMutexName, false, IPC::MaxProcessNum);
				}

				if (ProcessMutex)
				{
					FPlatformProcess::DeleteInterprocessSynchObject(ProcessMutex);
					ProcessMutex = NULL;
				}
			}
			else
			{
				// FAILED!
				return false;
			}
		}

		// Try open exist memory mutex:
		ProcessMutex = bCreateNew? nullptr : FPlatformProcess::NewInterprocessSynchObject(*GlobalProcessMemoryMutexName, false, IPC::MaxProcessNum);
		if (!ProcessMutex)
		{
			// Try create new:
			ProcessMutex = FPlatformProcess::NewInterprocessSynchObject(*GlobalProcessMemoryMutexName, true, IPC::MaxProcessNum);

			if (!ProcessMutex)
			{
				FPlatformMemory::UnmapNamedSharedMemoryRegion(ProcessMemory);
				ProcessMemory = NULL;
				//! Failed create memory mutex
				return false;
			}
		}
		return true;
	}

	void FSharedResourceMemory::ReleaseProcessMemory()
	{
		//@todo: fixme
		// leave mutex, released on memory create op (fix leaks)
		if (ProcessMemory)
		{
			FPlatformMemory::UnmapNamedSharedMemoryRegion(ProcessMemory);
			ProcessMemory = NULL;
		}
		ProcessMutex = NULL;
	}

	/**
	 * FSharedResource
	 */
	bool FSharedResource::InitializeProcessMemory()
	{
		return FSharedResourceMemory::InitializeProcessMemory();
	}

	void FSharedResource::ReleaseProcessMemory()
	{
		FSharedResourceMemory::ReleaseProcessMemory();
	}

	FSharedResource::FSharedResource(ETextureShareProcess InProcessType, const FString& SharedResourceName)
		: Name(SharedResourceName)
		, SessionIndex(-1)
		, ProcessType(InProcessType)
		, ResourceMemory(*(new FSharedResourceMemory(InProcessType, SharedResourceName)))
	{
	}

	FSharedResource::~FSharedResource()
	{
		Release(DefaultMinMillisecondsToWait);
		delete &ResourceMemory;
	}

	bool FSharedResource::Initialize(uint32 MaxMillisecondsToWait)
	{
		SessionIndex = ResourceMemory.ConnectSession(ProcessType, GetName(), MaxMillisecondsToWait);
		if (SessionIndex<0)
		{
			//@todo: Handle error: no free space for new session
			return false;
		}

		// Search for conflicts:
		FSharedResourceProcessData LocalData;
		ReadLocalData(LocalData, DefaultMinMillisecondsToWait);

		// Resolve conflicts
		if (LocalData.DeviceType != ETextureShareDevice::Undefined)
		{
			// Current node in use. Reconnect?
			// Discard current data, and re-initialize
			FSharedResourceProcessData EmptyLocalData;
			if (!WriteLocalData(EmptyLocalData, DefaultMinMillisecondsToWait))
			{
				//@todo handle Error: no access
				Release(DefaultMinMillisecondsToWait);
				return false;
			}
		}
		return true;
	};

	void FSharedResource::Release(uint32 MaxMillisecondsToWait)
	{
		// Disconnect process from session
		ResourceMemory.DisconnectSession(ProcessType, SessionIndex, MaxMillisecondsToWait);
		SessionIndex = -1;

		// Clear local shared memory
		FSharedResourceProcessData EmptyLocalData;
		WriteLocalData(EmptyLocalData, DefaultMinMillisecondsToWait);
	}

	bool FSharedResource::InitializeTextureMutex(int32 TextureIndex, const FString& TextureName)
	{
		if (IsValid())
		{
			FString TextureMutexName = FString::Printf(TEXT("TextureMutex_%s_%s"), *GetName(), *TextureName);
			return ResourceMemory.InitializeTextureMutex(TextureIndex, *TextureMutexName);
		}
		return false;
	}

	void FSharedResource::ReleaseTextureMutex(int32 TextureIndex, bool bDeleteISO)
	{
		ResourceMemory.ReleaseTextureMutex(TextureIndex, bDeleteISO);
	}

	void FSharedResource::ReleaseTexturesMutex(bool bDeleteISO)
	{
		ResourceMemory.ReleaseTexturesMutex(bDeleteISO);
	}

	bool FSharedResource::WaitReadDataEvent(uint32 WaitTime, const bool bIgnoreThreadIdleStats)
	{
		return IsValid() && ResourceMemory.WaitReadDataEvent(WaitTime, bIgnoreThreadIdleStats);
	}
	
	bool FSharedResource::LockTextureMutex(int32 TextureIndex, uint32 MaxMillisecondsToWait)
	{
		return IsValid() && ResourceMemory.LockTextureMutex(TextureIndex, MaxMillisecondsToWait);
	}

	void FSharedResource::UnlockTextureMutex(int32 TextureIndex)
	{
		ResourceMemory.UnlockTextureMutex(TextureIndex);
	}

	// Unlock all locked mutex
	void FSharedResource::UnlockTexturesMutex()
	{
		ResourceMemory.UnlockTexturesMutex();
	}

	bool FSharedResource::WriteLocalData(const FSharedResourceProcessData& InLocalData, uint32 MaxMillisecondsToWait)
	{
		if (IsValid())
		{
			SIZE_T SessionDataOffset = offsetof(struct FSharedResourcePublicData, SessionData);
			SIZE_T SessionOffset = SessionDataOffset+SessionIndex * sizeof(FSharedResourceSessionData);
			SIZE_T DataOffset = IsClient() ? (offsetof(struct FSharedResourceSessionData, ClientData)) : (offsetof(struct FSharedResourceSessionData, ServerData));
			return ResourceMemory.WriteData((void*)(&InLocalData), SessionOffset+DataOffset, sizeof(FSharedResourceProcessData), MaxMillisecondsToWait);
		}
		return false;
	}

	bool FSharedResource::ReadData(FSharedResourceProcessData& OutData, bool bIsLocal, uint32 MaxMillisecondsToWait)
	{
		if (IsValid())
		{
			SIZE_T SessionDataOffset = offsetof(struct FSharedResourcePublicData, SessionData);
			SIZE_T SessionOffset = SessionDataOffset + SessionIndex * sizeof(FSharedResourceSessionData);
			SIZE_T DataOffset = (ProcessType == (bIsLocal ? (ETextureShareProcess::Client) : (ETextureShareProcess::Server) )) ? (offsetof(struct FSharedResourceSessionData, ClientData)) : (offsetof(struct FSharedResourceSessionData, ServerData));
			return ResourceMemory.ReadData((void*)(&OutData), SessionOffset+DataOffset, sizeof(FSharedResourceProcessData), MaxMillisecondsToWait);
		}
		return false;
	}
}
