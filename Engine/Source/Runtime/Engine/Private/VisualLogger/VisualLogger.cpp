// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisualLogger/VisualLogger.h"
#include "Misc/CoreMisc.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleInterface.h"
#include "Misc/CommandLine.h"
#include "Serialization/CustomVersion.h"
#include "Modules/ModuleManager.h"
#include "AI/NavDataGenerator.h"
#include "AI/NavigationSystemBase.h"
#include "Framework/Docking/TabManager.h"
#include "VisualLogger/VisualLoggerBinaryFileDevice.h"
#include "VisualLogger/VisualLoggerTraceDevice.h"
#include "VisualLogger/VisualLoggerDebugSnapshotInterface.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif


DEFINE_LOG_CATEGORY(LogVisual);
#if ENABLE_VISUAL_LOG
DEFINE_STAT(STAT_VisualLog);

namespace
{
	UWorld* GetWorldForVisualLogger(const UObject* Object)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Object, EGetWorldErrorMode::ReturnNull) : nullptr;
#if WITH_EDITOR
		UEditorEngine *EEngine = Cast<UEditorEngine>(GEngine);
		if (GIsEditor && EEngine != nullptr && World == nullptr)
		{
			// lets use PlayWorld during PIE/Simulate and regular world from editor otherwise, to draw debug information
			World = EEngine->PlayWorld != nullptr ? ToRawPtr(EEngine->PlayWorld) : EEngine->GetEditorWorldContext().World();
		}

#endif
		if (!GIsEditor && World == nullptr && GEngine)
		{
			World =  GEngine->GetWorld();
		}

		return World;
	}
}

TMap<const UWorld*, FVisualLogger::FOwnerToChildrenRedirectionMap> FVisualLogger::WorldToRedirectionMap;
int32 FVisualLogger::bIsRecording = false;
FVisualLogger::FNavigationDataDump FVisualLogger::NavigationDataDumpDelegate;

bool FVisualLogger::CheckVisualLogInputInternal(const UObject* Object, const FName& CategoryName, ELogVerbosity::Type Verbosity, UWorld **World, FVisualLogEntry **CurrentEntry)
{
	if (FVisualLogger::IsRecording() == false || !Object || !GEngine || GEngine->bDisableAILogging || Object->HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	FVisualLogger& VisualLogger = FVisualLogger::Get();
	if (VisualLogger.IsBlockedForAllCategories() && VisualLogger.IsCategoryAllowed(CategoryName) == false)
	{
		return false;
	}

	*World = GEngine->GetWorldFromContextObject(Object, EGetWorldErrorMode::ReturnNull);
	if (ensure(*World != nullptr) == false)
	{
		return false;
	}

	*CurrentEntry = VisualLogger.GetEntryToWrite(Object, VisualLogger.GetTimeStampForObject(Object));
	if (*CurrentEntry == nullptr)
	{
		return false;
	}

	return true;
}

float FVisualLogger::GetTimeStampForObject(const UObject* Object) const
{
	if (GetTimeStampFunc)
	{
		return GetTimeStampFunc(Object);
	}

	if (const UWorld* World = GEngine->GetWorldFromContextObject(Object, EGetWorldErrorMode::ReturnNull))
	{
		return World->TimeSeconds;
	}

	return 0.0f;
}

void FVisualLogger::SetGetTimeStampFunc(const TFunction<float(const UObject*)> Function)
{
	GetTimeStampFunc = Function;
}

void FVisualLogger::AddClassToAllowList(UClass& InClass)
{
	ClassAllowList.AddUnique(&InClass);
}

bool FVisualLogger::IsClassAllowed(const UClass& InClass) const
{
	for (const UClass* AllowedRoot : ClassAllowList)
	{
		if (InClass.IsChildOf(AllowedRoot))
		{
			return true;
		}
	}

	return false;
}

void FVisualLogger::AddObjectToAllowList(const UObject& InObject)
{
	const int32 PrevNum = ObjectAllowList.Num();
	ObjectAllowList.Add(&InObject);

	const bool bChanged = (PrevNum != ObjectAllowList.Num());
	if (bChanged)
	{
		FVisualLogEntry* CurrentEntry = CurrentEntryPerObject.Find(&InObject);
		if (CurrentEntry)
		{
			CurrentEntry->bPassedObjectAllowList = true;
			CurrentEntry->UpdateAllowedToLog();
		}
	}
}

void FVisualLogger::ClearObjectAllowList()
{
	for (FObjectKey It : ObjectAllowList)
	{
		FVisualLogEntry* CurrentEntry = CurrentEntryPerObject.Find(It);
		if (CurrentEntry)
		{
			CurrentEntry->bPassedObjectAllowList = false;
			CurrentEntry->UpdateAllowedToLog();
		}
	}

	ObjectAllowList.Empty();
}

bool FVisualLogger::IsObjectAllowed(const UObject* InObject) const
{
	return ObjectAllowList.Contains(InObject);
}

FVisualLogEntry* FVisualLogger::GetLastEntryForObject(const UObject* Object)
{
	const UObject* LogOwner = FVisualLogger::FindRedirection(Object);
	return CurrentEntryPerObject.Contains(LogOwner) ? &CurrentEntryPerObject[LogOwner] : nullptr;
}

FVisualLogEntry* FVisualLogger::GetEntryToWrite(const UObject* Object, const float TimeStamp, const ECreateIfNeeded ShouldCreate)
{
	const UObject* const LogOwner = FindRedirection(Object);
	if (LogOwner == nullptr)
	{
		return nullptr;
	}

	// Entry can be created or reused (after being flushed) and will need to be initialized
	bool bInitializeEntry = false;
	FVisualLogEntry* CurrentEntry = nullptr;

	if (CurrentEntryPerObject.Contains(LogOwner))
	{
		CurrentEntry = &CurrentEntryPerObject[LogOwner];

		// We serialize and reinitialize entries only when allowed to log and parameter
		// indicates that new entry can be created. Otherwise we simply return current entry.  
		if (CurrentEntry->bIsAllowedToLog && ShouldCreate == ECreateIfNeeded::Create)
		{
			// Same LogOwner can be used for logs at different time in the frame so need to flush entry right away
			// Other entries will be flushed in batch using FlushEntries
			if (CurrentEntry->bIsInitialized && TimeStamp > CurrentEntry->TimeStamp)
			{
				FlushEntry(*CurrentEntry, LogOwner);
			}
	
			bInitializeEntry = !CurrentEntry->bIsInitialized;
		}
	}
	else if (ShouldCreate == ECreateIfNeeded::Create)
	{
		// It's first and only one usage of LogOwner as regular object to get names. We assume once that LogOwner is correct here and only here.
		CurrentEntry = &CurrentEntryPerObject.Add(LogOwner);

		const UWorld* World = GetWorldForVisualLogger(LogOwner);
		const bool bIsStandalone = (World == nullptr || World->GetNetMode() == NM_Standalone);
		const FName LogName(*FString::Printf(TEXT("%s%s%s"),
			bIsStandalone ? TEXT("") : *FString::Printf(TEXT("(%s) "), *ToString(World->GetNetMode())),
			*LogOwner->GetName(),
			bForceUniqueLogNames ? *FString::Printf(TEXT(" [%d]"), LogOwner->GetUniqueID()) : TEXT("")));

		ObjectToNameMap.Add(LogOwner, LogName);
		ObjectToClassNameMap.Add(LogOwner, *(LogOwner->GetClass()->GetName()));
		ObjectToWorldMap.Add(LogOwner, World);

		// IsClassAllowed isn't super fast, but this gets calculated only once for every 
		// object trying to log something
		CurrentEntry->bPassedClassAllowList = (ClassAllowList.Num() == 0) || IsClassAllowed(*LogOwner->GetClass()) || IsClassAllowed(*Object->GetClass());
		CurrentEntry->bPassedObjectAllowList = IsObjectAllowed(LogOwner);
		CurrentEntry->UpdateAllowedToLog();

		bInitializeEntry = CurrentEntry->bIsAllowedToLog;
	}

	if (bInitializeEntry)
	{
		checkf(CurrentEntry != nullptr, TEXT("bInitializeEntry can only be true when CurrentEntry is valid."));
		CurrentEntry->Reset();
		CurrentEntry->TimeStamp = TimeStamp;
		CurrentEntry->bIsInitialized = true;

		if (const AActor* ObjectAsActor = Cast<AActor>(LogOwner))
		{
			CurrentEntry->Location = ObjectAsActor->GetActorLocation();
			CurrentEntry->bIsLocationValid = true;
		}

		FOwnerToChildrenRedirectionMap& RedirectionMap = GetRedirectionMap(LogOwner);
		if (RedirectionMap.Contains(LogOwner))
		{
			if (const IVisualLoggerDebugSnapshotInterface* DebugSnapshotInterface = Cast<const IVisualLoggerDebugSnapshotInterface>(LogOwner))
			{
				DebugSnapshotInterface->GrabDebugSnapshot(CurrentEntry);
			}
			for (TWeakObjectPtr<const UObject>& Child : RedirectionMap[LogOwner])
			{
				if (const IVisualLoggerDebugSnapshotInterface* DebugSnapshotInterface = Cast<const IVisualLoggerDebugSnapshotInterface>(Child.Get()))
				{
					DebugSnapshotInterface->GrabDebugSnapshot(CurrentEntry);
				}
			}
		}
		else
		{
			if (const IVisualLoggerDebugSnapshotInterface* DebugSnapshotInterface = Cast<const IVisualLoggerDebugSnapshotInterface>(LogOwner))
			{
				DebugSnapshotInterface->GrabDebugSnapshot(CurrentEntry);
			}
		}
	}

	if (CurrentEntry != nullptr && CurrentEntry->bIsAllowedToLog)
	{
		bIsFlushRequired = true;
	}
	else
	{
		CurrentEntry = nullptr;
	}

	return CurrentEntry;
}

void FVisualLogger::Tick(float DeltaTime)
{
	if (bIsFlushRequired)
	{
		Flush();
		bIsFlushRequired = false;
	}
}

void FVisualLogger::Flush()
{
	for (auto &CurrentEntry : CurrentEntryPerObject)
	{
		if (CurrentEntry.Value.bIsInitialized)
		{
			FlushEntry(CurrentEntry.Value, CurrentEntry.Key);
		}
	}
}

void FVisualLogger::FlushEntry(FVisualLogEntry& Entry, const FObjectKey& ObjectKey)
{
	ensureMsgf(Entry.bIsInitialized, TEXT("FlushEntry should only be called with an initialized entry."));

	const UObject* OwnerObject = ObjectKey.ResolveObjectPtrEvenIfPendingKill();
	for (FVisualLogDevice* Device : OutputDevices)
	{
		Device->Serialize(OwnerObject, ObjectToNameMap[ObjectKey], ObjectToClassNameMap[ObjectKey], Entry);
	}
	Entry.Reset();
}

void FVisualLogger::EventLog(const UObject* Object, const FName EventTag1, const FVisualLogEventBase& Event1, const FVisualLogEventBase& Event2, const FVisualLogEventBase& Event3, const FVisualLogEventBase& Event4, const FVisualLogEventBase& Event5, const FVisualLogEventBase& Event6)
{
	EventLog(Object, EventTag1, Event1, Event2, Event3, Event4, Event5);
	EventLog(Object, EventTag1, Event6);
}


void FVisualLogger::EventLog(const UObject* Object, const FName EventTag1, const FVisualLogEventBase& Event1, const FVisualLogEventBase& Event2, const FVisualLogEventBase& Event3, const FVisualLogEventBase& Event4, const FVisualLogEventBase& Event5)
{
	EventLog(Object, EventTag1, Event1, Event2, Event3, Event4);
	EventLog(Object, EventTag1, Event5);
}


void FVisualLogger::EventLog(const UObject* Object, const FName EventTag1, const FVisualLogEventBase& Event1, const FVisualLogEventBase& Event2, const FVisualLogEventBase& Event3, const FVisualLogEventBase& Event4)
{
	EventLog(Object, EventTag1, Event1, Event2, Event3);
	EventLog(Object, EventTag1, Event4);
}


void FVisualLogger::EventLog(const UObject* Object, const FName EventTag1, const FVisualLogEventBase& Event1, const FVisualLogEventBase& Event2, const FVisualLogEventBase& Event3)
{
	EventLog(Object, EventTag1, Event1, Event2);
	EventLog(Object, EventTag1, Event3);
}


void FVisualLogger::EventLog(const UObject* Object, const FName EventTag1, const FVisualLogEventBase& Event1, const FVisualLogEventBase& Event2)
{
	EventLog(Object, EventTag1, Event1);
	EventLog(Object, EventTag1, Event2);
}


void FVisualLogger::EventLog(const UObject* LogOwner, const FVisualLogEventBase& Event1, const FName EventTag1, const FName EventTag2, const FName EventTag3, const FName EventTag4, const FName EventTag5, const FName EventTag6)
{
	EventLog(LogOwner, EventTag1, Event1, EventTag2, EventTag3, EventTag4, EventTag5, EventTag6);
}


void FVisualLogger::EventLog(const UObject* Object, const FName EventTag1, const FVisualLogEventBase& Event, const FName EventTag2, const FName EventTag3, const FName EventTag4, const FName EventTag5, const FName EventTag6)
{
	SCOPE_CYCLE_COUNTER(STAT_VisualLog);
	UWorld *World = nullptr;
	FVisualLogEntry *CurrentEntry = nullptr;
	const FName CategoryName(*Event.Name);
	if (CheckVisualLogInputInternal(Object, CategoryName, ELogVerbosity::Log, &World, &CurrentEntry) == false)
	{
		return;
	}

	int32 Index = CurrentEntry->Events.Find(FVisualLogEvent(Event));
	if (Index != INDEX_NONE)
	{
		CurrentEntry->Events[Index].Counter++;
	}
	else
	{
		Index = CurrentEntry->AddEvent(Event);
	}

	CurrentEntry->Events[Index].EventTags.FindOrAdd(EventTag1)++;
	CurrentEntry->Events[Index].EventTags.FindOrAdd(EventTag2)++;
	CurrentEntry->Events[Index].EventTags.FindOrAdd(EventTag3)++;
	CurrentEntry->Events[Index].EventTags.FindOrAdd(EventTag4)++;
	CurrentEntry->Events[Index].EventTags.FindOrAdd(EventTag5)++;
	CurrentEntry->Events[Index].EventTags.FindOrAdd(EventTag6)++;
	CurrentEntry->Events[Index].EventTags.Remove(NAME_None);
}

void FVisualLogger::NavigationDataDump(const UObject* Object, const FLogCategoryBase& Category, const ELogVerbosity::Type Verbosity, const FBox& Box)
{
	NavigationDataDump(Object, Category.GetCategoryName(), Verbosity, Box);
}

void FVisualLogger::NavigationDataDump(const UObject* Object, const FName& CategoryName, const ELogVerbosity::Type Verbosity, const FBox& Box)
{
	SCOPE_CYCLE_COUNTER(STAT_VisualLog);

	UWorld* World = nullptr;
	FVisualLogEntry* CurrentEntry = nullptr;
	if (CheckVisualLogInputInternal(Object, CategoryName, Verbosity, &World, &CurrentEntry) == false
		|| CurrentEntry == nullptr)
	{
		return;
	}

	NavigationDataDumpDelegate.Broadcast(Object, CategoryName, Verbosity, Box, *World, *CurrentEntry);
}

FVisualLogger& FVisualLogger::Get()
{
	static FVisualLogger GVisLog;
	return GVisLog;
}

FVisualLogger::FVisualLogger()
{
	bForceUniqueLogNames = true;
	bIsRecordingToFile = false;
	bIsRecordingToTrace = false;
	bIsFlushRequired = false;

	BlockAllCategories(false);
	AddDevice(&FVisualLoggerBinaryFileDevice::Get());
	SetIsRecording(GEngine ? !!GEngine->bEnableVisualLogRecordingOnStart : false);
	SetIsRecordingOnServer(false);

	if (FParse::Param(FCommandLine::Get(), TEXT("EnableAILogging")))
	{
		SetIsRecording(true);
		SetIsRecordingToFile(true);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("VisualLogger"), 0.0f, [this](const float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FVisualLogger_Tick);

		Tick(DeltaTime);

		return true;
	});
}

void FVisualLogger::Shutdown()
{
	SetIsRecording(false);
	SetIsRecordingToFile(false);
	RemoveDevice(&FVisualLoggerBinaryFileDevice::Get());
}

void FVisualLogger::Cleanup(UWorld* OldWorld, const bool bReleaseMemory)
{
	const bool WasRecordingToFile = IsRecordingToFile();
	if (WasRecordingToFile)
	{
		SetIsRecordingToFile(false);
	}

	Flush();
	for (FVisualLogDevice* Device : FVisualLogger::Get().OutputDevices)
	{
		Device->Cleanup(bReleaseMemory);
	}

	if (OldWorld != nullptr)
	{
		// perform cleanup only if provided world is valid and was registered
		if (WorldToRedirectionMap.Remove(OldWorld))
		{
		    if (WorldToRedirectionMap.Num() == 0)
            {
                WorldToRedirectionMap.Reset();
                ObjectToWorldMap.Reset();
                ChildToOwnerMap.Reset();
                CurrentEntryPerObject.Reset();
                ObjectToNameMap.Reset();
                ObjectToClassNameMap.Reset();
            }
            else
            {
                for (auto It = ObjectToWorldMap.CreateIterator(); It; ++It)
                {
                    if (It.Value() == OldWorld)
                    {
						FObjectKey Obj = It.Key();
                        ObjectToWorldMap.Remove(Obj);
                        CurrentEntryPerObject.Remove(Obj);
                        ObjectToNameMap.Remove(Obj);
                        ObjectToClassNameMap.Remove(Obj);
                    }
                }

                for (FChildToOwnerRedirectionMap::TIterator It = ChildToOwnerMap.CreateIterator(); It; ++It)
                {
					UObject* Object = It->Key.ResolveObjectPtrEvenIfPendingKill();
					if (Object == nullptr || Object->GetWorld() == OldWorld)
                    {
                        It.RemoveCurrent();
                    }
                }
            }
		}
	}
	else
	{
		WorldToRedirectionMap.Reset();
		ObjectToWorldMap.Reset();
		ChildToOwnerMap.Reset();
		CurrentEntryPerObject.Reset();
		ObjectToNameMap.Reset();
		ObjectToClassNameMap.Reset();
	}

	LastUniqueIds.Reset();

	if (WasRecordingToFile)
	{
		SetIsRecordingToFile(true);
	}
}

int32 FVisualLogger::GetUniqueId(const float Timestamp)
{
	return LastUniqueIds.FindOrAdd(Timestamp)++;
}

FVisualLogger::FOwnerToChildrenRedirectionMap& FVisualLogger::GetRedirectionMap(const UObject* InObject)
{
	const UWorld* World = nullptr;
	if (FVisualLogger::Get().ObjectToWorldMap.Contains(InObject))
	{
		World = FVisualLogger::Get().ObjectToWorldMap[InObject].Get();
	}

	if (World == nullptr)
	{
		World = GetWorldForVisualLogger(nullptr);
	}

	return WorldToRedirectionMap.FindOrAdd(World);
}

void FVisualLogger::Redirect(const UObject* FromObject, const UObject* ToObject)
{
	if (FromObject == ToObject || FromObject == nullptr || ToObject == nullptr)
	{
		return;
	}

	const TWeakObjectPtr<const UObject> FromWeakPtr(FromObject);
	UObject* OldRedirection = FindRedirection(FromObject);
	UObject* NewRedirection = FindRedirection(ToObject);

	if (OldRedirection != NewRedirection)
	{
		FOwnerToChildrenRedirectionMap& OwnerToChildrenMap = GetRedirectionMap(FromObject);

		TArray<TWeakObjectPtr<const UObject>>* OldArray = OwnerToChildrenMap.Find(OldRedirection);
		if (OldArray)
		{
			OldArray->RemoveSingleSwap(FromWeakPtr);
		}

		OwnerToChildrenMap.FindOrAdd(NewRedirection).AddUnique(FromWeakPtr);
	}

	FChildToOwnerRedirectionMap& ChildToOwnerMap = FVisualLogger::Get().GetChildToOwnerRedirectionMap();
	ChildToOwnerMap.FindOrAdd(FromWeakPtr.Get(true/*bEvenIfPendingKill*/)) = ToObject;

	UE_CVLOG(FromObject != nullptr, FromObject, LogVisual, Log, TEXT("Redirected '%s' to '%s'"), *FromObject->GetName(), *NewRedirection->GetName());
}

UObject* FVisualLogger::FindRedirection(const UObject* Object)
{
	FChildToOwnerRedirectionMap& Map = FVisualLogger::Get().GetChildToOwnerRedirectionMap();

	TWeakObjectPtr<const UObject> TargetWeakPtr(Object);
	TWeakObjectPtr<const UObject>* Parent = &TargetWeakPtr;

	while (Parent)
	{
		Parent = Map.Find(TargetWeakPtr.Get(/*bEvenIfPendingKill*/true));
		if (Parent)
		{
			if (Parent->IsValid())
			{
				TargetWeakPtr = *Parent;
			}
			else
			{
				Parent = nullptr;
				Map.Remove(TargetWeakPtr.Get(/*bEvenIfPendingKill*/true));
			}
		}
	}

	const UObject* const Target = TargetWeakPtr.Get(/*bEvenIfPendingKill*/true);
	return const_cast<UObject*>(Target);
}

void FVisualLogger::SetIsRecording(const bool InIsRecording)
{
	if (InIsRecording == false && InIsRecording != !!bIsRecording && FParse::Param(FCommandLine::Get(), TEXT("LogNavOctree")))
	{
		FVisualLogger::NavigationDataDump(GetWorldForVisualLogger(nullptr), LogNavigation, ELogVerbosity::Log, FBox());
	}
	if (IsRecordingToFile())
	{
		SetIsRecordingToFile(false);
	}
	bIsRecording = InIsRecording;
};

void FVisualLogger::SetIsRecordingToFile(const bool InIsRecording)
{
	if (!bIsRecording && InIsRecording)
	{
		SetIsRecording(true);
	}

	UWorld* World = GEngine ? GEngine->GetWorld() : nullptr;

	const FString BaseFileName = LogFileNameGetter.IsBound() ? LogFileNameGetter.Execute() : TEXT("VisualLog");
	const FString MapName = World ? World->GetMapName() : TEXT("");

	const FString OutputFileName = FString::Printf(TEXT("%s_%s"), *BaseFileName, *MapName);

	if (bIsRecordingToFile && !InIsRecording)
	{
		for (FVisualLogDevice* Device : OutputDevices)
		{
			if (Device->HasFlags(EVisualLoggerDeviceFlags::CanSaveToFile))
			{
				Device->SetFileName(OutputFileName);
				Device->StopRecordingToFile(World ? World->TimeSeconds : StartRecordingToFileTime);
			}
		}
	}
	else if (!bIsRecordingToFile && InIsRecording)
	{
		StartRecordingToFileTime = World ? World->TimeSeconds : 0;
		for (FVisualLogDevice* Device : OutputDevices)
		{
			if (Device->HasFlags(EVisualLoggerDeviceFlags::CanSaveToFile))
			{
				Device->StartRecordingToFile(StartRecordingToFileTime);
			}
		}
	}

	bIsRecordingToFile = InIsRecording;
}

void FVisualLogger::SetIsRecordingToTrace(const bool InIsRecording)
{
	if (!bIsRecording && InIsRecording)
	{
		SetIsRecording(true);
	}

	FVisualLoggerTraceDevice& Device = FVisualLoggerTraceDevice::Get();
	if (bIsRecordingToTrace && !InIsRecording)
	{
		Device.StopRecordingToFile(0.0);
		RemoveDevice(&Device);
	}
	else if (!bIsRecordingToTrace && InIsRecording)
	{
		Device.StartRecordingToFile(0.0);
		AddDevice(&Device);
	}

	bIsRecordingToTrace = InIsRecording;
}


void FVisualLogger::DiscardRecordingToFile()
{
	if (bIsRecordingToFile)
	{
		for (FVisualLogDevice* Device : OutputDevices)
		{
			if (Device->HasFlags(EVisualLoggerDeviceFlags::CanSaveToFile))
			{
				Device->DiscardRecordingToFile();
			}
		}

		bIsRecordingToFile = false;
	}
}

bool FVisualLogger::IsCategoryLogged(const FLogCategoryBase& Category) const
{
	if ((GEngine && GEngine->bDisableAILogging) || IsRecording() == false)
	{
		return false;
	}

	const FName CategoryName = Category.GetCategoryName();
	if (IsBlockedForAllCategories() && IsCategoryAllowed(CategoryName) == false)
	{
		return false;
	}

	return true;
}

#endif //ENABLE_VISUAL_LOG

const FGuid EVisualLoggerVersion::GUID = FGuid(0xA4237A36, 0xCAEA41C9, 0x8FA218F8, 0x58681BF3);
FCustomVersionRegistration GVisualLoggerVersion(EVisualLoggerVersion::GUID, EVisualLoggerVersion::LatestVersion, TEXT("VisualLogger"));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

class FLogVisualizerExec : private FSelfRegisteringExec
{
public:
	/** Console commands, see embedded usage statement **/
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		if (FParse::Command(&Cmd, TEXT("VISLOG")))
		{
			if (FModuleManager::Get().LoadModulePtr<IModuleInterface>("LogVisualizer") != nullptr)
			{
#if ENABLE_VISUAL_LOG
				const FString Command = FParse::Token(Cmd, /*UseEscape*/ false);
				if (Command == TEXT("record"))
				{
					FVisualLogger::Get().SetIsRecording(true);
					return true;
				}
				else if (Command == TEXT("stop"))
				{
					FVisualLogger::Get().SetIsRecording(false);
					return true;
				}
				else if (Command == TEXT("disableallbut"))
				{
					const FString Category = FParse::Token(Cmd, /*UseEscape*/ true);
					FVisualLogger::Get().BlockAllCategories(true);
					FVisualLogger::Get().AddCategoryToAllowList(*Category);
					return true;
				}
#if WITH_EDITOR
				else
				{
					FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("VisualLogger")));
					return true;
				}
#endif
#else
			UE_LOG(LogVisual, Warning, TEXT("Unable to open LogVisualizer - logs are disabled"));
#endif
			}
		}
#if ENABLE_VISUAL_LOG
		else if (FParse::Command(&Cmd, TEXT("LogNavOctree")))
		{
			FVisualLogger::NavigationDataDump(GetWorldForVisualLogger(nullptr), LogNavigation, ELogVerbosity::Log, FBox());
		}
#endif
		return false;
	}
} LogVisualizerExec;

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
