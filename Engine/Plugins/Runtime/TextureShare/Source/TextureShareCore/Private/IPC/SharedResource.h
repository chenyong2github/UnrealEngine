// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "SharedResourceContainers.h"

namespace TextureShareItem
{
	class FSharedResourceMemory;
	class FSharedResource
	{
		bool ReadData(FSharedResourceProcessData& OutData, bool bIsLocal, uint32 MaxMillisecondsToWait);

	public:
		FSharedResource(ETextureShareProcess ProcessType, const FString& ResourceName);
		virtual ~FSharedResource();

		static bool InitializeProcessMemory();
		static void ReleaseProcessMemory();

		bool Initialize(uint32 MaxMillisecondsToWait);
		void Release(uint32 MaxMillisecondsToWait);

		bool WriteLocalData(const FSharedResourceProcessData& InLocalData, uint32 MaxMillisecondsToWait);
		bool ReadLocalData(FSharedResourceProcessData& OutLocalData, uint32 MaxMillisecondsToWait)
			{ return ReadData(OutLocalData, true, MaxMillisecondsToWait); }
		bool ReadRemoteData(FSharedResourceProcessData& OutRemoteData, uint32 MaxMillisecondsToWait)
			{ return ReadData(OutRemoteData, false, MaxMillisecondsToWait); }

		bool InitializeTextureMutex(int32 TextureIndex, const FString& TextureName);
		void ReleaseTextureMutex(int32 TextureIndex, bool bDeleteISO);
		void ReleaseTexturesMutex(bool bDeleteISO);

		bool LockTextureMutex(int32 TextureIndex, uint32 MaxMillisecondsToWait);
		void UnlockTextureMutex(int32 TextureIndex);
		void UnlockTexturesMutex();

		bool WaitReadDataEvent(uint32 WaitTime, const bool bIgnoreThreadIdleStats = false);

		const FString& GetName() const
			{ return Name; }
		bool IsValid()
			{ return SessionIndex>=0; }
		bool IsClient() const
			{ return ETextureShareProcess::Client == ProcessType; }

	protected:
		FString Name;
		int32     SessionIndex;
		ETextureShareProcess   ProcessType;
		FSharedResourceMemory& ResourceMemory;
	};
};
