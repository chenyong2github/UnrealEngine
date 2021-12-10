// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/PoseWatch.h"
#include "Animation/AnimBlueprint.h"

#if WITH_EDITOR
#include "AnimationEditorUtils.h"
#endif

#define LOCTEXT_NAMESPACE "PoseWatch"

UPoseWatchFolder::UPoseWatchFolder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	Label = GetDefaultLabel();
#endif
}

UPoseWatch::UPoseWatch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	Label = GetDefaultLabel();
	SetColor(PoseWatchUtil::ChoosePoseWatchColor());
#endif
}

#if WITH_EDITOR


// PoseWatchUtil

TSet<UPoseWatch*> PoseWatchUtil::GetChildrenPoseWatchOf(const UPoseWatchFolder* Folder, const UAnimBlueprint* AnimBlueprint)
{
	TSet<UPoseWatch*> Children;

	for (UPoseWatch* SomePoseWatch : AnimBlueprint->PoseWatches)
	{
		if (SomePoseWatch->IsIn(Folder))
		{
			Children.Add(SomePoseWatch);
		}
	}

	return Children;
}

TSet<UPoseWatchFolder*> PoseWatchUtil::GetChildrenPoseWatchFoldersOf(const UPoseWatchFolder* Folder, const UAnimBlueprint* AnimBlueprint)
{
	TSet<UPoseWatchFolder*> Children;

	for (UPoseWatchFolder* SomePoseWatchFolder : AnimBlueprint->PoseWatchFolders)
	{
		if (SomePoseWatchFolder->IsIn(Folder))
		{
			Children.Add(SomePoseWatchFolder);
		}
	}

	return Children;
}

template <typename T>
T* PoseWatchUtil::FindInFolderInCollection(const FName& Label, UPoseWatchFolder* InFolder, const TArray<TObjectPtr<T>>& PoseWatchCollection)
{
	for (const TObjectPtr<T>& PoseWatchItem : PoseWatchCollection)
	{
		if (PoseWatchItem->IsIn(InFolder) && PoseWatchItem->GetLabel().ToString().Equals(Label.ToString()))
		{
			return PoseWatchItem;
		}
	}
	return nullptr;
}

template <typename T>
FText PoseWatchUtil::FindUniqueNameInFolder(UPoseWatchFolder* InParent, const T* Item, const TArray<TObjectPtr<T>>& Collection)
{
	FName NewName(Item->GetLabel().ToString());
	FString BaseLabel = Item->GetLabel().ToString();
	int32 Index = 0;
	T* ConflictingItem = nullptr;
	do
	{
		++Index;
		NewName = *FString::Printf(TEXT("%s%d"), *BaseLabel, Index);
		ConflictingItem = FindInFolderInCollection(NewName, InParent, Collection);
	} while (ConflictingItem != nullptr && ConflictingItem != Item);

	return FText::FromName(NewName);
}

FColor PoseWatchUtil::ChoosePoseWatchColor()
{
	return FColor::MakeRandomColor();
}


// UPoseWatchFolder

const FText UPoseWatchFolder::GetPath() const
{
	if (Parent.Get() != nullptr)
	{
		return FText::Format(LOCTEXT("Path", "{0}/{1}"), Parent->GetPath(), Label);
	}
	return Label;
}

FText UPoseWatchFolder::GetDefaultLabel() const
{
	return LOCTEXT("PoseWatchFolderDefaultName", "NewFolder");
}

FText UPoseWatchFolder::GetLabel() const
{
	return Label;
}

bool UPoseWatchFolder::GetIsVisible() const
{
	return bIsVisible;
}

UPoseWatchFolder* UPoseWatchFolder::GetParent() const
{
	return Parent.Get();
}

bool UPoseWatchFolder::SetParent(UPoseWatchFolder* InParent, bool bForce)
{
	if (IsFolderLabelUniqueInFolder(Label, InParent))
	{
		Parent = InParent;
		return true;
	}
	else if (bForce)
	{
		Label = FindUniqueNameInFolder(InParent);
		check(IsFolderLabelUniqueInFolder(Label, InParent))
		Parent = InParent;
		return true;
	}
	return false;
}

bool UPoseWatchFolder::IsFolderLabelUniqueInFolder(const FText& InLabel, UPoseWatchFolder* InFolder) const
{
	for (UPoseWatchFolder* SomeChildFolder : PoseWatchUtil::GetChildrenPoseWatchFoldersOf(InFolder, GetAnimBlueprint()))
	{
		if (SomeChildFolder->GetLabel().ToString().Equals(InLabel.ToString()))
		{
			if (SomeChildFolder != this)
			{
				return false;
			}
		}
	}
	return true;
}

void UPoseWatchFolder::MoveTo(UPoseWatchFolder* InFolder)
{
	SetParent(InFolder);
}

bool UPoseWatchFolder::SetLabel(const FText& InLabel)
{
	if (IsFolderLabelUniqueInFolder(InLabel, Parent.Get()))
	{
		Label = InLabel;
		return true;
	}
	return false;
}

void UPoseWatchFolder::SetIsVisible(bool bInIsVisible, bool bUpdateChildren)
{
	// Can only become visible if there are no children descendents
	if (!HasPoseWatchDescendents() && bInIsVisible)
	{
		bIsVisible = false;
		return;
	}

	bIsVisible = bInIsVisible;

	if (bUpdateChildren)
	{
		for (UPoseWatch* SomePoseWatch : PoseWatchUtil::GetChildrenPoseWatchOf(this, GetAnimBlueprint()))
		{
			SomePoseWatch->SetIsVisible(bInIsVisible);
		}
		for (UPoseWatchFolder* SomeChildFolder : PoseWatchUtil::GetChildrenPoseWatchFoldersOf(this, GetAnimBlueprint()))
		{
			SomeChildFolder->SetIsVisible(bInIsVisible);
		}
	}
}

void UPoseWatchFolder::SetIsExpanded(bool bInIsExpanded)
{
	bIsExpanded = bInIsExpanded;
}

bool UPoseWatchFolder::GetIsExpanded() const
{
	return bIsExpanded;
}

void UPoseWatchFolder::OnRemoved()
{
	// Move all this folder's children to this folder's parent
	for (UPoseWatch* SomePoseWatch : PoseWatchUtil::GetChildrenPoseWatchOf(this, GetAnimBlueprint()))
	{
		SomePoseWatch->SetParent(Parent.Get(), /* bForce*/ true);
	}
	for (UPoseWatchFolder* SomePoseWatchFolder : PoseWatchUtil::GetChildrenPoseWatchFoldersOf(this, GetAnimBlueprint()))
	{
		SomePoseWatchFolder->SetParent(Parent.Get(), /* bForce*/ true);
	}

	UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(GetOuter());
	AnimBlueprint->PoseWatchFolders.Remove(this);

	if (Parent.IsValid())
	{
		Parent->UpdateVisibility();
	}

	AnimationEditorUtils::OnPoseWatchesChanged().Broadcast(GetAnimBlueprint(), nullptr);
}

void UPoseWatchFolder::UpdateVisibility()
{
	bool bNewIsVisible = false;

	for (UPoseWatch* SomePoseWatch : PoseWatchUtil::GetChildrenPoseWatchOf(this, GetAnimBlueprint()))
	{
		bNewIsVisible |= SomePoseWatch->GetIsVisible();
	}
	for (UPoseWatchFolder* SomePoseWatchFolder : PoseWatchUtil::GetChildrenPoseWatchFoldersOf(this, GetAnimBlueprint()))
	{
		bNewIsVisible |= SomePoseWatchFolder->GetIsVisible();
	}

	SetIsVisible(bNewIsVisible, false);
	if (Parent.IsValid())
	{
		Parent->UpdateVisibility();
	}
}

UAnimBlueprint* UPoseWatchFolder::GetAnimBlueprint() const
{
	return  CastChecked<UAnimBlueprint>(GetOuter());
}

bool UPoseWatchFolder::IsIn(const UPoseWatchFolder* InFolder) const
{
	return Parent.Get() == InFolder;
}

bool UPoseWatchFolder::IsDescendantOf(const UPoseWatchFolder* InFolder) const
{
	if (IsIn(InFolder))
	{
		return true;
	}

	TWeakObjectPtr<UPoseWatchFolder> ParentFolder = Parent;
	while (ParentFolder.IsValid())
	{
		if (ParentFolder->IsIn(InFolder))
		{
			return true;
		}
		ParentFolder = ParentFolder->Parent;
	}
	return false;
}

bool UPoseWatchFolder::IsAssignedFolder() const
{
	return Parent != nullptr;
}

bool UPoseWatchFolder::ValidateLabelRename(const FText& InLabel, FText& OutErrorMessage)
{
	if (!IsFolderLabelUniqueInFolder(InLabel, Parent.Get()))
	{
		OutErrorMessage = LOCTEXT("PoseWatchFolderNameTaken", "A folder already has this name at this level");
		return false;
	}
	return true;
}

bool UPoseWatchFolder::HasChildren() const
{
	if (PoseWatchUtil::GetChildrenPoseWatchFoldersOf(this, GetAnimBlueprint()).Num() > 0)
	{
		return true;
	}
	if (PoseWatchUtil::GetChildrenPoseWatchOf(this, GetAnimBlueprint()).Num() > 0)
	{
		return true;
	}
	return false;
}

void UPoseWatchFolder::SetUniqueDefaultLabel()
{
	Label = GetDefaultLabel();
	Label = FindUniqueNameInFolder(Parent.Get());
}

FText UPoseWatchFolder::FindUniqueNameInFolder(UPoseWatchFolder* InParent) const
{
	return PoseWatchUtil::FindUniqueNameInFolder(InParent, this, GetAnimBlueprint()->PoseWatchFolders);
}

bool UPoseWatchFolder::HasPoseWatchChildren() const
{
	return PoseWatchUtil::GetChildrenPoseWatchOf(this, GetAnimBlueprint()).Num() > 0;
}

bool UPoseWatchFolder::HasPoseWatchDescendents() const
{
	if (HasPoseWatchChildren())
	{
		return true;
	}
	for (UPoseWatchFolder* SomePoseWatchFolder : PoseWatchUtil::GetChildrenPoseWatchFoldersOf(this, GetAnimBlueprint()))
	{
		if (SomePoseWatchFolder->HasPoseWatchDescendents())
		{
			return true;
		}
	}
	return false;
}

// UPoseWatch

const FText UPoseWatch::GetPath() const
{
	check(!Label.IsEmpty())
	if (Parent.Get() != nullptr)
	{
		return FText::Format(LOCTEXT("Path", "{0}/{1}"), Parent->GetPath(), Label);
	}
	return Label;
}

FText UPoseWatch::GetLabel() const
{
	return Label;
}

FText UPoseWatch::GetDefaultLabel() const
{
	if (Node.IsValid())
	{
		return Node->GetNodeTitle(ENodeTitleType::ListView);
	}
	return LOCTEXT("NewPoseWatch", "NewPoseWatch");
}

bool UPoseWatch::GetIsVisible() const
{
	return bIsVisible;
}

FColor UPoseWatch::GetColor() const
{
	return Color;
}

bool UPoseWatch::GetShouldDeleteOnDeselect() const
{
	return bDeleteOnDeselection;
}

void UPoseWatch::OnRemoved()
{
	UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(GetOuter());
	AnimBlueprint->PoseWatches.Remove(this);
	
	AnimationEditorUtils::RemovePoseWatch(this, AnimBlueprint);


	if (Parent.IsValid())
	{
		Parent->UpdateVisibility();
	}

	AnimationEditorUtils::OnPoseWatchesChanged().Broadcast(GetAnimBlueprint(), Node.Get());
}

UPoseWatchFolder* UPoseWatch::GetParent() const
{
	return Parent.Get();
}

bool UPoseWatch::SetParent(UPoseWatchFolder* InParent, bool bForce)
{
	if (!IsPoseWatchLabelUniqueInFolder(Label, InParent))
	{
		if (!bForce)
		{
			return false;
		}
		Label = FindUniqueNameInFolder(InParent);
	}

	UPoseWatchFolder* OldParent = Parent.Get();
	Parent = InParent;

	if (OldParent)
	{
		OldParent->UpdateVisibility();
	}

	if (InParent)
	{
		InParent->UpdateVisibility();
		InParent->SetIsExpanded(true);
	}

	return true;
}

bool UPoseWatch::GetIsEnabled() const
{
	return bIsEnabled;
}

void UPoseWatch::SetIsEnabled(bool bInIsEnabled)
{
	bIsEnabled = bInIsEnabled;
}

void UPoseWatch::MoveTo(UPoseWatchFolder* InFolder)
{
	SetParent(InFolder);
}

bool UPoseWatch::SetLabel(const FText& InLabel)
{
	UPoseWatch* ConflictingPoseWatch = nullptr;
	if (IsPoseWatchLabelUniqueInFolder(InLabel, Parent.Get()))
	{
		Label = InLabel;
		return true;
	}
	else if (ConflictingPoseWatch == this)
	{
		Label = InLabel;
		return true;
	}
	return false;
}

void UPoseWatch::SetIsVisible(bool bInIsVisible)
{
	bIsVisible = bInIsVisible;

	if (Parent.IsValid())
	{
		Parent->UpdateVisibility();
	}
}

void UPoseWatch::SetColor(const FColor& InColor)
{
	Color = InColor;
}

void UPoseWatch::SetShouldDeleteOnDeselect(const bool bInDeleteOnDeselection)
{
	bDeleteOnDeselection = bInDeleteOnDeselection;
}

void UPoseWatch::ToggleIsVisible()
{
	SetIsVisible(!bIsVisible);
}

bool UPoseWatch::IsIn(const UPoseWatchFolder* InFolder) const
{
	return Parent.Get() == InFolder;
}

bool UPoseWatch::IsAssignedFolder() const
{
	return Parent != nullptr;
}

bool UPoseWatch::ValidateLabelRename(const FText& InLabel, FText& OutErrorMessage)
{
	UPoseWatch* ConflictingPoseWatch = nullptr;
	if (!IsPoseWatchLabelUniqueInFolder(InLabel, Parent.Get()))
	{
		if (ConflictingPoseWatch != this)
		{
			OutErrorMessage = LOCTEXT("PoseWatchNameTaken", "A pose watch already has this name at this level");
			return false;
		}
	}
	return true;
}

bool UPoseWatch::IsPoseWatchLabelUniqueInFolder(const FText& InLabel, UPoseWatchFolder* InFolder) const
{
	for (UPoseWatch* SomePoseWatch : PoseWatchUtil::GetChildrenPoseWatchOf(InFolder, GetAnimBlueprint()))
	{
		if (SomePoseWatch->GetLabel().ToString().Equals(InLabel.ToString()))
		{
			if (SomePoseWatch != this)
			{
				return false;
			}
		}
	}
	return true;
}

void UPoseWatch::SetUniqueDefaultLabel()
{
	Label = GetDefaultLabel();
	Label = FindUniqueNameInFolder(Parent.Get());
}

UAnimBlueprint* UPoseWatch::GetAnimBlueprint() const
{
	return  CastChecked<UAnimBlueprint>(GetOuter());
}

FText UPoseWatch::FindUniqueNameInFolder(UPoseWatchFolder* InParent) const
{
	return PoseWatchUtil::FindUniqueNameInFolder(InParent, this, GetAnimBlueprint()->PoseWatches);
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE