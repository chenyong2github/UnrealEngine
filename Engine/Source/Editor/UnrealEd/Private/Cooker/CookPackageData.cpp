// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookPackageData.h"

#include "Algo/AnyOf.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "CookPlatformManager.h"
#include "Containers/StringView.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/Paths.h"
#include "Misc/PreloadableFile.h"
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
		, bIsUrgent(0), bIsVisited(0), bIsPreloadAttempted(0), bIsPreloaded(0), bHasSaveCache(0), bCookedPlatformDataStarted(0), bCookedPlatformDataCalled(0), bCookedPlatformDataComplete(0), bMonitorIsCooked(0)
	{
		SetState(EPackageState::Idle);
		SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}

	FPackageData::~FPackageData()
	{
		ClearCookedPlatforms(); // We need to send OnCookedPlatformRemoved message to the monitor, so it is not valid to destruct without calling ClearCookedPlatforms
		SendToState(EPackageState::Idle, ESendFlags::QueueNone); // Update the monitor's counters and call exit functions
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

	const TArray<const ITargetPlatform*>& FPackageData::GetRequestedPlatforms() const
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

	void FPackageData::UpdateRequestData(const TArrayView<const ITargetPlatform* const>& InRequestedPlatforms, bool bInIsUrgent,
		FCompletionCallback&& InCompletionCallback, ESendFlags SendFlags)
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
				// Send back to the Request state (canceling any current operations) and then add the new platforms
				if (GetState() != EPackageState::Request)
				{
					check(SendFlags == ESendFlags::QueueAddAndRemove);
					SendToState(EPackageState::Request, ESendFlags::QueueAddAndRemove);
				}
				AddRequestedPlatforms(InRequestedPlatforms);
			}
			else if (bUrgencyChanged && SendFlags == ESendFlags::QueueAddAndRemove)
			{
				SendToState(GetState(), SendFlags);
			}
		}
		else
		{
			SetRequestData(InRequestedPlatforms, bInIsUrgent, MoveTemp(InCompletionCallback));
			SendToState(EPackageState::Request, SendFlags);
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

	void FPackageData::ClearInProgressData()
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
		return Package.Get();
	}

	void FPackageData::SetPackage(UPackage* InPackage)
	{
		Package = InPackage;
	}

	EPackageState FPackageData::GetState() const
	{
		return static_cast<EPackageState>(State);
	}

	/** Boilerplate-reduction struct that defines all of the multi-state properties and sets them based on the given state */
	struct FStateProperties
	{
		EPackageStateProperty Properties;
		explicit FStateProperties(EPackageState InState)
		{
			switch (InState)
			{
			case EPackageState::Idle:
				Properties = EPackageStateProperty::None;
				break;
			case EPackageState::Request:
				Properties = EPackageStateProperty::InProgress;
				break;
			case EPackageState::LoadPrepare:
				Properties = EPackageStateProperty::InProgress | EPackageStateProperty::Loading;
				break;
			case EPackageState::LoadReady:
				Properties = EPackageStateProperty::InProgress | EPackageStateProperty::Loading;
				break;
			// TODO_SaveQueue: When we add state PrepareForSave, it will also have bHasPackage = true, 
			case EPackageState::Save:
				Properties = EPackageStateProperty::InProgress | EPackageStateProperty::HasPackage;
				break;
			default:
				check(false);
				Properties = EPackageStateProperty::None;
				break;
			}
		}
	};

	void FPackageData::SendToState(EPackageState NextState, ESendFlags SendFlags)
	{
		EPackageState OldState = GetState();
		switch (OldState)
		{
		case EPackageState::Idle:
			OnExitIdle();
			break;
		case EPackageState::Request:
			if (!!(SendFlags & ESendFlags::QueueRemove))
			{
				ensure(PackageDatas.GetRequestQueue().Remove(this) == 1);
			}
			OnExitRequest();
			break;
		case EPackageState::LoadPrepare:
			if (!!(SendFlags & ESendFlags::QueueRemove))
			{
				ensure(PackageDatas.GetLoadPrepareQueue().Remove(this) == 1);
			}
			OnExitLoadPrepare();
			break;
		case EPackageState::LoadReady:
			if (!!(SendFlags & ESendFlags::QueueRemove))
			{
				ensure(PackageDatas.GetLoadReadyQueue().Remove(this) == 1);
			}
			OnExitLoadReady();
			break;
		case EPackageState::Save:
			if (!!(SendFlags & ESendFlags::QueueRemove))
			{
				ensure(PackageDatas.GetSaveQueue().Remove(this) == 1);
			}
			OnExitSave();
			break;
		default:
			check(false);
			break;
		}

		FStateProperties OldProperties(OldState);
		FStateProperties NewProperties(NextState);
		// Exit state properties in order from highest to lowest, then enter state properties in order from lowest to highest
		// This ensures that properties that rely on earlier properties are constructed later and torn down earlier than the earlier properties.
		for (EPackageStateProperty Iterator = EPackageStateProperty::Max; Iterator >= EPackageStateProperty::Min; Iterator = static_cast<EPackageStateProperty>(static_cast<uint32>(Iterator) >> 1))
		{
			if (((OldProperties.Properties & Iterator) != EPackageStateProperty::None) & ((NewProperties.Properties & Iterator) == EPackageStateProperty::None))
			{
				switch (Iterator)
				{
				case EPackageStateProperty::InProgress:
					OnExitInProgress();
					break;
				case EPackageStateProperty::Loading:
					OnExitLoading();
					break;
				case EPackageStateProperty::HasPackage:
					OnExitHasPackage();
					break;
				default:
					check(false);
					break;
				}
			}
		}
		for (EPackageStateProperty Iterator = EPackageStateProperty::Min; Iterator <= EPackageStateProperty::Max; Iterator = static_cast<EPackageStateProperty>(static_cast<uint32>(Iterator) << 1))
		{
			if (((OldProperties.Properties & Iterator) == EPackageStateProperty::None) & ((NewProperties.Properties & Iterator) != EPackageStateProperty::None))
			{
				switch (Iterator)
				{
				case EPackageStateProperty::InProgress:
					OnEnterInProgress();
					break;
				case EPackageStateProperty::Loading:
					OnEnterLoading();
					break;
				case EPackageStateProperty::HasPackage:
					OnEnterHasPackage();
					break;
				default:
					check(false);
					break;
				}
			}
		}


		SetState(NextState);
		switch (NextState)
		{
		case EPackageState::Idle:
			OnEnterIdle();
			break;
		case EPackageState::Request:
			OnEnterRequest();
			if (((SendFlags & ESendFlags::QueueAdd) != ESendFlags::QueueNone))
			{
				PackageDatas.GetRequestQueue().AddRequest(this);
			}
			break;
		case EPackageState::LoadPrepare:
			OnEnterLoadPrepare();
			if ((SendFlags & ESendFlags::QueueAdd) != ESendFlags::QueueNone)
			{
				if (GetIsUrgent())
				{
					PackageDatas.GetLoadPrepareQueue().AddFront(this);
				}
				else
				{
					PackageDatas.GetLoadPrepareQueue().Add(this);
				}
			}
			break;
		case EPackageState::LoadReady:
			OnEnterLoadReady();
			if ((SendFlags & ESendFlags::QueueAdd) != ESendFlags::QueueNone)
			{
				if (GetIsUrgent())
				{
					PackageDatas.GetLoadReadyQueue().AddFront(this);
				}
				else
				{
					PackageDatas.GetLoadReadyQueue().Add(this);
				}
			}
			break;
		case EPackageState::Save:
			OnEnterSave();
			if (((SendFlags & ESendFlags::QueueAdd) != ESendFlags::QueueNone))
			{
				if (GetIsUrgent())
				{
					PackageDatas.GetSaveQueue().AddFront(this);
				}
				else
				{
					PackageDatas.GetSaveQueue().Add(this);
				}
			}
			break;
		default:
			check(false);
			break;
		}

		PackageDatas.GetMonitor().OnStateChanged(*this, OldState);
	}

	void FPackageData::CheckInContainer() const
	{
		switch (GetState())
		{
		case EPackageState::Idle:
			break;
		case EPackageState::Request:
			check(PackageDatas.GetRequestQueue().Contains(this));
			break;
		case EPackageState::LoadPrepare:
			check(PackageDatas.GetLoadPrepareQueue().Contains(this));
			break;
		case EPackageState::LoadReady:
			check(Algo::Find(PackageDatas.GetLoadReadyQueue(), this) != nullptr);
			break;
		case EPackageState::Save:
			check(Algo::Find(PackageDatas.GetSaveQueue(), this) != nullptr);
			break;
		default:
			check(false);
			break;
		}
	}

	bool FPackageData::IsInProgress() const
	{
		return IsInStateProperty(EPackageStateProperty::InProgress);
	}

	bool FPackageData::IsInStateProperty(EPackageStateProperty Property) const
	{
		return (FStateProperties(GetState()).Properties & Property) != EPackageStateProperty::None;
	}

	void FPackageData::OnEnterIdle()
	{
		// Note that this might be on construction of the PackageData
	}

	void FPackageData::OnExitIdle()
	{
	}

	void FPackageData::OnEnterRequest()
	{
		check(RequestedPlatforms.Num() > 0); // It is not valid to enter the request state without requested platforms; it indicates a bug due to e.g. calling SendToState without UpdateRequestData from Idle
	}

	void FPackageData::OnExitRequest()
	{
	}

	void FPackageData::OnEnterLoadPrepare()
	{
	}

	void FPackageData::OnExitLoadPrepare()
	{
	}

	void FPackageData::OnEnterLoadReady()
	{
	}

	void FPackageData::OnExitLoadReady()
	{
	}

	void FPackageData::OnEnterSave()
	{
		check(GetPackage() != nullptr && GetPackage()->IsFullyLoaded());

		CheckObjectCacheEmpty();
		CheckCookedPlatformDataEmpty();
	}

	void FPackageData::OnExitSave()
	{
		PackageDatas.GetCookOnTheFlyServer().ReleaseCookedPlatformData(*this);
		ClearObjectCache();
	}

	void FPackageData::OnEnterInProgress()
	{
		PackageDatas.GetMonitor().OnInProgressChanged(*this, true);
	}

	void FPackageData::OnExitInProgress()
	{
		PackageDatas.GetMonitor().OnInProgressChanged(*this, false);
		UE::Cook::FCompletionCallback LocalCompletionCallback(MoveTemp(GetCompletionCallback()));
		if (LocalCompletionCallback)
		{
			LocalCompletionCallback();
		}
		ClearInProgressData();
	}

	void FPackageData::OnEnterLoading()
	{
		CheckPreloadEmpty();
	}

	void FPackageData::OnExitLoading()
	{
		ClearPreload();
	}

	void FPackageData::OnEnterHasPackage()
	{
	}

	void FPackageData::OnExitHasPackage()
	{
		SetPackage(nullptr);
	}

	void FPackageData::SetState(EPackageState NextState)
	{
		State = static_cast<uint32>(NextState);
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

	bool FPackageData::TryPreload()
	{
		check(IsInStateProperty(EPackageStateProperty::Loading));
		if (GetIsPreloadAttempted())
		{
			return true;
		}
		if (FindObjectFast<UPackage>(nullptr, GetPackageName()))
		{
			// If the package has already loaded, then there is no point in further preloading
			ClearPreload();
			SetIsPreloadAttempted(true);
			return true;
		}
		if (GAllowCookedDataInEditorBuilds)
		{
			// Use of preloaded files is not yet implemented when GAllowCookedDataInEditorBuilds is on, see FLinkerLoad::CreateLoader
			SetIsPreloadAttempted(true);
			return true;
		}
		if (!PreloadableFile.Get())
		{
			TStringBuilder<NAME_SIZE> FileNameString;
			GetFileName().ToString(FileNameString);
			PreloadableFile.Set(MakeShared<FPreloadableFile>(FileNameString.ToString()), *this);
			PreloadableFile.Get()->InitializeAsync(FPreloadableFile::Flags::PreloadHandle | FPreloadableFile::Flags::Prime);
		}
		const TSharedPtr<FPreloadableFile>& FilePtr = PreloadableFile.Get();
		if (!FilePtr->IsInitialized())
		{
			if (GetIsUrgent())
			{
				// For urgent requests, wait on them to finish preloading rather than letting them run asynchronously and coming back to them later
				FilePtr->WaitForInitialization();
				check(FilePtr->IsInitialized());
			}
			else
			{
				return false;
			}
		}
		if (FilePtr->TotalSize() < 0)
		{
			UE_LOG(LogCook, Warning, TEXT("Failed to find file when preloading %s."), *GetFileName().ToString());
			SetIsPreloadAttempted(true);
			PreloadableFile.Reset(*this);
			return true;
		}

		if (!FPreloadableFile::TryRegister(FilePtr))
		{
			UE_LOG(LogCook, Warning, TEXT("Duplicate attempts to register %s for preload."), *GetFileName().ToString());
			SetIsPreloadAttempted(true);
			PreloadableFile.Reset(*this);
			return true;
		}

		SetIsPreloaded(true);
		SetIsPreloadAttempted(true);
		return true;
	}

	void FPackageData::FTrackedPreloadableFilePtr::Set(TSharedPtr<FPreloadableFile>&& InPtr, FPackageData& Owner)
	{
		Reset(Owner);
		if (InPtr)
		{
			Ptr = MoveTemp(InPtr);
			Owner.PackageDatas.GetMonitor().OnPreloadAllocatedChanged(Owner, true);
		}
	}

	void FPackageData::FTrackedPreloadableFilePtr::Reset(FPackageData& Owner)
	{
		if (Ptr)
		{
			Owner.PackageDatas.GetMonitor().OnPreloadAllocatedChanged(Owner, false);
			Ptr.Reset();
		}
	}

	void FPackageData::ClearPreload()
	{
		const TSharedPtr<FPreloadableFile>& FilePtr = PreloadableFile.Get();
		if (GetIsPreloaded())
		{
			check(FilePtr);
			if (FPreloadableFile::UnRegister(FilePtr))
			{
				UE_LOG(LogCook, Display, TEXT("PreloadableFile was created for %s but never used. This is wasteful and bad for cook performance."), *PackageName.ToString());
			}
			FilePtr->ReleaseCache(); // ReleaseCache to conserve memory if the Linker still has a pointer to it
		}
		else
		{
			check(!FilePtr || !FilePtr->IsCacheAllocated());
			check(!FilePtr || !FPreloadableFile::UnRegister(FilePtr));
		}

		PreloadableFile.Reset(*this);
		SetIsPreloaded(false);
		SetIsPreloadAttempted(false);
	}

	void FPackageData::CheckPreloadEmpty()
	{
		check(!GetIsPreloadAttempted());
		check(!PreloadableFile.Get());
		check(!GetIsPreloaded());
	}

	TArray<FWeakObjectPtr>& FPackageData::GetCachedObjectsInOuter()
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

		UPackage* LocalPackage = GetPackage();
		if (LocalPackage && LocalPackage->IsFullyLoaded())
		{
			PackageName = LocalPackage->GetFName();
			TArray<UObject*> ObjectsInOuter;
			GetObjectsWithOuter(LocalPackage, ObjectsInOuter);
			CachedObjectsInOuter.Reset(ObjectsInOuter.Num());
			for (UObject* Object : ObjectsInOuter)
			{
				FWeakObjectPtr ObjectWeakPointer(Object);
				if (!ObjectWeakPointer.Get()) // ignore pending kill objects; they will not be serialized out so we don't need to call BeginCacheForCookedPlatformData on them
				{
					continue;
				}
				CachedObjectsInOuter.Emplace(MoveTemp(ObjectWeakPointer));
			}
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

	void FPackageData::RemapTargetPlatforms(const TMap<ITargetPlatform*, ITargetPlatform*>& Remap)
	{
		RemapArrayElements(RequestedPlatforms, Remap);
		RemapArrayElements(CookedPlatforms, Remap);
	}

	bool FPackageData::IsSaveInvalidated() const
	{
		if (GetState() != EPackageState::Save)
		{
			return false;
		}

		return GetPackage() == nullptr || !GetPackage()->IsFullyLoaded() ||
			Algo::AnyOf(CachedObjectsInOuter, [](const FWeakObjectPtr& WeakPtr)
				{
					// TODO: Keep track of which objects were public, and only invalidate the save if the object that has been deleted or marked pending kill was public
					// Until we make that change, we will unnecessarily invalidate and demote some packages after a garbage collect
					return WeakPtr.Get() == nullptr;
				});
	}

	//////////////////////////////////////////////////////////////////////////
	// FPendingCookedPlatformData


	FPendingCookedPlatformData::FPendingCookedPlatformData(UObject* InObject, const ITargetPlatform* InTargetPlatform, FPackageData& InPackageData, bool bInNeedsResourceRelease, UCookOnTheFlyServer& InCookOnTheFlyServer)
		:Object(InObject), TargetPlatform(InTargetPlatform), PackageData(InPackageData), CookOnTheFlyServer(InCookOnTheFlyServer),
		CancelManager(nullptr), ClassName(InObject->GetClass()->GetFName()), bHasReleased(false), bNeedsResourceRelease(bInNeedsResourceRelease)
	{
		check(InObject);
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

		UObject* LocalObject = Object.Get();
		if (!LocalObject || LocalObject->IsCachedCookedPlatformDataLoaded(TargetPlatform))
		{
			Release();
			return true;
		}
		else
		{
#if DEBUG_COOKONTHEFLY
			UE_LOG(LogCook, Display, TEXT("Object %s isn't cached yet"), *LocalObject->GetFullName());
#endif
			/*if ( LocalObject->IsA(UMaterial::StaticClass()) )
			{
				if (GShaderCompilingManager->HasShaderJobs() == false)
				{
					UE_LOG(LogCook, Warning, TEXT("Shader compiler is in a bad state!  Shader %s is finished compile but shader compiling manager did not notify shader.  "), *LocalObject->GetPathName());
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

	void FPendingCookedPlatformData::RemapTargetPlatforms(const TMap<ITargetPlatform*, ITargetPlatform*>& Remap)
	{
		TargetPlatform = Remap[TargetPlatform];
	}


	//////////////////////////////////////////////////////////////////////////
	// FPendingCookedPlatformDataCancelManager


	void FPendingCookedPlatformDataCancelManager::Release(FPendingCookedPlatformData& Data)
	{
		--NumPendingPlatforms;
		if (NumPendingPlatforms <= 0)
		{
			check(NumPendingPlatforms == 0);
			UObject* LocalObject = Data.Object.Get();
			if (LocalObject)
			{
				LocalObject->ClearAllCachedCookedPlatformData();
			}
			delete this;
		}
	}


	//////////////////////////////////////////////////////////////////////////
	// FPackageDataMonitor
	FPackageDataMonitor::FPackageDataMonitor()
	{
		FMemory::Memset(NumUrgentInState, 0);
	}

	int32 FPackageDataMonitor::GetNumUrgent() const
	{
		int32 NumUrgent = 0;
		for (EPackageState State = EPackageState::Min; State <= EPackageState::Max; State = static_cast<EPackageState>(static_cast<uint32>(State) + 1))
		{
			NumUrgent += NumUrgentInState[static_cast<uint32>(State) - static_cast<uint32>(EPackageState::Min)];
		}
		return NumUrgent;
	}

	int32 FPackageDataMonitor::GetNumUrgent(EPackageState InState) const
	{
		check(EPackageState::Min <= InState && InState <= EPackageState::Max);
		return NumUrgentInState[static_cast<uint32>(InState) - static_cast<uint32>(EPackageState::Min)];
	}

	int32 FPackageDataMonitor::GetNumPreloadAllocated() const
	{
		return NumPreloadAllocated;
	}

	int32 FPackageDataMonitor::GetNumInProgress() const
	{
		return NumInProgress;
	}

	int32 FPackageDataMonitor::GetNumCooked() const
	{
		return NumCooked;
	}

	void FPackageDataMonitor::OnInProgressChanged(FPackageData& PackageData, bool bInProgress)
	{
		NumInProgress += bInProgress ? 1 : -1;
		check(NumInProgress >= 0);
	}

	void FPackageDataMonitor::OnPreloadAllocatedChanged(FPackageData& PackageData, bool bPreloadAllocated)
	{
		NumPreloadAllocated += bPreloadAllocated ? 1 : -1;
		check(NumPreloadAllocated >= 0);
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
		check(EPackageState::Min <= State && State <= EPackageState::Max);
		NumUrgentInState[static_cast<uint32>(State) - static_cast<uint32>(EPackageState::Min)] += Delta;
		check(NumUrgentInState[static_cast<uint32>(State) - static_cast<uint32>(EPackageState::Min)] >= 0);
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

	FRequestQueue& FPackageDatas::GetRequestQueue()
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
		if (FileName.IsNone())
		{
			// This can happen if PackageName is a script package
			return nullptr;
		}
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
				PendingCookedPlatformDatas.RemoveAtSwap(Index, 1 /* Count */, false /* bAllowShrinking */);
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

	void FPackageDatas::RemapTargetPlatforms(const TMap<ITargetPlatform*, ITargetPlatform*>& Remap)
	{
		for (FPackageData* PackageData : PackageDatas)
		{
			PackageData->RemapTargetPlatforms(Remap);
		}
		for (FPendingCookedPlatformData& CookedPlatformData : PendingCookedPlatformDatas)
		{
			CookedPlatformData.RemapTargetPlatforms(Remap);
		}
	}

	void FRequestQueue::Empty()
	{
		NormalRequests.Empty();
		UrgentRequests.Empty();
	}

	bool FRequestQueue::IsEmpty() const
	{
		return Num() == 0;
	}

	uint32 FRequestQueue::Num() const
	{
		return NormalRequests.Num() + UrgentRequests.Num();
	}

	bool FRequestQueue::Contains(const FPackageData* InPackageData) const
	{
		FPackageData* PackageData = const_cast<FPackageData*>(InPackageData);
		return NormalRequests.Contains(PackageData) || UrgentRequests.Contains(PackageData);
	}

	uint32 FRequestQueue::RemoveRequest(FPackageData* PackageData)
	{
		uint32 OriginalNum = NormalRequests.Num() + UrgentRequests.Num();
		NormalRequests.Remove(PackageData);
		UrgentRequests.Remove(PackageData);
		uint32 Result = OriginalNum - (NormalRequests.Num() + UrgentRequests.Num());
		check(Result == 0 || Result == 1);
		return Result;
	}

	uint32 FRequestQueue::Remove(FPackageData* PackageData)
	{
		return RemoveRequest(PackageData);
	}

	FPackageData* FRequestQueue::PopRequest()
	{
		for (FPackageData* PackageData : UrgentRequests)
		{
			UrgentRequests.Remove(PackageData);
			return PackageData;
		}
		for (FPackageData* PackageData : NormalRequests)
		{
			NormalRequests.Remove(PackageData);
			return PackageData;
		}
		return nullptr;
	}

	void FRequestQueue::AddRequest(FPackageData* PackageData, bool bForceUrgent)
	{
		if (bForceUrgent || PackageData->GetIsUrgent())
		{
			UrgentRequests.Add(PackageData);
		}
		else
		{
			NormalRequests.Add(PackageData);
		}
	}

	bool FLoadPrepareQueue::IsEmpty()
	{
		return Num() == 0;
	}

	int32 FLoadPrepareQueue::Num() const
	{
		return PreloadingQueue.Num() + EntryQueue.Num();
	}

	FPackageData* FLoadPrepareQueue::PopFront()
	{
		if (!PreloadingQueue.IsEmpty())
		{
			return PreloadingQueue.PopFrontValue();
		}
		else
		{
			return EntryQueue.PopFrontValue();
		}
	}

	void FLoadPrepareQueue::Add(FPackageData* PackageData)
	{
		EntryQueue.Add(PackageData);
	}

	void FLoadPrepareQueue::AddFront(FPackageData* PackageData)
	{
		PreloadingQueue.AddFront(PackageData);
	}

	bool FLoadPrepareQueue::Contains(const FPackageData* PackageData) const
	{
		return (Algo::Find(PreloadingQueue, PackageData) != nullptr) || (Algo::Find(EntryQueue, PackageData) != nullptr);
	}

	uint32 FLoadPrepareQueue::Remove(FPackageData* PackageData)
	{
		return PreloadingQueue.Remove(PackageData) + EntryQueue.Remove(PackageData);
	}

	FPoppedPackageDataScope::FPoppedPackageDataScope(FPackageData& InPackageData)
#if COOK_CHECKSLOW_PACKAGEDATA
		: PackageData(InPackageData)
#endif
	{
	}

#if COOK_CHECKSLOW_PACKAGEDATA
	FPoppedPackageDataScope::~FPoppedPackageDataScope()
	{
		PackageData.CheckInContainer();
	}
#endif

}
}