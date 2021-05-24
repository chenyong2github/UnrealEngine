// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRewindDebuggerComponentTree.h"
#include "Async/Future.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IGameplayInsightsModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Views/SListView.h"

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

TSharedPtr<SWidget> SRewindDebuggerComponentTree::ComponentTreeOnContextMenuOpening()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);
	IGameplayInsightsModule& GameplayInsightsModule = FModuleManager::LoadModuleChecked<IGameplayInsightsModule>("GameplayInsights");

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("Property Tracing", "Property Tracing"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Trace Object Properties", "Trace Object Properties"),
			LOCTEXT("Trace Object Properties Tooltip", "Record this object's properties so they will show in the Rewind Debugger when scrubbing."),
			FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([this, &GameplayInsightsModule]()
				{
					TArray<TSharedPtr<FDebugObjectInfo>> SelectedObjects = ComponentTreeView->GetSelectedItems();

					if (SelectedObjects.Num() > 0)
					{
						if (UObject* Object = FObjectTrace::GetObjectFromId(SelectedObjects[0]->ObjectId))
						{
							GameplayInsightsModule.EnableObjectPropertyTrace(Object, !GameplayInsightsModule.IsObjectPropertyTraceEnabled(Object));
						}
					}
				}),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([this, &GameplayInsightsModule]()
				{
					bool bEnabled = false;
					TArray<TSharedPtr<FDebugObjectInfo>> SelectedObjects = ComponentTreeView->GetSelectedItems();

					if (SelectedObjects.Num() > 0)
					{
						if (UObject* Object = FObjectTrace::GetObjectFromId(SelectedObjects[0]->ObjectId))
						{
							bEnabled = GameplayInsightsModule.IsObjectPropertyTraceEnabled(Object);
						}
					}
                    return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}
				),
				FIsActionButtonVisible()
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
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
									.OnContextMenuOpening(this, &SRewindDebuggerComponentTree::ComponentTreeOnContextMenuOpening);

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
