// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigOutliner.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "UnrealEd/Public/Selection.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ControlRigControlsProxy.h"
#include "ControlRigEditModeSettings.h"
#include "TimerManager.h"
#include "SRigHierarchyTreeView.h"
#include "Rigs/RigHierarchyController.h"
#include "ControlRigObjectBinding.h"

#define LOCTEXT_NAMESPACE "ControlRigOutliner"


void SControlRigOutlinerItem::Construct(const FArguments& InArgs)
{

	bIsChangingRigHierarchy = false;

	DisplaySettings.bShowBones = false;
	DisplaySettings.bShowControls = true;
	DisplaySettings.bShowNulls = false;
	DisplaySettings.bShowReferences = false;
	DisplaySettings.bShowRigidBodies = false;
	DisplaySettings.bHideParentsOnFilter = true;
	DisplaySettings.bFlattenHierarchyOnFilter = true;

	FRigTreeDelegates RigTreeDelegates;
	RigTreeDelegates.OnGetHierarchy = FOnGetRigTreeHierarchy::CreateSP(this, &SControlRigOutlinerItem::GetHierarchy);
	RigTreeDelegates.OnGetDisplaySettings = FOnGetRigTreeDisplaySettings::CreateSP(this, &SControlRigOutlinerItem::GetDisplaySettings);
	RigTreeDelegates.OnSelectionChanged = FOnRigTreeSelectionChanged::CreateSP(this, &SControlRigOutlinerItem::HandleSelectionChanged);

	FText AreaTitle;
	if (InArgs._ControlRig)
	{
		FString ControlRigName = InArgs._ControlRig->GetName();
		FString BoundObjectName;
		if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = InArgs._ControlRig->GetObjectBinding())
		{
			if (ObjectBinding->GetBoundObject())
			{
				AActor* Actor = ObjectBinding->GetBoundObject()->GetTypedOuter<AActor>();
				if (Actor)
				{
					BoundObjectName = Actor->GetActorLabel();
				}
			}
		}
		AreaTitle = FText::Format(LOCTEXT("ControlTitle", "{0}  ({1})"), FText::AsCultureInvariant(ControlRigName), FText::AsCultureInvariant((BoundObjectName)));
	}
	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(PickerExpander, SExpandableArea)
					.InitiallyCollapsed(false)
					//.AreaTitle(AreaTitle)
					//.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
					.BodyContent()
					[
						SAssignNew(HierarchyTreeView, SSearchableRigHierarchyTreeView)
						.RigTreeDelegates(RigTreeDelegates)
					]
					.HeaderContent()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ContentPadding(2)
								.ButtonStyle(FEditorStyle::Get(), "NoBorder")
								.OnClicked(this, &SControlRigOutlinerItem::OnToggleVisibility)
								.ToolTipText(LOCTEXT("ControlRigShapesVisibility", "Control Rig Shapes Visibility"))
								.IsEnabled(this, &SControlRigOutlinerItem::VisibilityToggleEnabled)
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								.Content()
								[
									SNew(SImage)
									.Image(this, &SControlRigOutlinerItem::GetVisibilityBrushForElement)
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(AreaTitle)
								.Justification(ETextJustify::Left)
								//.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
							]
						]
				]
		
			]
		];
	NewControlRigSet(InArgs._ControlRig);
}

FReply SControlRigOutlinerItem::OnToggleVisibility()
{
	if (UControlRig *ControlRig  = CurrentControlRig.Get())
	{
		ControlRig->ToggleControlsVisible();
	}
	return FReply::Handled();
}

bool SControlRigOutlinerItem::VisibilityToggleEnabled() const
{
	return CurrentControlRig.IsValid();
}

const FSlateBrush* SControlRigOutlinerItem::GetVisibilityBrushForElement() const
{
	if (UControlRig* ControlRig = CurrentControlRig.Get())
	{
		if (ControlRig->GetControlsVisible())
		{
			return IsHovered() ? FEditorStyle::GetBrush("Level.VisibleHighlightIcon16x") :
				FEditorStyle::GetBrush("Level.VisibleIcon16x");
		}
	}
	return IsHovered() ? FEditorStyle::GetBrush("Level.NotVisibleHighlightIcon16x") :
		FEditorStyle::GetBrush("Level.NotVisibleIcon16x");
}

void SControlRigOutlinerItem::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	if (CurrentControlRig.IsValid())
	{
		UObject* OldObject = CurrentControlRig.Get();
		UObject* NewObject = OldToNewInstanceMap.FindRef(OldObject);
		if (NewObject)
		{
			if (UControlRig* ControlRig = Cast<UControlRig>(NewObject))
			{
				NewControlRigSet(ControlRig);
			}
		}
	}
}

void SControlRigOutlinerItem::NewControlRigSet(UControlRig* ControlRig)
{
	if (CurrentControlRig.IsValid())
	{
		(CurrentControlRig.Get())->ControlSelected().RemoveAll(this);
	}
	CurrentControlRig = ControlRig;
	if (ControlRig)
	{
		ControlRig->ControlSelected().RemoveAll(this);
		ControlRig->ControlSelected().AddRaw(this, &SControlRigOutlinerItem::HandleControlSelected);
	}
	HierarchyTreeView->GetTreeView()->RefreshTreeView(true);
}

SControlRigOutlinerItem::SControlRigOutlinerItem()
{
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &SControlRigOutlinerItem::OnObjectsReplaced);
}

void SControlRigOutlinerItem::HandleControlSelected(UControlRig* Subject, FRigControlElement* ControlElement, bool bSelected)
{
	const FRigElementKey Key = ControlElement->GetKey();
	for (int32 RootIndex = 0; RootIndex < HierarchyTreeView->GetTreeView()->GetRootElements().Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeElement> Found = HierarchyTreeView->GetTreeView()->FindElement(Key, HierarchyTreeView->GetTreeView()->GetRootElements()[RootIndex]);
		if (Found.IsValid())
		{
			HierarchyTreeView->GetTreeView()->SetItemSelection(Found, bSelected, ESelectInfo::OnNavigation);

			TArray<TSharedPtr<FRigTreeElement>> SelectedItems = HierarchyTreeView->GetTreeView()->GetSelectedItems();
			for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
			{
				HierarchyTreeView->GetTreeView()->SetExpansionRecursive(SelectedItem, false, true);
			}

			if (SelectedItems.Num() > 0)
			{
				HierarchyTreeView->GetTreeView()->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}
}

const URigHierarchy* SControlRigOutlinerItem::GetHierarchy() const
{
	if (CurrentControlRig.IsValid())
	{
		return CurrentControlRig.Get()->GetHierarchy();
	}
	return nullptr;
}

void SControlRigOutlinerItem::HandleSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo)
{
	if (bIsChangingRigHierarchy)
	{
		return;
	}

	const URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		URigHierarchyController* Controller = ((URigHierarchy*)Hierarchy)->GetController(true);
		check(Controller);

		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		const TArray<FRigElementKey> NewSelection = HierarchyTreeView->GetTreeView()->GetSelectedKeys();
		if (!Controller->SetSelection(NewSelection))
		{
			return;
		}
	}
}

SControlRigOutlinerItem::~SControlRigOutlinerItem()
{
	if (CurrentControlRig.IsValid())
	{
		(CurrentControlRig.Get())->ControlSelected().RemoveAll(this);
	}
	CurrentControlRig = nullptr;
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
}

void SControlRigOutliner::SetEditMode(FControlRigEditMode& InEditMode)
{
	ModeTools = InEditMode.GetModeManager();
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->OnControlRigAddedOrRemoved().AddRaw(this, &SControlRigOutliner::HandleControlAdded);
	}
}
void SControlRigOutliner::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode)
{
	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(MainBoxPtr, SVerticalBox)
			]
		];

	SetEditMode(InEditMode);
	Rebuild();
}

SControlRigOutliner::~SControlRigOutliner()
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->OnControlRigAddedOrRemoved().RemoveAll(this);
	}
	//base class handles control rig related cleanup
}


void SControlRigOutliner::HandleControlAdded(UControlRig* ControlRig, bool bIsAdded)
{
	Rebuild();

}
void SControlRigOutliner::Rebuild()
{
	MainBoxPtr->ClearChildren();
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		TArray<UControlRig*> ControlRigs = EditMode->GetControlRigsArray(false /*bIsVisible*/);
		for (UControlRig* ControlRig : ControlRigs)
		{
			if (ControlRig)
			{
				MainBoxPtr->AddSlot()
				.AutoHeight()
				[
					SNew(SControlRigOutlinerItem)
					.ControlRig(ControlRig)
				];
			}
		}
	}

}


#undef LOCTEXT_NAMESPACE
