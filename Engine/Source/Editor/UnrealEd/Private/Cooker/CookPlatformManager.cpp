// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookPlatformManager.h"

#include "Commandlets/AssetRegistryGenerator.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "CookRequests.h"
#include "Interfaces/ITargetPlatform.h"

namespace UE
{
namespace Cook
{

	FPlatformManager::FPlatformManager(FCriticalSection& InSessionLock)
		:SessionLock(InSessionLock)
	{
	}

	FCriticalSection& FPlatformManager::GetSessionLock()
	{
		return SessionLock;
	}

	const TArray<const ITargetPlatform*>& FPlatformManager::GetSessionPlatforms() const
	{
		checkf(bHasSelectedSessionPlatforms, TEXT("Calling GetSessionPlatforms or (any of the top level cook functions that call it) without first calling SelectSessionPlatforms is invalid"));
		return SessionPlatforms;
	}

	bool FPlatformManager::HasSelectedSessionPlatforms() const
	{
		return bHasSelectedSessionPlatforms;
	}

	bool FPlatformManager::HasSessionPlatform(const ITargetPlatform* TargetPlatform) const
	{
		return SessionPlatforms.Contains(TargetPlatform);
	}

	void FPlatformManager::SelectSessionPlatforms(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
	{
		FScopeLock Lock(&SessionLock);

		SessionPlatforms.Empty(TargetPlatforms.Num());
		SessionPlatforms.Append(TargetPlatforms.GetData(), TargetPlatforms.Num());
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			CreatePlatformData(TargetPlatform);
		}
		bHasSelectedSessionPlatforms = true;
	}

	void FPlatformManager::ClearSessionPlatforms()
	{
		FScopeLock Lock(&SessionLock);

		SessionPlatforms.Empty();
		bHasSelectedSessionPlatforms = false;
	}

	void FPlatformManager::AddSessionPlatform(const ITargetPlatform* TargetPlatform)
	{
		FScopeLock Lock(&SessionLock);

		if (!SessionPlatforms.Contains(TargetPlatform))
		{
			SessionPlatforms.Add(TargetPlatform);
			CreatePlatformData(TargetPlatform);
			bHasSelectedSessionPlatforms = true;
		}
	}

	FPlatformData* FPlatformManager::GetPlatformData(const ITargetPlatform* Platform)
	{
		return PlatformDatas.Find(Platform);
	}

	FPlatformData& FPlatformManager::CreatePlatformData(const ITargetPlatform* Platform)
	{
		check(Platform != nullptr);
		FPlatformData& PlatformData = PlatformDatas.FindOrAdd(Platform);
		if (PlatformData.PlatformName.IsNone())
		{
			check(!bPlatformDataFrozen); // It is not legal to add new platforms to this map when running CookOnTheFlyServer because we read this map from threads handling network requests, and mutating the map is not threadsafe when it is being read

			// Newly added, construct
			PlatformData.PlatformName = FName(Platform->PlatformName());
			checkf(!PlatformData.PlatformName.IsNone(), TEXT("Invalid ITargetPlatform with an empty name"));
		}
		return PlatformData;
	}

	bool FPlatformManager::IsPlatformInitialized(const ITargetPlatform* Platform) const
	{
		const FPlatformData* PlatformData = PlatformDatas.Find(Platform);
		if (!PlatformData)
		{
			return false;
		}
		return PlatformData->bIsSandboxInitialized;
	}

	void FPlatformManager::SetPlatformDataFrozen(bool bFrozen)
	{
		bPlatformDataFrozen = bFrozen;
	}

	void FPlatformManager::PruneUnreferencedSessionPlatforms(UCookOnTheFlyServer& CookOnTheFlyServer)
	{
		const double SecondsToLive = 5.0 * 60;

		double OldestKeepTime = -1.0e10; // Constructed to something smaller than 0 - SecondsToLive, so we can robustly detect not-yet-initialized
		TArray<const ITargetPlatform*, TInlineAllocator<1>> RemovePlatforms;

		for (auto& kvpair : PlatformDatas)
		{
			FPlatformData& PlatformData = kvpair.Value;
			if (PlatformData.LastReferenceTime > 0. && PlatformData.ReferenceCount == 0)
			{
				// We have a platform that we need to check for pruning.  Initialize the OldestKeepTime so we can check whether the platform has aged out.
				if (OldestKeepTime < -SecondsToLive)
				{
					const double CurrentTimeSeconds = FPlatformTime::Seconds();
					OldestKeepTime = CurrentTimeSeconds - SecondsToLive;
				}

				// Note that this loop is outside of the critical section, for performance.
				// If we find any candidates for pruning we have to check them again once inside the critical section.
				if (kvpair.Value.LastReferenceTime < OldestKeepTime)
				{
					RemovePlatforms.Add(kvpair.Key);
				}
			}
		}

		if (RemovePlatforms.Num() > 0)
		{
			FScopeLock Lock(&SessionLock);

			for (const ITargetPlatform* TargetPlatform : RemovePlatforms)
			{
				FPlatformData* PlatformData = PlatformDatas.Find(TargetPlatform);
				if (PlatformData->LastReferenceTime > 0. && PlatformData->ReferenceCount == 0 && PlatformData->LastReferenceTime < OldestKeepTime)
				{
					// Mark that the platform no longer needs to be inspected for pruning because we have removed it from CookOnTheFly's SessionPlatforms
					PlatformData->LastReferenceTime = 0.;

					// Remove the SessionPlatform
					CookOnTheFlyServer.OnRemoveSessionPlatform(TargetPlatform);

					SessionPlatforms.Remove(TargetPlatform);
					if (SessionPlatforms.Num() == 0)
					{
						bHasSelectedSessionPlatforms = false;
					}
				}
			}
		}
	}

	void FPlatformManager::AddRefCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform, UCookOnTheFlyServer& CookOnTheFlyServer)
	{
		check(TargetPlatform != nullptr);
		FPlatformData* PlatformData = GetPlatformData(TargetPlatform);
		checkf(PlatformData != nullptr, TEXT("Unrecognized Platform %s"), *TargetPlatform->PlatformName());
		++PlatformData->ReferenceCount;

		if (!HasSessionPlatform(TargetPlatform))
		{
			CookOnTheFlyServer.ExternalRequests->AddCallback([this, TargetPlatform, LocalCookOnTheFlyServer = &CookOnTheFlyServer]()
				{
					AddSessionPlatform(TargetPlatform);
					LocalCookOnTheFlyServer->bPackageFilterDirty = true;
				});
		}
	}

	void FPlatformManager::ReleaseCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform)
	{
		check(TargetPlatform != nullptr);
		FPlatformData* PlatformData = GetPlatformData(TargetPlatform);
		checkf(PlatformData != nullptr, TEXT("Unrecognized Platform %s"), *TargetPlatform->PlatformName());
		check(PlatformData->ReferenceCount > 0);
		--PlatformData->ReferenceCount;
		PlatformData->LastReferenceTime = FPlatformTime::Seconds();
	}

}
}
