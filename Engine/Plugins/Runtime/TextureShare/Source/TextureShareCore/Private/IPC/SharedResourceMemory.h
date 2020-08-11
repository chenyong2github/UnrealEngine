// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SharedResourceContainers.h"
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
		static FPlatformProcess::FSemaphore * ProcessMutex;
		/** Low-level memory region */
		static FPlatformMemory::FSharedMemoryRegion * ProcessMemory;
		/** Lock that guard access to shared textures */
		FTextureMutex TextureMutex[ELimits::MaxTextureShareItemTexturesCount];

	public:
		FSharedResourceMemory()
		{
			for (int i = 0; i < ELimits::MaxTextureShareItemTexturesCount; i++)
			{ 
				TextureMutex[i] = FTextureMutex(); 
			}
		}

		virtual ~FSharedResourceMemory()
		{
			for (int i = 0; i < ELimits::MaxTextureShareItemTexturesCount; i++)
			{
				ReleaseTextureMutex(i, false);
			}
		}

		bool InitializeTextureMutex(int TextureIndex, const FString& TextureName)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			return TextureMutex[TextureIndex].Initialize(TextureName);
		}

		void ReleaseTextureMutex(int TextureIndex, bool bDeleteISO)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			TextureMutex[TextureIndex].Release(bDeleteISO);
		}

		void ReleaseTexturesMutex(bool bDeleteISO)
		{
			for (int TextureIndex = 0; TextureIndex < ELimits::MaxTextureShareItemTexturesCount; ++TextureIndex)
			{
				TextureMutex[TextureIndex].Release(bDeleteISO);
			}
		}

		bool LockTextureMutex(int TextureIndex, uint32 MaxMillisecondsToWait)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			return TextureMutex[TextureIndex].Lock(MaxMillisecondsToWait);
		}

		void UnlockTextureMutex(int TextureIndex)
		{
			check(TextureIndex >= 0 && TextureIndex < MaxTextureShareItemTexturesCount);
			TextureMutex[TextureIndex].Unlock();
		}

		void UnlockTexturesMutex()
		{
			for (int TextureIndex = 0; TextureIndex < ELimits::MaxTextureShareItemTexturesCount; ++TextureIndex)
			{
				TextureMutex[TextureIndex].Unlock();
			}
		}
		
		static bool InitializeProcessMemory();
		static void ReleaseProcessMemory();

		/** update session header. Call inside locked mutex */
		void WriteSessionProcessState_LockedMutex(ETextureShareProcess ProcessType, ESharedResourceProcessState ProcessState, int SessionIndex, const FString& SessionName)
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

		bool DisconnectSession(ETextureShareProcess ProcessType, int SessionIndex, uint32 MaxMillisecondsToWait)
		{
			check(ProcessMutex);
			check(ProcessMemory);
			if (SessionIndex < 0 || SessionIndex>= MaxTextureShareItemSessionsCount)
			{
				return false;
			}

			// acquire
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

			WriteSessionProcessState_LockedMutex(ProcessType, ESharedResourceProcessState::Undefined, SessionIndex, FString());
			ProcessMutex->Unlock();
			return true;
		}

		int ConnectSession(ETextureShareProcess& InOutProcessType, const FString& SessionName, uint32 MaxMillisecondsToWait)
		{
			if (!ProcessMemory || !ProcessMutex)
			{
				return -1;
			}

			// acquire
			if (!MaxMillisecondsToWait)
			{
				ProcessMutex->Lock();
			}
			else
			{
				if (!ProcessMutex->TryLock(MaxMillisecondsToWait * 1000000ULL))	// 1ms = 10^6 ns
				{
					return -1;
				}
			}

			int FreeSessionIndex = -1;
			FSharedResourceSessionHeader SessionHeader[MaxTextureShareItemSessionsCount];
			FMemory::Memcpy(&SessionHeader[0], ((char*)ProcessMemory->GetAddress()), sizeof(SessionHeader));

			for (int i = 0; i < MaxTextureShareItemSessionsCount; i++)
			{
				// Save free session index:
				if (SessionHeader[i].IsFreeSession())
				{
					FreeSessionIndex = (FreeSessionIndex<0) ? i : FreeSessionIndex;
				}
				else
				{
					// Search exist session by name:
					if (!FPlatformString::Stricmp(SessionHeader[i].SessionName, *SessionName))
					{
						// Connect to exist session
						WriteSessionProcessState_LockedMutex(InOutProcessType, ESharedResourceProcessState::Used, i, SessionName);
						ProcessMutex->Unlock();
						return i;
					}
				}
			}

			// Register new session
			if (FreeSessionIndex >=0)
			{
				// Connect to new session
				WriteSessionProcessState_LockedMutex(InOutProcessType, ESharedResourceProcessState::Used, FreeSessionIndex, SessionName);
			}

			// relinquish
			ProcessMutex->Unlock();
			return FreeSessionIndex;
		}

		bool WriteData(const void* SrcDataPtr, SIZE_T DataOffset, SIZE_T DataSize, uint32 MaxMillisecondsToWait)
		{
			check(ProcessMutex);
			check(ProcessMemory);
			check(SrcDataPtr);
			check(DataSize);

			// acquire
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

			// we have exclusive ownership now!
			FMemory::Memcpy(((char*)ProcessMemory->GetAddress()) + DataOffset, SrcDataPtr, DataSize);
			ProcessMutex->Unlock();
			return true;
		}

		bool ReadData(void* DstDataPtr, SIZE_T DataOffset, SIZE_T DataSize, uint32 MaxMillisecondsToWait)
		{
			check(ProcessMutex);
			check(ProcessMemory);
			check(DstDataPtr);
			check(DataSize);

			// acquire
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

			// we have exclusive ownership now!
			FMemory::Memcpy(DstDataPtr, ((char*)ProcessMemory->GetAddress()) + DataOffset, DataSize);
			ProcessMutex->Unlock();
			return true;
		}
	};
};
