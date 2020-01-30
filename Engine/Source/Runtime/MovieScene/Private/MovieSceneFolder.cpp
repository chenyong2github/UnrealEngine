// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneFolder.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "Algo/Count.h"

void GetMovieSceneFoldersRecursive(const TArray<UMovieSceneFolder*>& InFoldersToRecurse, TArray<UMovieSceneFolder*>& OutFolders)
{
	for (UMovieSceneFolder* Folder : InFoldersToRecurse)
	{
		if (Folder)
		{
			OutFolders.Add(Folder);
			GetMovieSceneFoldersRecursive(Folder->GetChildFolders(), OutFolders);
		}
	}
}

UMovieSceneFolder::UMovieSceneFolder( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
#if WITH_EDITORONLY_DATA
	, FolderColor(FColor::White)
	, SortingOrder(-1)
#endif
{
}

FName UMovieSceneFolder::GetFolderName() const
{
	return FolderName;
}


void UMovieSceneFolder::SetFolderName( FName InFolderName )
{
	Modify();

	FolderName = InFolderName;
}


const TArray<UMovieSceneFolder*>& UMovieSceneFolder::GetChildFolders() const
{
	return ChildFolders;
}


void UMovieSceneFolder::AddChildFolder( UMovieSceneFolder* InChildFolder )
{
	Modify();

#if WITH_EDITORONLY_DATA
	// Ensure the added folder does not belong to any other folder in the same scene.
	UMovieScene* OwningScene = GetTypedOuter<UMovieScene>();
	if (OwningScene)
	{
		TArray<UMovieSceneFolder*> AllFolders;
		GetMovieSceneFoldersRecursive(OwningScene->GetRootFolders(), AllFolders);

		for (UMovieSceneFolder* MovieSceneFolder : AllFolders)
		{
			MovieSceneFolder->RemoveChildFolder(InChildFolder);
		}
	}
#endif

	// Now add it as a child of ourself
	ChildFolders.Add( InChildFolder );
}


void UMovieSceneFolder::RemoveChildFolder( UMovieSceneFolder* InChildFolder )
{
	Modify();

	ChildFolders.Remove(InChildFolder);
}


const TArray<UMovieSceneTrack*>& UMovieSceneFolder::GetChildMasterTracks() const
{
	return ChildMasterTracks;
}


void UMovieSceneFolder::AddChildMasterTrack( UMovieSceneTrack* InMasterTrack )
{
	Modify();

#if WITH_EDITORONLY_DATA
	// Ensure the added track does not belong to any other folder in the same scene.
	UMovieScene* OwningScene = GetTypedOuter<UMovieScene>();
	if (OwningScene)
	{
		TArray<UMovieSceneFolder*> AllFolders;
		GetMovieSceneFoldersRecursive(OwningScene->GetRootFolders(), AllFolders);

		for (UMovieSceneFolder* MovieSceneFolder : AllFolders)
		{
			MovieSceneFolder->RemoveChildMasterTrack(InMasterTrack);
		}
	}
#endif

	ChildMasterTracks.Add( InMasterTrack );
}


void UMovieSceneFolder::RemoveChildMasterTrack( UMovieSceneTrack* InMasterTrack )
{
	Modify();

	ChildMasterTracks.Remove( InMasterTrack );
}


const TArray<FGuid>& UMovieSceneFolder::GetChildObjectBindings() const
{
	return ChildObjectBindings;
}


void UMovieSceneFolder::AddChildObjectBinding(const FGuid& InObjectBinding )
{
	Modify();

#if WITH_EDITORONLY_DATA
	// Ensure the added object  does not belong to any other folder in the same scene.
	UMovieScene* OwningScene = GetTypedOuter<UMovieScene>();
	if (OwningScene)
	{
		TArray<UMovieSceneFolder*> AllFolders;
		GetMovieSceneFoldersRecursive(OwningScene->GetRootFolders(), AllFolders);

		for (UMovieSceneFolder* MovieSceneFolder : AllFolders)
		{
			MovieSceneFolder->RemoveChildObjectBinding(InObjectBinding);
		}
	}
#endif

	ChildObjectBindings.Add( InObjectBinding );
}


void UMovieSceneFolder::RemoveChildObjectBinding( const FGuid& InObjectBinding )
{
	Modify();

	ChildObjectBindings.Remove( InObjectBinding );
}

void UMovieSceneFolder::PostLoad()
{
	// Remove any null folders
	for (int32 ChildFolderIndex = 0; ChildFolderIndex < ChildFolders.Num(); )
	{
		if (ChildFolders[ChildFolderIndex] == nullptr)
		{
			ChildFolders.RemoveAt(ChildFolderIndex);
		}
		else
		{
			++ChildFolderIndex;
		}
	}

#if WITH_EDITORONLY_DATA
	// Historically we've not been very strict about ensuring a folder, track, or object binding existed
	// only in one folder. This is now enforced (via automatically removing the item from other folders
	// when they are added to this folder), and checked (the tree view trips an ensure on invalid children)
	// but all legacy content can still have the invalid children which continuously trips the ensure.
	// Since we now enforce child-only-exists-in-one-folder, we can safely remove any invalid children on
	// load, and be confident that we shouldn't run into situations in the future where an invalid child is
	// left in a folder.
	UMovieScene* OwningScene = GetTypedOuter<UMovieScene>();
	if (OwningScene)
	{
		// Validate child Master Tracks
		for(int32 ChildMasterTrackIndex = 0; ChildMasterTrackIndex < ChildMasterTracks.Num(); ChildMasterTrackIndex++)
		{
			const UMovieSceneTrack* ChildTrack = ChildMasterTracks[ChildMasterTrackIndex];
			if (!OwningScene->GetMasterTracks().Contains(ChildTrack))
			{
				ChildMasterTracks.RemoveAt(ChildMasterTrackIndex);
				ChildMasterTrackIndex--;

				UE_LOG(LogMovieScene, Warning, TEXT("Folder (%s) in Sequence (%s) contained a reference to a Master Track (%s) that no longer exists in the sequence, removing."), *GetFolderName().ToString(), *OwningScene->GetPathName(), *GetNameSafe(ChildTrack));
			}
		}

		// Validate child Object Bindings
		for (int32 ChildObjectBindingIndex = 0; ChildObjectBindingIndex < ChildObjectBindings.Num(); ChildObjectBindingIndex++)
		{
			const FGuid& ChildBinding = ChildObjectBindings[ChildObjectBindingIndex];
			if (!OwningScene->FindBinding(ChildBinding))
			{
				ChildObjectBindings.RemoveAt(ChildObjectBindingIndex);
				ChildObjectBindingIndex--;

				UE_LOG(LogMovieScene, Warning, TEXT("Folder (%s) in Sequence (%s) contained a reference to an Object Binding (%s) that no longer exists in the sequence, removing."), *GetFolderName().ToString(), *OwningScene->GetPathName(), *ChildBinding.ToString());
			}
		}

		// A folder should exist in only one place in the tree, as a child of ourself. If they exist in more
		// than one place, two folders point to the same actual UObject, so we'll remove it from ourself. When
		// the that folder is PostLoaded it will search the whole tree again and only find the one reference as
		// our reference will no longer exist.
		TArray<UMovieSceneFolder*> AllFolders;
		GetMovieSceneFoldersRecursive(OwningScene->GetRootFolders(), AllFolders);
		for (int32 ChildFolderIndex = 0; ChildFolderIndex < ChildFolders.Num(); ChildFolderIndex++)
		{
			int32 NumFolderInstances = Algo::Count(AllFolders, ChildFolders[ChildFolderIndex]);
			if (NumFolderInstances > 1)
			{
				ChildFolders.RemoveAt(ChildFolderIndex);
				ChildFolderIndex--;

				UE_LOG(LogMovieScene, Warning, TEXT("Folder (%s) in Sequence (%s) contained a reference to an Folder (%s) that exists in multiple places in the sequence, removing."), *GetFolderName().ToString(), *OwningScene->GetPathName(), *ChildFolders[ChildFolderIndex]->GetFolderName().ToString());
			}
		}
	}
#endif

	Super::PostLoad();
}

UMovieSceneFolder* UMovieSceneFolder::FindFolderContaining(const FGuid& InObjectBinding)
{
	for (FGuid ChildGuid : GetChildObjectBindings())
	{
		if (ChildGuid == InObjectBinding)
		{
			return this;
		}
	}

	for (UMovieSceneFolder* ChildFolder : GetChildFolders())
	{
		UMovieSceneFolder* Folder = ChildFolder->FindFolderContaining(InObjectBinding);
		if (Folder != nullptr)
		{
			return Folder;
		}
	}

	return nullptr;
}

void UMovieSceneFolder::Serialize( FArchive& Archive )
{
	if ( Archive.IsLoading() )
	{
		Super::Serialize( Archive );
		ChildObjectBindings.Empty();
		for ( const FString& ChildObjectBindingString : ChildObjectBindingStrings )
		{
			FGuid ChildObjectBinding;
			FGuid::Parse( ChildObjectBindingString, ChildObjectBinding );
			ChildObjectBindings.Add( ChildObjectBinding );
		}
	}
	else
	{
		ChildObjectBindingStrings.Empty();
		for ( const FGuid& ChildObjectBinding : ChildObjectBindings )
		{
			ChildObjectBindingStrings.Add( ChildObjectBinding.ToString() );
		}
		Super::Serialize( Archive );
	}
}
