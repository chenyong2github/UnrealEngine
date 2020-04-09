// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PhysicsCoreTypes.h"
#include "ChaosLog.h"

namespace Chaos
{
	using EThreadingMode = EChaosThreadingMode;

	/**
	 * Recursive Read/Write lock object for protecting external data accesses for physics scenes.
	 * This is a fairly heavy lock designed to allow scene queries and user code to safely access
	 * external physics data.
	 * 
	 * The lock also allows a thread to recursively lock data to avoid deadlocks on repeated writes
	 * or undefined behavior for nesting read locks.
	 * 
	 * Fairness is determined by the underlying platform FRWLock type as this lock uses FRWLock
	 * as it's internal primitive
	 */
	class FPhysicsSceneGuard
	{
	public:
		FPhysicsSceneGuard()
		{
			TlsSlot = FPlatformTLS::AllocTlsSlot();
			CurrentWriterThreadId.Store(0);
		}

		~FPhysicsSceneGuard()
		{
			if(FPlatformTLS::IsValidTlsSlot(TlsSlot))
			{
				// Validate the lock as it shuts down
#if CHAOS_CHECKED
				ensureMsgf(CurrentWriterThreadId.Load() == 0, TEXT("Shutting down a physics scene guard but thread %u still holds a write lock"), CurrentWriterThreadId.Load());
#endif
				FPlatformTLS::FreeTlsSlot(TlsSlot);
			}
		}

		FPhysicsSceneGuard(const FPhysicsSceneGuard& InOther) = delete;
		FPhysicsSceneGuard(FPhysicsSceneGuard&& InOther) = delete;
		FPhysicsSceneGuard& operator=(const FPhysicsSceneGuard& InOther) = delete;
		FPhysicsSceneGuard& operator=(FPhysicsSceneGuard&& InOther) = delete;

		void ReadLock()
		{
			const FSceneLockTls ThreadData = ModifyTls([](FSceneLockTls& ThreadDataInner) {ThreadDataInner.ReadDepth++; });

			const uint32 ThisThreadId = FPlatformTLS::GetCurrentThreadId();

			// If we're already writing then don't attempt the lock, we already have exclusive access
			if(CurrentWriterThreadId.Load() != ThisThreadId && ThreadData.ReadDepth == 1)
			{
				InnerLock.ReadLock();
			}
		}

		void WriteLock()
		{
			ModifyTls([](FSceneLockTls& ThreadDataInner) {ThreadDataInner.WriteDepth++; });

			const uint32 ThisThreadId = FPlatformTLS::GetCurrentThreadId();
			
			if(CurrentWriterThreadId.Load() != ThisThreadId)
			{
				InnerLock.WriteLock();
				CurrentWriterThreadId.Store(ThisThreadId);
			}
		}

		void ReadUnlock()
		{
			const FSceneLockTls ThreadData = ModifyTls([](FSceneLockTls& ThreadDataInner)
			{
				if(ThreadDataInner.ReadDepth > 0)
				{
					ThreadDataInner.ReadDepth--;
				}
				else
				{
#if CHAOS_CHECKED
					ensureMsgf(false, TEXT("ReadUnlock called on physics scene guard when the thread does not hold the lock"));
#else
					UE_LOG(LogChaos, Warning, TEXT("ReadUnlock called on physics scene guard when the thread does not hold the lock"))
#endif
				}
				
			});

			const uint32 ThisThreadId = FPlatformTLS::GetCurrentThreadId();

			if(CurrentWriterThreadId.Load() != ThisThreadId && ThreadData.ReadDepth == 0)
			{
				InnerLock.ReadUnlock();
			}
		}

		void WriteUnlock()
		{
			const uint32 ThisThreadId = FPlatformTLS::GetCurrentThreadId();

			if(CurrentWriterThreadId.Load() == ThisThreadId)
			{
				const FSceneLockTls ThreadData = ModifyTls([](FSceneLockTls& ThreadDataInner) {ThreadDataInner.WriteDepth--; });

				if(ThreadData.WriteDepth == 0)
				{
					CurrentWriterThreadId.Store(0);
					InnerLock.WriteUnlock();
				}
			}
			else
			{
#if CHAOS_CHECKED
				ensureMsgf(false, TEXT("WriteUnlock called on physics scene guard when the thread does not hold the lock"));
#else
				UE_LOG(LogChaos, Warning, TEXT("ReadUnlock called on physics scene guard when the thread does not hold the lock"))
#endif
			}
		}

	private:

		// We use 32 bits to store our depths (16 read and 16 write) allowing a maximum
		// recursive lock of depth 65,536. This unions to whatever the platform ptr size
		// is so we can store this directly into TLS without allocating more storage
		class FSceneLockTls
		{
		public:

			FSceneLockTls()
				: WriteDepth(0)
				, ReadDepth(0)
			{}

			union
			{
				struct  
				{
					uint16 WriteDepth;
					uint16 ReadDepth;
				};
				void* PtrDummy;
			};

		};

		// Helper for modifying the current TLS data
		template<typename CallableType>
		const FSceneLockTls ModifyTls(CallableType Callable)
		{
			checkSlow(FPlatformTLS::IsValidTlsSlot(TlsSlot));

			void* ThreadData = FPlatformTLS::GetTlsValue(TlsSlot);

			FSceneLockTls TlsData;
			TlsData.PtrDummy = ThreadData;

			Callable(TlsData);

			FPlatformTLS::SetTlsValue(TlsSlot, TlsData.PtrDummy);

			return TlsData;
		}

		uint32 TlsSlot;
		TAtomic<uint32> CurrentWriterThreadId;
		FRWLock InnerLock;
	};
}
