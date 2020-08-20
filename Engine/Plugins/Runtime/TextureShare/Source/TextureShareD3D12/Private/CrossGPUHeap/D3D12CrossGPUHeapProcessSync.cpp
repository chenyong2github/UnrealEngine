// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12CrossGPUHeapProcessSync.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)
#include "TextureShareD3D12Log.h"

/*
 * FPublicCrossGPUSyncData
 */
int FPublicCrossGPUSyncData::FindTexture(const FString& ResourceID) const
{
	for (int i = 0; i < CrossGPUProcessSync::MaxSharedTextures; ++i)
	{
		// Case insensitive search
		if (!FPlatformString::Stricmp(Textures[i].Name, *ResourceID))
		{
			return i;
		}
	}
	return -1;
}

int FPublicCrossGPUSyncData::FindFreeTexture() const
{
	for (int i = 0; i < CrossGPUProcessSync::MaxSharedTextures; ++i)
	{
		// Free sync element
		if (Textures[i].Name[0] == 0)
		{
			return i;
		}
	}
	return -1;
}

#if TE_ENABLE_FENCE	
int FPublicCrossGPUSyncData::FindMasterFence(const HANDLE ProcessHandle) const
{
	for (int i = 0; i < CrossGPUProcessSync::MaxProcessNum; ++i)
	{
		// Case insensitive search
		if (Fences[i].MasterProcessHandle == ProcessHandle)
		{
			return i;
		}
	}
	return -1;
}

int FPublicCrossGPUSyncData::FindFreeMasterFence() const
{
	for (int i = 0; i < CrossGPUProcessSync::MaxProcessNum; ++i)
	{
		// Free sync element
		if (Fences[i].MasterProcessHandle == 0)
		{
			return i;
		}
	}
	return -1;
}

bool FPublicCrossGPUSyncData::IsFenceConnected()
{
	for (int i = 0; i < CrossGPUProcessSync::MaxProcessNum; ++i)
	{
		// Free sync element
		if (Fences[i].IsDefined() && !Fences[i].IsConnected())
		{
			return false;
		}
	}
	return true;
}
#endif

/*
 * FCrossGPUProcessSync
 */

FCrossGPUProcessSync::~FCrossGPUProcessSync()
{}

void FCrossGPUProcessSync::StartupCrossGPU()
{
	FCrossGPUSharedMemory::InitializeProcessMemory(sizeof(FPublicCrossGPUSyncData), nullptr);
}

void FCrossGPUProcessSync::ShutdownCrossGPU()
{
	FCrossGPUSharedMemory::ReleaseProcessMemory();
}

bool FCrossGPUProcessSync::ReadSyncData(FPublicCrossGPUSyncData& OutData)
{
	return SharedMemory.IsValid() && SharedMemory.ReadData(&OutData, 0, sizeof(FPublicCrossGPUSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

bool FCrossGPUProcessSync::WriteSyncData(const FPublicCrossGPUSyncData& InData)
{
	return SharedMemory.IsValid() && SharedMemory.WriteData(&InData, 0, sizeof(FPublicCrossGPUSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

bool FCrossGPUProcessSync::ReadTextureData(int Index, FTextureSyncData& OutData)
{
	return SharedMemory.IsValid() && SharedMemory.ReadData(&OutData, FPublicCrossGPUSyncData::GetTextureDataOffset(Index), sizeof(FTextureSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

bool FCrossGPUProcessSync::WriteTextureData(int Index, const FTextureSyncData InData)
{
	return SharedMemory.IsValid() && SharedMemory.WriteData(&InData, FPublicCrossGPUSyncData::GetTextureDataOffset(Index), sizeof(FTextureSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

#if TE_ENABLE_FENCE	
bool FCrossGPUProcessSync::ReadFenceData(int Index, FFenceSyncData& OutData)
{
	return SharedMemory.IsValid() && SharedMemory.ReadData(&OutData, FPublicCrossGPUSyncData::GetFenceDataOffset(Index), sizeof(FFenceSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

bool FCrossGPUProcessSync::WriteFenceData(int Index, const FFenceSyncData InData)
{
	return SharedMemory.IsValid() && SharedMemory.WriteData(&InData, FPublicCrossGPUSyncData::GetFenceDataOffset(Index), sizeof(FFenceSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

#endif

bool FCrossGPUProcessSync::ReadIPCData(FIPCSyncData& OutData)
{
	return SharedMemory.IsValid() && SharedMemory.ReadData(&OutData, FPublicCrossGPUSyncData::GetIPCDataOffset(), sizeof(FIPCSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

bool FCrossGPUProcessSync::WriteIPCData(const FIPCSyncData InData)
{
	return SharedMemory.IsValid() && SharedMemory.WriteData(&InData, FPublicCrossGPUSyncData::GetIPCDataOffset(), sizeof(FIPCSyncData), CrossGPUProcessSync::MemoryLockMaxMillisecondsToWait);
}

/*
 * FCrossGPUSharedMemory
 */

FPlatformProcess::FSemaphore* FCrossGPUSharedMemory::ProcessMutex = nullptr;
FPlatformMemory::FSharedMemoryRegion* FCrossGPUSharedMemory::ProcessMemory = nullptr;

// Initialize one library memory share per process (multi session usage)
bool FCrossGPUSharedMemory::InitializeProcessMemory(SIZE_T TotalSize, const void* pSecurityAttributes)
{
	if (ProcessMutex || ProcessMemory)
	{
		// multi initialize, ignore
		return false;
	}

	uint32 AccessMode = FPlatformMemory::ESharedMemoryAccess::Read | FPlatformMemory::ESharedMemoryAccess::Write;

	bool bCreateNew = false;

	// Try open existing share memory:
	ProcessMemory = FPlatformMemory::MapNamedSharedMemoryRegion(Memory_GUID, false, AccessMode, TotalSize, pSecurityAttributes);
	if (!ProcessMemory)
	{
		//Try Open new:
		ProcessMemory = FPlatformMemory::MapNamedSharedMemoryRegion(Memory_GUID, true, AccessMode, TotalSize, pSecurityAttributes);

		if (ProcessMemory)
		{
			bCreateNew = true;

			// initialize memory with zeroes
			check(ProcessMemory->GetAddress());
			FMemory::Memzero(ProcessMemory->GetAddress(), ProcessMemory->GetSize());

			// Release old mutex (leak fix??)
			ProcessMutex = FPlatformProcess::NewInterprocessSynchObject(Memory_MutexName, false);
			if (ProcessMutex)
			{
				FPlatformProcess::DeleteInterprocessSynchObject(ProcessMutex);
				ProcessMutex = NULL;
			}
		}
		else
		{
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("CrossGPU IPC memory Failed map shared memory region "));
			return false;
		}
	}

	// Try open exist memory mutex:
	ProcessMutex = bCreateNew ? nullptr : FPlatformProcess::NewInterprocessSynchObject(Memory_MutexName, false, CrossGPUProcessSync::MaxProcessNum);
	if (!ProcessMutex)
	{
		// Try create new:
		ProcessMutex = FPlatformProcess::NewInterprocessSynchObject(Memory_MutexName, true, CrossGPUProcessSync::MaxProcessNum);

		if (!ProcessMutex)
		{
			FPlatformMemory::UnmapNamedSharedMemoryRegion(ProcessMemory);
			ProcessMemory = NULL;
			
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("CrossGPU IPC memory Failed create memory mutex"));
			return false;
		}
	}

	return true;
}

void FCrossGPUSharedMemory::ReleaseProcessMemory()
{
	if (ProcessMemory)
	{
		FPlatformMemory::UnmapNamedSharedMemoryRegion(ProcessMemory);
		ProcessMemory = NULL;
	}

	if (ProcessMutex)
	{
		FPlatformProcess::DeleteInterprocessSynchObject(ProcessMutex);
		ProcessMutex = NULL;
	}
}
#endif
