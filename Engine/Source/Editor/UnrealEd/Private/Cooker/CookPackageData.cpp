// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookPackageData.h"

#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Containers/StringView.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace UE
{
namespace Cook
{


	//////////////////////////////////////////////////////////////////////////
	// FPackageData


	FPackageData::FPackageData(FPackageDatas& PackageDatas, const FName& InPackageName, const FName& InFileName)
		: PackageName(InPackageName), FileName(InFileName), PackageDatas(PackageDatas)
		, bIsUrgent(0), bHasSaveCache(0), bCookedPlatformDataStarted(0), bCookedPlatformDataCalled(0), bCookedPlatformDataComplete(0), bMonitorIsCooked(0)
	{
		SetState(EPackageState::Idle);
		SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}

	FPackageData::~FPackageData()
	{
		ClearCookedPlatforms(); // We need to send OnCookedPlatformRemoved message to the monitor, so it is not valid to destruct without calling ClearCookedPlatforms
	}

	const FName& FPackageData::GetPackageName() const
	{
		return PackageName;
	}

	const FName& FPackageData::GetFileName() const
	{
		return FileName;
	}

	void FPackageData::SetFileName(const FName& InFileName)
	{
		FileName = InFileName;
	}

	const TArray<const ITargetPlatform*> FPackageData::GetRequestedPlatforms() const
	{
		return RequestedPlatforms;
	}

	void FPackageData::SetRequestedPlatforms(const TArrayView<const ITargetPlatform* const>& Platforms)
	{
		RequestedPlatforms.Empty(Platforms.Num());
		AddRequestedPlatforms(Platforms);
	}

	void FPackageData::AddRequestedPlatforms(const TArrayView<const ITargetPlatform* const>& New)
	{
		for (const ITargetPlatform* TargetPlatform : New)
		{
			RequestedPlatforms.AddUnique(TargetPlatform);
		}
	}

	void FPackageData::ClearRequestedPlatforms()
	{
		RequestedPlatforms.Empty();
	}

	bool FPackageData::ContainsAllRequestedPlatforms(const TArrayView<const ITargetPlatform* const>& Platforms) const
	{
		if (Platforms.Num() == 0)
		{
			return true;
		}
		if (RequestedPlatforms.Num() == 0)
		{
			return false;
		}

		for (const ITargetPlatform* QueryPlatform : Platforms)
		{
			if (!RequestedPlatforms.Contains(QueryPlatform))
			{
				return false;
			}
		}
		return true;
	}

	void FPackageData::SetIsUrgent(bool Value)
	{
		bool OldValue = static_cast<bool>(bIsUrgent);
		if (OldValue != Value)
		{
			bIsUrgent = Value != 0;
			PackageDatas.GetMonitor().OnUrgencyChanged(*this);
		}
	}

	void FPackageData::UpdateRequestData(const TArrayView<const ITargetPlatform* const>& InRequestedPlatforms, bool bInIsUrgent, FCompletionCallback&& InCompletionCallback)
	{
		if (IsInProgress())
		{
			AddCompletionCallback(MoveTemp(InCompletionCallback));

			bool bUrgencyChanged = false;
			if (bInIsUrgent && !GetIsUrgent())
			{
				bUrgencyChanged = true;
				SetIsUrgent(true);
			}

			if (!ContainsAllRequestedPlatforms(InRequestedPlatforms))
			{
				// Send back to the Request state (cancelling any current operations) and then add the new platforms
				SendToState(EPackageState::Request, ESendFlags::QueueAddAndRemove);
				AddRequestedPlatforms(InRequestedPlatforms);
			}
			else if (bUrgencyChanged)
			{
				FPackageDataQueue* Queue = GetQueue();
				check(Queue);
				if (Queue)
				{
					Queue->Remove(this);
					Queue->PushFront(this);
				}
			}
		}
		else
		{
			SetRequestData(InRequestedPlatforms, bInIsUrgent, MoveTemp(InCompletionCallback));
			SendToState(EPackageState::Request, ESendFlags::QueueAddAndRemove);
		}
	}

	void FPackageData::SetRequestData(const TArrayView<const ITargetPlatform* const>& InRequestedPlatforms, bool bInIsUrgent, FCompletionCallback&& InCompletionCallback)
	{
		check(!CompletionCallback);
		check(RequestedPlatforms.Num() == 0);
		check(!bIsUrgent);
		SetRequestedPlatforms(InRequestedPlatforms);
		SetIsUrgent(bInIsUrgent);
		AddCompletionCallback(MoveTemp(InCompletionCallback));
	}

	void FPackageData::ClearRequestData()
	{
		ClearRequestedPlatforms();
		SetIsUrgent(false);
		CompletionCallback = FCompletionCallback();
	}

	void FPackageData::AddCookedPlatforms(const TArrayView<const ITargetPlatform* const>& New, const TArrayView<const bool>& Succeeded)
	{
		check(New.Num() == Succeeded.Num());
		int32 Num = New.Num();
		if (Num == 0)
		{
			return;
		}

		const ITargetPlatform* const* NewData = New.GetData();
		const bool* SucceededData = Succeeded.GetData();
		for (int32 n = 0; n < Num; ++n)
		{
			const ITargetPlatform* TargetPlatform = NewData[n];
			bool bSucceeded = SucceededData[n];

			int32 ExistingIndex = CookedPlatforms.IndexOfByKey(TargetPlatform);
			if (ExistingIndex != INDEX_NONE)
			{
				CookSucceeded[ExistingIndex] = bSucceeded;
			}
			else
			{
				CookedPlatforms.Add(TargetPlatform);
				CookSucceeded.Add(bSucceeded);
			}
		}

		PackageDatas.GetMonitor().OnCookedPlatformAdded(*this);
	}

	void FPackageData::AddCookedPlatforms(const TArrayView<const ITargetPlatform* const>& New, bool bSucceeded)
	{
		int32 Num = New.Num();
		if (Num == 0)
		{
			return;
		}

		for (const ITargetPlatform* TargetPlatform : New)
		{
			int32 ExistingIndex = CookedPlatforms.IndexOfByKey(TargetPlatform);
			if (ExistingIndex != INDEX_NONE)
			{
				CookSucceeded[ExistingIndex] = bSucceeded;
			}
			else
			{
				CookedPlatforms.Add(TargetPlatform);
				CookSucceeded.Add(bSucceeded);
			}
		}

		PackageDatas.GetMonitor().OnCookedPlatformAdded(*this);
	}

	void FPackageData::RemoveCookedPlatform(const ITargetPlatform* Platform)
	{
		int32 Num = CookedPlatforms.Num();
		for (int32 n = CookedPlatforms.Num() - 1; n >= 0; --n)
		{
			if (CookedPlatforms[n] == Platform)
			{
				CookedPlatforms.RemoveAtSwap(n);
				CookSucceeded.RemoveAtSwap(n);
				PackageDatas.GetMonitor().OnCookedPlatformRemoved(*this);
				break;
			}
		}
	}

	void FPackageData::RemoveCookedPlatforms(const TArrayView<const ITargetPlatform* const>& Platforms)
	{
		for (const ITargetPlatform* Platform : Platforms)
		{
			RemoveCookedPlatform(Platform);
		}
	}

	void FPackageData::ClearCookedPlatforms()
	{
		if (CookedPlatforms.Num() > 0)
		{
			CookedPlatforms.Empty();
			CookSucceeded.Empty();
			PackageDatas.GetMonitor().OnCookedPlatformRemoved(*this);
		}
	}

	const TArray<const ITargetPlatform*>& FPackageData::GetCookedPlatforms() const
	{
		return CookedPlatforms;
	}

	int32 FPackageData::GetNumCookedPlatforms() const
	{
		return CookedPlatforms.Num();
	}

	bool FPackageData::HasAnyCookedPlatform() const
	{
		return CookedPlatforms.Num() > 0;
	}

	bool FPackageData::HasAnyCookedPlatforms(const TArrayView<const ITargetPlatform* const>& Platforms, bool bIncludeFailed) const
	{
		if (CookedPlatforms.Num() == 0)
		{
			return false;
		}

		for (const ITargetPlatform* QueryPlatform : Platforms)
		{
			if (HasCookedPlatform(QueryPlatform, bIncludeFailed))
			{
				return true;
			}
		}
		return false;
	}

	bool FPackageData::HasAllCookedPlatforms(const TArrayView<const ITargetPlatform* const>& Platforms, bool bIncludeFailed) const
	{
		if (Platforms.Num() == 0)
		{
			return true;
		}
		if (CookedPlatforms.Num() == 0)
		{
			return false;
		}

		for (const ITargetPlatform* QueryPlatform : Platforms)
		{
			if (!HasCookedPlatform(QueryPlatform, bIncludeFailed))
			{
				return false;
			}
		}
		return true;
	}

	bool FPackageData::HasCookedPlatform(const ITargetPlatform* Platform, bool bIncludeFailed) const
	{
		ECookResult Result = GetCookResults(Platform);
		return (Result == ECookResult::Succeeded) | ((Result == ECookResult::Failed) & (bIncludeFailed != 0));
	}

	ECookResult FPackageData::GetCookResults(const ITargetPlatform* Platform) const
	{
		int32 Num = CookedPlatforms.Num();
		for (int n = 0; n < Num; ++n)
		{
			if (CookedPlatforms[n] == Platform)
			{
				return CookSucceeded[n] ? ECookResult::Succeeded : ECookResult::Failed;
			}
		}
		return ECookResult::Unseen;
	}

	UPackage* FPackageData::GetPackage() const
	{
		return Package;
	}

	void FPackageData::SetPackage(UPackage* InPackage)
	{
		Package = InPackage;
	}

	EPackageState FPackageData::GetState() const
	{
		return static_cast<EPackageState>(State);
	}

	void FPackageData::SendToState(EPackageState NextState, ESendFlags SendFlags)
	{
		bool bWasInProgress;
		EPackageState OldState = GetState();
		switch (OldState)
		{
		case EPackageState::Idle:
			bWasInProgress = false;
			PackageDatas.GetCookOnTheFlyServer().ExitIdle(*this);
			break;
		case EPackageState::Request:
			bWasInProgress = true;
			if (!!(SendFlags & ESendFlags::QueueRemove))
			{
				ensure(PackageDatas.GetRequestQueue().Remove(this) == 1);
			}
			PackageDatas.GetCookOnTheFlyServer().ExitRequest(*this);
			break;
		case EPackageState::Save:
			bWasInProgress = true;
			if (!!(SendFlags & ESendFlags::QueueRemove))
			{
				ensure(PackageDatas.GetSaveQueue().Remove(this) == 1);
			}
			PackageDatas.GetCookOnTheFlyServer().ExitSave(*this);
			break;
		default:
			bWasInProgress = false;
			check(false);
			break;
		}

		FPackageDataQueue* Queue = nullptr;
		switch (NextState)
		{
		case EPackageState::Idle:
			SetInProgressState(bWasInProgress, false);
			SetState(NextState);
			PackageDatas.GetCookOnTheFlyServer().EnterIdle(*this);
			break;
		case EPackageState::Request:
			SetInProgressState(bWasInProgress, true);
			SetState(NextState);
			PackageDatas.GetCookOnTheFlyServer().EnterRequest(*this);
			Queue = &PackageDatas.GetRequestQueue();
			break;
		case EPackageState::Save:
			SetInProgressState(bWasInProgress, true);
			SetState(NextState);
			PackageDatas.GetCookOnTheFlyServer().EnterSave(*this);
			Queue = &PackageDatas.GetSaveQueue();
			break;
		default:
			check(false);
			break;
		}

		if (((SendFlags & ESendFlags::QueueAdd) != ESendFlags::QueueNone) & (Queue != nullptr))
		{
			if (GetIsUrgent())
			{
				Queue->PushFront(this);
			}
			else
			{
				Queue->PushBack(this);
			}
		}
		PackageDatas.GetMonitor().OnStateChanged(*this, OldState);
	}

	FPackageDataQueue* FPackageData::GetQueue() const
	{
		switch (GetState())
		{
		case EPackageState::Idle:
			return nullptr;
		case EPackageState::Request:
			return &PackageDatas.GetRequestQueue();
		case EPackageState::Save:
			return &PackageDatas.GetSaveQueue();
		default:
			check(false);
			return nullptr;
		}
	}

	bool FPackageData::IsInProgress() const
	{
		switch (GetState())
		{
		case EPackageState::Idle:
			return false;
		case EPackageState::Request:
			return true;
		case EPackageState::Save:
			return true;
		default:
			check(false);
			return false;
		}
	}

	void FPackageData::SetState(EPackageState NextState)
	{
		State = static_cast<uint32>(NextState);
	}

	void FPackageData::SetInProgressState(bool bOldInProgress, bool bNewInProgress)
	{
		if (bOldInProgress == bNewInProgress)
		{
			return;
		}
		if (bNewInProgress)
		{
			PackageDatas.GetCookOnTheFlyServer().EnterInProgress(*this);
		}
		else
		{
			PackageDatas.GetCookOnTheFlyServer().ExitInProgress(*this);
		}
	}

	FCompletionCallback& FPackageData::GetCompletionCallback()
	{
		return CompletionCallback;
	}

	void FPackageData::AddCompletionCallback(FCompletionCallback&& InCompletionCallback)
	{
		if (InCompletionCallback)
		{
			// We don't yet have a mechanism for calling two completion callbacks.  CompletionCallbacks only come from external requests, and it should not be possible to request twice, so a failed check here shouldn't happen.
			check(!CompletionCallback);
			CompletionCallback = MoveTemp(InCompletionCallback);
		}
	}

	TArray<UObject*>& FPackageData::GetCachedObjectsInOuter()
	{
		return CachedObjectsInOuter;
	}

	void FPackageData::CheckObjectCacheEmpty() const
	{
		check(CachedObjectsInOuter.Num() == 0);
		check(!GetHasSaveCache());
	}

	void FPackageData::CreateObjectCache()
	{
		if (GetHasSaveCache())
		{
			return;
		}

		if (Package && Package->IsFullyLoaded())
		{
			PackageName = Package->GetFName();
			GetObjectsWithOuter(Package, CachedObjectsInOuter);
			SetHasSaveCache(true);
		}
		else
		{
			check(false);
		}
	}

	void FPackageData::ClearObjectCache()
	{
		CachedObjectsInOuter.Empty();
		SetHasSaveCache(false);
	}

	const int32& FPackageData::GetNumPendingCookedPlatformData() const
	{
		return NumPendingCookedPlatformData;
	}

	int32& FPackageData::GetNumPendingCookedPlatformData()
	{
		return NumPendingCookedPlatformData;
	}

	const int32& FPackageData::GetCookedPlatformDataNextIndex() const
	{
		return CookedPlatformDataNextIndex;
	}

	int32& FPackageData::GetCookedPlatformDataNextIndex()
	{
		return CookedPlatformDataNextIndex;
	}

	void FPackageData::CheckCookedPlatformDataEmpty() const
	{
		check(GetCookedPlatformDataNextIndex() == 0);
		check(!GetCookedPlatformDataStarted());
		check(!GetCookedPlatformDataCalled());
		check(!GetCookedPlatformDataComplete());
	}

	void FPackageData::ClearCookedPlatformData()
	{
		CookedPlatformDataNextIndex = 0;
		// Note that GetNumPendingCookedPlatformData is not cleared; it persists across Saves and CookSessions
		SetCookedPlatformDataStarted(false);
		SetCookedPlatformDataCalled(false);
		SetCookedPlatformDataComplete(false);
	}

	void FPackageData::OnRemoveSessionPlatform(const ITargetPlatform* Platform)
	{
		RequestedPlatforms.RemoveSwap(Platform);
	}

	bool FPackageData::HasReferencedObjects() const
	{
		return Package != nullptr || CachedObjectsInOuter.Num() > 0;
	}

	void FPackageData::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(Package);
		Collector.AddReferencedObjects(CachedObjectsInOuter);
	}

	bool FPackageData::IsSaveInvalidated() const
	{
		if (GetState() != EPackageState::Save)
		{
			return false;
		}

		return GetPackage() == nullptr || !GetPackage()->IsFullyLoaded() || CachedObjectsInOuter.Contains(nullptr);
	}


	//////////////////////////////////////////////////////////////////////////
	// FPendingCookedPlatformData


	FPendingCookedPlatformData::FPendingCookedPlatformData(UObject* InObject, const ITargetPlatform* InTargetPlatform, FPackageData& InPackageData, bool bInNeedsResourceRelease, UCookOnTheFlyServer& InCookOnTheFlyServer)
		:Object(InObject), TargetPlatform(InTargetPlatform), PackageData(InPackageData), CookOnTheFlyServer(InCookOnTheFlyServer),
		CancelManager(nullptr), ClassName(InObject->GetClass()->GetFName()), bHasReleased(false), bNeedsResourceRelease(bInNeedsResourceRelease)
	{
		check(Object);
		PackageData.GetNumPendingCookedPlatformData() += 1;
	}

	FPendingCookedPlatformData::FPendingCookedPlatformData(FPendingCookedPlatformData&& Other)
		:Object(Other.Object), TargetPlatform(Other.TargetPlatform), PackageData(Other.PackageData), CookOnTheFlyServer(Other.CookOnTheFlyServer),
		CancelManager(Other.CancelManager), ClassName(Other.ClassName), bHasReleased(Other.bHasReleased), bNeedsResourceRelease(Other.bNeedsResourceRelease)
	{
		Other.Object = nullptr;
	}

	FPendingCookedPlatformData::~FPendingCookedPlatformData()
	{
		Release();
	}

	bool FPendingCookedPlatformData::PollIsComplete()
	{
		if (bHasReleased)
		{
			return true;
		}

		if (!Object || Object->IsCachedCookedPlatformDataLoaded(TargetPlatform))
		{
			Release();
			return true;
		}
		else
		{
#if DEBUG_COOKONTHEFLY
			UE_LOG(LogCook, Display, TEXT("Object %s isn't cached yet"), *Object->GetFullName());
#endif
			/*if ( Object->IsA(UMaterial::StaticClass()) )
			{
				if (GShaderCompilingManager->HasShaderJobs() == false)
				{
					UE_LOG(LogCook, Warning, TEXT("Shader compiler is in a bad state!  Shader %s is finished compile but shader compiling manager did not notify shader.  "), *Object->GetPathName());
				}
			}*/
			return false;
		}
	}

	void FPendingCookedPlatformData::Release()
	{
		if (bHasReleased)
		{
			return;
		}

		if (bNeedsResourceRelease)
		{
			int32* CurrentAsyncCache = CookOnTheFlyServer.CurrentAsyncCacheForType.Find(ClassName);
			check(CurrentAsyncCache != nullptr); // bNeedsRelease should not have been set if the AsyncCache does not have an entry for the class
			*CurrentAsyncCache += 1;
		}

		PackageData.GetNumPendingCookedPlatformData() -= 1;
		check(PackageData.GetNumPendingCookedPlatformData() >= 0);
		if (CancelManager)
		{
			CancelManager->Release(*this);
			CancelManager = nullptr;
		}

		Object = nullptr;
		bHasReleased = true;
	}


	//////////////////////////////////////////////////////////////////////////
	// FPendingCookedPlatformDataCancelManager


	void FPendingCookedPlatformDataCancelManager::Release(FPendingCookedPlatformData& Data)
	{
		--NumPendingPlatforms;
		if (NumPendingPlatforms <= 0)
		{
			check(NumPendingPlatforms == 0);
			if (Data.Object)
			{
				Data.Object->ClearAllCachedCookedPlatformData();
			}
			delete this;
		}
	}


	//////////////////////////////////////////////////////////////////////////
	// FPackageDataMonitor


	int32 FPackageDataMonitor::GetNumUrgent(EPackageState InState)
	{
		switch (InState)
		{
		case EPackageState::Request:
			return NumUrgentRequests;
		case EPackageState::Save:
			return NumUrgentSaves;
		default:
			check(false); // Tracking not yet implemented for this state
			return 0;
		}
	}

	int32 FPackageDataMonitor::GetNumCooked()
	{
		return NumCooked;
	}

	void FPackageDataMonitor::OnCookedPlatformAdded(FPackageData& PackageData)
	{
		if (!PackageData.GetMonitorIsCooked())
		{
			++NumCooked;
			PackageData.SetMonitorIsCooked(true);
		}
	}

	void FPackageDataMonitor::OnCookedPlatformRemoved(FPackageData& PackageData)
	{
		if (PackageData.GetNumCookedPlatforms() == 0)
		{
			if (PackageData.GetMonitorIsCooked())
			{
				--NumCooked;
				PackageData.SetMonitorIsCooked(false);
			}
		}
	}

	void FPackageDataMonitor::OnUrgencyChanged(FPackageData& PackageData)
	{
		int32 Delta = PackageData.GetIsUrgent() ? 1 : -1;
		TrackUrgentRequests(PackageData.GetState(), Delta);
	}

	void FPackageDataMonitor::OnStateChanged(FPackageData& PackageData, EPackageState OldState)
	{
		if (!PackageData.GetIsUrgent())
		{
			return;
		}

		TrackUrgentRequests(OldState, -1);
		TrackUrgentRequests(PackageData.GetState(), 1);
	}

	void FPackageDataMonitor::TrackUrgentRequests(EPackageState State, int32 Delta)
	{
		switch (State)
		{
		case EPackageState::Idle:
			break;
		case EPackageState::Request:
			NumUrgentRequests += Delta;
			check(NumUrgentRequests >= 0);
			break;
		case EPackageState::Save:
			NumUrgentSaves += Delta;
			check(NumUrgentSaves >= 0);
			break;
		default:
			check(false);
			break;
		}
	}


	//////////////////////////////////////////////////////////////////////////
	// FPackageDatas


	FPackageDatas::FPackageDatas(UCookOnTheFlyServer& InCookOnTheFlyServer)
		: CookOnTheFlyServer(InCookOnTheFlyServer)
	{
	}

	FPackageDatas::~FPackageDatas()
	{
		Clear();
	}

	FString FPackageDatas::GetReferencerName() const
	{
		return TEXT("FPackageDatas");
	}

	void FPackageDatas::AddReferencedObjects(FReferenceCollector& Collector)
	{
		return CookOnTheFlyServer.CookerAddReferencedObjects(Collector);
	}

	const FPackageNameCache& FPackageDatas::GetPackageNameCache() const
	{
		return PackageNameCache;
	}

	FPackageDataMonitor& FPackageDatas::GetMonitor()
	{
		return Monitor;
	}

	UCookOnTheFlyServer& FPackageDatas::GetCookOnTheFlyServer()
	{
		return CookOnTheFlyServer;
	}

	FPackageDataQueue& FPackageDatas::GetRequestQueue()
	{
		return RequestQueue;
	}

	FPackageDataQueue& FPackageDatas::GetSaveQueue()
	{
		return SaveQueue;
	}

	FPackageData& FPackageDatas::FindOrAddPackageData(const FName& PackageName, const FName& NormalizedFileName)
	{
		FPackageData** PackageDataMapAddr = PackageNameToPackageData.Find(PackageName);
		if (PackageDataMapAddr != nullptr)
		{
			FPackageData** FileNameMapAddr = FileNameToPackageData.Find(NormalizedFileName);
			check(FileNameMapAddr && *FileNameMapAddr == *PackageDataMapAddr);
			return **PackageDataMapAddr;
		}

		checkf(FileNameToPackageData.Find(NormalizedFileName) == nullptr, TEXT("Package \"%s\" and package \"%s\" share the same filename \"%s\"."),
			*PackageName.ToString(), *(*FileNameToPackageData.Find(NormalizedFileName))->GetPackageName().ToString(), *NormalizedFileName.ToString());
		return CreatePackageData(PackageName, NormalizedFileName);
	}

	FPackageData* FPackageDatas::FindPackageDataByPackageName(const FName& PackageName)
	{
		if (PackageName.IsNone())
		{
			return nullptr;
		}

		FPackageData** PackageDataMapAddr = PackageNameToPackageData.Find(PackageName);
		return PackageDataMapAddr ? *PackageDataMapAddr : nullptr;
	}

	FPackageData* FPackageDatas::TryAddPackageDataByPackageName(const FName& PackageName)
	{
		if (PackageName.IsNone())
		{
			return nullptr;
		}

		FPackageData** PackageDataMapAddr = PackageNameToPackageData.Find(PackageName);
		if (PackageDataMapAddr != nullptr)
		{
			return *PackageDataMapAddr;
		}

		FName FileName = PackageNameCache.GetCachedStandardFileName(PackageName);
		checkf(FileNameToPackageData.Find(FileName) == nullptr, TEXT("Package \"%s\" and package \"%s\" share the same filename \"%s\"."),
			*PackageName.ToString(), *(*FileNameToPackageData.Find(FileName))->GetPackageName().ToString(), *FileName.ToString()); 
		return &CreatePackageData(PackageName, FileName);
	}

	FPackageData& FPackageDatas::AddPackageDataByPackageNameChecked(const FName& PackageName)
	{
		FPackageData* PackageData = TryAddPackageDataByPackageName(PackageName);
		check(PackageData);
		return *PackageData;
	}

	FPackageData* FPackageDatas::FindPackageDataByFileName(const FName& InFileName)
	{
		FName FileName(FPackageNameCache::GetStandardFileName(InFileName));
		if (FileName.IsNone())
		{
			return nullptr;
		}

		FPackageData** PackageDataMapAddr = FileNameToPackageData.Find(FileName);
		return PackageDataMapAddr ? *PackageDataMapAddr : nullptr;
	}

	FPackageData* FPackageDatas::TryAddPackageDataByFileName(const FName& InFileName)
	{
		FName FileName(FPackageNameCache::GetStandardFileName(InFileName));
		if (FileName.IsNone())
		{
			return nullptr;
		}

		FPackageData** PackageDataMapAddr = FileNameToPackageData.Find(FileName);
		if (PackageDataMapAddr != nullptr)
		{
			return *PackageDataMapAddr;
		}

		const FName* PackageName = PackageNameCache.GetCachedPackageNameFromStandardFileName(FileName);
		if (!PackageName)
		{
			return nullptr;
		}
		return &CreatePackageData(*PackageName, FileName);
	}

	FPackageData& FPackageDatas::CreatePackageData(FName PackageName, FName FileName)
	{
		if (PackageName.IsNone())
		{
			check(!FileName.IsNone());
			const FName* FoundPackageName = PackageNameCache.GetCachedPackageNameFromStandardFileName(FileName);
			check(FoundPackageName);
			PackageName = *FoundPackageName;
			check(!PackageName.IsNone());
		}
		else if (FileName.IsNone())
		{
			FileName = PackageNameCache.GetCachedStandardFileName(PackageName);
			check(!FileName.IsNone());
		}

		FPackageData* PackageData = new FPackageData(*this, PackageName, FileName);
		PackageDatas.Add(PackageData);
		PackageNameToPackageData.Add(PackageName, PackageData);
		FileNameToPackageData.Add(FileName, PackageData);
		return *PackageData;
	}

	FPackageData& FPackageDatas::AddPackageDataByFileNameChecked(const FName& FileName)
	{
		FPackageData* PackageData = TryAddPackageDataByFileName(FileName);
		check(PackageData);
		return *PackageData;
	}

	FPackageData* FPackageDatas::UpdateFileName(const FName& PackageName)
	{
		if (!PackageNameCache.HasCacheForPackageName(PackageName))
		{
			return nullptr;
		}

		FName OldFileName = PackageNameCache.GetCachedStandardFileName(PackageName);
		PackageNameCache.ClearPackageFileNameCacheForPackage(PackageName);
		FName NewFileName = PackageNameCache.GetCachedStandardFileName(PackageName);

		FPackageData** PackageDataAddr = PackageNameToPackageData.Find(PackageName);
		if (!PackageDataAddr)
		{
			check(OldFileName.IsNone() || !FileNameToPackageData.Find(OldFileName));
			return nullptr;
		}
		FPackageData* PackageData = *PackageDataAddr;

		if (OldFileName == NewFileName)
		{
			return PackageData;
		}

		if (!OldFileName.IsNone())
		{
			PackageDataAddr = FileNameToPackageData.Find(OldFileName);
			check(PackageDataAddr == nullptr || *PackageDataAddr == PackageData);
			FileNameToPackageData.Remove(OldFileName);
		}
		PackageData->SetFileName(NewFileName);
		if (!NewFileName.IsNone())
		{
			check(FileNameToPackageData.Find(NewFileName) == nullptr);
			FileNameToPackageData.Add(NewFileName, PackageData);
		}

		return PackageData;
	}

	void FPackageDatas::RegisterFileNameAlias(FPackageData& PackageData, FName FileName)
	{
		FileName = FPackageNameCache::GetStandardFileName(FileName);
		if (FileName.IsNone())
		{
			return;
		}

		FPackageData*& PackageDataMapAddr = FileNameToPackageData.FindOrAdd(FileName);
		check(PackageDataMapAddr == nullptr || PackageDataMapAddr == &PackageData);
		PackageDataMapAddr = &PackageData;
	}

	int32 FPackageDatas::GetNumCooked()
	{
		return Monitor.GetNumCooked();
	}

	void FPackageDatas::GetCookedFileNamesForPlatform(const ITargetPlatform* Platform, TArray<FName>& CookedFiles, bool bGetFailedCookedPackages, bool bGetSuccessfulCookedPackages)
	{
		for (const FPackageData* PackageData : PackageDatas)
		{
			ECookResult CookResults = PackageData->GetCookResults(Platform);
			if (((CookResults == ECookResult::Succeeded) & (bGetSuccessfulCookedPackages != 0)) |
				((CookResults == ECookResult::Failed) & (bGetFailedCookedPackages != 0)))
			{
				CookedFiles.Add(PackageData->GetFileName());
			}
		}
	}

	void FPackageDatas::Clear()
	{
		PendingCookedPlatformDatas.Empty(); // These destructors will dereference PackageDatas
		RequestQueue.Empty();
		SaveQueue.Empty();
		PackageNameToPackageData.Empty();
		FileNameToPackageData.Empty();
		for (FPackageData* PackageData : PackageDatas)
		{
			delete PackageData;
		}
		PackageDatas.Empty();
	}

	void FPackageDatas::ClearCookedPlatforms()
	{
		for (FPackageData* PackageData : PackageDatas)
		{
			PackageData->ClearCookedPlatforms();
		}
	}

	void FPackageDatas::OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform)
	{
		for (FPackageData* PackageData : PackageDatas)
		{
			PackageData->OnRemoveSessionPlatform(TargetPlatform);
		}
	}

	TArray<FPendingCookedPlatformData>& FPackageDatas::GetPendingCookedPlatformDatas()
	{
		return PendingCookedPlatformDatas;
	}

	void FPackageDatas::PollPendingCookedPlatformDatas()
	{
		if (PendingCookedPlatformDatas.Num() == 0)
		{
			return;
		}

		GShaderCompilingManager->ProcessAsyncResults(true /* bLimitExecutionTime */, false /* bBlockOnGlobalShaderCompletion */);

		FPendingCookedPlatformData* Datas = PendingCookedPlatformDatas.GetData();
		for (int Index = 0; Index < PendingCookedPlatformDatas.Num();)
		{
			if (Datas[Index].PollIsComplete())
			{
				PendingCookedPlatformDatas.RemoveAtSwap(Index);
			}
			else
			{
				++Index;
			}
		}
	}

	TArray<FPackageData*>::RangedForIteratorType FPackageDatas::begin()
	{
		return PackageDatas.begin();
	}

	TArray<FPackageData*>::RangedForIteratorType FPackageDatas::end()
	{
		return PackageDatas.end();
	}
}
}