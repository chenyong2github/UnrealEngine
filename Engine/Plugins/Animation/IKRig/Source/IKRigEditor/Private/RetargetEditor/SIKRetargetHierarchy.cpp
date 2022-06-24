// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/SIKRetargetHierarchy.h"

#include "Preferences/PersonaOptions.h"
#include "RetargetEditor/SIKRetargetPoseEditor.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "RigEditor/IKRigEditorStyle.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SIKRetargetHierarchy"

static const FName BoneColumnName(TEXT("BoneName"));
static const FName ChainColumnName(TEXT("RetargetChainName"));

FIKRetargetHierarchyElement::FIKRetargetHierarchyElement(
	const FName& InName,
	const TSharedRef<FIKRetargetEditorController>& InEditorController)
	: Key(FText::FromName(InName)),
	Name(InName),
	EditorController(InEditorController)
{}

TSharedRef<SWidget> SIKRetargetHierarchyRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	FName BoneName = WeakTreeElement.Pin()->Name;
	EIKRetargetSkeletonMode CurrentSkeleton = EditorController.Pin()->GetSkeletonMode();
	const bool bIsBoneRetargeted = EditorController.Pin()->IsBoneRetargeted(BoneName, CurrentSkeleton);
	const FText ChainName = FText::FromName(EditorController.Pin()->GetChainNameFromBone(BoneName, CurrentSkeleton));

	// determine icon based on if bone is retargeted
	const FSlateBrush* Brush;
	if (bIsBoneRetargeted)
	{
		Brush = FAppStyle::Get().GetBrush("SkeletonTree.Bone");
	}
	else
	{
		Brush = FAppStyle::Get().GetBrush("SkeletonTree.BoneNonWeighted");
	}

	// determine text based on if bone is retargeted
	FTextBlockStyle NormalText = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.NormalText");
	FTextBlockStyle ItalicText = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.ItalicText");
	FSlateFontInfo TextFont;
	FSlateColor TextColor;
	if (bIsBoneRetargeted)
	{
		// elements connected to the selected solver are green
		TextFont = ItalicText.Font;
		TextColor = NormalText.ColorAndOpacity;
	}
	else
	{
		TextFont = NormalText.Font;
		TextColor = FLinearColor(0.2f, 0.2f, 0.2f, 0.5f);
	}
	
	if (ColumnName == BoneColumnName)
	{
		TSharedPtr< SHorizontalBox > RowBox;
		SAssignNew(RowBox, SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew( SExpanderArrow, SharedThis(this) )
			.ShouldDrawWires(true)
		];

		RowBox->AddSlot()
		.MaxWidth(18)
		.FillWidth(1.0)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(Brush)
		];

		RowBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromName(BoneName))
			.Font(TextFont)
			.ColorAndOpacity(TextColor)
		];

		return RowBox.ToSharedRef();
	}

	if (ColumnName == ChainColumnName)
	{
		TSharedPtr< SHorizontalBox > RowBox;
		SAssignNew(RowBox, SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(ChainName)
			.Font(TextFont)
			.ColorAndOpacity(TextColor)
		];

		return RowBox.ToSharedRef();
	}
	
	return SNullWidget::NullWidget;
}

void SIKRetargetHierarchy::Construct(
	const FArguments& InArgs,
	TSharedRef<FIKRetargetEditorController> InEditorController)
{
	EditorController = InEditorController;
	EditorController.Pin()->HierarchyView = SharedThis(this);
	CommandList = MakeShared<FUICommandList>();
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SBox)
			.Padding(2.0f)
			.HAlign(HAlign_Center)
			[
				SNew(SSegmentedControl<EIKRetargetSkeletonMode>)
				.Value_Lambda([this]()
				{
					return EditorController.Pin()->GetSkeletonMode();
				})
				.OnValueChanged_Lambda([this](EIKRetargetSkeletonMode Mode)
				{
					EditorController.Pin()->SetSkeletonMode(Mode);
				})
				+SSegmentedControl<EIKRetargetSkeletonMode>::Slot(EIKRetargetSkeletonMode::Source)
				.Text(LOCTEXT("SourceSkeleton", "Source"))
				+ SSegmentedControl<EIKRetargetSkeletonMode>::Slot(EIKRetargetSkeletonMode::Target)
				.Text(LOCTEXT("TargetSkeleton", "Target"))
			]
		]
		
		+SVerticalBox::Slot()
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SIKRetargetPoseEditor, InEditorController)
		]

		+SVerticalBox::Slot()
		.Padding(2.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FAppStyle::GetBrush("SCSEditor.TreePanel"))
			[
				SAssignNew(TreeView, SIKRetargetHierarchyTreeView)
				.TreeItemsSource(&RootElements)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow_Lambda( [this](
					TSharedPtr<FIKRetargetHierarchyElement> InItem,
					const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
				{
					return SNew(SIKRetargetHierarchyRow, OwnerTable)
					.EditorController(EditorController.Pin())
					.TreeElement(InItem);
				})
				.OnGetChildren(this, &SIKRetargetHierarchy::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SIKRetargetHierarchy::OnSelectionChanged)
				.OnMouseButtonClick(this, &SIKRetargetHierarchy::OnItemClicked)
				.OnMouseButtonDoubleClick(this, &SIKRetargetHierarchy::OnItemDoubleClicked)
				.OnSetExpansionRecursive(this, &SIKRetargetHierarchy::OnSetExpansionRecursive)
				.HighlightParentNodesForSelection(false)
				.ItemHeight(24)
				.HeaderRow
				(
					SNew(SHeaderRow)
					
					+ SHeaderRow::Column(BoneColumnName)
					.DefaultLabel(LOCTEXT("RetargetBoneNameLabel", "Bone Name"))
					.FillWidth(0.7f)
						
					+ SHeaderRow::Column(ChainColumnName)
					.DefaultLabel(LOCTEXT("RetargetChainNameLabel", "Retarget Chain"))
					.FillWidth(0.3f)
				)
			]
		]
	];

	constexpr bool IsInitialSetup = true;
	RefreshTreeView(IsInitialSetup);
}

void SIKRetargetHierarchy::ShowItemAfterSelection(FName ItemName)
{
	TSharedPtr<FIKRetargetHierarchyElement> ItemToShow;
	for (const TSharedPtr<FIKRetargetHierarchyElement>& Element : AllElements)
	{
		if (Element.Get()->Name == ItemName)
		{
			ItemToShow = Element;
		}
	}

	if (!ItemToShow.IsValid())
	{
		return;
	}
	
	if(GetDefault<UPersonaOptions>()->bExpandTreeOnSelection)
	{
		TSharedPtr<FIKRetargetHierarchyElement> ItemToExpand = ItemToShow->Parent;
		while(ItemToExpand.IsValid())
		{
			TreeView->SetItemExpansion(ItemToExpand, true);
			ItemToExpand = ItemToExpand->Parent;
		}
	}
    
	TreeView->RequestScrollIntoView(ItemToShow);
}

void SIKRetargetHierarchy::RefreshTreeView(bool IsInitialSetup)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	const TSharedRef<FIKRetargetEditorController> ControllerRef = Controller.ToSharedRef();

	// synchronize selection with editor controller
	const TArray<FName> SelectedBones = Controller->GetSelectedBones();
	for (auto Element : AllElements)
	{
		const bool bIsSelected = SelectedBones.Contains(Element.Get()->Name);
		TreeView->SetItemSelection(Element, bIsSelected, ESelectInfo::Direct);
	}
	
	// save expansion and selection state
	TreeView->SaveAndClearState();

	// reset all tree items
	RootElements.Reset();
	AllElements.Reset();

	// validate we have a skeleton to load
	const UIKRetargetProcessor* Processor = Controller->GetRetargetProcessor();
	if (!(Processor && Processor->IsInitialized()))
	{
		TreeView->RequestTreeRefresh();
		return;
	}

	// get the skeleton that is currently being viewed in the editor
	const FTargetSkeleton& TargetSkeleton = Processor->GetTargetSkeleton();
	const FRetargetSkeleton& SourceSkeleton = Processor->GetSourceSkeleton();
	const bool bViewTarget = Controller->GetSkeletonMode() == EIKRetargetSkeletonMode::Target;
	const TArray<FName>& BoneNames = bViewTarget ? TargetSkeleton.BoneNames : SourceSkeleton.BoneNames;
	const TArray<int32>& ParentIndices = bViewTarget ? TargetSkeleton.ParentIndices : SourceSkeleton.ParentIndices;
	
	// record bone element indices
	TMap<FName, int32> BoneTreeElementIndices;

	// create all bone elements
	// TODO, add filtering of retargeted bones, by name, etc..
	for (const FName BoneName : BoneNames)
	{
		// create "Bone" tree element for this bone
		TSharedPtr<FIKRetargetHierarchyElement> BoneElement = MakeShared<FIKRetargetHierarchyElement>(BoneName, ControllerRef);
		BoneElement.Get()->Name = BoneName;
		const int32 BoneElementIndex = AllElements.Add(BoneElement);
		BoneTreeElementIndices.Add(BoneName, BoneElementIndex);
	}

	// store children/parent pointers on all bone elements
	for (int32 BoneIndex=0; BoneIndex<BoneNames.Num(); ++BoneIndex)
	{
		const FName BoneName = BoneNames[BoneIndex];
		const TSharedPtr<FIKRetargetHierarchyElement> BoneTreeElement = AllElements[BoneTreeElementIndices[BoneName]];
		const int32 ParentIndex = ParentIndices[BoneIndex];
		if (ParentIndex < 0)
		{
			// store the root element
			RootElements.Add(BoneTreeElement);
			// has no parent, so skip storing parent pointer
			continue;
		}

		// get parent tree element
		const FName ParentBoneName = BoneNames[ParentIndex];
		const TSharedPtr<FIKRetargetHierarchyElement> ParentBoneTreeElement = AllElements[BoneTreeElementIndices[ParentBoneName]];
		// store pointer to child on parent
		ParentBoneTreeElement->Children.Add(BoneTreeElement);
		// store pointer to parent on child
		BoneTreeElement->Parent = ParentBoneTreeElement;
	}

	// expand all elements upon the initial construction of the tree
	if (IsInitialSetup)
	{
		for (TSharedPtr<FIKRetargetHierarchyElement> RootElement : RootElements)
		{
			SetExpansionRecursive(RootElement, false, true);
		}
	}
	else
	{
		// restore expansion and selection state
		for (const TSharedPtr<FIKRetargetHierarchyElement>& Element : AllElements)
		{
			TreeView->RestoreState(Element);
		}
	}
	
	TreeView->RequestTreeRefresh();
}

void SIKRetargetHierarchy::HandleGetChildrenForTree(
	TSharedPtr<FIKRetargetHierarchyElement> InItem,
	TArray<TSharedPtr<FIKRetargetHierarchyElement>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SIKRetargetHierarchy::OnSelectionChanged(
	TSharedPtr<FIKRetargetHierarchyElement> Selection,
	ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	TArray<FName> SelectedBoneNames;
	TArray<TSharedPtr<FIKRetargetHierarchyElement>> SelectedItems = TreeView->GetSelectedItems();
	for (auto SelectedItem : SelectedItems)
	{
		SelectedBoneNames.Add(SelectedItem.Get()->Name);
	}

	constexpr bool bFromHierarchy = true;
	Controller->EditBoneSelection(SelectedBoneNames, EBoneSelectionEdit::Replace, bFromHierarchy);
}

void SIKRetargetHierarchy::OnItemClicked(TSharedPtr<FIKRetargetHierarchyElement> InItem)
{
	// TODO show bone details
}

void SIKRetargetHierarchy::OnItemDoubleClicked(TSharedPtr<FIKRetargetHierarchyElement> InItem)
{
	if (TreeView->IsItemExpanded(InItem))
	{
		SetExpansionRecursive(InItem, false, false);
	}
	else
	{
		SetExpansionRecursive(InItem, false, true);
	}
}

void SIKRetargetHierarchy::OnSetExpansionRecursive(TSharedPtr<FIKRetargetHierarchyElement> InItem, bool bShouldBeExpanded)
{
	SetExpansionRecursive(InItem, false, bShouldBeExpanded);
}

void SIKRetargetHierarchy::SetExpansionRecursive(
	TSharedPtr<FIKRetargetHierarchyElement> InElement,
	bool bTowardsParent,
	bool bShouldBeExpanded)
{
	TreeView->SetItemExpansion(InElement, bShouldBeExpanded);
    
	if (bTowardsParent)
	{
		if (InElement->Parent.Get())
		{
			SetExpansionRecursive(InElement->Parent, bTowardsParent, bShouldBeExpanded);
		}
	}
	else
	{
		for (int32 ChildIndex = 0; ChildIndex < InElement->Children.Num(); ++ChildIndex)
		{
			SetExpansionRecursive(InElement->Children[ChildIndex], bTowardsParent, bShouldBeExpanded);
		}
	}
}

#undef LOCTEXT_NAMESPACE
