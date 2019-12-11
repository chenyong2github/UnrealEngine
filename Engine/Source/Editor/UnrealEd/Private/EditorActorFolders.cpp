// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FActorFolders"

void UEditorActorFolders::Serialize(FArchive& Ar)
{
	Ar << Folders;
}

FString GetWorldStateFilename(UPackage* Package)
{
	const FString PathName = Package->GetPathName();
	const uint32 PathNameCrc = FCrc::MemCrc32(*PathName, sizeof(TCHAR)*PathName.Len());
	return FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("WorldState"), *FString::Printf(TEXT("%u.json"), PathNameCrc));
}

/** Convert an old path to a new path, replacing an ancestor branch with something else */
static FName OldPathToNewPath(const FString& InOldBranch, const FString& InNewBranch, const FString& PathToMove)
{
	return FName(*(InNewBranch + PathToMove.RightChop(InOldBranch.Len())));
}

// Static member definitions
FOnActorFolderCreate	FActorFolders::OnFolderCreate;
FOnActorFolderMove		FActorFolders::OnFolderMove;
FOnActorFolderDelete	FActorFolders::OnFolderDelete;
FActorFolders*			FActorFolders::Singleton;

FActorFolders::FActorFolders()
{
	check(GEngine);
	GEngine->OnLevelActorFolderChanged().AddRaw(this, &FActorFolders::OnActorFolderChanged);
	GEngine->OnLevelActorListChanged().AddRaw(this, &FActorFolders::OnLevelActorListChanged);

	FEditorDelegates::MapChange.AddRaw(this, &FActorFolders::OnMapChange);
	FEditorDelegates::PostSaveWorld.AddRaw(this, &FActorFolders::OnWorldSaved);
}

FActorFolders::~FActorFolders()
{
	check(GEngine);
	GEngine->OnLevelActorFolderChanged().RemoveAll(this);
	GEngine->OnLevelActorListChanged().RemoveAll(this);

	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PostSaveWorld.RemoveAll(this);
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

void FActorFolders::OnWorldSaved(uint32 SaveFlags, UWorld* World, bool bSuccess)
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
				TSharedRef<FJsonObject> JsonFolder = MakeShareable(new FJsonObject);
				JsonFolder->SetBoolField(TEXT("bIsExpanded"), KeyValue.Value.bIsExpanded);

				JsonFolders->SetObjectField(KeyValue.Key.ToString(), JsonFolder);
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
	const auto NewPath = InActor->GetFolderPath();

	if (AddFolderToWorld(*World, NewPath))
	{
		OnFolderCreate.Broadcast(*World, NewPath);
	}
	else
	{
		Transaction.Cancel();
	}
}

bool FActorFolders::PathIsChildOf(const FString& InPotentialChild, const FString& InParent)
{
	const int32 ParentLen = InParent.Len();
	return
		InPotentialChild.Len() > ParentLen &&
		InPotentialChild[ParentLen] == '/' &&
		InPotentialChild.Left(ParentLen) == InParent;
}

void FActorFolders::RebuildFolderListForWorld(UWorld& InWorld)
{
	if (TemporaryWorldFolders.Contains(&InWorld))
	{
		// We don't empty the existing folders so that we keep empty ones.
		// Explicitly deleted folders will already be removed from the list

		// Iterate over every actor in memory. WARNING: This is potentially very expensive!
		for (FActorIterator ActorIt(&InWorld); ActorIt; ++ActorIt)
		{
			AddFolderToWorld(InWorld, ActorIt->GetFolderPath());
		}
	}
	else
	{
		// No folders exist for this world yet - creating them will ensure they're up to date
		InitializeForWorld(InWorld);
	}
}

const TMap<FName, FActorFolderProps>& FActorFolders::GetFolderPropertiesForWorld(UWorld& InWorld)
{
	return GetOrCreateFoldersForWorld(InWorld).Folders;
}

FActorFolderProps* FActorFolders::GetFolderProperties(UWorld& InWorld, FName InPath)
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

	// We intentionally don't pass RF_Transactional to ConstructObject so that we don't record the creation of the object into the undo buffer
	// (to stop it getting deleted on undo as we manage its lifetime), but we still want it to be RF_Transactional so we can record any changes later
	UEditorActorFolders* Folders = NewObject<UEditorActorFolders>(GetTransientPackage(), NAME_None, RF_NoFlags);
	Folders->SetFlags(RF_Transactional);
	TemporaryWorldFolders.Add(&InWorld, Folders);

	// Ensure the list is entirely up to date with the world before we write our serialized properties into it.
	for (FActorIterator ActorIt(&InWorld); ActorIt; ++ActorIt)
	{
		AddFolderToWorld(InWorld, ActorIt->GetFolderPath());
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
				if (FActorFolderProps* FolderInWorld = Folders->Folders.Find(*KeyValue.Key))
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

FName FActorFolders::GetDefaultFolderNameForSelection(UWorld& InWorld)
{
	// Find a common parent folder, or put it at the root
	FName CommonParentFolder;
	for( FSelectionIterator SelectionIt( *GEditor->GetSelectedActors() ); SelectionIt; ++SelectionIt )
	{
		AActor* Actor = CastChecked<AActor>(*SelectionIt);
		if (CommonParentFolder.IsNone())
		{
			CommonParentFolder = Actor->GetFolderPath();
		}
		else if (Actor->GetFolderPath() != CommonParentFolder)
		{
			CommonParentFolder = NAME_None;
			break;
		}
	}

	return GetDefaultFolderName(InWorld, CommonParentFolder);
}

FName FActorFolders::GetFolderName(UWorld& InWorld, FName ParentPath, FName InLeafName)
{
	// This is potentially very slow but necessary to find a unique name
	const auto& ExistingFolders = GetFolderPropertiesForWorld(InWorld);

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

	FString ParentFolderPath = ParentPath.IsNone() ? TEXT("") : ParentPath.ToString();
	if (!ParentFolderPath.IsEmpty())
	{
		ParentFolderPath += "/";
	}

	FName FolderName(*(ParentFolderPath + LeafName.ToString()));
	while (ExistingFolders.Contains(FolderName))
	{
		LeafName = FText::Format(LOCTEXT("FolderNamePattern", "{0}{1}"), FText::FromString(LeafNameRoot), FText::AsNumber(Suffix++, &NumberFormat));
		FolderName = FName(*(ParentFolderPath + LeafName.ToString()));
		if (Suffix == 0)
		{
			// We've wrapped around a 32bit unsigned int - something must be seriously wrong!
			return FName();
		}
	}

	return FolderName;
}

FName FActorFolders::GetDefaultFolderName(UWorld& InWorld, FName ParentPath)
{
	// This is potentially very slow but necessary to find a unique name
	const auto& ExistingFolders = GetFolderPropertiesForWorld(InWorld);

	// Create a valid base name for this folder
	FNumberFormattingOptions NumberFormat;
	NumberFormat.SetUseGrouping(false);
	uint32 Suffix = 1;
	FText LeafName = FText::Format(LOCTEXT("DefaultFolderNamePattern", "NewFolder{0}"), FText::AsNumber(Suffix++, &NumberFormat));

	FString ParentFolderPath = ParentPath.IsNone() ? TEXT("") : ParentPath.ToString();
	if (!ParentFolderPath.IsEmpty())
	{
		ParentFolderPath += "/";
	}

	FName FolderName(*(ParentFolderPath + LeafName.ToString()));
	while (ExistingFolders.Contains(FolderName))
	{
		LeafName = FText::Format(LOCTEXT("DefaultFolderNamePattern", "NewFolder{0}"), FText::AsNumber(Suffix++, &NumberFormat));
		FolderName = FName(*(ParentFolderPath + LeafName.ToString()));
		if (Suffix == 0)
		{
			// We've wrapped around a 32bit unsigned int - something must be seriously wrong!
			return FName();
		}
	}

	return FolderName;
}

void FActorFolders::CreateFolderContainingSelection(UWorld& InWorld, FName Path)
{
	const FScopedTransaction Transaction(LOCTEXT("UndoAction_CreateFolder", "Create Folder"));
	CreateFolder(InWorld, Path);
	SetSelectedFolderPath(Path);
}

void FActorFolders::SetSelectedFolderPath(FName Path) const
{
	// Move the currently selected actors into the new folder
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
	{
		AActor* Actor = CastChecked<AActor>(*SelectionIt);

		// If this actor is parented to another, which is also in the selection, skip it so that it moves when its parent does (otherwise it's orphaned)
		const AActor* ParentActor = Actor->GetAttachParentActor();
		if (ParentActor && SelectedActors->IsSelected(ParentActor))
		{
			continue;
		}

		Actor->SetFolderPath_Recursively(Path);
	}
}

void FActorFolders::CreateFolder(UWorld& InWorld, FName Path)
{
	FScopedTransaction Transaction(LOCTEXT("UndoAction_CreateFolder", "Create Folder"));

	if (AddFolderToWorld(InWorld, Path))
	{
		OnFolderCreate.Broadcast(InWorld, Path);
	}
	else
	{
		Transaction.Cancel();
	}
}

void FActorFolders::DeleteFolder(UWorld& InWorld, FName FolderToDelete)
{
	const FScopedTransaction Transaction(LOCTEXT("UndoAction_DeleteFolder", "Delete Folder"));

	UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);
	if (Folders.Folders.Contains(FolderToDelete))
	{
		Folders.Modify();
		Folders.Folders.Remove(FolderToDelete);
		OnFolderDelete.Broadcast(InWorld, FolderToDelete);
	}
}

bool FActorFolders::RenameFolderInWorld(UWorld& World, FName OldPath, FName NewPath)
{
	const FString OldPathString = OldPath.ToString();
	const FString NewPathString = NewPath.ToString();

	if (OldPath.IsNone() || OldPathString.Equals(NewPathString) || PathIsChildOf(NewPathString, OldPathString))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("UndoAction_RenameFolder", "Rename Folder"));

	TSet<FName> RenamedFolders;
	bool RenamedFolder = false;

	// Move any folders we currently hold - old ones will be deleted later
	UEditorActorFolders& FoldersInWorld = GetOrCreateFoldersForWorld(World);
	FoldersInWorld.Modify();

	auto ExistingFoldersCopy = FoldersInWorld.Folders;
	for (const auto& Pair : ExistingFoldersCopy)
	{
		auto Path = Pair.Key;

		const FString FolderPath = Path.ToString();
		if (OldPath == Path || PathIsChildOf(FolderPath, OldPathString))
		{
			const FName NewFolder = OldPathToNewPath(OldPathString, NewPathString, FolderPath);
			
			// Needs to be done this way otherwise case insensitive comparison is used.
			bool ContainsFolder = false;
			for (const auto& FolderPair : FoldersInWorld.Folders)
			{
				if (FolderPair.Key.IsEqual(NewFolder, ENameCase::CaseSensitive))
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
				OnFolderMove.Broadcast(World, Path, NewFolder);
				OnFolderCreate.Broadcast(World, NewFolder);
			}

			// case insensive compare as we don't want to remove the folder if it has the same name
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
		const FName OldActorPath = ActorIt->GetFolderPath();
		if (OldActorPath.IsNone())
		{
			continue;
		}

		if (OldActorPath == OldPath || PathIsChildOf(OldActorPath.ToString(), OldPathString))
		{
			ActorIt->SetFolderPath_Recursively(OldPathToNewPath(OldPathString, NewPathString, OldActorPath.ToString()));
			const FName& NewActorPath = ActorIt->GetFolderPath();

			// case insensive compare as we don't want to remove the folder if it has the same name
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
		OnFolderDelete.Broadcast(World, Path);
	}

	return RenamedFolder;
}

bool FActorFolders::AddFolderToWorld(UWorld& InWorld, FName Path)
{
	if (!Path.IsNone())
	{
		UEditorActorFolders& Folders = GetOrCreateFoldersForWorld(InWorld);

		if (!Folders.Folders.Contains(Path))
		{
			// Add the parent as well
			const FName ParentPath(*FPaths::GetPath(Path.ToString()));
			if (!ParentPath.IsNone())
			{
				AddFolderToWorld(InWorld, ParentPath);
			}

			Folders.Modify();
			Folders.Folders.Add(Path);

			return true;
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
