// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkClient.h"

#include "Async/Async.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "IMediaModule.h"
#include "LiveLinkAnimationVirtualSubject.h"
#include "LiveLinkLog.h"
#include "LiveLinkPreset.h"
#include "LiveLinkRole.h"
#include "LiveLinkRoleTrait.h"
#include "LiveLinkSettings.h"
#include "LiveLinkSourceCollection.h"
#include "LiveLinkSourceFactory.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkSubject.h"
#include "LiveLinkVirtualSource.h"
#include "IMediaModule.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkBasicTypes.h"
#include "Roles/LiveLinkBasicTypes.h"
#include "Stats/Stats.h"
#include "Stats/Stats2.h"
#include "TimeSynchronizationSource.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"


/**
 * Declare stats to see what takes up time in LiveLink
 */
DECLARE_CYCLE_STAT(TEXT("LiveLink - Push StaticData"), STAT_LiveLink_PushStaticData, STATGROUP_LiveLink);
DECLARE_CYCLE_STAT(TEXT("LiveLink - Push FrameData"), STAT_LiveLink_PushFrameData, STATGROUP_LiveLink);
DECLARE_CYCLE_STAT(TEXT("LiveLink - Client - Tick"), STAT_LiveLink_Client_Tick, STATGROUP_LiveLink);
DECLARE_CYCLE_STAT(TEXT("LiveLink - EvaluateFrame"), STAT_LiveLink_EvaluateFrame, STATGROUP_LiveLink);

DEFINE_LOG_CATEGORY(LogLiveLink);

static TAutoConsoleVariable<int32> CVarMaxNewStaticDataPerUpdate(
	TEXT("LiveLink.Client.MaxNewStaticDataPerUpdate"),
	64,
	TEXT("Maximun number of new static data that can be added in a single UE4 frame."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMaxNewFrameDataPerUpdate(
	TEXT("LiveLink.Client.MaxNewFrameDataPerUpdate"),
	64,
	TEXT("Maximun number of new frame data that can be added in a single UE4 frame."),
	ECVF_Default);


FLiveLinkClient::FLiveLinkClient()
{
#if WITH_EDITOR
	CachedEngineTime = 0.0;
#endif

	Collection = MakeUnique<FLiveLinkSourceCollection>();
	FCoreDelegates::OnPreExit.AddRaw(this, &FLiveLinkClient::Shutdown);

	IMediaModule& MediaModule = FModuleManager::LoadModuleChecked<IMediaModule>("Media");
	MediaModule.GetOnTickPreEngineCompleted().AddRaw(this, &FLiveLinkClient::Tick);
}

FLiveLinkClient::~FLiveLinkClient()
{
	FCoreDelegates::OnPreExit.RemoveAll(this);
	Shutdown();
}

void FLiveLinkClient::Tick()
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_Client_Tick);

	FScopeLock Lock(&CollectionAccessCriticalSection);
	DoPendingWork();
	CacheValues();
	UpdateSources();
	BuildThisTicksSubjectSnapshot();

	OnLiveLinkTickedDelegate.Broadcast();
}

void FLiveLinkClient::DoPendingWork()
{
	check(Collection);

	// Remove Sources and Subjects
	Collection->RemovePendingKill();

	{
		// Add new Subject static data
		for (FPendingSubjectStatic& SubjectStatic : SubjectStaticToPush)
		{
			PushSubjectStaticData_Internal(MoveTemp(SubjectStatic));
		}
		SubjectStaticToPush.Reset();

		// Add new Subject frame data
		for (FPendingSubjectFrame& SubjectFrame : SubjectFrameToPush)
		{
			PushSubjectFrameData_Internal(MoveTemp(SubjectFrame));
		}
		SubjectFrameToPush.Reset();
	}
}

void FLiveLinkClient::UpdateSources()
{
	for (FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
#if WITH_EDITOR
		if (SourceItem.Setting)
		{
			SourceItem.Setting->SourceDebugInfos.Reset();
		}
#endif

		SourceItem.Source->Update();
	}
}

void FLiveLinkClient::BuildThisTicksSubjectSnapshot()
{
	check(Collection);

	EnabledSubjects.Reset();

	// Update the Live Subject before the Virtual Subject
	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if (FLiveLinkSubject* LiveSubject = SubjectItem.GetLiveSubject())
		{
			if (SubjectItem.bEnabled)
			{
				LiveSubject->CacheSettings(GetSourceSettings(SubjectItem.Key.Source), SubjectItem.GetLinkSettings());
				LiveSubject->Update();
				EnabledSubjects.Add(SubjectItem.Key.SubjectName, SubjectItem.Key);
			}
			else
			{
				LiveSubject->ClearFrames();
			}
		}
	}

	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if (ULiveLinkVirtualSubject* VSubject = SubjectItem.GetVirtualSubject())
		{
			if (SubjectItem.bEnabled)
			{
				VSubject->Update();
				EnabledSubjects.Add(SubjectItem.Key.SubjectName, SubjectItem.Key);
			}
			else
			{
				VSubject->ClearFrames();
			}
		}
	}
}

void FLiveLinkClient::CacheValues()
{
#if WITH_EDITOR
	CachedEngineTime = FApp::GetCurrentTime();
	CachedEngineFrameTime = FApp::GetCurrentFrameTime();
#endif
}

void FLiveLinkClient::Shutdown()
{
	if(IMediaModule* MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media"))
	{
		MediaModule->GetOnTickPreEngineCompleted().RemoveAll(this);
	}

	if (Collection)
	{
		double Timeout = 2.0;
		GConfig->GetDouble(TEXT("LiveLink"), TEXT("ClientShutdownTimeout"), Timeout, GGameIni);

		const double StartShutdownSeconds = FPlatformTime::Seconds();
		bool bContinue = true;
		while(bContinue)
		{
			FScopeLock Lock(&CollectionAccessCriticalSection);
			bContinue = !Collection->RequestShutdown();

			if (FPlatformTime::Seconds() - StartShutdownSeconds > Timeout)
			{
				bContinue = false;
				UE_LOG(LogLiveLink, Warning, TEXT("Force shutdown LiveLink after %f seconds. One or more sources refused to shutdown."), Timeout);
			}
		}
	}
}

FGuid FLiveLinkClient::AddSource(TSharedPtr<ILiveLinkSource> InSource)
{
	check(Collection);

	FGuid Guid;
	if (Collection->FindSource(InSource) == nullptr)
	{
		Guid = FGuid::NewGuid();

		FLiveLinkCollectionSourceItem Data;
		Data.Guid = Guid;
		Data.Source = InSource;
		{
			UClass* SourceSettingsClass = InSource->GetSettingsClass().Get();
			UClass* SettingsClass = SourceSettingsClass ? SourceSettingsClass : ULiveLinkSourceSettings::StaticClass();
			Data.Setting = NewObject<ULiveLinkSourceSettings>(GetTransientPackage(), SettingsClass);
		}
		Collection->AddSource(Data);

		InSource->ReceiveClient(this, Data.Guid);
		InSource->InitializeSettings(Data.Setting);
	}
	return Guid;
}

FGuid FLiveLinkClient::AddVirtualSubjectSource(FName SourceName)
{
	check(Collection);

	FGuid Guid;

	if (Collection->FindVirtualSource(SourceName) == nullptr)
	{
		TSharedPtr<FLiveLinkVirtualSubjectSource> Source = MakeShared<FLiveLinkVirtualSubjectSource>();
		Guid = FGuid::NewGuid();

		FLiveLinkCollectionSourceItem Data;
		Data.Guid = Guid;
		Data.Source = Source;

		ULiveLinkVirtualSubjectSourceSettings* NewSettings = NewObject<ULiveLinkVirtualSubjectSourceSettings>(GetTransientPackage(), ULiveLinkVirtualSubjectSourceSettings::StaticClass());
		NewSettings->SourceName = SourceName;
		Data.Setting = NewSettings;
		Data.bIsVirtualSource = true;
		Collection->AddSource(Data);

		Source->ReceiveClient(this, Data.Guid);
		Source->InitializeSettings(Data.Setting);
	}
	else
	{
		FLiveLinkLog::Warning(TEXT("The virtual subject Source '%s' could not be created. It already exists."), *SourceName.ToString());
	}

	return Guid;
}

bool FLiveLinkClient::CreateSource(const FLiveLinkSourcePreset& InSourcePreset)
{
	check(Collection);

	if (InSourcePreset.Settings == nullptr)
	{
		FLiveLinkLog::Warning(TEXT("Create Source Failure: The settings are not defined."));
		return false;
	}

	if (InSourcePreset.Guid == FLiveLinkSourceCollection::DefaultVirtualSubjectGuid)
	{
		FLiveLinkLog::Warning(TEXT("Create Source Failure: Can't create default virtual subject source. It will be created automatically."));
		return false;
	}

	if (!InSourcePreset.Guid.IsValid())
	{
		FLiveLinkLog::Warning(TEXT("Create Source Failure: The guid is invalid."));
		return false;
	}

	if (Collection->FindSource(InSourcePreset.Guid) != nullptr)
	{
		FLiveLinkLog::Warning(TEXT("Create Source Failure: The guid already exist."));
		return false;
	}

	FLiveLinkCollectionSourceItem Data;
	Data.Guid = InSourcePreset.Guid;

	// Virtual subject source have a special settings class. We can differentiate them using this
	if (InSourcePreset.Settings->GetClass() == ULiveLinkVirtualSubjectSourceSettings::StaticClass())
	{
		Data.Source = MakeShared<FLiveLinkVirtualSubjectSource>();
		Data.bIsVirtualSource = true;
	}
	else
	{
		if (InSourcePreset.Settings->Factory.Get() == nullptr || InSourcePreset.Settings->Factory.Get() == ULiveLinkSourceFactory::StaticClass())
		{
			FLiveLinkLog::Warning(TEXT("Create Source Failure: The factory is not defined."));
			return false;
		}

		Data.Source = InSourcePreset.Settings->Factory.Get()->GetDefaultObject<ULiveLinkSourceFactory>()->CreateSource(InSourcePreset.Settings->ConnectionString);
		if (!Data.Source.IsValid())
		{
			FLiveLinkLog::Warning(TEXT("Create Source Failure: The source couldn't be created by the factory."));
			return false;
		}
	}
	
	Data.Setting = DuplicateObject<ULiveLinkSourceSettings>(InSourcePreset.Settings, GetTransientPackage());
	
	Collection->AddSource(Data);

	Data.Source->ReceiveClient(this, Data.Guid);
	Data.Source->InitializeSettings(Data.Setting);

	return true;
}

void FLiveLinkClient::RemoveSource(TSharedPtr<ILiveLinkSource> InSource)
{
	check(Collection);
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSource))
	{
		SourceItem->bPendingKill = true;
	}
}

void FLiveLinkClient::RemoveSource(FGuid InEntryGuid)
{
	check(Collection);
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		SourceItem->bPendingKill = true;
	}
}

void FLiveLinkClient::RemoveAllSources()
{
	check(Collection);
	for (FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
		SourceItem.bPendingKill = true;
	}
}

bool FLiveLinkClient::HasSourceBeenAdded(TSharedPtr<ILiveLinkSource> InSource) const
{
	check(Collection);
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSource))
	{
		return !SourceItem->bPendingKill;
	}
	return false;
}

TArray<FGuid> FLiveLinkClient::GetSources() const
{
	check(Collection);

	TArray<FGuid> Result;
	for (const FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
		if (!SourceItem.bPendingKill && !SourceItem.IsVirtualSource())
		{
			Result.Add(SourceItem.Guid);
		}
	}
	return Result;
}

TArray<FGuid> FLiveLinkClient::GetVirtualSources() const
{
	check(Collection);

	TArray<FGuid> Result;
	for (const FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
		if (!SourceItem.bPendingKill && SourceItem.IsVirtualSource())
		{
			Result.Add(SourceItem.Guid);
		}
	}
	return Result;
}

FLiveLinkSourcePreset FLiveLinkClient::GetSourcePreset(FGuid InSourceGuid, UObject* InDuplicatedObjectOuter) const
{
	check(Collection);

	UObject* DuplicatedObjectOuter = InDuplicatedObjectOuter ? InDuplicatedObjectOuter : GetTransientPackage();

	FLiveLinkSourcePreset SourcePreset;
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSourceGuid))
	{
		if (SourceItem->Guid != FLiveLinkSourceCollection::DefaultVirtualSubjectGuid && SourceItem->Setting && SourceItem->Source)
		{
			SourcePreset.Guid = SourceItem->Guid;
			SourcePreset.SourceType = SourceItem->Source->CanBeDisplayedInUI() ? SourceItem->Source->GetSourceType() : FText::GetEmpty();
			SourcePreset.Settings = DuplicateObject<ULiveLinkSourceSettings>(SourceItem->Setting, DuplicatedObjectOuter);
		}
	}
	return SourcePreset;
}

void FLiveLinkClient::PushSubjectStaticData_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, FLiveLinkStaticDataStruct&& InStaticData)
{
	FPendingSubjectStatic SubjectStatic{ InSubjectKey, InRole, MoveTemp(InStaticData) };
	const int32 MaxNumBufferToCached = CVarMaxNewStaticDataPerUpdate.GetValueOnAnyThread();
	bool bLogError = false;
	{
		FScopeLock Lock(&CollectionAccessCriticalSection);
		if (SubjectStaticToPush.Num() > MaxNumBufferToCached) // Something is wrong somewhere. Warn the user and discard the new Static Data.
		{
			bLogError = true;
		}
		else
		{
			SubjectStaticToPush.Add(MoveTemp(SubjectStatic));
		}
	}

	if (bLogError)
	{
		static const FName NAME_TooManyStatic = "LiveLinkClient_TooManyStatic";
		FLiveLinkLog::ErrorOnce(NAME_TooManyStatic, FLiveLinkSubjectKey(), TEXT("Trying to add more than %d static subjects in the same frame. New Subjects will be discarded."), MaxNumBufferToCached);
	}
}

void FLiveLinkClient::PushSubjectStaticData_Internal(FPendingSubjectStatic&& SubjectStaticData)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_PushStaticData);

	check(Collection);

	if (!FLiveLinkRoleTrait::Validate(SubjectStaticData.Role, SubjectStaticData.StaticData))
	{
		if (SubjectStaticData.Role == nullptr)
		{
			FLiveLinkLog::Error(TEXT("Trying to add unsupported static data type with subject '%s'."), *SubjectStaticData.SubjectKey.SubjectName.ToString());
		}
		else
		{
			FLiveLinkLog::Error(TEXT("Trying to add unsupported static data type to role '%s' with subject '%s'."), *SubjectStaticData.Role->GetName(), *SubjectStaticData.SubjectKey.SubjectName.ToString());
		}
		return;
	}

	bool bShouldLogIfInvalidStaticData = true;
	if (!SubjectStaticData.Role.GetDefaultObject()->IsStaticDataValid(SubjectStaticData.StaticData, bShouldLogIfInvalidStaticData))
	{
		if (bShouldLogIfInvalidStaticData)
		{
			FLiveLinkLog::Error(TEXT("Trying to add static data that is not formatted properly to role '%s' with subject '%s'."), *SubjectStaticData.Role->GetName(), *SubjectStaticData.SubjectKey.SubjectName.ToString());
		}
		return;
	}
	
	const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(SubjectStaticData.SubjectKey.Source);
	if (SourceItem == nullptr || SourceItem->bPendingKill)
	{
		return;
	}

	if (SourceItem->IsVirtualSource())
	{
		FLiveLinkLog::Error(TEXT("Trying to add frame data to the virtual subject '%s'."), *SubjectStaticData.Role->GetName(), *SubjectStaticData.SubjectKey.SubjectName.ToString());
		return;
	}

	FLiveLinkSubject* LiveLinkSubject = nullptr;
	if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(SubjectStaticData.SubjectKey))
	{
		LiveLinkSubject = SubjectItem->GetLiveSubject();

		if (LiveLinkSubject->GetRole() != SubjectStaticData.Role)
		{
			FLiveLinkLog::Warning(TEXT("Subject '%s' of role '%s' is changing its role to '%s'. Current subject will be removed and a new one will be created"), *SubjectStaticData.SubjectKey.SubjectName.ToString(), *LiveLinkSubject->GetRole().GetDefaultObject()->GetDisplayName().ToString(), *SubjectStaticData.Role.GetDefaultObject()->GetDisplayName().ToString());

			Collection->RemoveSubject(SubjectStaticData.SubjectKey);
			LiveLinkSubject = nullptr;
		}
		else
		{
			LiveLinkSubject->ClearFrames();
		}
	}

	//Clear any pending frame for that subject. This will enforce one frame delay between reception of static data and frame data but will ensure both matches, especially in the case of deprecated path
	for (int32 Index = SubjectFrameToPush.Num() - 1; Index >= 0; --Index)
	{
		FPendingSubjectFrame& PendingFrame = SubjectFrameToPush[Index];
		if (PendingFrame.SubjectKey == SubjectStaticData.SubjectKey)
		{
			SubjectFrameToPush.RemoveAtSwap(Index);
		}
	}

	if(LiveLinkSubject == nullptr)
	{
		const ULiveLinkSettings* LiveLinkSettings = GetDefault<ULiveLinkSettings>();
		const FLiveLinkRoleProjectSetting DefaultSetting = LiveLinkSettings->GetDefaultSettingForRole(SubjectStaticData.Role.Get());

		// Setting class should always be valid
		ULiveLinkSubjectSettings* SubjectSettings = nullptr;
		{
			UClass* SettingClass = DefaultSetting.SettingClass.Get();
			if (SettingClass == nullptr)
			{
				SettingClass = ULiveLinkSubjectSettings::StaticClass();
			}

			SubjectSettings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage(), SettingClass);
			SubjectSettings->Role = SubjectStaticData.Role;

			UClass* FrameInterpolationProcessorClass = DefaultSetting.FrameInterpolationProcessor.Get();
			if (FrameInterpolationProcessorClass != nullptr)
			{
				UClass* InterpolationRole = FrameInterpolationProcessorClass->GetDefaultObject<ULiveLinkFrameInterpolationProcessor>()->GetRole();
				if (SubjectStaticData.Role->IsChildOf(InterpolationRole))
				{
					SubjectSettings->InterpolationProcessor = NewObject<ULiveLinkFrameInterpolationProcessor>(SubjectSettings, FrameInterpolationProcessorClass);
				}
				else
				{
					FLiveLinkLog::Warning(TEXT("The interpolator '%s' is not valid for the Role '%s'"), *FrameInterpolationProcessorClass->GetName(), *SubjectStaticData.Role->GetName());
				}
			}
			else
			{
				// If no settings were found for a specific role, check if the default interpolator is compatible with the role
				UClass* FallbackInterpolationProcessorClass = LiveLinkSettings->FrameInterpolationProcessor.Get();
				if (FallbackInterpolationProcessorClass != nullptr)
				{
					UClass* InterpolationRole = FallbackInterpolationProcessorClass->GetDefaultObject<ULiveLinkFrameInterpolationProcessor>()->GetRole();
					if (SubjectStaticData.Role->IsChildOf(InterpolationRole))
					{
						SubjectSettings->InterpolationProcessor = NewObject<ULiveLinkFrameInterpolationProcessor>(SubjectSettings, FallbackInterpolationProcessorClass);
					}
				}
			}

			for (TSubclassOf<ULiveLinkFramePreProcessor> PreProcessor : DefaultSetting.FramePreProcessors)
			{
				if (PreProcessor != nullptr)
				{
					UClass* PreProcessorRole = PreProcessor->GetDefaultObject<ULiveLinkFramePreProcessor>()->GetRole();
					if (SubjectStaticData.Role->IsChildOf(PreProcessorRole))
					{
						SubjectSettings->PreProcessors.Add(NewObject<ULiveLinkFramePreProcessor>(SubjectSettings, PreProcessor.Get()));
					}
					else
					{
						FLiveLinkLog::Warning(TEXT("The pre processor '%s' is not valid for the Role '%s'"), *PreProcessor->GetName(), *SubjectStaticData.Role->GetName());
					}
				}
			}
		}

		bool bEnabled = Collection->FindEnabledSubject(SubjectStaticData.SubjectKey.SubjectName) == nullptr;
		FLiveLinkCollectionSubjectItem CollectionSubjectItem(SubjectStaticData.SubjectKey, MakeUnique<FLiveLinkSubject>(), SubjectSettings, bEnabled);
		CollectionSubjectItem.GetLiveSubject()->Initialize(SubjectStaticData.SubjectKey, SubjectStaticData.Role.Get(), this);

		LiveLinkSubject = CollectionSubjectItem.GetLiveSubject();

		Collection->AddSubject(MoveTemp(CollectionSubjectItem));
	}

	if (LiveLinkSubject)
	{
		if (const FSubjectFramesReceivedHandles* Handles = SubjectFrameReceivedHandles.Find(SubjectStaticData.SubjectKey.SubjectName))
		{
			Handles->OnStaticDataReceived.Broadcast(SubjectStaticData.SubjectKey, SubjectStaticData.Role, SubjectStaticData.StaticData);
		}

		LiveLinkSubject->SetStaticData(SubjectStaticData.Role, MoveTemp(SubjectStaticData.StaticData));
	}
}

void FLiveLinkClient::PushSubjectFrameData_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, FLiveLinkFrameDataStruct&& InFrameData)
{
	FPendingSubjectFrame SubjectFrame{ InSubjectKey, MoveTemp(InFrameData) };
	const int32 MaxNumBufferToCached = CVarMaxNewFrameDataPerUpdate.GetValueOnAnyThread();
	bool bLogError = false;
	{
		FScopeLock Lock(&CollectionAccessCriticalSection);
		if (SubjectFrameToPush.Num() > MaxNumBufferToCached) // Something is wrong somewhere. Warn the user and discard the new Static Data.
		{
			bLogError = true;
			SubjectFrameToPush.RemoveAt(0, SubjectFrameToPush.Num() - MaxNumBufferToCached, false);
		}
		else
		{
			SubjectFrameToPush.Add(MoveTemp(SubjectFrame));
		}
	}

	if (bLogError)
	{
		static const FName NAME_TooManyFrame = "LiveLinkClient_TooManyFrame";
		FLiveLinkLog::InfoOnce(NAME_TooManyFrame, FLiveLinkSubjectKey(), TEXT("Trying to add more than %d frames in the same frame. Oldest frames will be discarded."), MaxNumBufferToCached);
	}
}

void FLiveLinkClient::PushSubjectFrameData_Internal(FPendingSubjectFrame&& SubjectFrameData)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_PushFrameData);

	check(Collection);

	const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(SubjectFrameData.SubjectKey.Source);
	if (SourceItem == nullptr || SourceItem->bPendingKill)
	{
		return;
	}

	//To add a frame data, we need to find our subject but also have a static data associated to it. 
	//With presets, the subject could exist but no static data received yet.
	FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(SubjectFrameData.SubjectKey);
	if (SubjectItem == nullptr)
	{
		return;
	}

	if (!SubjectItem->bEnabled || SubjectItem->bPendingKill)
	{
		return;
	}
	
	FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject();
	if (LinkSubject == nullptr)
	{
		FLiveLinkLog::Error(TEXT("The Subject is not allowed to push to a virtual subject."));
		return;
	}
	
	
	if (!LinkSubject->HasStaticData())
	{
		return;
	}

	const TSubclassOf<ULiveLinkRole> Role = LinkSubject->GetRole();
	if (Role == nullptr)
	{
		return;
	}

	bool bShouldLogWarning = true;
	if (!Role.GetDefaultObject()->IsFrameDataValid(LinkSubject->GetStaticData(), SubjectFrameData.FrameData, bShouldLogWarning))
	{
		if (bShouldLogWarning)
		{
			static const FName NAME_InvalidFrameData = "LiveLinkClient_InvalidFrameData";
			FLiveLinkLog::ErrorOnce(NAME_InvalidFrameData, SubjectFrameData.SubjectKey, TEXT("Trying to add frame data that is not formatted properly to role '%s' with subject '%s'."), *Role->GetName(), *SubjectFrameData.SubjectKey.SubjectName.ToString());
		}
		return;
	}

	if (const FSubjectFramesReceivedHandles* Handles = SubjectFrameReceivedHandles.Find(SubjectFrameData.SubjectKey.SubjectName))
	{
		Handles->OnFrameDataReceived.Broadcast(SubjectItem->Key, Role, SubjectFrameData.FrameData);
	}

	LinkSubject->AddFrameData(MoveTemp(SubjectFrameData.FrameData));
}

bool FLiveLinkClient::CreateSubject(const FLiveLinkSubjectPreset& InSubjectPreset)
{
	check(Collection);

	if (InSubjectPreset.Role.Get() == nullptr || InSubjectPreset.Role.Get() == ULiveLinkRole::StaticClass())
	{
		FLiveLinkLog::Warning(TEXT("Create Subject Failure: The role is not defined."));
		return false;
	}

	if (InSubjectPreset.Key.Source == FLiveLinkSourceCollection::DefaultVirtualSubjectGuid && InSubjectPreset.VirtualSubject == nullptr)
	{
		FLiveLinkLog::Warning(TEXT("Create Source Failure: Can't create an empty virtual subject."));
		return false;
	}

	if (InSubjectPreset.Key.SubjectName.IsNone())
	{
		FLiveLinkLog::Warning(TEXT("Create Subject Failure: The subject name is invalid."));
		return false;
	}

	FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSubjectPreset.Key.Source);
	if (SourceItem == nullptr || SourceItem->bPendingKill)
	{
		FLiveLinkLog::Warning(TEXT("Create Subject Failure: The source doesn't exist."));
		return false;
	}

	FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectPreset.Key);
	if (SubjectItem != nullptr)
	{
		FScopeLock Lock(&CollectionAccessCriticalSection);
		if (SubjectItem->bPendingKill)
		{
			Collection->RemoveSubject(InSubjectPreset.Key);
		}
		else
		{
			FLiveLinkLog::Warning(TEXT("Create Subject Failure: The subject already exist."));
			return false;
		}
	}

	if (InSubjectPreset.VirtualSubject)
	{
		bool bEnabled = false;
		ULiveLinkVirtualSubject* VSubject = DuplicateObject<ULiveLinkVirtualSubject>(InSubjectPreset.VirtualSubject, GetTransientPackage());
		FLiveLinkCollectionSubjectItem VSubjectData(InSubjectPreset.Key, VSubject, bEnabled);
		VSubject->Initialize(VSubjectData.Key, VSubject->GetRole(), this);

		FScopeLock Lock(&CollectionAccessCriticalSection);
		Collection->AddSubject(MoveTemp(VSubjectData));
		Collection->SetSubjectEnabled(InSubjectPreset.Key, InSubjectPreset.bEnabled);
	}
	else
	{
		ULiveLinkSubjectSettings* SubjectSettings = nullptr;
		if (InSubjectPreset.Settings)
		{
			SubjectSettings = DuplicateObject<ULiveLinkSubjectSettings>(InSubjectPreset.Settings, GetTransientPackage());
		}
		else
		{
			SubjectSettings = NewObject<ULiveLinkSubjectSettings>();
		}

		bool bEnabled = false;
		FLiveLinkCollectionSubjectItem CollectionSubjectItem(InSubjectPreset.Key, MakeUnique<FLiveLinkSubject>(), SubjectSettings, bEnabled);
		CollectionSubjectItem.GetLiveSubject()->Initialize(InSubjectPreset.Key, InSubjectPreset.Role.Get(), this);

		FScopeLock Lock(&CollectionAccessCriticalSection);
		Collection->AddSubject(MoveTemp(CollectionSubjectItem));
		Collection->SetSubjectEnabled(InSubjectPreset.Key, InSubjectPreset.bEnabled);
	}
	return true;
}

void FLiveLinkClient::RemoveSubject_AnyThread(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			SubjectItem->bPendingKill = true;
		}
	}
}

bool FLiveLinkClient::AddVirtualSubject(FLiveLinkSubjectKey InVirtualSubjectKey, TSubclassOf<ULiveLinkVirtualSubject> InVirtualSubjectClass)
{
	bool bResult = false;

	if (Collection && !InVirtualSubjectKey.SubjectName.IsNone() && InVirtualSubjectClass != nullptr)
	{
		FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InVirtualSubjectKey.Source);
		if (SourceItem == nullptr || SourceItem->bPendingKill)
		{
			FLiveLinkLog::Warning(TEXT("Create Virtual Subject Failure: The source doesn't exist."));
		}
		else
		{
			FScopeLock Lock(&CollectionAccessCriticalSection);
			const bool bFoundVirtualSubject = nullptr != Collection->GetSubjects().FindByPredicate(
				[InVirtualSubjectKey](const FLiveLinkCollectionSubjectItem& Other)
			{
				return Other.Key == InVirtualSubjectKey && Other.GetVirtualSubject() != nullptr;
			});

			if (!bFoundVirtualSubject)
			{
				ULiveLinkVirtualSubject* VSubject = NewObject<ULiveLinkVirtualSubject>(GetTransientPackage(), InVirtualSubjectClass.Get());
				const bool bDoEnableSubject = Collection->FindEnabledSubject(InVirtualSubjectKey.SubjectName) == nullptr;
				FLiveLinkCollectionSubjectItem VSubjectData(InVirtualSubjectKey, VSubject, bDoEnableSubject);

				VSubject->Initialize(VSubjectData.Key, VSubject->GetRole(), this);
				Collection->AddSubject(MoveTemp(VSubjectData));

				bResult = true;
			}
			else
			{
				FLiveLinkLog::Warning(TEXT("The virtual subject '%s' could not be created."), *InVirtualSubjectKey.SubjectName.Name.ToString());
			}
		}
	}

	return bResult;
}

void FLiveLinkClient::RemoveVirtualSubject(FLiveLinkSubjectKey InVirtualSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);
	if (Collection)
	{
		Collection->RemoveSubject(InVirtualSubjectKey);
	}
}

void FLiveLinkClient::ClearSubjectsFrames_AnyThread(FLiveLinkSubjectName InSubjectName)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	// Use the subject enabled for at this frame
	if (FLiveLinkSubjectKey* SubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		ClearSubjectsFrames_AnyThread(*SubjectKey);
	}
}

void FLiveLinkClient::ClearSubjectsFrames_AnyThread(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			SubjectItem->GetSubject()->ClearFrames();
		}
	}
}

void FLiveLinkClient::ClearAllSubjectsFrames_AnyThread()
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
		{
			SubjectItem.GetSubject()->ClearFrames();
		}
	}
}

FLiveLinkSubjectPreset FLiveLinkClient::GetSubjectPreset(const FLiveLinkSubjectKey& InSubjectKey, UObject* InDuplicatedObjectOuter) const
{
	UObject* DuplicatedObjectOuter = InDuplicatedObjectOuter ? InDuplicatedObjectOuter : GetTransientPackage();

	FLiveLinkSubjectPreset SubjectPreset;
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		SubjectPreset.Key = SubjectItem->Key;
		SubjectPreset.Role = SubjectItem->GetSubject()->GetRole();
		SubjectPreset.bEnabled = SubjectItem->bEnabled;
		if (SubjectItem->GetVirtualSubject() != nullptr)
		{
			SubjectPreset.VirtualSubject = DuplicateObject<ULiveLinkVirtualSubject>(SubjectItem->GetVirtualSubject(), DuplicatedObjectOuter);
		}
		else
		{
			SubjectPreset.Settings = DuplicateObject<ULiveLinkSubjectSettings>(SubjectItem->GetLinkSettings(), DuplicatedObjectOuter);
		}
	}
	return SubjectPreset;
}

TArray<FLiveLinkSubjectKey> FLiveLinkClient::GetSubjects(bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const
{
	TArray<FLiveLinkSubjectKey> SubjectEntries;
	SubjectEntries.Reserve(Collection->GetSubjects().Num());

	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if ((SubjectItem.bEnabled || bIncludeDisabledSubject) && (bIncludeVirtualSubject || SubjectItem.GetVirtualSubject() == nullptr))
		{
			SubjectEntries.Add(SubjectItem.Key);
		}
	}

	return SubjectEntries;
}

bool FLiveLinkClient::IsSubjectValid(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		if (SubjectItem->GetSubject()->HasValidFrameSnapshot())
		{
			if (FLiveLinkSubject* LiveSubject = SubjectItem->GetLiveSubject())
			{
				return (FApp::GetCurrentTime() - LiveSubject->GetLastPushTime()) < GetDefault<ULiveLinkSettings>()->GetTimeWithoutFrameToBeConsiderAsInvalid();
			}
		}
	}
	return false;
}

bool FLiveLinkClient::IsSubjectValid(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		return IsSubjectValid(*FoundSubjectKey);
	}
	return false;
}

bool FLiveLinkClient::IsSubjectEnabled(const FLiveLinkSubjectKey& InSubjectKey, bool bForThisFrame) const
{
	if (bForThisFrame)
	{
		if (const FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectKey.SubjectName))
		{
			return *FoundSubjectKey == InSubjectKey;
		}
		return false;
	}
	return Collection->IsSubjectEnabled(InSubjectKey);
}

bool FLiveLinkClient::IsSubjectEnabled(FLiveLinkSubjectName InSubjectName) const
{
	return EnabledSubjects.Find(InSubjectName) != nullptr;
}

void FLiveLinkClient::SetSubjectEnabled(const FLiveLinkSubjectKey& InSubjectKey, bool bInEnabled)
{
	Collection->SetSubjectEnabled(InSubjectKey, bInEnabled);
}

bool FLiveLinkClient::IsSubjectTimeSynchronized(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->IsTimeSynchronized();
		}
	}
	return false;
}

bool FLiveLinkClient::IsSubjectTimeSynchronized(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->IsTimeSynchronized();
		}
	}
	return false;
}

TSubclassOf<ULiveLinkRole> FLiveLinkClient::GetSubjectRole(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSubject()->GetRole();
	}

	return TSubclassOf<ULiveLinkRole>();
}

TSubclassOf<ULiveLinkRole> FLiveLinkClient::GetSubjectRole(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		return SubjectItem->GetSubject()->GetRole();
	}

	return TSubclassOf<ULiveLinkRole>();
}

bool FLiveLinkClient::DoesSubjectSupportsRole(const FLiveLinkSubjectKey& InSubjectKey, TSubclassOf<ULiveLinkRole> InSupportedRole) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSubject()->SupportsRole(InSupportedRole);
	}

	return false;
}

TArray<FLiveLinkTime> FLiveLinkClient::GetSubjectFrameTimes(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSubject()->GetFrameTimes();
	}
	return TArray<FLiveLinkTime>();
}

TArray<FLiveLinkTime> FLiveLinkClient::GetSubjectFrameTimes(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		return SubjectItem->GetSubject()->GetFrameTimes();
	}

	return TArray<FLiveLinkTime>();
}

TArray<FLiveLinkSubjectKey> FLiveLinkClient::GetSubjectsSupportingRole(TSubclassOf<ULiveLinkRole> InSupportedRole, bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const
{
	TArray<FLiveLinkSubjectKey> SubjectKeys;
	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if (SubjectItem.GetSubject()->SupportsRole(InSupportedRole))
		{
			if ((SubjectItem.bEnabled || bIncludeDisabledSubject) && (bIncludeVirtualSubject || SubjectItem.GetVirtualSubject() == nullptr))
			{
				SubjectKeys.Add(SubjectItem.Key);
			}
		}
	}
	return SubjectKeys;
}

bool FLiveLinkClient::EvaluateFrameFromSource_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
	}

	return false;
}

bool FLiveLinkClient::EvaluateFrame_AnyThread(FLiveLinkSubjectName InSubjectName, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	bool bResult = false;

	// Used the cached enabled list
	if (FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(*FoundSubjectKey))
		{
			bResult = SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
		}

#if WITH_EDITOR
		if (OnLiveLinkSubjectEvaluated().IsBound())
		{
			FLiveLinkTime RequestedTime = FLiveLinkTime(CachedEngineTime, CachedEngineFrameTime.Get(FQualifiedFrameTime()));
			FLiveLinkTime ResultTime;
			if (bResult)
			{
				ResultTime = FLiveLinkTime(OutFrame.FrameData.GetBaseData()->WorldTime.GetOffsettedTime(), OutFrame.FrameData.GetBaseData()->MetaData.SceneTime);
			}
			OnLiveLinkSubjectEvaluated().Broadcast(*FoundSubjectKey, InDesiredRole, RequestedTime, bResult, ResultTime);
		}
#endif //WITH_EDITOR
	}
	else
	{
		UE_LOG(LogLiveLink, Verbose, TEXT("Subject '%s' is not enabled or doesn't exist"), *InSubjectName.ToString());
	}

	return bResult;
}

bool FLiveLinkClient::EvaluateFrameAtWorldTime_AnyThread(FLiveLinkSubjectName InSubjectName, double InWorldTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	bool bResult = false;

	// Used the cached enabled list
	if (FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(*FoundSubjectKey))
		{
			if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
			{
				bResult = LinkSubject->EvaluateFrameAtWorldTime(InWorldTime, InDesiredRole, OutFrame);
			}
			else
			{
				bResult = SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
			}

#if WITH_EDITOR
			if (OnLiveLinkSubjectEvaluated().IsBound())
			{
				FLiveLinkTime RequestedTime = FLiveLinkTime(InWorldTime, FQualifiedFrameTime());
				FLiveLinkTime ResultTime;
				if (bResult)
				{
					ResultTime = FLiveLinkTime(OutFrame.FrameData.GetBaseData()->WorldTime.GetOffsettedTime(), OutFrame.FrameData.GetBaseData()->MetaData.SceneTime);
				}
				OnLiveLinkSubjectEvaluated().Broadcast(*FoundSubjectKey, InDesiredRole, RequestedTime, bResult, ResultTime);
			}
#endif //WITH_EDITOR
		}
	}
	else
	{
		UE_LOG(LogLiveLink, Verbose, TEXT("Subject '%s' is not enabled or doesn't exist"), *InSubjectName.ToString());
	}

	return bResult;
}

bool FLiveLinkClient::EvaluateFrameAtSceneTime_AnyThread(FLiveLinkSubjectName InSubjectName, const FQualifiedFrameTime& InSceneTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	bool bResult = false;

	// Used the cached enabled list
	if (FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(*FoundSubjectKey))
		{
			if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
			{
				bResult = LinkSubject->EvaluateFrameAtSceneTime(InSceneTime, InDesiredRole, OutFrame);
			}
			else
			{
				bResult = SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
			}

#if WITH_EDITOR
			if (OnLiveLinkSubjectEvaluated().IsBound())
			{
				FLiveLinkTime RequestedTime = FLiveLinkTime(0.0, InSceneTime);
				FLiveLinkTime ResultTime;
				if (bResult)
				{
					ResultTime = FLiveLinkTime(OutFrame.FrameData.GetBaseData()->WorldTime.GetOffsettedTime(), OutFrame.FrameData.GetBaseData()->MetaData.SceneTime);
				}
				OnLiveLinkSubjectEvaluated().Broadcast(*FoundSubjectKey, InDesiredRole, RequestedTime, bResult, ResultTime);
			}
#endif //WITH_EDITOR
		}
	}
	else
	{
		UE_LOG(LogLiveLink, Verbose, TEXT("Subject '%s' is not enabled or doesn't exist"), *InSubjectName.ToString());
	}

	return bResult;
}

FSimpleMulticastDelegate& FLiveLinkClient::OnLiveLinkTicked()
{
	return OnLiveLinkTickedDelegate;
}

TArray<FGuid> FLiveLinkClient::GetDisplayableSources() const
{
	TArray<FGuid> Results;

	const TArray<FLiveLinkCollectionSourceItem>& PresetSources = Collection->GetSources();
	Results.Reserve(PresetSources.Num());

	for (const FLiveLinkCollectionSourceItem& Data : PresetSources)
	{
		if (Data.Source->CanBeDisplayedInUI())
		{
			Results.Add(Data.Guid);
		}
	}

	return Results;
}

FLiveLinkSubjectTimeSyncData FLiveLinkClient::GetTimeSyncData(FLiveLinkSubjectName InSubjectName)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->GetTimeSyncData();
		}
	}

	return FLiveLinkSubjectTimeSyncData();
}

FText FLiveLinkClient::GetSourceType(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->GetSourceType();
	}
	return FText(NSLOCTEXT("TempLocTextLiveLink", "InvalidSourceType", "Invalid Source Type"));
}

FText FLiveLinkClient::GetSourceMachineName(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->GetSourceMachineName();
	}
	return FText(NSLOCTEXT("TempLocTextLiveLink", "InvalidSourceMachineName", "Invalid Source Machine Name"));
}

FText FLiveLinkClient::GetSourceStatus(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->GetSourceStatus();
	}
	return FText(NSLOCTEXT("TempLocTextLiveLink", "InvalidSourceStatus", "Invalid Source Status"));
}

bool FLiveLinkClient::IsSourceStillValid(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->IsSourceStillValid();
	}
	return false;
}

bool FLiveLinkClient::IsVirtualSubject(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetVirtualSubject() != nullptr;
	}
	return false;
}

void FLiveLinkClient::OnPropertyChanged(FGuid InEntryGuid, const FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		SourceItem->Source->OnSettingsChanged(SourceItem->Setting, InPropertyChangedEvent);
	}
}

ULiveLinkSourceSettings* FLiveLinkClient::GetSourceSettings(const FGuid& InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Setting;
	}
	return nullptr;
}

UObject* FLiveLinkClient::GetSubjectSettings(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSettings();
	}
	return nullptr;
}

bool FLiveLinkClient::RegisterForSubjectFrames(FLiveLinkSubjectName InSubjectName, const FOnLiveLinkSubjectStaticDataReceived::FDelegate& InOnStaticDataReceived, const FOnLiveLinkSubjectFrameDataReceived::FDelegate& InOnFrameDataReceived, FDelegateHandle& OutStaticDataReceivedHandle, FDelegateHandle& OutFrameDataReceivedHandle, TSubclassOf<ULiveLinkRole>& OutSubjectRole, FLiveLinkStaticDataStruct* OutStaticData)
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (SubjectItem->GetSubject()->GetStaticData().IsValid())
		{
			//Register both delegates
			FSubjectFramesReceivedHandles& Handles = SubjectFrameReceivedHandles.FindOrAdd(InSubjectName);
			OutStaticDataReceivedHandle = Handles.OnStaticDataReceived.Add(InOnStaticDataReceived);
			OutFrameDataReceivedHandle = Handles.OnFrameDataReceived.Add(InOnFrameDataReceived);

			//Give back the current static data and role associated to the subject
			OutSubjectRole = SubjectItem->GetSubject()->GetRole();

			//Copy the current static data
			if (OutStaticData)
			{
				OutStaticData->InitializeWith(SubjectItem->GetSubject()->GetStaticData());
			}
		}

		return true;
	}

	return false;
}

void FLiveLinkClient::UnregisterSubjectFramesHandle(FLiveLinkSubjectName InSubjectName, FDelegateHandle InStaticDataReceivedHandle, FDelegateHandle InFrameDataReceivedHandle)
{
	if (FSubjectFramesReceivedHandles* Handles = SubjectFrameReceivedHandles.Find(InSubjectName))
	{
		Handles->OnStaticDataReceived.Remove(InStaticDataReceivedHandle);
		Handles->OnFrameDataReceived.Remove(InFrameDataReceivedHandle);
	}
}

FSimpleMulticastDelegate& FLiveLinkClient::OnLiveLinkSourcesChanged()
{
	return Collection->OnLiveLinkSourcesChanged();
}

FSimpleMulticastDelegate& FLiveLinkClient::OnLiveLinkSubjectsChanged()
{
	return Collection->OnLiveLinkSubjectsChanged();
}


FOnLiveLinkSourceChangedDelegate& FLiveLinkClient::OnLiveLinkSourceAdded()
{
	return Collection->OnLiveLinkSourceAdded();
}

FOnLiveLinkSourceChangedDelegate& FLiveLinkClient::OnLiveLinkSourceRemoved()
{
	return Collection->OnLiveLinkSourceRemoved();
}

FOnLiveLinkSubjectChangedDelegate& FLiveLinkClient::OnLiveLinkSubjectRemoved()
{
	return Collection->OnLiveLinkSubjectRemoved();
}

FOnLiveLinkSubjectChangedDelegate& FLiveLinkClient::OnLiveLinkSubjectAdded()
{
	return Collection->OnLiveLinkSubjectAdded();
}

#if WITH_EDITOR
FOnLiveLinkSubjectEvaluated& FLiveLinkClient::OnLiveLinkSubjectEvaluated()
{
	return OnLiveLinkSubjectEvaluatedDelegate;
}
#endif

PRAGMA_DISABLE_DEPRECATION_WARNINGS
/**
 * Function for Deprecation
 */
void FLiveLinkClient::AquireLock_Deprecation() 
{
	CollectionAccessCriticalSection.Lock();
}

void FLiveLinkClient::ReleaseLock_Deprecation()
{
	CollectionAccessCriticalSection.Unlock();
}

void FLiveLinkClient::ClearFrames_Deprecation(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			SubjectItem->GetSubject()->ClearFrames();
		}
	}
}

FLiveLinkSkeletonStaticData* FLiveLinkClient::GetSubjectAnimationStaticData_Deprecation(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			if (SubjectItem->GetSubject()->GetRole() == ULiveLinkAnimationRole::StaticClass() && !SubjectItem->bPendingKill)
			{
				return SubjectItem->GetSubject()->GetStaticData().Cast<FLiveLinkSkeletonStaticData>();
			}
		}
	}

	return nullptr;
}


/**
 * Function that are now deprecated
 */
const TArray<FGuid>& FLiveLinkClient::GetSourceEntries() const
{
	static TArray<FGuid> CopiedSources;
	CopiedSources = GetSources();
	return CopiedSources;
}

void FLiveLinkClient::AddVirtualSubject(FName InNewVirtualSubjectName)
{
	AddVirtualSubject(InNewVirtualSubjectName, ULiveLinkAnimationVirtualSubject::StaticClass());
}

void FLiveLinkClient::AddVirtualSubject(FLiveLinkSubjectName VirtualSubjectName, TSubclassOf<ULiveLinkVirtualSubject> VirtualSubjectClass)
{
	const FLiveLinkSubjectKey DefaultSubjectKey(FLiveLinkSourceCollection::DefaultVirtualSubjectGuid, VirtualSubjectName);
	AddVirtualSubject(DefaultSubjectKey, ULiveLinkAnimationVirtualSubject::StaticClass());
}

/**
 * Deprecated function
 */
void FLiveLinkClient_Base_DEPRECATED::PushSubjectSkeleton(FGuid SourceGuid, FName SubjectName, const FLiveLinkRefSkeleton& RefSkeleton)
{
	// Backward compatibility with old API. Default to pushing animation data

	FLiveLinkSubjectKey Key = FLiveLinkSubjectKey(SourceGuid, SubjectName);

	RemoveSubject_AnyThread(Key);

	FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
	FLiveLinkSkeletonStaticData* SkeletonData = StaticData.Cast<FLiveLinkSkeletonStaticData>();
	SkeletonData->SetBoneNames(RefSkeleton.GetBoneNames());
	SkeletonData->SetBoneParents(RefSkeleton.GetBoneParents());
	PushSubjectStaticData_AnyThread(Key, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
}

void UpdateForAnimationStatic(TArray<FName>& InOutCurveNames, const TArray<FLiveLinkCurveElement>& InCurveElements)
{
	InOutCurveNames.Reset(InCurveElements.Num());
	for (const FLiveLinkCurveElement& Elem : InCurveElements)
	{
		InOutCurveNames.Add(Elem.CurveName);
	}
}

void FLiveLinkClient_Base_DEPRECATED::PushSubjectData(FGuid InSourceGuid, FName InSubjectName, const FLiveLinkFrameData& InFrameData)
{
	FLiveLinkSubjectKey SubjectKey(InSourceGuid, InSubjectName);

	//Update curve names in the static data for backward compatibility
	int32 NumberOfPropertyNames = 0;
	{
		AquireLock_Deprecation();

		FLiveLinkSkeletonStaticData* AnimationStaticData = GetSubjectAnimationStaticData_Deprecation(SubjectKey);
		if (AnimationStaticData)
		{
			NumberOfPropertyNames = AnimationStaticData->PropertyNames.Num();
			if (NumberOfPropertyNames == 0 && InFrameData.CurveElements.Num() > 0)
			{
				static const FName NAME_UpdateYourCode = "LiveLinkClient_PushSubjectData";
				FLiveLinkLog::WarningOnce(NAME_UpdateYourCode, FLiveLinkSubjectKey(InSourceGuid, InSubjectName), TEXT("Upgrade your code. Curve elements count has changed from the previous frame. That will clear the previous frames of that subject."));

				ClearFrames_Deprecation(SubjectKey);
				UpdateForAnimationStatic(AnimationStaticData->PropertyNames, InFrameData.CurveElements);
				NumberOfPropertyNames = AnimationStaticData->PropertyNames.Num();
			}
		}

		ReleaseLock_Deprecation();
	}

	//Convert incoming data as animation data
	FLiveLinkFrameDataStruct AnimationStruct(FLiveLinkAnimationFrameData::StaticStruct());
	FLiveLinkAnimationFrameData& NewData = *AnimationStruct.Cast<FLiveLinkAnimationFrameData>();
	NewData.MetaData = InFrameData.MetaData;
	NewData.WorldTime = InFrameData.WorldTime;
	NewData.Transforms = InFrameData.Transforms;

	//Always match FrameData property count to StaticData property count.
	//If StaticData has more properties than current FrameData, set non existent properties to Infinity
	//If StaticData has less properties than current FrameData, only use a subset of the incoming properties
	int32 MaxNumberOfProperties = FMath::Min(NumberOfPropertyNames, InFrameData.CurveElements.Num());
	NewData.PropertyValues.SetNumZeroed(NumberOfPropertyNames);
	for (int32 i = 0; i < MaxNumberOfProperties; ++i)
	{
		NewData.PropertyValues[i] = InFrameData.CurveElements[i].CurveValue;
	}
	for (int32 i = MaxNumberOfProperties; i < NumberOfPropertyNames; ++i)
	{
		NewData.PropertyValues[i] = INFINITY;
	}
	PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(AnimationStruct));
}

void FLiveLinkClient_Base_DEPRECATED::ClearSubject(FName InSubjectName)
{
	bool bRemovedSubject = false;
	{
		TArray<FLiveLinkSubjectKey> AllSubjects = GetSubjects(false, true);
		for (const FLiveLinkSubjectKey& SubjectKey : AllSubjects)
		{
			if (SubjectKey.SubjectName == InSubjectName)
			{
				RemoveSubject_AnyThread(SubjectKey);
			}
		}
	}
}

void FLiveLinkClient_Base_DEPRECATED::GetSubjectNames(TArray<FName>& SubjectNames)
{
	TArray<FLiveLinkSubjectKey> SubjectKeys = GetSubjects(false, true);
	SubjectNames.Reset(SubjectKeys.Num());

	for (const FLiveLinkSubjectKey& SubjectKey : SubjectKeys)
	{
		SubjectNames.Add(SubjectKey.SubjectName);
	}
}

const FLiveLinkSubjectFrame* FLiveLinkClient_Base_DEPRECATED::GetSubjectData(FName InSubjectName)
{
	//Old getters default to Animation role and copies data into old data structure
	static const FName NAME_UpdateYourCode = "LiveLinkClient_GetSubjectData";
	FLiveLinkLog::WarningOnce(NAME_UpdateYourCode, FLiveLinkSubjectKey(FGuid(), InSubjectName), TEXT("Upgrade your code. There is no way to deprecate GetSubjectData without creating new memory."));
	return nullptr;
}

const FLiveLinkSubjectFrame* FLiveLinkClient_Base_DEPRECATED::GetSubjectDataAtWorldTime(FName InSubjectName, double InWorldTime)
{
	//Old getters default to Animation role and copies data into old data structure
	static const FName NAME_UpdateYourCode = "LiveLinkClient_GetSubjectDataAtWorldTime";
	FLiveLinkLog::WarningOnce(NAME_UpdateYourCode, FLiveLinkSubjectKey(FGuid(), InSubjectName), TEXT("Upgrade your code. There is no way to deprecate GetSubjectDataAtWorldTime without creating new memory."));
	return nullptr;
}

const FLiveLinkSubjectFrame* FLiveLinkClient_Base_DEPRECATED::GetSubjectDataAtSceneTime(FName InSubjectName, const FTimecode& InTimecode)
{
	//Old getters default to Animation role and copies data into old data structure
	static const FName NAME_UpdateYourCode = "LiveLinkClient_GetSubjectDataAtSceneTime";
	FLiveLinkLog::WarningOnce(NAME_UpdateYourCode, FLiveLinkSubjectKey(FGuid(), InSubjectName), TEXT("Upgrade your code. There is no way to deprecate GetSubjectDataAtSceneTime without creating new memory."));
	return nullptr;
}

bool FLiveLinkClient_Base_DEPRECATED::EvaluateFrameAtSceneTime_AnyThread(FLiveLinkSubjectName SubjectName, const FTimecode& SceneTime, TSubclassOf<ULiveLinkRole> DesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	return static_cast<ILiveLinkClient*>(this)->EvaluateFrameAtSceneTime_AnyThread(SubjectName, FQualifiedFrameTime(SceneTime, FApp::GetTimecodeFrameRate()), DesiredRole, OutFrame);
}

const TArray<FLiveLinkFrame>* FLiveLinkClient_Base_DEPRECATED::GetSubjectRawFrames(FName InSubjectName)
{
	static const FName NAME_UpdateYourCode = "LiveLinkClient_GetSubjectRawFrames";
	FLiveLinkLog::WarningOnce(NAME_UpdateYourCode, FLiveLinkSubjectKey(FGuid(), InSubjectName), TEXT("Upgrade your code. There is no way to deprecate GetSubjectRawFrames without creating new memory."));
	return nullptr;
}

void FLiveLinkClient_Base_DEPRECATED::ClearSubjectsFrames(FName SubjectName)
{
	ClearSubjectsFrames_AnyThread(SubjectName);
}

void FLiveLinkClient_Base_DEPRECATED::ClearAllSubjectsFrames()
{
	ClearAllSubjectsFrames_AnyThread();
}

void FLiveLinkClient_Base_DEPRECATED::AddSourceToSubjectWhiteList(FName SubjectName, FGuid SourceGuid)
{
	SetSubjectEnabled({ SourceGuid, SubjectName }, true);
}

void FLiveLinkClient_Base_DEPRECATED::RemoveSourceFromSubjectWhiteList(FName SubjectName, FGuid SourceGuid)
{
	SetSubjectEnabled({ SourceGuid, SubjectName}, false);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS