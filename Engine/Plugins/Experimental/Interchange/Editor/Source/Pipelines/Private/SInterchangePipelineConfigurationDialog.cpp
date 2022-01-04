// Copyright Epic Games, Inc. All Rights Reserved.
#include "SInterchangePipelineConfigurationDialog.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "IDocumentation.h"
#include "InterchangePipelineBase.h"
#include "InterchangeProjectSettings.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"

#define LOCTEXT_NAMESPACE "InterchangePipelineConfiguration"

/************************************************************************/
/* FInterchangePipelineStacksTreeNodeItem Implementation                    */
/************************************************************************/

void FInterchangePipelineStacksTreeNodeItem::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (Pipeline)
	{
		Collector.AddReferencedObject(Pipeline);
	}
}

/************************************************************************/
/* SInterchangePipelineStacksTreeView Implementation                    */
/************************************************************************/

SInterchangePipelineStacksTreeView::~SInterchangePipelineStacksTreeView()
{

}

void SInterchangePipelineStacksTreeView::Construct(const FArguments& InArgs)
{
	OnSelectionChangedDelegate = InArgs._OnSelectionChangedDelegate;
	//Build the FbxNodeInfoPtr tree data

	const FName& DefaultPipelineStackName = GetDefault<UInterchangeProjectSettings>()->DefaultPipelineStack;
	const TMap<FName, FInterchangePipelineStack>& DefaultPipelineStacks = GetDefault<UInterchangeProjectSettings>()->PipelineStacks;

	 
	for (const TPair<FName, FInterchangePipelineStack>& NameAndPipelineStack : DefaultPipelineStacks)
	{
		TSharedPtr<FInterchangePipelineStacksTreeNodeItem> StackNode = MakeShared<FInterchangePipelineStacksTreeNodeItem>();
		StackNode->StackName = NameAndPipelineStack.Key;
		StackNode->Pipeline = nullptr;
		const FInterchangePipelineStack& PipelineStack = NameAndPipelineStack.Value;
		for (int32 PipelineIndex = 0; PipelineIndex < PipelineStack.Pipelines.Num(); ++PipelineIndex)
		{
			const UClass* PipelineClass = PipelineStack.Pipelines[PipelineIndex].LoadSynchronous();
			if (PipelineClass)
			{
				UInterchangePipelineBase* GeneratedPipeline = NewObject<UInterchangePipelineBase>(GetTransientPackage(), PipelineClass, NAME_None, RF_NoFlags);
				//Load the settings for this pipeline
				GeneratedPipeline->LoadSettings(NameAndPipelineStack.Key);
				GeneratedPipeline->PreDialogCleanup(NameAndPipelineStack.Key);
				TSharedPtr<FInterchangePipelineStacksTreeNodeItem> PipelineNode = MakeShared<FInterchangePipelineStacksTreeNodeItem>();
				PipelineNode->StackName = NameAndPipelineStack.Key;
				PipelineNode->Pipeline = GeneratedPipeline;
				StackNode->Childrens.Add(PipelineNode);
			}
		}
		RootNodeArray.Add(StackNode);
	}

	STreeView::Construct
	(
		STreeView::FArguments()
		.TreeItemsSource(&RootNodeArray)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateRow(this, &SInterchangePipelineStacksTreeView::OnGenerateRowPipelineConfigurationTreeView)
		.OnGetChildren(this, &SInterchangePipelineStacksTreeView::OnGetChildrenPipelineConfigurationTreeView)
		.OnContextMenuOpening(this, &SInterchangePipelineStacksTreeView::OnOpenContextMenu)
		.OnSelectionChanged(this, &SInterchangePipelineStacksTreeView::OnTreeViewSelectionChanged)
	);
}

/** The item used for visualizing the class in the tree. */
class SInterchangePipelineStacksTreeViewItem : public STableRow< TSharedPtr<FInterchangePipelineStacksTreeNodeItem> >
{
public:

	SLATE_BEGIN_ARGS(SInterchangePipelineStacksTreeViewItem)
		: _InterchangeNode(nullptr)
	{}

		/** The item content. */
		SLATE_ARGUMENT(TSharedPtr<FInterchangePipelineStacksTreeNodeItem>, InterchangeNode)
	SLATE_END_ARGS()

	/**
	* Construct the widget
	*
	* @param InArgs   A declaration from which to construct the widget
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		InterchangeNode = InArgs._InterchangeNode;
		//This is suppose to always be valid
		check(InterchangeNode);
		const bool bIsPipelineStackNode = InterchangeNode->Pipeline == nullptr;

		//Prepare the tooltip
		FString Tooltip;
		//FString Tooltip = bIsPipelineStackNode ? InterchangeNode->GetDisplayLabel();
		FText NodeDisplayLabel = FText::FromName(bIsPipelineStackNode ? InterchangeNode->StackName : InterchangeNode->Pipeline->GetClass()->GetFName());
		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SImage)
				.Image(this, &SInterchangePipelineStacksTreeViewItem::GetImageItemIcon)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 3.0f, 6.0f, 3.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(NodeDisplayLabel)
				.ToolTipText(FText::FromString(Tooltip))
			]
		];

		STableRow< TSharedPtr<FInterchangePipelineStacksTreeNodeItem> >::ConstructInternal(
			STableRow::FArguments()
			.ShowSelection(true),
			InOwnerTableView
		);
	}

private:
	const FSlateBrush* GetImageItemIcon() const
	{
		const FName DefaultPipelineStackName = GetDefault<UInterchangeProjectSettings>()->DefaultPipelineStack;
		//This is suppose to always be valid
		check(InterchangeNode);
		const bool bIsPipelineStackNode = InterchangeNode->Pipeline == nullptr;
		const bool bIsDefaultStackNode = bIsPipelineStackNode && (DefaultPipelineStackName == InterchangeNode->StackName);
		const FSlateBrush * TypeIcon = nullptr;
		FName IconName = bIsDefaultStackNode ? "PipelineConfigurationIcon.PipelineStackDefault" : bIsPipelineStackNode ? "PipelineConfigurationIcon.PipelineStack" : "PipelineConfigurationIcon.Pipeline";
		if (IconName != NAME_None)
		{
			const FSlateIcon SlateIcon = FSlateIconFinder::FindIcon(IconName);
			TypeIcon = SlateIcon.GetOptionalIcon();
		}

		if (!TypeIcon)
		{
			TypeIcon = FSlateIconFinder::FindIconBrushForClass(AActor::StaticClass());
		}
		return TypeIcon;
	}

	/** The node to build the tree view row from. */
	TSharedPtr<FInterchangePipelineStacksTreeNodeItem> InterchangeNode = nullptr;
};

TSharedRef< ITableRow > SInterchangePipelineStacksTreeView::OnGenerateRowPipelineConfigurationTreeView(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Item, const TSharedRef< STableViewBase >& OwnerTable)
{
	TSharedRef< SInterchangePipelineStacksTreeViewItem > ReturnRow = SNew(SInterchangePipelineStacksTreeViewItem, OwnerTable)
		.InterchangeNode(Item);
	return ReturnRow;
}
void SInterchangePipelineStacksTreeView::OnGetChildrenPipelineConfigurationTreeView(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> InParent, TArray< TSharedPtr<FInterchangePipelineStacksTreeNodeItem> >& OutChildren)
{
	for (int32 ChildIndex = 0; ChildIndex < InParent->Childrens.Num(); ++ChildIndex)
	{
		TSharedPtr<FInterchangePipelineStacksTreeNodeItem> ChildNode = InParent->Childrens[ChildIndex];
		if (!ChildNode.IsValid())
			continue;
		OutChildren.Add(ChildNode);
	}
}

void SInterchangePipelineStacksTreeView::RecursiveSetExpand(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Node, bool ExpandState)
{
	SetItemExpansion(Node, ExpandState);
	for (int32 ChildIndex = 0; ChildIndex < Node->Childrens.Num(); ++ChildIndex)
	{
		TSharedPtr<FInterchangePipelineStacksTreeNodeItem> ChildNode = Node->Childrens[ChildIndex];
		if (!ChildNode.IsValid())
			continue;
		RecursiveSetExpand(ChildNode, ExpandState);
	}
}

FReply SInterchangePipelineStacksTreeView::OnExpandAll()
{
	for (TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Node : RootNodeArray)
	{
		if (!ensure(Node))
		{
			continue;
		}
		RecursiveSetExpand(Node, true);
	}
	return FReply::Handled();
}

FReply SInterchangePipelineStacksTreeView::OnCollapseAll()
{
	for (TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Node : RootNodeArray)
	{
		if (!ensure(Node))
		{
			continue;
		}
		RecursiveSetExpand(Node, false);
	}
	return FReply::Handled();
}

TSharedPtr<SWidget> SInterchangePipelineStacksTreeView::OnOpenContextMenu()
{
	// Build up the menu for a selection
	const bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, TSharedPtr<FUICommandList>());

	TArray<TSharedPtr<FInterchangePipelineStacksTreeNodeItem>> SelectedNodes;
	const auto NumSelectedItems = GetSelectedItems(SelectedNodes);

	if (SelectedNodes.Num() == 1)
	{
		TSharedPtr<FInterchangePipelineStacksTreeNodeItem> SelectNode = SelectedNodes[0];
		if (SelectNode->Pipeline == nullptr)
		{
			// We always create a section here, even if there is no parent so that clients can still extend the menu
			MenuBuilder.BeginSection("TreeViewContextMenuStackNodeSection");
			{
				const FSlateIcon DefaultIcon(FEditorStyle::GetStyleSetName(), "Icons.Default");
				MenuBuilder.AddMenuEntry(LOCTEXT("SetHasDefaultMenuAction", "Set Has Default Stack"), FText(), DefaultIcon, FUIAction(FExecuteAction::CreateSP(this, &SInterchangePipelineStacksTreeView::SetHasDefaultStack, SelectNode->StackName)));
			}
			MenuBuilder.EndSection();
		}
	}
	

	return MenuBuilder.MakeWidget();
}

void SInterchangePipelineStacksTreeView::SetHasDefaultStack(FName NewDefaultStackValue)
{
	UInterchangeProjectSettings* InterchangeProjectSettingsCDO = GetMutableDefault<UInterchangeProjectSettings>();
	FName& DefaultPipelineStackName = InterchangeProjectSettingsCDO->DefaultPipelineStack;
	const TMap<FName, FInterchangePipelineStack>& PipelineStacks = InterchangeProjectSettingsCDO->PipelineStacks;
	if (PipelineStacks.Contains(NewDefaultStackValue))
	{
		DefaultPipelineStackName = NewDefaultStackValue;
		InterchangeProjectSettingsCDO->SaveConfig(); //This ensure the default pipeline stack name is save into the local config
	}
}

void SInterchangePipelineStacksTreeView::OnTreeViewSelectionChanged(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Item, ESelectInfo::Type SelectionType)
{
	if (SelectionMode.Get() == ESelectionMode::None)
	{
		return;
	}

	if (OnSelectionChangedDelegate.IsBound())
	{
		OnSelectionChangedDelegate.ExecuteIfBound(Item, SelectionType);
	}
}


/************************************************************************/
/* SInterchangePipelineConfigurationDialog Implementation                      */
/************************************************************************/

SInterchangePipelineConfigurationDialog::SInterchangePipelineConfigurationDialog()
{
	PipelineConfigurationTreeView = nullptr;
	PipelineConfigurationDetailsView = nullptr;
	OwnerWindow = nullptr;
}

SInterchangePipelineConfigurationDialog::~SInterchangePipelineConfigurationDialog()
{
	PipelineConfigurationTreeView = nullptr;
	PipelineConfigurationDetailsView = nullptr;
	OwnerWindow = nullptr;
}

TSharedRef<SBox> SInterchangePipelineConfigurationDialog::SpawnPipelineConfiguration()
{
	//Create the treeview
	PipelineConfigurationTreeView = SNew(SInterchangePipelineStacksTreeView)
		.OnSelectionChangedDelegate(this, &SInterchangePipelineConfigurationDialog::OnSelectionChanged);

	TSharedPtr<SBox> InspectorBox;
	TSharedRef<SBox> PipelineConfigurationPanelBox = SNew(SBox)
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.4f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoHeight()
			[
				SNew(SUniformGridPanel)
				.SlotPadding(2)
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("FbxOptionWindow_Scene_ExpandAll", "Expand All"))
					.OnClicked(PipelineConfigurationTreeView.Get(), &SInterchangePipelineStacksTreeView::OnExpandAll)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("FbxOptionWindow_Scene_CollapseAll", "Collapse All"))
					.OnClicked(PipelineConfigurationTreeView.Get(), &SInterchangePipelineStacksTreeView::OnCollapseAll)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBox)
				[
					PipelineConfigurationTreeView.ToSharedRef()
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.6f)
		[
			SAssignNew(InspectorBox, SBox)
		]
	];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	PipelineConfigurationDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	InspectorBox->SetContent(PipelineConfigurationDetailsView->AsShared());
	PipelineConfigurationDetailsView->SetObject(nullptr);
	return PipelineConfigurationPanelBox;
}


void SInterchangePipelineConfigurationDialog::Construct(const FArguments& InArgs)
{
	//Make sure there is a valid default value

	OwnerWindow = InArgs._OwnerWindow;

	check(OwnerWindow.IsValid());

	this->ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(10.0f, 3.0f))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(2)
			[
				SpawnPipelineConfiguration()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(2)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(2)
				+ SUniformGridPanel::Slot(0, 0)
				[
					IDocumentation::Get()->CreateAnchor(FString("Engine/Content/Interchange/PipelineConfiguration"))
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("InspectorGraphWindow_Cancel", "Cancel"))
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnCloseDialog, ECloseEventType::Cancel)
				]
				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("InspectorGraphWindow_ImportAll", "Import All"))
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnCloseDialog, ECloseEventType::ImportAll)
				]
				+ SUniformGridPanel::Slot(3, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("InspectorGraphWindow_Import", "Import"))
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnCloseDialog, ECloseEventType::Import)
				]
			]
		]
	];
}

void SInterchangePipelineConfigurationDialog::OnSelectionChanged(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Item, ESelectInfo::Type SelectionType)
{
	UInterchangePipelineBase* Pipeline = Item ? Item->Pipeline : nullptr;
	//Change the object point by the InspectorBox
	PipelineConfigurationDetailsView->SetObject(Pipeline);
}

void SInterchangePipelineConfigurationDialog::RecursiveSavePipelineSettings(const TSharedPtr<FInterchangePipelineStacksTreeNodeItem>& ParentNode, const int32 PipelineIndex) const
{
	if (ParentNode->Pipeline)
	{
		ParentNode->Pipeline->SaveSettings(ParentNode->StackName);
	}
	for (int32 ChildIndex = 0; ChildIndex < ParentNode->Childrens.Num(); ++ChildIndex)
	{
		RecursiveSavePipelineSettings(ParentNode->Childrens[ChildIndex], ChildIndex);
	}
}

void SInterchangePipelineConfigurationDialog::ClosePipelineConfiguration(const ECloseEventType CloseEventType)
{
	if (CloseEventType == ECloseEventType::Cancel)
	{
		bCanceled = true;
		bImportAll = false;
	}
	else if (CloseEventType == ECloseEventType::ImportAll)
	{
		bCanceled = false;
		bImportAll = true;
	}
	else
	{
		//ECloseEventType::Import
		bCanceled = false;
		bImportAll = false;
	}

	if (PipelineConfigurationTreeView)
	{
		const TArray<TSharedPtr<FInterchangePipelineStacksTreeNodeItem>>& RootNodeArray = PipelineConfigurationTreeView->GetRootNodeArray();
		for (const TSharedPtr<FInterchangePipelineStacksTreeNodeItem>& RootNode : RootNodeArray)
		{
			RecursiveSavePipelineSettings(RootNode, 0);
		}
	}

	PipelineConfigurationTreeView = nullptr;
	PipelineConfigurationDetailsView = nullptr;

	if (TSharedPtr<SWindow> OwnerWindowPin = OwnerWindow.Pin())
	{
		OwnerWindowPin->RequestDestroyWindow();
	}
	OwnerWindow = nullptr;
}

FReply SInterchangePipelineConfigurationDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (!FApp::IsUnattended())
		{
			FString Message = FText(LOCTEXT("InterchangePipelineCancelEscKey", "Are you sure you want to cancel the import?")).ToString();
			if (FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Cancel Import")) == EAppReturnType::Type::Yes)
			{
				return OnCloseDialog(ECloseEventType::Cancel);
			}
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
