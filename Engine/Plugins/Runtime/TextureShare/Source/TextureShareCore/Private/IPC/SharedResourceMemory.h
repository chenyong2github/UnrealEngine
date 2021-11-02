// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SharedResourceContainers.h"
#include "SharedResourceInterprocessEvent.h"
#include "Containers/TextureShareCoreEnums.h"
#include "Windows/WindowsPlatformProcess.h"

namespace TextureShareItem
{
	namespace IPC
	{
		enum Limits
		{
			MaxProcessNum = 32,
		};
	};

	class FSharedResourceMemory
	{
		struct FTextureMutex
		{
			FPlatformProcess::FSemaphore * Mutex = NULL;
			bool bLocked = false;

			void Release(bool bDeleteISO)
			{
				Unlock();

				if(bDeleteISO && Mutex)
				{ 
					FPlatformProcess::DeleteInterprocessSynchObject(Mutex);
				}

				Mutex = NULL;
			}

			bool Initialize(const FString& TextureName)
			{
				Release(false);

				// Open Existing
				Mutex = FPlatformProcess::NewInterprocessSynchObject(*TextureName, false, IPC::MaxProcessNum);
				if (!Mutex)
				{
					// Open new
					Mutex = FPlatformProcess::NewInterprocessSynchObject(*TextureName, true, IPC::MaxProcessNum);
				}

				return Mutex != NULL;
			}

			bool Lock(uint32 MaxMillisecondsToWait)
			{
				if (Mutex)
				{
					// acquire
					if (!MaxMillisecondsToWait)
					{
						Mutex->Lock();
					}
					else
					{
						if (!Mutex->TryLock(MaxMillisecondsToWait * 1000000ULL))	// 1ms = 10^6 ns
						{
							return false;
						}
					}

					bLocked = true;
					return true;
				}
				return false;
			}

			void Unlock()
			{
				if(Mutex && bLocked)
				{
					Mutex->Unlock();
				}
				bLocked = false;
			}
		};

	protected:
		/** Lock that guards access to the memory region */
		static FPlatformProcess::FSemaphore* ProcessMutex;
		/** Low-level memory region */
		static FPlatformMemory::FSharedMemoryRegion* ProcessMemory;
		/** Lock that guard access to shared textures */
		FTextureMutex TextureMutex[ELimits::MaxTextureShareItemTexturesCount];

		/** Sync shared memory changes */
		FTextureShareEventWin ReadEvent;
		FTextureShareEventWin WriteEvent;

	private:
		bool LockProcessMutex(uint32 MaxMillisecondsToWait)
		{
			// acquire
			if (ProcessMutex)
			{
				if (!MaxMillisecondsToWait)
				{
					ProcessMutex->Lock();
				}
				else
				{
					if (!ProcessMutex->TryLock(MaxMillisecondsToWait * 1000000ULL))	// 1ms = 10^6 ns
					{
						return false;
					}
				}

				return true;
			}

			return false;
		}

		void UnlockProcessMutex()
		{
			if (ProcessMutex)
			{
				// relinquish
				ProcessMutex->Unlock();
			}
		}

	public:
		FSharedResourceMemory(ETextureShareProcess InProcessType, const FString& SharedResourceName)
			: ReadEvent(FString::Printf(TEXT("Global\\TextureShareDataEvent%d_%s"), (uint8)((InProcessType == ETextureShareProcess::Client) ? ETextureShareProcess::Server : ETextureShareProcess::Client), *SharedResourceName))
			, WriteEvent(FString::Printf(TEXT("Global\\TextureShareDataEvent%d_%s"), (uint8)InProcessType, *SharedResourceName))
		{
			for (int32 TextureIndex = 0; TextureIndex < ELimits::MaxTextureShareItemTexturesCount; TextureIndex++)
			{ 
				TextureMutex[TextureIndex] = FTextureMutex();
			}

			// initialize sync events for shared memory changes
			WriteEvent.Create();
			ReadEvent.Create();
		}

		virtual ~FSharedResourceMemory()
		{
			for (int32 TextureIndex = 0; TextureIndex < ELimits::MaxTextureShareItemTexturesCount; TextureIndex++)
			{
				ReleaseTextureMutex(TextureIndex, false);
			}
		}

		bool WaitReadDataEvent(uint32 WaitTime, const bool bIgnoreThreadIdleStats = false)
		{
			// Waiting for remote process data change
			return ReadEvent.Wait(WaitTime, bIgnoreThreadIdleStats);
		}

		bool InitializeTextureMutex(int32 TextureIndex, const FString& TextureName)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			return TextureMutex[TextureIndex].Initialize(TextureName);
		}

		void ReleaseTextureMutex(int32 TextureIndex, bool bDeleteISO)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			TextureMutex[TextureIndex].Release(bDeleteISO);
		}

		void ReleaseTexturesMutex(bool bDeleteISO)
		{
			for (int32 TextureIndex = 0; TextureIndex < ELimits::MaxTextureShareItemTexturesCount; ++TextureIndex)
			{
				TextureMutex[TextureIndex].Release(bDeleteISO);
			}
		}

		bool LockTextureMutex(int32 TextureIndex, uint32 MaxMillisecondsToWait)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			return TextureMutex[TextureIndex].Lock(MaxMillisecondsToWait);
		}

		void UnlockTextureMutex(int32 TextureIndex)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			TextureMutex[TextureIndex].Unlock();
		}

		void UnlockTexturesMutex()
		{
			for (int32 TextureIndex = 0; TextureIndex < ELimits::MaxTextureShareItemTexturesCount; ++TextureIndex)
			{
				TextureMutex[TextureIndex].Unlock();
			}
		}
		
		static bool InitializeProcessMemory();
		static void ReleaseProcessMemory();

		/** update session header. Call inside locked mutex */
		void WriteSessionProcessState_LockedMutex(ETextureShareProcess ProcessType, ESharedResourceProcessState ProcessState, int32 SessionIndex, const FString& SessionName)
		{
			FSharedResourceSessionHeader SessionHeader;
			SIZE_T HeaderOffset = sizeof(FSharedResourceSessionHeader)*SessionIndex;

			// Read current value:
			FMemory::Memcpy(&SessionHeader, ((char*)ProcessMemory->GetAddress())+ HeaderOffset, sizeof(SessionHeader));
			
			// Change session process state:
			SessionHeader.ProcessState[(uint8)ProcessType] = ProcessState;

			switch (ProcessState)
			{
			case ESharedResourceProcessState::Used:
				// Update session name
				FPlatformString::Strcpy(SessionHeader.SessionName, MaxTextureShareItemSessionName, *SessionName);
				break;
			case ESharedResourceProcessState::Undefined:
				// Release session name
				if (SessionHeader.IsFreeSession())
				{
					FMemory::Memzero(SessionHeader.SessionName, MaxTextureShareItemSessionName);
				}
				break;
			}

			// Write new value
			FMemory::Memcpy(((char*)ProcessMemory->GetAddress()) + HeaderOffset, &SessionHeader, sizeof(SessionHeader));
		}

		bool DisconnectSession(ETextureShareProcess ProcessType, int32 SessionIndex, uint32 MaxMillisecondsToWait)
		{
			check(ProcessMemory);
			if (SessionIndex < 0 || SessionIndex>= MaxTextureShareItemSessionsCount)
			{
				return false;
			}

			if (!LockProcessMutex(MaxMillisecondsToWait))
			{
				return false;
			}

			WriteSessionProcessState_LockedMutex(ProcessType, ESharedResourceProcessState::Undefined, SessionIndex, FString());
			UnlockProcessMutex();

			return true;
		}

		int32 ConnectSession(const ETextureShareProcess InProcessType, const FString& SessionName, uint32 MaxMillisecondsToWait)
		{
			if (!ProcessMemory || !LockProcessMutex(MaxMillisecondsToWait))
			{
				return -1;
			}

			int32 FreeSessionIndex = -1;
			FSharedResourceSessionHeader SessionHeader[MaxTextureShareItemSessionsCount];
			FMemory::Memcpy(&SessionHeader[0], ((char*)ProcessMemory->GetAddress()), sizeof(SessionHeader));

			for (int32 SessionIndexIt = 0; SessionIndexIt < MaxTextureShareItemSessionsCount; SessionIndexIt++)
			{
				// Save free session index:
				if (SessionHeader[SessionIndexIt].IsFreeSession())
				{
					FreeSessionIndex = (FreeSessionIndex<0) ? SessionIndexIt : FreeSessionIndex;
				}
				else
				{
					// Search exist session by name:
					if (!FPlatformString::Stricmp(SessionHeader[SessionIndexIt].SessionName, *SessionName))
					{
						// Connect to exist session
						WriteSessionProcessState_LockedMutex(InProcessType, ESharedResourceProcessState::Used, SessionIndexIt, SessionName);
						UnlockProcessMutex();

						return SessionIndexIt;
					}
				}
			}

			// Register new session
			if (FreeSessionIndex >=0)
			{
				// Connect to new session
				WriteSessionProcessState_LockedMutex(InProcessType, ESharedResourceProcessState::Used, FreeSessionIndex, SessionName);
			}

			UnlockProcessMutex();

			return FreeSessionIndex;
		}

		bool WriteData(const void* SrcDataPtr, SIZE_T DataOffset, SIZE_T DataSize, uint32 MaxMillisecondsToWait)
		{
			check(ProcessMemory);
			check(SrcDataPtr);
			check(DataSize);

			if (!LockProcessMutex(MaxMillisecondsToWait))
			{
				return false;
			}

			// we have exclusive ownership now!
			FMemory::Memcpy(((char*)ProcessMemory->GetAddress()) + DataOffset, SrcDataPtr, DataSize);

			// Send signal to remote process after data change
			WriteEvent.Trigger();

			UnlockProcessMutex();

			return true;
		}

		bool ReadData(void* DstDataPtr, SIZE_T DataOffset, SIZE_T DataSize, uint32 MaxMillisecondsToWait)
		{
			check(ProcessMemory);
			check(DstDataPtr);
			check(DataSize);

			if (!LockProcessMutex(MaxMillisecondsToWait))
			{
				return false;
			}

			// we have exclusive ownership now!
			FMemory::Memcpy(DstDataPtr, ((char*)ProcessMemory->GetAddress()) + DataOffset, DataSize);

			// Resetting the write signal of the remote process after reading the data
			ReadEvent.Reset();

			UnlockProcessMutex();

			return true;
		}
	};
};
