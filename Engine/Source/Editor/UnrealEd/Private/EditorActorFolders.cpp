// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorActorFolders.h"

#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "EngineGlobals.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorFolderUtils.h"
#include "ScopedTransaction.h"
#include "UObject/ObjectSaveContext.h"
#include "LevelInstance/LevelInstanceActor.h"

#define LOCTEXT_NAMESPACE "FActorFolders"

void UEditorActorFolders::Serialize(FArchive& Ar)
{
	Ar << Folders;
}

FString GetWorldStateFilename(UPackage* Package)
{
	const FString PathName = Package->GetPathName();
	const uint32 PathNameCrc = FCrc::MemCrc32(*PathName, sizeof(TCHAR) * PathName.Len());
	return FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("WorldState"), *FString::Printf(TEXT("%u.json"), PathNameCrc));
}

/** Convert an old path to a new path, replacing an ancestor branch with something else */
static FName OldPathToNewPath(const FString& InOldBranch, const FString& InNewBranch, const FString& PathToMove)
{
	return FName(*(InNewBranch + PathToMove.RightChop(InOldBranch.Len())));
}

// Static member definitions

//~ Begin Deprecated
PRAGMA_DISABLE_DEPRECATION_WARNINGS
FOnActorFolderCreate	FActorFolders::OnFolderCreate;
FOnActorFolderMove		FActorFolders::OnFolderMove;
FOnActorFolderDelete	FActorFolders::OnFolderDelete;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
//~ End Deprecated

FActorFolders* FActorFolders::Singleton;
FOnActorFolderCreated	FActorFolders::OnFolderCreated;
FOnActorFolderMoved		FActorFolders::OnFolderMoved;
FOnActorFolderDeleted	FActorFolders::OnFolderDeleted;

FActorFolders::FActorFolders()
{
	check(GEngine);
	GEngine->OnLevelActorFolderChanged().AddRaw(this, &FActorFolders::OnActorFolderChanged);
	GEngine->OnLevelActorListChanged().AddRaw(this, &FActorFolders::OnLevelActorListChanged);

	FEditorDelegates::MapChange.AddRaw(this, &FActorFolders::OnMapChange);
	FEditorDelegates::PostSaveWorldWithContext.AddRaw(this, &FActorFolders::OnWorldSaved);
}

FActorFolders::~FActorFolders()
{
	check(GEngine);
	GEngine->OnLevelActorFolderChanged().RemoveAll(this);
	GEngine->OnLevelActorListChanged().RemoveAll(this);

	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PostSaveWorldWithContext.RemoveAll(this);
}

void FActorFolders::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Add references for all our UObjects so they don't get collected
	Collector.AddReferencedObjects(TemporaryWorldFolders);
}

FActorFolders& FActorFolders::Get()
{
	check(Singleton);
	return *Singleton;
}

void FActorFolders::Init()
{
	Singleton = new FActorFolders;
}

void FActorFolders::Cleanup()
{
	delete Singleton;
	Singleton = nullptr;
}

void FActorFolders::Housekeeping()
{
	for (auto It = TemporaryWorldFolders.CreateIterator(); It; ++It)
	{
		if (!It.Key().Get())
		{
			It.RemoveCurrent();
		}
	}
}

void FActorFolders::BroadcastOnActorFolderCreated(UWorld& InWorld, const FFolder& InFolder)
{
	OnFolderCreated.Broadcast(InWorld, InFolder);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!InFolder.HasRootObject())
	{
		OnFolderCreate.Broadcast(InWorld, InFolder.GetPath());
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FActorFolders::BroadcastOnActorFolderDeleted(UWorld& InWorld, const FFolder& InFolder)
{
	OnFolderDeleted.Broadcast(InWorld, InFolder);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!InFolder.HasRootObject())
	{
		OnFolderDelete.Broadcast(InWorld, InFolder.GetPath());
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FActorFolders::BroadcastOnActorFolderMoved(UWorld& InWorld, const FFolder& InSrcFolder, const FFolder& InDstFolder)
{
	OnFolderMoved.Broadcast(InWorld, InSrcFolder, InDstFolder);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!InSrcFolder.HasRootObject() && !InDstFolder.HasRootObject())
	{
		OnFolderMove.Broadcast(InWorld, InSrcFolder.GetPath(), InDstFolder.GetPath());
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FActorFolders::OnLevelActorListChanged()
{
	Housekeeping();

	check(GEngine);

	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* ThisWorld = Context.World();
		if (!ThisWorld)
		{
			continue;
		}
		else if (Context.WorldType == EWorldType::PIE)
		{
			World = ThisWorld;
			break;
		}
		else if (Context.WorldType == EWorldType::Editor)
		{
			World = ThisWorld;
		}
	}

	if (World)
	{
		RebuildFolderListForWorld(*World);
	}
}

void FActorFolders::OnMapChange(uint32 MapChangeFlags)
{
	OnLevelActorListChanged();
}

void FActorFolders::OnWorldSaved(UWorld* World, FObjectPostSaveContext ObjectSaveContext)
{
	// Attempt to save the folder state
	const UEditorActorFolders* const* ExisingFolders = TemporaryWorldFolders.Find(World);

	if (ExisingFolders)
	{
		const auto Filename = GetWorldStateFilename(World->GetOutermost());
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*Filename));
		if (Ar)
		{
			TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject);
			TSharedRef<FJsonObject> JsonFolders = MakeShareable(new FJsonObject);

			for (const auto& KeyValue : (*ExisingFolders)->Folders)
			{
				// Only write for World root
				if (!KeyValue.Key.HasRootObject())
				{
					TSharedRef<FJsonObject> JsonFolder = MakeShareable(new FJsonObject);
					JsonFolder->SetBoolField(TEXT("bIsExpanded"), KeyValue.Value.bIsExpanded);
					JsonFolders->SetObjectField(KeyValue.Key.ToString(), JsonFolder);
				}
			}

			RootObject->SetObjectField(TEXT("Folders"), JsonFolders);

			{
				auto Writer = TJsonWriterFactory<TCHAR>::Create(Ar.Get());
				FJsonSerializer::Serialize(RootObject, Writer);
				Ar->Close();
			}
		}
	}
}

void FActorFolders::OnActorFolderChanged(const AActor* InActor, FName OldPath)
{
	check(InActor && InActor->GetWorld());

	FScopedTransaction Transaction(LOCTEXT("UndoAction_FolderChanged", "Actor Folder Changed"));

	UWorld* World = InActor->GetWorld();
	const FFolder NewPath = InActor->GetFolder();

	if (AddFolderToWorld(*World, NewPath))
	{
		BroadcastOnActorFolderCreated(*World, NewPath);
	}
}

void FActorFolders::RebuildFolderListForWorld(UWorld& InWorld)
{
	if (TemporaryWorldFolders.Contains(&InWorld))
	{
		// For world folders, we don't empty the existing folders so that we keep empty ones.
		// Explicitly deleted folders will already be removed from the list.
		
		// Clear folders with a Root Object.
		TArray<FFolder> FoldersToRemove;
		ForEachFolder(InWorld, [&FoldersToRemove](const FFolder& Folder)
		{
			if (Folder.HasRootObject())
			{
				FoldersToRemove.Add(Folder);
			}
			return true;
		});
		RemoveFoldersFromWorld(InWorld, FoldersToRemove, /*bBroadcastDelete*/ false);

		// Iterate over every actor in memory. WARNING: This is potentially very expensive!
		for (FActorIterator ActorIt(&InWorld); ActorIt; ++ActorIt)
		{
			AddFolderToWorld(InWorld, ActorIt->GetFolder());
		}
	}
	else
	{
		// No folders exist for this world yet - creating them will ensure they're up to date
		InitializeForWorld(InWorld);
	}
}

const TMap<FFolder, FActorFolderProps>& FActorFolders::GetFolderPropertiesForWorld(UWorld& InWorld)
{
	return GetOrCreateFoldersForWorld(InWorld).Folders;
}

FActorFolderProps* FActorFolders::GetFolderProperties(UWorld& InWorld, const FFolder& InPath)
{
	return GetOrCreateFoldersForWorld(InWorld).Folders.Find(InPath);
}

bool FActorFolders::FoldersExistForWorld(UWorld& InWorld) const
{
	return (TemporaryWorldFolders.Find(&InWorld) != NULL);
}

UEditorActorFolders& FActorFolders::GetOrCreateFoldersForWorld(UWorld& InWorld)
{
	if (UEditorActorFolders** Folders = TemporaryWorldFolders.Find(&InWorld))
	{
		return **Folders;
	}

	return InitializeForWorld(InWorld);
}

UEditorActorFolders& FActorFolders::InitializeForWorld(UWorld& InWorld)
{
	// Clean up any stale worlds
	Housekeeping();

	InWorld.OnLevelsChanged().AddRaw(this, &FActorFolders::OnLevelActorListChanged);

	// We intentionally don't pass RF_Transactional to ConstructObject so that we don't record the creation of the object into the undo buffer
	// (to stop it getting deleted on undo as we manage its lifetime), but we still want it to be RF_Transactional so we can record any changes later
	UEditorActorFolders* Folders = NewObject<UEditorActorFolders>(GetTransientPackage(), NAME_None, RF_NoFlags);
	Folders->SetFlags(RF_Transactional);
	TemporaryWorldFolders.Add(&InWorld, Folders);

	// Ensure the list is entirely up to date with the world before we write our serialized properties into it.
	for (FActorIterator ActorIt(&InWorld); ActorIt; ++ActorIt)
	{
		AddFolderToWorld(InWorld, ActorIt->GetFolder());
	}

	// Attempt to load the folder properties from this user's saved world state directory
	const auto Filename = GetWorldStateFilename(InWorld.GetOutermost());
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*Filename));
	if (Ar)
	{
		TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

		auto Reader = TJsonReaderFactory<TCHAR>::Create(Ar.Get());
		if (FJsonSerializer::Deserialize(Reader, RootObject))
		{
			const TSharedPtr<FJsonObject>& JsonFolders = RootObject->GetObjectField(TEXT("Folders"));
			for (const auto& KeyValue : JsonFolders->Values)
			{
				// Only pull in the folder's properties if this folder still exists in the world.
				// This means that old stale folders won't re-appear in the world (they'll won't get serialized when the world is saved anyway)
				if (FActorFolderProps* FolderInWorld = Folders->Folders.Find(FFolder(FName(*KeyValue.Key))))
				{
					auto FolderProperties = KeyValue.Value->AsObject();
					FolderInWorld->bIsExpanded = FolderProperties->GetBoolField(TEXT("bIsExpanded"));
				}
			}
		}
		Ar->Close();
	}

	return *Folders;
}

FFolder FActorFolders::GetDefaultFolderForSelection(UWorld& InWorld, TArray<FFolder>* InSelectedFolders)
{
	// Find a common parent folder, or put it at the root
	TOptional<FFolder> CommonFolder;

	auto MergeFolders = [&CommonFolder](FFolder& Folder)
	{
		if (!CommonFolder.IsSet())
		{
			CommonFolder = Folder;
		}
		else if (CommonFolder.GetValue().GetRootObject() != Folder.GetRootObject())
		{
			CommonFolder = FFolder();
			return false;
		}
		else if (CommonFolder.GetValue().GetPath() != Folder.GetPath())
		{
			// Empty path and continue iterating as we need to continue validating RootObjects
			CommonFolder.GetValue().SetPath(NAME_None);
		}
		return true;
	};

	bool bMergeStopped = false;
	for( FSelectionIterator SelectionIt( *GEditor->GetSelectedActors() ); SelectionIt; ++SelectionIt )
	{
		AActor* Actor = CastChecked<AActor>(*SelectionIt);

		FFolder Folder = Actor->GetFolder();
		// Special case for Level Instance, make root as level instance if editing
		if (ALevelInstance* LevelInstance = Cast<ALevelInstance>(Actor))
		{
			if (LevelInstance->IsEditing())
			{
				Folder = FFolder(Folder.GetPath(), LevelInstance);
			}
		}
		if (!MergeFolders(Folder))
		{
			bMergeStopped = true;
			break;
		}
	}
	if (!bMergeStopped && InSelectedFolders)
	{
		for (FFolder& Folder : *InSelectedFolders)
		{
			if (!MergeFolders(Folder))
			{
				break;
			}
		}
	}

	return GetDefaultFolderName(InWorld, CommonFolder.Get(FFolder()));
}

FFolder FActorFolders::GetFolderName(UWorld& InWorld, const FFolder& InParentFolder, const FName& InLeafName)
{
	// This is potentially very slow but necessary to find a unique name
	const auto& ExistingFolders = GetFolderPropertiesForWorld(InWorld);
	const FFolder::FRootObject& RootObject = InParentFolder.GetRootObject();

	const FString LeafNameString = InLeafName.ToString();

	// Find the last non-numeric character
	int32 LastDigit = LeafNameString.FindLastCharByPredicate([](TCHAR Ch) { return !FChar::IsDigit(Ch); });
	uint32 SuffixLen = (LeafNameString.Len() - LastDigit) - 1;

	if (LastDigit == INDEX_NONE)
	{
		// Name is entirely numeric, eg. "123", so no suffix exists
		SuffixLen = 0;
	}

	// Trim any numeric suffix
	uint32 Suffix = 1;
	FString LeafNameRoot;
	if (SuffixLen > 0)
	{
		LeafNameRoot = LeafNameString.LeftChop(SuffixLen);
		FString LeafSuffix = LeafNameString.RightChop(LeafNameString.Len() - SuffixLen);
		Suffix = LeafSuffix.IsNumeric() ? FCString::Atoi(*LeafSuffix) : 1;
	}
	else
	{
		LeafNameRoot = LeafNameString;
	}

	// Create a valid base name for this folder
	FNumberFormattingOptions NumberFormat;
	NumberFormat.SetUseGrouping(false);
	NumberFormat.SetMinimumIntegralDigits(SuffixLen);

	FText LeafName = FText::Format(LOCTEXT("FolderNamePattern", "{0}{1}"), FText::FromString(LeafNameRoot), SuffixLen > 0 ? FText::AsNumber(Suffix++, &NumberFormat) : FText::GetEmpty());

	FString ParentFolderPath = InParentFolder.IsNone() ? TEXT("") : InParentFolder.ToString();
	if (!ParentFolderPath.IsEmpty())
	{
		ParentFolderPath += "/";
	}

	FName FolderName(*(ParentFolderPath + LeafName.ToString()));
	while (ExistingFolders.Contains(FFolder(FolderName, RootObject)))
	{
		LeafName = FText::Format(LOCTEXT("FolderNamePattern", "{0}{1}"), FText::FromString(LeafNameRoot), FText::AsNumber(Suffix++, &NumberFormat));
		FolderName = FName(*(ParentFolderPath + LeafName.ToString()));
		if (Suffix == 0)
		{
			// We've wrapped around a 32bit unsigned int - something must be seriously wrong!
			FolderName = NAME_None;
			break;
		}
	}

	return FFolder(FolderName, RootObject);
}

FFolder FActorFolders::GetDefaultFolderName(UWorld& InWorld, const FFolder& InParentFolder)
{
	// This is potentially very slow but necessary to find a unique name
	const auto& ExistingFolders = GetFolderPropertiesForWorld(InWorld);
	const FFolder::FRootObject& RootObject = InParentFolder.GetRootObject();

	// Create a valid base name for this folder
	FNumberFormattingOptions NumberFormat;
	NumberFormat.SetUseGrouping(false);
	uint32 Suffix = 1;
	FText LeafName = FText::Format(LOCTEXT("DefaultFolderNamePattern", "NewFolder{0}"), FText::AsNumber(Suffix++, &NumberFormat));

	FString ParentFolderPath = InParentFolder.IsNone() ? TEXT("") : InParentFolder.ToString();
	if (!ParentFolderPath.IsEmpty())
	{
		ParentFolderPath += "/";
	}

	FName FolderName(*(ParentFolderPath + LeafName.ToString()));
	while (ExistingFolders.Contains(FFolder(FolderName, RootObject)))
	{
		LeafName = FText::Format(LOCTEXT("DefaultFolderNamePattern", "NewFolder{0}"), FText::AsNumber(Suffix++, &NumberFormat));
		FolderName = FName(*(ParentFolderPath + LeafName.ToString()));
		if (Suffix == 0)
		{
			// We've wrapped around a 32bit unsigned int - something must be seriously wrong!
			FolderName = NAME_None;
			break;
		}
	}

	return FFolder(FolderName, RootObject);
}

void FActorFolders::CreateFolderContainingSelection(UWorld& InWorld, const FFolder& InFolder)
{
	const FScopedTransaction Transaction(LOCTEXT("UndoAction_CreateFolder", "Create Folder"));
	CreateFolder(InWorld, InFolder);
	SetSelectedFolderPath(InFolder);
}

void FActorFolders::SetSelectedFolderPath(const FFolder& InFolder) const
{
	// Move the currently selected actors into the new folder
	USelection* SelectedActors = GEditor->GetSelectedActors();

	const FFolder::FRootObject& RootObject = InFolder.GetRootObject();
	FName Path = InFolder.GetPath();

	for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
	{
		AActor* Actor = CastChecked<AActor>(*SelectionIt);

		// If this actor is parented to another, which is also in the selection, skip it so that it moves when its parent does (otherwise it's orphaned)
		const AActor* ParentActor = Actor->GetAttachParentActor();
		if (ParentActor && SelectedActors->IsSelected(ParentActor))
		{
			continue;
		}

		// Currently not supported to change rootobject through this interface
		if (RootObject == Actor->GetFolderRootObject())
		{
			Actor->SetFolderPath_Recursively(Path);
		}
	}
}

void FActorFolders::CreateFolder(UWorld& InWorld, const FFolder& InFolder)
{
	FScopedTransaction Transaction(LOCTEXT("UndoAction_CreateFolder", "Create Folder"));

	if (AddFolderToWorld(InWorld, InFolder))
	{
		BroadcastOnActorFolderCreated(InWorld, InFolder);
	}
}

void FActorFolders::OnFolderRootObjectRemoved(UWorld& InWorld, const FFolder::FRootObject& InFolderRootObject)
{
	TArray<FFolder> FoldersToDelete;
	ForEachFolderWithRootObject(InWorld, InFolderRootObject, [&FoldersToDelete](const FFolder& Folder)
	{
		FoldersToDelete.Add(Folder);
		return true;
	});

	RemoveFoldersFromWorld(InWorld, FoldersToDelete, /*bBroadcastDelete*/ true);
}

void FActorFolders::DeleteFolder(UWorld& InWorld, const FFolder& InFolderToDelete)
{
	const FScopedTransaction Transaction(LOCTEXT("UndoAction_DeleteFolder", "Delete Folder"));

	UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);
	if (Folders.Folders.Contains(InFolderToDelete))
	{
		Folders.Modify();
		Folders.Folders.Remove(InFolderToDelete);
		BroadcastOnActorFolderDeleted(InWorld, InFolderToDelete);
	}
}

bool FActorFolders::RenameFolderInWorld(UWorld& World, const FFolder& OldPath, const FFolder& NewPath)
{
	// We currently don't support changing the root object
	check(OldPath.GetRootObject() == NewPath.GetRootObject());
	const FFolder::FRootObject& RootObject = OldPath.GetRootObject();

	const FString OldPathString = OldPath.ToString();
	const FString NewPathString = NewPath.ToString();

	if (OldPath.IsNone() || OldPathString.Equals(NewPathString) || FEditorFolderUtils::PathIsChildOf(NewPathString, OldPathString))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("UndoAction_RenameFolder", "Rename Folder"));

	TSet<FFolder> RenamedFolders;
	bool RenamedFolder = false;

	// Move any folders we currently hold - old ones will be deleted later
	UEditorActorFolders& FoldersInWorld = GetOrCreateFoldersForWorld(World);
	FoldersInWorld.Modify();

	auto ExistingFoldersCopy = FoldersInWorld.Folders;
	for (const auto& Pair : ExistingFoldersCopy)
	{
		const FFolder& Path = Pair.Key;

		const FString FolderPath = Path.ToString();
		if (OldPath == Path || FEditorFolderUtils::PathIsChildOf(FolderPath, OldPathString))
		{
			const FFolder NewFolder = FFolder(OldPathToNewPath(OldPathString, NewPathString, FolderPath), RootObject);
			
			// Needs to be done this way otherwise case insensitive comparison is used.
			bool ContainsFolder = false;
			for (const auto& FolderPair : FoldersInWorld.Folders)
			{
				if (FolderPair.Key.GetRootObject() == NewFolder.GetRootObject() && FolderPair.Key.GetPath().IsEqual(NewFolder.GetPath(), ENameCase::CaseSensitive))
				{
					ContainsFolder = true;
					break;
				}
			}

			if (!ContainsFolder)
			{
				// Use the existing properties for the folder if we have them
				if (FActorFolderProps* ExistingProperties = FoldersInWorld.Folders.Find(Path))
				{
					FoldersInWorld.Folders.Add(NewFolder, *ExistingProperties);
				}
				else
				{
					// Otherwise use default properties
					FoldersInWorld.Folders.Add(NewFolder);
				}
				BroadcastOnActorFolderMoved(World, Path, NewFolder);
				BroadcastOnActorFolderCreated(World, NewFolder);
			}

			// case insensitive compare as we don't want to remove the folder if it has the same name
			if (Path != NewFolder)
			{
				RenamedFolders.Add(Path);
			}

			RenamedFolder = true;
		}
	}

	// Now that we have folders created, move any actors that ultimately reside in that folder too
	for (auto ActorIt = FActorIterator(&World); ActorIt; ++ActorIt)
	{
		// copy, otherwise it returns the new value when set later
		const FFolder OldActorPath = ActorIt->GetFolder();
		if (OldActorPath.IsNone())
		{
			continue;
		}

		if (OldActorPath == OldPath || FEditorFolderUtils::PathIsChildOf(OldActorPath.ToString(), OldPathString))
		{
			ActorIt->SetFolderPath_Recursively(OldPathToNewPath(OldPathString, NewPathString, OldActorPath.ToString()));
			const FFolder NewActorPath = ActorIt->GetFolder();

			// case insensitive compare as we don't want to remove the folder if it has the same name
			if (OldActorPath != NewActorPath)
			{
				RenamedFolders.Add(OldActorPath);
			}

			RenamedFolder = true;
		}
	}

	// Cleanup any old folders
	for (const auto& Path : RenamedFolders)
	{
		FoldersInWorld.Folders.Remove(Path);
		BroadcastOnActorFolderDeleted(World, Path);
	}

	return RenamedFolder;
}

bool FActorFolders::AddFolderToWorld(UWorld& InWorld, const FFolder& InFolder)
{
	if (!InFolder.IsNone())
	{
		UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);

		if (!Folders.Folders.Contains(InFolder))
		{
			// Add the parent as well
			const FFolder ParentFolder = InFolder.GetParent();
			if (!ParentFolder.IsNone())
			{
				AddFolderToWorld(InWorld, ParentFolder);
			}

			Folders.Modify();
			Folders.Folders.Add(InFolder);

			return true;
		}
	}

	return false;
}

void FActorFolders::RemoveFoldersFromWorld(UWorld& InWorld, const TArray<FFolder>& InFolders, bool bBroadcastDelete)
{
	if (InFolders.Num() > 0)
	{
		UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);
		Folders.Modify();
		for (const FFolder& Folder : InFolders)
		{
			Folders.Folders.Remove(Folder);
			if (bBroadcastDelete)
			{
				BroadcastOnActorFolderDeleted(InWorld, Folder);
			}
		}
	}
}

bool FActorFolders::ContainsFolder(UWorld& InWorld, const FFolder& InFolder)
{
	return GetFolderProperties(InWorld, InFolder) != nullptr;
}

bool FActorFolders::IsFolderExpanded(UWorld& InWorld, const FFolder& InFolder)
{
	FActorFolderProps* FolderProps = GetFolderProperties(InWorld, InFolder);
	return FolderProps ? FolderProps->bIsExpanded : false;
}

void FActorFolders::SetIsFolderExpanded(UWorld& InWorld, const FFolder& InFolder, bool bIsExpanded)
{
	if (FActorFolderProps* FolderProps = GetFolderProperties(InWorld, InFolder))
	{
		FolderProps->bIsExpanded = bIsExpanded;
	}
}

void FActorFolders::ForEachFolder(UWorld& InWorld, TFunctionRef<bool(const FFolder&)> Operation)
{
	UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);
	for (const auto& Pair : Folders.Folders)
	{
		if (!Operation(Pair.Key))
		{
			break;
		}
	}
}

void FActorFolders::ForEachFolderWithRootObject(UWorld& InWorld, const FFolder::FRootObject& InFolderRootObject, TFunctionRef<bool(const FFolder&)> Operation)
{
	UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);
	for (const auto& Pair : Folders.Folders)
	{
		const FFolder& Folder = Pair.Key;
		if (Folder.GetRootObject() == InFolderRootObject)
		{
			if (!Operation(Folder))
			{
				break;
			}
		}
	}
}

void FActorFolders::ForEachActorInFolders(UWorld& World, const TArray<FName>& Paths, TFunctionRef<bool(AActor*)> Operation, const FFolder::FRootObject& InFolderRootObject)
{
	for (FActorIterator ActorIt(&World); ActorIt; ++ActorIt)
	{
		if (ActorIt->GetFolderRootObject() != InFolderRootObject)
		{
			continue;
		}
		FName ActorPath = ActorIt->GetFolderPath();
		if (ActorPath.IsNone() || !Paths.Contains(ActorPath))
		{
			continue;
		}

		if (!Operation(*ActorIt))
		{
			return;
		}
	}
}

void FActorFolders::GetActorsFromFolders(UWorld& World, const TArray<FName>& Paths, TArray<AActor*>& OutActors, const FFolder::FRootObject& InFolderRootObject)
{
	ForEachActorInFolders(World, Paths, [&OutActors](AActor* InActor)
	{
		OutActors.Add(InActor);
		return true;
	}, InFolderRootObject);
}

void FActorFolders::GetWeakActorsFromFolders(UWorld& World, const TArray<FName>& Paths, TArray<TWeakObjectPtr<AActor>>& OutActors, const FFolder::FRootObject& InFolderRootObject)
{
	ForEachActorInFolders(World, Paths, [&OutActors](AActor* InActor)
	{
		OutActors.Add(InActor);
		return true;
	}, InFolderRootObject);
}

////////////////////////////////////////////
//~ Begin Deprecated

FActorFolderProps* FActorFolders::GetFolderProperties(UWorld& InWorld, FName InPath)
{
	return GetFolderProperties(InWorld, FFolder(InPath));
}

FName FActorFolders::GetDefaultFolderName(UWorld& InWorld, FName ParentPath)
{
	FFolder DefaultFolderName = GetDefaultFolderName(InWorld, FFolder(ParentPath));
	return DefaultFolderName.GetPath();
}

FName FActorFolders::GetDefaultFolderNameForSelection(UWorld& InWorld)
{
	FFolder FolderName = GetDefaultFolderForSelection(InWorld);
	return FolderName.GetPath();
}

FName FActorFolders::GetFolderName(UWorld& InWorld, FName InParentPath, FName InFolderName)
{
	FFolder FolderName = GetFolderName(InWorld, FFolder(InParentPath), InFolderName);
	return FolderName.GetPath();
}

void FActorFolders::CreateFolder(UWorld& InWorld, FName Path)
{
	CreateFolder(InWorld, FFolder(Path));
}

void FActorFolders::CreateFolderContainingSelection(UWorld& InWorld, FName Path)
{
	CreateFolderContainingSelection(InWorld, FFolder(Path));
}

void FActorFolders::SetSelectedFolderPath(FName Path) const
{
	SetSelectedFolderPath(FFolder(Path));
}

void FActorFolders::DeleteFolder(UWorld& InWorld, FName FolderToDelete)
{
	DeleteFolder(InWorld, FFolder(FolderToDelete));
}

bool FActorFolders::RenameFolderInWorld(UWorld& InWorld, FName OldPath, FName NewPath)
{
	return RenameFolderInWorld(InWorld, FFolder(OldPath), FFolder(NewPath));
}

//~ End Deprecated
////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE