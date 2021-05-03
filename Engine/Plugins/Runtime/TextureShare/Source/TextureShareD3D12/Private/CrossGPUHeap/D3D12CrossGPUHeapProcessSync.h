// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "ID3D12CrossGPUHeap.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)
#include "D3D12CrossGPUHeapRules.h"
#include "D3D12CrossGPUHeapSharedMemory.h"
#include "../Platform/TextureShareD3D12PlatformWindows.h"

namespace CrossGPUProcessSync
{
	enum Limits
	{
		MaxSharedTextures = 128,
		MaxTextureNameLength = 128,
		MaxProcessNum = 16,
		MemoryLockMaxMillisecondsToWait = 500,
	};
};

struct FTextureSyncData
{
	TCHAR        Name[CrossGPUProcessSync::MaxTextureNameLength] = { 0 };
	HANDLE       ResourceHandle = nullptr;
	EPixelFormat Format = EPixelFormat::PF_Unknown;

	HANDLE       MasterProcessHandle = nullptr;
	HANDLE       SlaveProcessHandle = nullptr;
};

#if TE_ENABLE_FENCE
struct FFenceSyncData
{
	HANDLE       MasterProcessHandle = nullptr;
	HANDLE       SlaveProcessHandle = nullptr;
	HANDLE       FenceHandle = nullptr;

	inline bool IsConnected() const
	{
		return ((MasterProcessHandle == SlaveProcessHandle) && (MasterProcessHandle != nullptr));
	}
	
	inline bool IsDefined() const
	{
		return (FenceHandle != nullptr) || (MasterProcessHandle != nullptr) || (SlaveProcessHandle != nullptr);
	}
};
#endif

struct FIPCSyncData
{
	bool bProcessError = false;
};

struct FPublicCrossGPUSyncData
{
	int FindFreeTexture() const;
	int FindTexture(const FString& ResourceID) const;

	FTextureSyncData& GetTexture(int Index)
	{ return Textures[Index]; }

	static inline SIZE_T GetTextureDataOffset(int Index)
	{
		return offsetof(struct FPublicCrossGPUSyncData, Textures) + (Index * sizeof(FTextureSyncData));
	}

	static inline SIZE_T GetIPCDataOffset()
	{
		return offsetof(struct FPublicCrossGPUSyncData, IPC);
	}

#if TE_ENABLE_FENCE	
	int FindFreeMasterFence() const;
	int FindMasterFence(const HANDLE MasterProcessHandle) const;

	FFenceSyncData& GetFence(int Index)
	{ return Fences[Index]; }

	bool IsFenceConnected();


	static inline SIZE_T GetFenceDataOffset(int Index)
	{
		return offsetof(struct FPublicCrossGPUSyncData, Fences) + (Index * sizeof(FFenceSyncData));
	}

private:
	FFenceSyncData   Fences[CrossGPUProcessSync::MaxProcessNum];

#endif
private:
	FTextureSyncData Textures[CrossGPUProcessSync::MaxSharedTextures];
	FIPCSyncData     IPC;
};


class FCrossGPUProcessSync
{
public:
	virtual ~FCrossGPUProcessSync();

public:
	void StartupCrossGPU();
	void ShutdownCrossGPU();

protected:
	bool ReadSyncData(FPublicCrossGPUSyncData& OutData);
	bool WriteSyncData(const FPublicCrossGPUSyncData& InData);

	bool ReadTextureData(int Index, FTextureSyncData& OutData);
	bool WriteTextureData(int Index, const FTextureSyncData InData);

#if TE_ENABLE_FENCE	
	bool ReadFenceData(int Index, FFenceSyncData& OutData);
	bool WriteFenceData(int Index, const FFenceSyncData InData);
#endif

	bool ReadIPCData(FIPCSyncData& OutData);
	bool WriteIPCData(const FIPCSyncData InData);

	void SetErrorIPCSignal()
	{
		FIPCSyncData Data;
		if (ReadIPCData(Data))
		{
			Data.bProcessError = true;
			WriteIPCData(Data);
		}
	}

	bool GetErrorIPCSignal()
	{
		FIPCSyncData Data;
		return !ReadIPCData(Data) || Data.bProcessError;
	}

private:
	FCrossGPUSharedMemory  SharedMemory;
};
#endif
