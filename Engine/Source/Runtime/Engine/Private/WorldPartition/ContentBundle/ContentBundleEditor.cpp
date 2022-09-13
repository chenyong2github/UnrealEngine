// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundleEditor.h"

#if WITH_EDITOR

#include "WorldPartition/ContentBundle/ContentBundle.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "WorldPartition/ContentBundle/ContentBundleEditorSubsystemInterface.h"
#include "WorldPartition/ContentBundle/ContentBundleLog.h"
#include "PackageTools.h"
#include "ObjectTools.h"
#include "Editor.h"
#include "WorldPartition/ContentBundle/ContentBundleWorldSubsystem.h"

FContentBundleEditor::FContentBundleEditor(TSharedPtr<FContentBundleClient>& InClient, UWorld* InWorld)
	:FContentBundleBase(InClient, InWorld),
	UnsavedActorMonitor(nullptr),
	ExternalStreamingObject(nullptr),
	Guid(FGuid::NewGuid()),
	bIsBeingEdited(false)
{

}


void FContentBundleEditor::DoInitialize()
{
	SetStatus(EContentBundleStatus::Registered);

	if (IContentBundleEditorSubsystemInterface* EditorSubsystem = IContentBundleEditorSubsystemInterface::Get())
	{
		EditorSubsystem->NotifyContentBundleAdded(this);
	}
}

void FContentBundleEditor::DoUninitialize()
{
	if (IContentBundleEditorSubsystemInterface* EditorSubsystem = IContentBundleEditorSubsystemInterface::Get())
	{
		EditorSubsystem->NotifyContentBundleRemoved(this);
	}

	SetStatus(EContentBundleStatus::Unknown);
}

void FContentBundleEditor::DoInjectContent()
{
	FString ActorDescContainerPackage;
	if (BuildContentBundleContainerPackagePath(ActorDescContainerPackage))
	{
		UnsavedActorMonitor = NewObject<UContentBundleUnsavedActorMonitor>(GetTransientPackage(), NAME_None, RF_Transactional);
		UnsavedActorMonitor->Initialize(*this);

		UWorldPartition* WorldPartition = GetInjectedWorld()->GetWorldPartition();
		ActorDescContainer = WorldPartition->RegisterActorDescContainer(FName(*ActorDescContainerPackage));
		if (ActorDescContainer.IsValid())
		{
			UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] ExternalActors in %s found. %u actors were injected"), *GetDescriptor()->GetDisplayName(), *ActorDescContainer->GetExternalActorPath(), ActorDescContainer->GetActorDescCount());

			if (!ActorDescContainer->IsEmpty())
			{
				WorldDataLayersActorReference = FWorldDataLayersReference(ActorDescContainer.Get(), BuildWorlDataLayersName());
				SetStatus(EContentBundleStatus::ContentInjected);
			}
			else
			{
				SetStatus(EContentBundleStatus::ReadyToInject);
			}

			RegisterDelegates();
		}
		else
		{
			UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] Failed to register actor desc container with %s"), *GetDescriptor()->GetDisplayName(), *ActorDescContainerPackage);
			SetStatus(EContentBundleStatus::FailedToInject);
		}
	}
	else
	{
		SetStatus(EContentBundleStatus::FailedToInject);
	}


	BroadcastChanged();
}

void FContentBundleEditor::DoRemoveContent()
{
	UnreferenceAllActors();

	WorldDataLayersActorReference.Reset();

	UnsavedActorMonitor->Uninitialize();

	if (ActorDescContainer.IsValid())
	{
		UnregisterDelegates();

		GetInjectedWorld()->GetWorldPartition()->UnregisterActorDescContainer(ActorDescContainer.Get());
		ActorDescContainer = nullptr;
	}

	SetStatus(EContentBundleStatus::Registered);

	BroadcastChanged();
}

bool FContentBundleEditor::IsValid() const
{
	bool bIsValid = true;

	return bIsValid;
}

bool FContentBundleEditor::AddActor(AActor* InActor)
{
	if (InActor->GetWorld() != ActorDescContainer->GetWorld() || InActor->HasAllFlags(RF_Transient) || !InActor->IsMainPackageActor())
	{
		return false;
	}

	if (InActor->IsA<AWorldDataLayers>())
	{
		return false;
	}

	check(GetStatus() == EContentBundleStatus::ContentInjected);

	FName ActorPackageNameInContentBundle(*ULevel::GetActorPackageName(ActorDescContainer->GetExternalActorPath(), EActorPackagingScheme::Reduced, InActor->GetName()));
	FName ActorPackageName = InActor->GetPackage()->GetFName();
	if (ActorPackageName != ActorPackageNameInContentBundle)
	{
		InActor->GetPackage()->Rename(*ActorPackageNameInContentBundle.ToString());
	}

	UnsavedActorMonitor->MonitorActor(InActor);

	UE_LOG(LogContentBundle, Verbose, TEXT("[CB: %s] Added new actor %s, ActorCount:  %u. Package %s."), *GetDescriptor()->GetDisplayName(), *InActor->GetActorNameOrLabel(), GetActorCount(), *InActor->GetPackage()->GetName());

	return true;
}

bool FContentBundleEditor::ContainsActor(const AActor* InActor) const
{
	if (InActor != nullptr)
	{
		return ActorDescContainer->GetActorDesc(InActor) != nullptr || UnsavedActorMonitor->IsMonitoring(InActor);
	}

	return false;
}

bool FContentBundleEditor::GetActors(TArray<AActor*>& Actors)
{
	Actors.Reserve(GetActorCount());

	for (FActorDescList::TIterator<> It(ActorDescContainer.Get()); It; ++It)
	{
		if (AActor* Actor = It->GetActor())
		{
			if (Actor != WorldDataLayersActorReference.Get())
			{
				Actors.Add(Actor);
			}
		}
	}

	for (auto UnsavedActor : UnsavedActorMonitor->GetUnsavedActors())
	{
		if (UnsavedActor.IsValid())
		{
			Actors.Add(UnsavedActor.Get());
		}
	}

	return !Actors.IsEmpty();
}

bool FContentBundleEditor::HasUserPlacedActors() const
{
	// If there is only one actor in the container its the WorldDataLayer automatically created when injecting base content.
	bool bActorDescContHasUserPlacedActors = ActorDescContainer.IsValid() && ActorDescContainer->GetActorDescCount() > 1;
	return (bActorDescContHasUserPlacedActors || UnsavedActorMonitor->IsMonitoringActors());
}

uint32 FContentBundleEditor::GetActorCount() const
{
	if (GetStatus() == EContentBundleStatus::ContentInjected)
	{
		uint32 UnsavedWorldDataLayerCount = WorldDataLayersActorReference.IsValid() && ActorDescContainer.IsValid() && ActorDescContainer->IsEmpty() ? 1 : 0;
		return ActorDescContainer->GetActorDescCount() + UnsavedActorMonitor->GetActorCount() + UnsavedWorldDataLayerCount;
	}

	return 0;
}

uint32 FContentBundleEditor::GetUnsavedActorAcount() const
{
	if (GetStatus() == EContentBundleStatus::ContentInjected)
	{
		return UnsavedActorMonitor->GetActorCount();
	}

	return 0;
}

void FContentBundleEditor::ReferenceAllActors()
{
	if (ActorDescContainer.IsValid())
	{
		ActorDescContainer->LoadAllActors(ForceLoadedActors);
	}
}

void FContentBundleEditor::UnreferenceAllActors()
{
	ForceLoadedActors.Empty();
}

void FContentBundleEditor::StartEditing()
{
	check(GetStatus() == EContentBundleStatus::ReadyToInject || GetStatus() == EContentBundleStatus::ContentInjected);

	UnsavedActorMonitor->StartListenOnActorEvents();

	bIsBeingEdited = true;
}

void FContentBundleEditor::StopEditing()
{
	check(GetStatus() == EContentBundleStatus::ReadyToInject || GetStatus() == EContentBundleStatus::ContentInjected);

	UnsavedActorMonitor->StopListeningOnActorEvents();

	bIsBeingEdited = false;
}

void FContentBundleEditor::InjectBaseContent()
{
	check(GetStatus() == EContentBundleStatus::ReadyToInject);
	UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] Injecting Base Content"), *GetDescriptor()->GetDisplayName());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = BuildWorlDataLayersName();
	SpawnParameters.OverrideLevel = GetInjectedWorld()->PersistentLevel;
	SpawnParameters.OverridePackage = CreateActorPackage(SpawnParameters.Name);
	SpawnParameters.bCreateActorPackage = false;

	WorldDataLayersActorReference = FWorldDataLayersReference(SpawnParameters);

	WorldDataLayersActorReference->SetActorLabel(GetDisplayName());

	SetStatus(EContentBundleStatus::ContentInjected);

	BroadcastChanged();
}

void FContentBundleEditor::RemoveBaseContent()
{
	check(!HasUserPlacedActors());
	check(GetStatus() == EContentBundleStatus::ContentInjected);
	UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] Removing Base Content"), *GetDescriptor()->GetDisplayName());

	GetInjectedWorld()->DestroyActor(WorldDataLayersActorReference.Get());
	WorldDataLayersActorReference.Reset();

	SetStatus(EContentBundleStatus::ReadyToInject);

	BroadcastChanged();
}

void FContentBundleEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	FContentBundleBase::AddReferencedObjects(Collector);

	Collector.AddReferencedObjects(ContentBundleCells);
	Collector.AddReferencedObject(ExternalStreamingObject);
	Collector.AddReferencedObject(UnsavedActorMonitor);
}

void FContentBundleEditor::GenerateStreaming()
{
	if (GetStatus() != EContentBundleStatus::ContentInjected)
	{
		UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] Skipping streaming generation. It's status is: %s."), *GetDescriptor()->GetDisplayName(), *UEnum::GetDisplayValueAsText(GetStatus()).ToString());
		return;
	}

	UWorldPartition* WorldPartition = GetInjectedWorld()->GetWorldPartition();
	WorldPartition->GenerateContainerStreaming(ActorDescContainer.Get());

	WorldPartition->RuntimeHash->GetAllStreamingCells(ContentBundleCells, true);

	FString ExternalStreamingObjectName = ObjectTools::SanitizeInvalidChars(GetDisplayName() + TEXT("_ExternalStreamingObject"), INVALID_LONGPACKAGE_CHARACTERS) ;
	ExternalStreamingObject = WorldPartition->RuntimeHash->StoreToExternalStreamingObject(GetInjectedWorld()->ContentBundleManager, *ExternalStreamingObjectName);

	UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] Generated streaming cells. %u cells were generated"), *GetDescriptor()->GetDisplayName(), ContentBundleCells.Num());

	if (!IsRunningCookCommandlet())
	{
		UContentBundleManager* ContentBundleManager = GetInjectedWorld()->ContentBundleManager;
		UContentBundleDuplicateForPIEHelper* DuplicateForPIEHelper = ContentBundleManager->GetPIEDuplicateHelper();
		if (!DuplicateForPIEHelper->StoreContentBundleStreamingObect(*this, ExternalStreamingObject))
		{
			UE_LOG(LogContentBundle, Error, TEXT("[CB: %s] Failed to store streaming object for %s. PIE duplication will not work."), *GetDescriptor()->GetDisplayName());
		}
	}

	WorldPartition->FlushStreaming();
}

void FContentBundleEditor::BroadcastChanged()
{
	if (IContentBundleEditorSubsystemInterface* EditorSubsystem = IContentBundleEditorSubsystemInterface::Get())
	{
		EditorSubsystem->NotifyContentBundleChanged(this);
	}
}

bool FContentBundleEditor::BuildContentBundleContainerPackagePath(FString& ContainerPackagePath) const
{
	FString PackageRoot, PackagePath, PackageName;
	FString LongPackageName = GetInjectedWorld()->GetPackage()->GetName();
	if (FPackageName::SplitLongPackageName(LongPackageName, PackageRoot, PackagePath, PackageName))
	{
		TStringBuilderWithBuffer<TCHAR, NAME_SIZE> PluginLeveldPackagePath;
		PluginLeveldPackagePath += TEXT("/");
		PluginLeveldPackagePath += GetDescriptor()->GetPackageRoot();
		PluginLeveldPackagePath += TEXT("/ContentBundle/");
		PluginLeveldPackagePath += GetDescriptor()->GetGuid().ToString();
		PluginLeveldPackagePath += TEXT("/");
		PluginLeveldPackagePath += PackagePath;
		PluginLeveldPackagePath += PackageName;

		ContainerPackagePath = UPackageTools::SanitizePackageName(*PluginLeveldPackagePath);
		return true;
	}

	UE_LOG(LogContentBundle, Error, TEXT("[CB: %s] Failed to build Container Package Path using %s"), *GetDescriptor()->GetDisplayName(), *LongPackageName);
	return false;
}

UPackage* FContentBundleEditor::CreateActorPackage(const FName& ActorName) const
{
	FString ActorPackagePath = ULevel::GetActorPackageName(ActorDescContainer->GetExternalActorPath(), EActorPackagingScheme::Reduced, ActorName.ToString());
	UPackage* ActorPackage = CreatePackage(*ActorPackagePath);

	ActorPackage->SetDirtyFlag(true);

	return ActorPackage;
}

FName FContentBundleEditor::BuildWorlDataLayersName() const
{
	return *GetDescriptor()->GetGuid().ToString();
}

void FContentBundleEditor::RegisterDelegates()
{
	ActorDescContainer->OnActorDescAddedEvent.AddRaw(this, &FContentBundleEditor::OnActorDescAdded);
	ActorDescContainer->OnActorDescRemovedEvent.AddRaw(this, &FContentBundleEditor::OnActorDescRemoved);
}

void FContentBundleEditor::UnregisterDelegates()
{
	ActorDescContainer->OnActorDescAddedEvent.RemoveAll(this);
	ActorDescContainer->OnActorDescRemovedEvent.RemoveAll(this);
}

void FContentBundleEditor::OnActorDescAdded(FWorldPartitionActorDesc* ActorDesc)
{
	UE_LOG(LogContentBundle, Verbose, TEXT("[CB: %s] Added actor %s to container, ActorCount: %u. Package %s."), 
		*GetDescriptor()->GetDisplayName(), *ActorDesc->GetActorLabelOrName().ToString(), GetActorCount(), *ActorDesc->GetActorPackage().ToString());

	AActor* Actor = ActorDesc->GetActor();
	UnsavedActorMonitor->StopMonitoringActor(Actor);
}

void FContentBundleEditor::OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc)
{
	UE_LOG(LogContentBundle, Verbose, TEXT("[CB: %s] Removed actor %s from container, ActorCount:  %u. Package %s."), 
		*GetDescriptor()->GetDisplayName(), *ActorDesc->GetActorLabelOrName().ToString(), GetActorCount(), *ActorDesc->GetActorPackage().ToString());

	if (!HasUserPlacedActors())
	{
		if (GetStatus() == EContentBundleStatus::ContentInjected)
		{
			RemoveBaseContent();
		}
		else
		{
			// If content is not injected, then RemoveContent was already called and we are deleting the WorldDataLayers
			check(ActorDesc->GetActorNativeClass() == AWorldDataLayers::StaticClass() && GetStatus() == EContentBundleStatus::ReadyToInject);
		}
	}

	AActor* ActorInWorld = ActorDesc->GetActor(false, false);
	check(ActorInWorld == nullptr || !UnsavedActorMonitor->IsMonitoring(ActorInWorld)); // ActorDesc existed is being deleted. Make sure the actor is not present in the unsaved list as it should have been saved for the desc to exist.
}

void FContentBundleEditor::OnUnsavedActorDeleted(AActor* Actor)
{
	UE_LOG(LogContentBundle, Verbose, TEXT("[CB: %s] Removed unsaved actor %s, ActorCount: %u. Package %s."),
		*GetDescriptor()->GetDisplayName(), *Actor->GetActorNameOrLabel(), GetActorCount(), *Actor->GetPackage()->GetName());

	if (!HasUserPlacedActors())
	{
		RemoveBaseContent();
	}
}

UContentBundleUnsavedActorMonitor::~UContentBundleUnsavedActorMonitor()
{
	check(UnsavedActors.IsEmpty());
	check(ContentBundle == nullptr);
}

void UContentBundleUnsavedActorMonitor::Initialize(FContentBundleEditor& InContentBundleEditor)
{
	ContentBundle = &InContentBundleEditor;
}

void UContentBundleUnsavedActorMonitor::StartListenOnActorEvents()
{
	GEngine->OnLevelActorDeleted().AddUObject(this, &UContentBundleUnsavedActorMonitor::OnActorDeleted);
}

void UContentBundleUnsavedActorMonitor::StopListeningOnActorEvents()
{
	GEngine->OnLevelActorDeleted().RemoveAll(this);
}

void UContentBundleUnsavedActorMonitor::Uninitialize()
{
	StopListeningOnActorEvents();

	for (TWeakObjectPtr<AActor>& Actor : UnsavedActors)
	{
		ContentBundle->GetInjectedWorld()->DestroyActor(Actor.Get());
	}
	UnsavedActors.Empty();

	ContentBundle = nullptr;
}

void UContentBundleUnsavedActorMonitor::MonitorActor(AActor* InActor)
{
	Modify();
	UnsavedActors.Add(InActor);
}

bool UContentBundleUnsavedActorMonitor::StopMonitoringActor(AActor* InActor)
{
	uint32 Idx = UnsavedActors.IndexOfByKey(InActor);
	if (Idx != INDEX_NONE)
	{
		Modify();
		UnsavedActors.Remove(InActor);
		check(UnsavedActors.IndexOfByKey(InActor) == INDEX_NONE);
		return true;
	}

	return false;
}

void UContentBundleUnsavedActorMonitor::OnActorDeleted(AActor* InActor)
{
	if (StopMonitoringActor(InActor))
	{
		ContentBundle->OnUnsavedActorDeleted(InActor);
	}
}

#endif