// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Filter/LevelSnapshotsEditorFilterClass.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Engine/Brush.h"

#include "ClassViewerFilter.h"
#include "PropertyHandle.h"

#include "LevelSnapshotFiltersBasic.h"
#include "LevelSnapshotFilters.h"

FLevelSnapshotsEditorFilterClass::FLevelSnapshotsEditorFilterClass(UClass* InClass)
{
	Class = InClass;
	ClassName = MakeShareable(new FString(Class->GetName()));
	ClassDisplayName = MakeShareable(new FString(Class->GetDisplayNameText().ToString()));
	ClassPath = FName(*Class->GetPathName());

	if (Class->GetSuperClass())
	{
		ParentClassPath = FName(*Class->GetSuperClass()->GetPathName());
	}

	if (Class->ClassGeneratedBy && Class->ClassGeneratedBy->IsA(UBlueprint::StaticClass()))
	{
		Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
	}
	else
	{
		Blueprint = nullptr;
	}

	bPassesFilter = false;
	bPassesFilterRegardlessTextFilter = false;
}

FLevelSnapshotsEditorFilterClass::FLevelSnapshotsEditorFilterClass(const FString& InClassName, const FString& InClassDisplayName)
{
	ClassName = MakeShareable(new FString(InClassName));
	ClassDisplayName = MakeShareable(new FString(InClassDisplayName));
	bPassesFilter = false;
	bPassesFilterRegardlessTextFilter = false;

	Class = nullptr;
	Blueprint = nullptr;
}

FLevelSnapshotsEditorFilterClass::FLevelSnapshotsEditorFilterClass( const FLevelSnapshotsEditorFilterClass& InCopyObject)
{
	ClassName = InCopyObject.ClassName;
	ClassDisplayName = InCopyObject.ClassDisplayName;
	bPassesFilter = InCopyObject.bPassesFilter;
	bPassesFilterRegardlessTextFilter = InCopyObject.bPassesFilterRegardlessTextFilter;

	Class = InCopyObject.Class;
	Blueprint = InCopyObject.Blueprint;
	
	UnloadedBlueprintData = InCopyObject.UnloadedBlueprintData;

	ClassPath = InCopyObject.ClassPath;
	ParentClassPath = InCopyObject.ParentClassPath;
	ClassName = InCopyObject.ClassName;
	BlueprintAssetPath = InCopyObject.BlueprintAssetPath;

	// We do not want to copy the child list, do not add it. It should be the only item missing.
}

/**
 * Adds the specified child to the node.
 *
 * @param	Child							The child to be added to this node for the tree.
 */
void FLevelSnapshotsEditorFilterClass::AddChild( TSharedPtr<FLevelSnapshotsEditorFilterClass> Child )
{
	check(Child.IsValid());
	Child->ParentNode = AsShared();
	ChildrenList.Add(Child);
}

void FLevelSnapshotsEditorFilterClass::AddUniqueChild(TSharedPtr<FLevelSnapshotsEditorFilterClass> NewChild)
{
	check(NewChild.IsValid());
	const UClass* NewChildClass = NewChild->Class.Get();
	if (nullptr != NewChildClass)
	{
		for(int ChildIndex = 0; ChildIndex < ChildrenList.Num(); ++ChildIndex)
		{
			TSharedPtr<FLevelSnapshotsEditorFilterClass> OldChild = ChildrenList[ChildIndex];
			if(OldChild.IsValid() && OldChild->Class == NewChildClass)
			{
				const bool bNewChildHasMoreInfo = NewChild->UnloadedBlueprintData.IsValid();
				const bool bOldChildHasMoreInfo = OldChild->UnloadedBlueprintData.IsValid();
				if(bNewChildHasMoreInfo && !bOldChildHasMoreInfo)
				{
					// make sure, that new child has all needed children
					for(int OldChildIndex = 0; OldChildIndex < OldChild->ChildrenList.Num(); ++OldChildIndex)
					{
						NewChild->AddUniqueChild( OldChild->ChildrenList[OldChildIndex] );
					}

					// replace child
					ChildrenList[ChildIndex] = NewChild;
				}
				return;
			}
		}
	}

	AddChild(NewChild);
}

bool FLevelSnapshotsEditorFilterClass::IsRestricted() const
{
	return PropertyHandle.IsValid() && PropertyHandle->IsRestricted(*ClassName);
}

bool FLevelSnapshotsEditorFilterClass::IsBasicClass() const
{
	if (Class->IsChildOf(ULevelSnapshotFiltersBasic::StaticClass()))
	{
		return true;
	}

	return false;
}

TSharedPtr<FString> FLevelSnapshotsEditorFilterClass::GetClassName(EClassViewerNameTypeToDisplay NameType) const
{
	switch (NameType)
	{
	case EClassViewerNameTypeToDisplay::ClassName:
		return ClassName;
		break;
	case EClassViewerNameTypeToDisplay::DisplayName:
		return ClassDisplayName;
		break;
	case EClassViewerNameTypeToDisplay::Dynamic:
		FString CombinedName;
		FString SanitizedName = FName::NameToDisplayString(*ClassName.Get(), false);
		if (ClassDisplayName.IsValid() && !ClassDisplayName->IsEmpty() && !ClassDisplayName->Equals(SanitizedName) && !ClassDisplayName->Equals(*ClassName.Get()))
		{
			TArray<FStringFormatArg> Args;
			Args.Add(*ClassName.Get());
			Args.Add(*ClassDisplayName.Get());
			CombinedName = FString::Format(TEXT("{0} ({1})"), Args);
		}
		else
		{
			CombinedName = *ClassName.Get();
		}
		return MakeShareable(new FString(CombinedName));
		break;
	}

	ensureMsgf(false, TEXT("FClassViewerNode::GetClassName called with invalid name type."));
	return ClassName;
}

bool FLevelSnapshotsEditorFilterClass::IsClassPlaceable() const
{
	const UClass* LoadedClass = Class.Get();
	if (LoadedClass)
	{
		const bool bPlaceableFlags = !LoadedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable);
		const bool bBasedOnActor = LoadedClass->IsChildOf(AActor::StaticClass());
		const bool bNotABrush = !LoadedClass->IsChildOf(ABrush::StaticClass());
		return bPlaceableFlags && bBasedOnActor && bNotABrush;
	}

	if (UnloadedBlueprintData.IsValid())
	{
		const bool bPlaceableFlags = !UnloadedBlueprintData->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable);
		const bool bBasedOnActor = UnloadedBlueprintData->IsChildOf(AActor::StaticClass());
		const bool bNotABrush = !UnloadedBlueprintData->IsChildOf(ABrush::StaticClass());
		return bPlaceableFlags && bBasedOnActor && bNotABrush;
	}

	return false;
}

bool FLevelSnapshotsEditorFilterClass::IsBlueprintClass() const
{
	if (Class->IsChildOf(ULevelSnapshotBlueprintFilter::StaticClass()))
	{
		return true;
	}

	return false;
}

bool FLevelSnapshotsEditorFilterClass::IsEditorOnlyClass() const
{
	return Class.IsValid() && IsEditorOnlyObject(Class.Get());
}