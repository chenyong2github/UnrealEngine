// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorActorUtilities.h"
#include "CoreMinimal.h"
#include "Engine/MeshMerging.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealEdGlobals.h"
#include "ScopedTransaction.h"
#include "Engine/Brush.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "AssetSelection.h"

#define LOCTEXT_NAMESPACE "EditorActorUtilities"

void UEditorActorUtilities::DuplicateSelectedActors(UWorld* InWorld)
{
	if (!GEditor || !InWorld)
	{
		return;
	}

	bool bComponentsSelected = GEditor->GetSelectedComponentCount() > 0;
	//@todo locked levels - if all actor levels are locked, cancel the transaction
	const FScopedTransaction Transaction(bComponentsSelected ? NSLOCTEXT("UnrealEd", "DuplicateComponents", "Duplicate Components") : NSLOCTEXT("UnrealEd", "DuplicateActors", "Duplicate Actors"));

	FEditorDelegates::OnDuplicateActorsBegin.Broadcast();

	// duplicate selected
	ABrush::SetSuppressBSPRegeneration(true);
	GEditor->edactDuplicateSelected(InWorld->GetCurrentLevel(), GetDefault<ULevelEditorViewportSettings>()->GridEnabled);
	ABrush::SetSuppressBSPRegeneration(false);

	// Find out if any of the selected actors will change the BSP.
	// and only then rebuild BSP as this is expensive.
	const FSelectedActorInfo& SelectedActors = AssetSelectionUtils::GetSelectedActorInfo();
	if (SelectedActors.bHaveBrush)
	{
		GEditor->RebuildAlteredBSP(); // Update the Bsp of any levels containing a modified brush
	}

	FEditorDelegates::OnDuplicateActorsEnd.Broadcast();

	GEditor->RedrawLevelEditingViewports();
}

void UEditorActorUtilities::DeleteSelectedActors(UWorld* InWorld)
{
	if (!GEditor || !InWorld)
	{
		return;
	}

	bool bComponentsSelected = GEditor->GetSelectedComponentCount() > 0;

	const FScopedTransaction Transaction(bComponentsSelected ? NSLOCTEXT("UnrealEd", "DeleteComponents", "Delete Components") : NSLOCTEXT("UnrealEd", "DeleteActors", "Delete Actors"));

	FEditorDelegates::OnDeleteActorsBegin.Broadcast();
	GEditor->edactDeleteSelected(InWorld);
	FEditorDelegates::OnDeleteActorsEnd.Broadcast();
}

void UEditorActorUtilities::InvertSelection(UWorld* InWorld)
{
	if (!GUnrealEd || !InWorld)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SelectInvert", "Select Invert"));
	GUnrealEd->edactSelectInvert(InWorld);
}

void UEditorActorUtilities::SelectAll(UWorld* InWorld)
{
	if (!GUnrealEd || !InWorld)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SelectAll", "Select All"));
	GUnrealEd->edactSelectAll(InWorld);
}

void UEditorActorUtilities::SelectAllChildren(bool bRecurseChildren)
{
	if (!GUnrealEd)
	{
		return;
	}

	if (bRecurseChildren)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SelectAllDescendants", "Select All Descendants"));
	}
	else
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SelectAllChildren", "Select All Children"));
	}

	GUnrealEd->edactSelectAllChildren(bRecurseChildren);
}

#undef LOCTEXT_NAMESPACE
