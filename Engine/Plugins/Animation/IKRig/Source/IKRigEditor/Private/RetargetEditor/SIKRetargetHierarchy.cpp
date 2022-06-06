// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/SIKRetargetHierarchy.h"

#include "Preferences/PersonaOptions.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "RigEditor/IKRigEditorStyle.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SIKRetargetHierarchy"

FIKRetargetTreeElement::FIKRetargetTreeElement(
	const FText& InKey,
	const TSharedRef<FIKRetargetEditorController>& InEditorController)
	: Key(InKey)
	, EditorController(InEditorController)
{}

TSharedRef<ITableRow> FIKRetargetTreeElement::MakeTreeRowWidget(
	TSharedRef<FIKRetargetEditorController> InEditorController,
	const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FIKRetargetTreeElement> InTreeElement,
	TSharedRef<FUICommandList> InCommandList,
	TSharedPtr<SIKRetargetHierarchy> InHierarchy)
{
	return SNew(SIKRetargetHierarchyItem, InEditorController, InOwnerTable, InTreeElement, InCommandList, InHierarchy);
}

void SIKRetargetHierarchyItem::Construct(
	const FArguments& InArgs,
	TSharedRef<FIKRetargetEditorController> InEditorController,
	const TSharedRef<STableViewBase>& OwnerTable,
	TSharedRef<FIKRetargetTreeElement> InTreeElement,
	TSharedRef<FUICommandList> InCommandList,
	TSharedPtr<SIKRetargetHierarchy> InHierarchy)
{
	WeakTreeElement = InTreeElement;
	EditorController = InEditorController;
	HierarchyView = InHierarchy;

	const FSlateBrush* Brush = FAppStyle::Get().GetBrush("SkeletonTree.Bone");
	
	const FTextBlockStyle NormalText = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.NormalText");
	const FTextBlockStyle ItalicText = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.ItalicText");
	FSlateFontInfo TextFont = NormalText.Font;
	FSlateColor TextColor = FLinearColor(0.2f, 0.2f, 0.2f, 0.5f);
	
	TSharedPtr<SHorizontalBox> HorizontalBox;
	STableRow<TSharedPtr<FIKRetargetTreeElement>>::Construct(
		STableRow<TSharedPtr<FIKRetargetTreeElement>>::FArguments()
		.ShowWires(true)
		.Content()
		[
			SAssignNew(HorizontalBox, SHorizontalBox)
			+ SHorizontalBox::Slot()
			.MaxWidth(18)
			.FillWidth(1.0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(Brush)
			]
		], OwnerTable);

	HorizontalBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SIKRetargetHierarchyItem::GetName)
			.Font(TextFont)
			.ColorAndOpacity(TextColor)
		];
}

FText SIKRetargetHierarchyItem::GetName() const
{
	return WeakTreeElement.Pin()->Key;
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
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FAppStyle::GetBrush("SCSEditor.TreePanel"))
			[
				SAssignNew(TreeView, SIKRetargetHierarchyTreeView)
				.TreeItemsSource(&RootElements)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SIKRetargetHierarchy::MakeTableRowWidget)
				.OnGetChildren(this, &SIKRetargetHierarchy::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SIKRetargetHierarchy::OnSelectionChanged)
				.OnMouseButtonClick(this, &SIKRetargetHierarchy::OnItemClicked)
				.OnMouseButtonDoubleClick(this, &SIKRetargetHierarchy::OnItemDoubleClicked)
				.OnSetExpansionRecursive(this, &SIKRetargetHierarchy::OnSetExpansionRecursive)
				.HighlightParentNodesForSelection(false)
				.ItemHeight(24)
			]
		]
	];

	constexpr bool IsInitialSetup = true;
	RefreshTreeView(IsInitialSetup);
}

void SIKRetargetHierarchy::ShowItemAfterSelection(FName ItemName)
{
	TSharedPtr<FIKRetargetTreeElement> ItemToShow;
	for (const TSharedPtr<FIKRetargetTreeElement>& Element : AllElements)
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
		TSharedPtr<FIKRetargetTreeElement> ItemToExpand = ItemToShow->Parent;
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
	for (const FName BoneName : BoneNames)
	{
		// create "Bone" tree element for this bone
		const FText BoneDisplayName = FText::FromName(BoneName);
		TSharedPtr<FIKRetargetTreeElement> BoneElement = MakeShared<FIKRetargetTreeElement>(BoneDisplayName, ControllerRef);
		BoneElement.Get()->Name = BoneName;
		const int32 BoneElementIndex = AllElements.Add(BoneElement);
		BoneTreeElementIndices.Add(BoneName, BoneElementIndex);

		// TODO, this will show ALL bones in the skeletal mesh. represent bone differently if it's in the retargeted set
		// TODO, add filtering of retargeted bones, by name, etc..
	}

	// store children/parent pointers on all bone elements
	for (int32 BoneIndex=0; BoneIndex<BoneNames.Num(); ++BoneIndex)
	{
		const FName BoneName = BoneNames[BoneIndex];
		const TSharedPtr<FIKRetargetTreeElement> BoneTreeElement = AllElements[BoneTreeElementIndices[BoneName]];
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
		const TSharedPtr<FIKRetargetTreeElement> ParentBoneTreeElement = AllElements[BoneTreeElementIndices[ParentBoneName]];
		// store pointer to child on parent
		ParentBoneTreeElement->Children.Add(BoneTreeElement);
		// store pointer to parent on child
		BoneTreeElement->Parent = ParentBoneTreeElement;
	}

	// expand all elements upon the initial construction of the tree
	if (IsInitialSetup)
	{
		for (TSharedPtr<FIKRetargetTreeElement> RootElement : RootElements)
		{
			SetExpansionRecursive(RootElement, false, true);
		}
	}
	else
	{
		// restore expansion and selection state
		for (const TSharedPtr<FIKRetargetTreeElement>& Element : AllElements)
		{
			TreeView->RestoreState(Element);
		}
	}
	
	TreeView->RequestTreeRefresh();
}

TSharedRef<ITableRow> SIKRetargetHierarchy::MakeTableRowWidget(
	TSharedPtr<FIKRetargetTreeElement> InItem,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget(EditorController.Pin().ToSharedRef(), OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
}

void SIKRetargetHierarchy::HandleGetChildrenForTree(
	TSharedPtr<FIKRetargetTreeElement> InItem,
	TArray<TSharedPtr<FIKRetargetTreeElement>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SIKRetargetHierarchy::OnSelectionChanged(
	TSharedPtr<FIKRetargetTreeElement> Selection,
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
	TArray<TSharedPtr<FIKRetargetTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (auto SelectedItem : SelectedItems)
	{
		SelectedBoneNames.Add(SelectedItem.Get()->Name);
	}

	constexpr bool bFromHierarchy = true;
	Controller->EditBoneSelection(SelectedBoneNames, EBoneSelectionEdit::Replace, bFromHierarchy);
}

void SIKRetargetHierarchy::OnItemClicked(TSharedPtr<FIKRetargetTreeElement> InItem)
{
	// TODO show bone details
}

void SIKRetargetHierarchy::OnItemDoubleClicked(TSharedPtr<FIKRetargetTreeElement> InItem)
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

void SIKRetargetHierarchy::OnSetExpansionRecursive(TSharedPtr<FIKRetargetTreeElement> InItem, bool bShouldBeExpanded)
{
	SetExpansionRecursive(InItem, false, bShouldBeExpanded);
}

void SIKRetargetHierarchy::SetExpansionRecursive(
	TSharedPtr<FIKRetargetTreeElement> InElement,
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
