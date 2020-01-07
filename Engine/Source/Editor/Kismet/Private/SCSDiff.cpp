// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCSDiff.h"
#include "Widgets/Layout/SSplitter.h"
#include "GameFramework/Actor.h"
#include "SKismetInspector.h"
#include "SSCSEditor.h"
#include "IDetailsView.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include <vector>

FSCSDiff::FSCSDiff(const UBlueprint* InBlueprint)
{
	if (!FBlueprintEditorUtils::SupportsConstructionScript(InBlueprint) || InBlueprint->SimpleConstructionScript == NULL)
	{
		ContainerWidget = SNew(SBox);
		return;
	}

	// Need to pull off const because the ICH access functions theoretically could modify it
	Blueprint = const_cast<UBlueprint*>(InBlueprint);

	Inspector = SNew(SKismetInspector)
		.HideNameArea(true)
		.ViewIdentifier(FName("BlueprintInspector"))
		.IsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateStatic([] { return false; }));

	ContainerWidget = SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+ SSplitter::Slot()
		[
			SAssignNew(SCSEditor, SSCSEditor)
				.ActorContext(InBlueprint->GeneratedClass->GetDefaultObject<AActor>())
				.AllowEditing(false)
				.HideComponentClassCombo(true)
				.OnSelectionUpdated(SSCSEditor::FOnSelectionUpdated::CreateRaw(this, &FSCSDiff::OnSCSEditorUpdateSelectionFromNodes))
				.OnHighlightPropertyInDetailsView(SSCSEditor::FOnHighlightPropertyInDetailsView::CreateRaw(this, &FSCSDiff::OnSCSEditorHighlightPropertyInDetailsView))
				.IsDiffing(true)
		]
		+ SSplitter::Slot()
		[
			Inspector.ToSharedRef()
		];
}

void FSCSDiff::HighlightProperty(FName VarName, FPropertySoftPath Property)
{
	if (SCSEditor.IsValid())
	{
		check(VarName != FName());
		SCSEditor->HighlightTreeNode(VarName, FPropertyPath());
	}
}

TSharedRef< SWidget > FSCSDiff::TreeWidget()
{
	return ContainerWidget.ToSharedRef();
}

void GetDisplayedHierarchyRecursive(UBlueprint* Blueprint, TArray< int32 >& TreeAddress, const FSCSEditorTreeNode& Node, TArray< FSCSResolvedIdentifier >& OutResult)
{
	FSCSIdentifier Identifier = { Node.GetVariableName(), TreeAddress };
	FSCSResolvedIdentifier ResolvedIdentifier = { Identifier, Node.GetOrCreateEditableComponentTemplate(Blueprint) };
	OutResult.Push(ResolvedIdentifier);
	const auto& Children = Node.GetChildren();
	for (int32 Iter = 0; Iter != Children.Num(); ++Iter)
	{
		TreeAddress.Push(Iter);
		GetDisplayedHierarchyRecursive(Blueprint, TreeAddress, *Children[Iter], OutResult);
		TreeAddress.Pop();
	}
}

TArray< FSCSResolvedIdentifier > FSCSDiff::GetDisplayedHierarchy() const
{
	TArray< FSCSResolvedIdentifier > Ret;

	if( SCSEditor.IsValid() && SCSEditor->GetActorNode().IsValid())
	{
		const TArray<FSCSEditorTreeNodePtrType>& RootNodes = SCSEditor->GetActorNode()->GetComponentNodes();
		for (int32 Iter = 0; Iter != RootNodes.Num(); ++Iter)
		{
			TArray< int32 > TreeAddress;
			TreeAddress.Push(Iter);
			GetDisplayedHierarchyRecursive(Blueprint, TreeAddress, *RootNodes[Iter], Ret);
		}
	}

	return Ret;
}

void FSCSDiff::OnSCSEditorUpdateSelectionFromNodes(const TArray<FSCSEditorTreeNodePtrType>& SelectedNodes)
{
	FText InspectorTitle = FText::GetEmpty();
	TArray<UObject*> InspectorObjects;
	InspectorObjects.Empty(SelectedNodes.Num());
	for (auto NodeIt = SelectedNodes.CreateConstIterator(); NodeIt; ++NodeIt)
	{
		auto NodePtr = *NodeIt;
		if(NodePtr.IsValid() && NodePtr->CanEditDefaults())
		{
			InspectorTitle = FText::FromString(NodePtr->GetDisplayString());
			InspectorObjects.Add(NodePtr->GetOrCreateEditableComponentTemplate(Blueprint));
		}
	}

	if( Inspector.IsValid() )
	{
		SKismetInspector::FShowDetailsOptions Options(InspectorTitle, true);
		Inspector->ShowDetailsForObjects(InspectorObjects, Options);
	}
}

void FSCSDiff::OnSCSEditorHighlightPropertyInDetailsView(const FPropertyPath& InPropertyPath)
{
	if( Inspector.IsValid() )
	{
		Inspector->GetPropertyView()->HighlightProperty(InPropertyPath);
	}
}
