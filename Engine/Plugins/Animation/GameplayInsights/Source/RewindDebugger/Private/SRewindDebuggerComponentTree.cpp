// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRewindDebuggerComponentTree.h"
#include "ObjectTrace.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "SAnimationInsights"

SRewindDebuggerComponentTree::SRewindDebuggerComponentTree() 
	: SCompoundWidget()
	, DebugComponents(nullptr)
{ 
}

SRewindDebuggerComponentTree::~SRewindDebuggerComponentTree() 
{
}

TSharedRef<ITableRow> ComponentTreeViewGenerateRow(TSharedPtr<FDebugObjectInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString ReadableName = InItem->ObjectName;

	FSlateIcon ObjectIcon = FSlateIconFinder::FindIconForClass(UObject::StaticClass());

	if (UObject* Object = FObjectTrace::GetObjectFromId(InItem->ObjectId))
	{
		if (AActor* Actor = Cast<AActor>(Object))
		{
			ReadableName = Actor->GetActorLabel();
		}
		else if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			ReadableName = Component->GetName();
		}

		ObjectIcon = FSlateIconFinder::FindIconForClass(Object->GetClass());
	}

	return 
		SNew(STableRow<TSharedPtr<FDebugObjectInfo>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot().AutoWidth().Padding(3)
			[
				SNew(SImage)
				.Image(ObjectIcon.GetIcon())
			]
			+SHorizontalBox::Slot().Padding(3)
			[
				SNew(STextBlock)
				.Text(FText::FromString(ReadableName))
			]
		];
}

void ComponentTreeViewGetChildren(TSharedPtr<FDebugObjectInfo> InItem, TArray<TSharedPtr<FDebugObjectInfo>>& OutChildren)
{
	OutChildren.Append(InItem->Children);
}

void ComponentTreeViewExpansionChanged(TSharedPtr<FDebugObjectInfo> InItem, bool bShouldBeExpanded)
{
	InItem->bExpanded = bShouldBeExpanded;
}



void SRewindDebuggerComponentTree::Construct(const FArguments& InArgs)
{
	DebugComponents = InArgs._DebugComponents;

	ComponentTreeView = SNew(STreeView<TSharedPtr<FDebugObjectInfo>>)
									.ItemHeight(16.0f)
									.TreeItemsSource(DebugComponents)
									.OnGenerateRow_Static(&ComponentTreeViewGenerateRow)
									.OnGetChildren_Static(&ComponentTreeViewGetChildren)
									.OnExpansionChanged_Static(&ComponentTreeViewExpansionChanged)
									.SelectionMode(ESelectionMode::Single)
									.OnSelectionChanged(InArgs._OnSelectionChanged)
									.OnMouseButtonDoubleClick(InArgs._OnMouseButtonDoubleClick)
									.OnContextMenuOpening(InArgs._OnContextMenuOpening);

	ChildSlot
	[
		ComponentTreeView.ToSharedRef()
	];
}

void RestoreExpansion(TArray<TSharedPtr<FDebugObjectInfo>>& Components, TSharedPtr<STreeView<TSharedPtr<FDebugObjectInfo>>>& TreeView)
{
	for(TSharedPtr<FDebugObjectInfo> &Component : Components)
	{
		TreeView->SetItemExpansion(Component, Component->bExpanded);
		RestoreExpansion(Component->Children, TreeView);
	}
}

void SRewindDebuggerComponentTree::Refresh()
{
	ComponentTreeView->RebuildList();

	if (DebugComponents)
	{
		// make sure any newly added TreeView nodes are created expanded
		RestoreExpansion(*DebugComponents, ComponentTreeView);
	}
}

#undef LOCTEXT_NAMESPACE
