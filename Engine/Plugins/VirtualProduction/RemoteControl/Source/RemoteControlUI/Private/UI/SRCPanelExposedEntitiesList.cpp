// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntitiesList.h"

#include "Algo/ForEach.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "ISettingsModule.h"
#include "RCPanelWidgetRegistry.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "Engine/Selection.h"
#include "Editor/EditorEngine.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
#include "Misc/MessageDialog.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "RemoteControlUIModule.h"
#include "ScopedTransaction.h"
#include "SRCPanelFieldGroup.h"
#include "SRCPanelExposedField.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/Panels/SRCDockPanel.h"
#include "UObject/Object.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelEntitiesList"

class SEntityRow : public SMultiColumnTableRow<TSharedPtr<SRCPanelTreeNode>>
{
public:

	SLATE_BEGIN_ARGS(SEntityRow)
	{}
	
		SLATE_ARGUMENT(TSharedPtr<SRCPanelTreeNode>, Entity)

		SLATE_ATTRIBUTE(FMargin, Padding)
		SLATE_STYLE_ARGUMENT(FTableRowStyle, Style)

		// Low level DragAndDrop
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_EVENT(FOnTableRowDragEnter, OnDragEnter)
		SLATE_EVENT(FOnTableRowDragLeave, OnDragLeave)
		SLATE_EVENT(FOnTableRowDrop, OnDrop)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		Entity = InArgs._Entity;

		FSuperRowType::FArguments SuperArgs = FSuperRowType::FArguments();

		SuperArgs.OnDragDetected(InArgs._OnDragDetected);
		SuperArgs.OnDragEnter(InArgs._OnDragEnter);
		SuperArgs.OnDragLeave(InArgs._OnDragLeave);
		SuperArgs.OnDrop(InArgs._OnDrop);

		SuperArgs.ExpanderStyleSet(&FCoreStyle::Get());
		SuperArgs.Padding(InArgs._Padding);
		SuperArgs.ShowWires(false);
		SuperArgs.Style(InArgs._Style);

		FSuperRowType::Construct(SuperArgs, OwnerTableView);
	}

	TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (Entity.IsValid())
		{
			if (Entity->HasChildren() && InColumnName == FRemoteControlPresetColumns::Description)
			{
				// -- Row is for TreeView --
				SHorizontalBox::FSlot* InnerContentSlotNativePtr = nullptr;

				TSharedRef<SWidget> TreeNode = SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::OnDemand)
					
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(ExpanderArrowWidget, SExpanderArrow, SharedThis(this))
						.StyleSet(ExpanderStyleSet)
						.ShouldDrawWires(false)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1)
					.Expose(InnerContentSlotNativePtr)
					.Padding(8.f, 2.f)
					[
						Entity->GetWidget(InColumnName)
					];

				InnerContentSlot = InnerContentSlotNativePtr;

				return TreeNode;

			}
			else
			{
				return SNew(SBox)
					.Padding(FMargin(4.f, 2.f))
					.VAlign(VAlign_Center)
					[
						Entity->GetWidget(InColumnName)
					];
			}
		}

		return SNullWidget::NullWidget;
	}

private:

	//~ SWidget Interface
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		if (Entity.IsValid())
		{
			Entity->EnterRenameMode();
		}

		return FSuperRowType::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
	}

private:

	/** Cached reference of Entity */
	TSharedPtr<SRCPanelTreeNode> Entity;
};

void SRCPanelExposedEntitiesList::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TWeakPtr<FRCPanelWidgetRegistry> InWidgetRegistry)
{
	bIsInEditMode = InArgs._EditMode;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	OnEntityListUpdatedDelegate = InArgs._OnEntityListUpdated;
	WidgetRegistry = MoveTemp(InWidgetRegistry);

	ColumnSizeData.LeftColumnWidth = TAttribute<float>(this, &SRCPanelExposedEntitiesList::OnGetLeftColumnWidth);
	ColumnSizeData.RightColumnWidth = TAttribute<float>(this, &SRCPanelExposedEntitiesList::OnGetRightColumnWidth);
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SRCPanelExposedEntitiesList::OnSetColumnWidth);

	bFilterApplicationRequested = false;
	bSearchRequested = false;
	SearchedText = MakeShared<FText>();

	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	// Major Panel
	TSharedPtr<SRCMajorPanel> ExposePanel = SNew(SRCMajorPanel)
		.HeaderLabel(LOCTEXT("ExposePanelHeader", "EXPOSE"))
		.EnableFooter(false);

	// Groups List
	SAssignNew(GroupsListView, SListView<TSharedPtr<SRCPanelTreeNode>>)
		.ItemHeight(24.f)
		.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
		.OnSelectionChanged(this, &SRCPanelExposedEntitiesList::OnSelectionChanged)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(reinterpret_cast<TArray<TSharedPtr<SRCPanelTreeNode>>*>(&FieldGroups))
		.OnContextMenuOpening(this, &SRCPanelExposedEntitiesList::OnContextMenuOpening, SRCPanelTreeNode::Group)
		.ListViewStyle(&RCPanelStyle->TableViewStyle)
		.ClearSelectionOnClick(true);

	// Group Dock Panel
	TSharedPtr<SRCMinorPanel> GroupDockPanel = SNew(SRCMinorPanel)
		.HeaderLabel(LOCTEXT("GroupsLabel", "Group"))
		.EnableFooter(false)
		[
			GroupsListView.ToSharedRef()
		];

	// Add New Group Button
	TSharedPtr<SWidget> NewGroupButton = SNew(SButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Group")))
		.IsEnabled_Lambda([this]() { return bIsInEditMode.Get(); })
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ForegroundColor(FSlateColor::UseForeground())
		.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
		.ToolTipText(LOCTEXT("NewGroupToolTip", "Create new group."))
		.OnClicked(this, &SRCPanelExposedEntitiesList::OnCreateGroup)
		[
			SNew(SBox)
			.WidthOverride(RCPanelStyle->IconSize.X)
			.HeightOverride(RCPanelStyle->IconSize.Y)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
			]
		];

	// Delete All Groups Button
	TSharedPtr<SWidget> DeleteAllGroupsButton = SNew(SButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Delete All Groups")))
		.IsEnabled_Lambda([this]() { return bIsInEditMode.Get() && FieldGroups.Num() > 0; })
		.Visibility_Lambda([this]() { return (bIsInEditMode.Get() && FieldGroups.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ForegroundColor(FSlateColor::UseForeground())
		.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
		.ToolTipText(LOCTEXT("DeleteAllGroupsToolTip", "Delete all groups."))
		.OnClicked(this, &SRCPanelExposedEntitiesList::RequestDeleteAllGroups)
		[
			SNew(SBox)
			.WidthOverride(RCPanelStyle->IconSize.X)
			.HeightOverride(RCPanelStyle->IconSize.Y)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Icons.Delete"))
			]
		];

	GroupDockPanel->AddHeaderToolbarItem(EToolbar::Left, NewGroupButton.ToSharedRef());
	GroupDockPanel->AddHeaderToolbarItem(EToolbar::Right, DeleteAllGroupsButton.ToSharedRef());

	ExposePanel->AddPanel(GroupDockPanel.ToSharedRef(), 0.25f);

	// Fields List
	SAssignNew(FieldsListView, STreeView<TSharedPtr<SRCPanelTreeNode>>)
		.ItemHeight(24.f)
		.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
		.OnSelectionChanged(this, &SRCPanelExposedEntitiesList::OnSelectionChanged)
		.SelectionMode(ESelectionMode::Single)
		.TreeItemsSource(&FieldEntities)
		.OnContextMenuOpening(this, &SRCPanelExposedEntitiesList::OnContextMenuOpening, SRCPanelTreeNode::Field)
		.ClearSelectionOnClick(true)
		.TreeViewStyle(&RCPanelStyle->TableViewStyle)
		.OnGetChildren(this, &SRCPanelExposedEntitiesList::OnGetNodeChildren)
		.HeaderRow(
			SNew(SHeaderRow)
			.Style(&RCPanelStyle->HeaderRowStyle)

			+ SHeaderRow::Column(FRemoteControlPresetColumns::DragDropHandle)
			.DefaultLabel(LOCTEXT("RCPresetDragDropHandleColumnHeader", ""))
			.FixedWidth(25.f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(FRemoteControlPresetColumns::Description)
			.DefaultLabel(LOCTEXT("RCPresetDescColumnHeader", "Description"))
			.FillWidth(0.5f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(FRemoteControlPresetColumns::Value)
			.DefaultLabel(LOCTEXT("RCPresetValueColumnHeader", "Value"))
			.FillWidth(0.5f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)

			+ SHeaderRow::Column(FRemoteControlPresetColumns::Reset)
			.DefaultLabel(LOCTEXT("RCPresetResetButtonColumnHeader", ""))
			.FixedWidth(48.f)
			.HeaderContentPadding(RCPanelStyle->HeaderRowPadding)
		);

	// Exposed Entities Dock Panel
	TSharedPtr<SRCMinorPanel> ExposeDockPanel = SNew(SRCMinorPanel)
		.HeaderLabel(LOCTEXT("PropertiesLabel", "Properties"))
		.EnableFooter(true)
		[
			FieldsListView.ToSharedRef()
		];

	// Placeholder Box
	TSharedPtr<SWidget> PlaceholderBox = SNew(SBox)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Placeholder")))
		.WidthOverride(30.f)
		.HeightOverride(30.f)
		[
			SNullWidget::NullWidget
		];

	// Delete All Entities Button
	TSharedPtr<SWidget> DeleteAllEntitiesButton = SNew(SButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Delete All Entities")))
		.IsEnabled_Lambda([this]() { return bIsInEditMode.Get() && FieldWidgetMap.Num() > 0; })
		.Visibility_Lambda([this]() { return (bIsInEditMode.Get() && FieldWidgetMap.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ForegroundColor(FSlateColor::UseForeground())
		.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
		.ToolTipText(LOCTEXT("DeleteAllEntitiesToolTip", "Delete all entities."))
		.OnClicked(this, &SRCPanelExposedEntitiesList::RequestDeleteAllEntities)
		[
			SNew(SBox)
			.WidthOverride(RCPanelStyle->IconSize.X)
			.HeightOverride(RCPanelStyle->IconSize.Y)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Icons.Delete"))
			]
		];

	// Expose Button
	ExposeDockPanel->AddHeaderToolbarItem(EToolbar::Left, PlaceholderBox.ToSharedRef());
	ExposeDockPanel->AddFooterToolbarItem(EToolbar::Right, InArgs._ExposeComboButton.Get().ToSharedRef());
	ExposeDockPanel->AddFooterToolbarItem(EToolbar::Right, DeleteAllEntitiesButton.ToSharedRef());

	ExposePanel->AddPanel(ExposeDockPanel.ToSharedRef(), 0.75f);

	ChildSlot
		.Padding(RCPanelStyle->PanelPadding)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(1.f)
			[
				ExposePanel.ToSharedRef()
			]
		];

	RegisterEvents();
	RegisterPresetDelegates();
	Refresh();
}

SRCPanelExposedEntitiesList::~SRCPanelExposedEntitiesList()
{
	UnregisterPresetDelegates();
	UnregisterEvents();
}

void SRCPanelExposedEntitiesList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bSearchRequested)
	{
		TryRefreshingSearch(*SearchedText);

		bSearchRequested = false;
	}
	
	if (bFilterApplicationRequested)
	{
		ApplyFilters();

		bFilterApplicationRequested = false;
	}
}

TSharedPtr<SRCPanelTreeNode> SRCPanelExposedEntitiesList::GetSelectedGroup() const
{
	TArray<TSharedPtr<SRCPanelTreeNode>> SelectedNodes;

	GroupsListView->GetSelectedItems(SelectedNodes);

	if (SelectedNodes.Num())
	{
		return SelectedNodes[0];
	}

	return nullptr;
}

TSharedPtr<SRCPanelTreeNode> SRCPanelExposedEntitiesList::GetSelectedEntity() const
{
	TArray<TSharedPtr<SRCPanelTreeNode>> SelectedNodes;

	FieldsListView->GetSelectedItems(SelectedNodes);

	if (SelectedNodes.Num())
	{
		return SelectedNodes[0];
	}

	return nullptr;
}

void SRCPanelExposedEntitiesList::SetSelection(const TSharedPtr<SRCPanelTreeNode>& Node, const bool bForceMouseClick)
{
	if (Node)
	{
		const ESelectInfo::Type SelectInfo = bForceMouseClick ? ESelectInfo::OnMouseClick : ESelectInfo::Direct;

		if (TSharedPtr<SRCPanelTreeNode>* FoundTreeNode = FieldWidgetMap.Find(Node->GetRCId()))
		{
			FieldsListView->SetSelection(*FoundTreeNode, SelectInfo);
			return;
		}

		if (TSharedPtr<SRCPanelGroup> SRCGroup = FindGroupById(Node->GetRCId()))
		{
			GroupsListView->SetSelection(SRCGroup, SelectInfo);
		}	
	}
}

void SRCPanelExposedEntitiesList::SetBackendFilter(const FRCFilter& InBackendFilter)
{
	BackendFilter = InBackendFilter;

	bFilterApplicationRequested = true;
}

void SRCPanelExposedEntitiesList::OnObjectPropertyChange(UObject* InObject, FPropertyChangedEvent& InChangeEvent)
{
	EPropertyChangeType::Type TypesNeedingRefresh = EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayClear | EPropertyChangeType::ArrayRemove | EPropertyChangeType::ValueSet;
	auto IsRelevantProperty = [](FFieldClass* PropertyClass)
	{
		return PropertyClass && (PropertyClass == FArrayProperty::StaticClass() || PropertyClass == FSetProperty::StaticClass() || PropertyClass == FMapProperty::StaticClass());
	};

	if ((InChangeEvent.ChangeType & TypesNeedingRefresh) != 0 
		&& ((InChangeEvent.MemberProperty && IsRelevantProperty(InChangeEvent.MemberProperty->GetClass()))
			|| (InChangeEvent.Property && IsRelevantProperty(InChangeEvent.Property->GetClass()))))
	{
		if (TSharedPtr<FRCPanelWidgetRegistry> Registry = WidgetRegistry.Pin())
		{
			Registry->Refresh(InObject);
		}

		if (Preset)
		{
			// If the modified property is a parent of an exposed property, re-enable the edit condition.
			// This is useful in case we re-add an array element which contains a nested property that is exposed.
			for (TWeakPtr<FRemoteControlProperty> WeakProp : Preset->GetExposedEntities<FRemoteControlProperty>())
			{
				if (TSharedPtr<FRemoteControlProperty> RCProp = WeakProp.Pin())
				{
					if (RCProp->FieldPathInfo.IsResolved() && InObject && InObject->GetClass()->IsChildOf(RCProp->GetSupportedBindingClass()))
					{
						for (int32 SegmentIndex = 0; SegmentIndex < RCProp->FieldPathInfo.GetSegmentCount(); SegmentIndex++)
						{
							FProperty* ResolvedField = RCProp->FieldPathInfo.GetFieldSegment(SegmentIndex).ResolvedData.Field;
							if (ResolvedField == InChangeEvent.MemberProperty || ResolvedField == InChangeEvent.Property)
							{
								RCProp->EnableEditCondition();
								break;
							}
						}
					}
				}
			}
		}

		for (const TPair <FGuid, TSharedPtr<SRCPanelTreeNode>>& Node : FieldWidgetMap)
		{
			Node.Value->Refresh();
		}
	}

	GroupsListView->RequestListRefresh();
	FieldsListView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::Refresh()
{
	GenerateListWidgets();

	RefreshGroups();

	if (Preset.IsValid())
	{
		constexpr bool bForceMouseClick = true;

		FRemoteControlPresetGroup& DefaultGroup = Preset->Layout.GetDefaultGroup();
		GenerateListWidgets(DefaultGroup);
		SetSelection(FindGroupById(DefaultGroup.Id), bForceMouseClick);
	}
}

void SRCPanelExposedEntitiesList::TryRefreshingSearch(const FText& InSearchText, bool bApplyFilter)
{
	FieldEntities.Reset();

	if (FieldWidgetMap.IsEmpty() || !Preset.IsValid())
	{
		FieldsListView->RequestListRefresh();

		*SearchedText = FText::GetEmpty();

		return;
	}

	*SearchedText = InSearchText;

	for (TWeakPtr<FRemoteControlEntity> WeakEntity : Preset->GetExposedEntities())
	{
		if (const TSharedPtr<FRemoteControlEntity> Entity = WeakEntity.Pin())
		{
			if (TSharedPtr<SRCPanelTreeNode> SelectedGroup = GetSelectedGroup())
			{
				if (FRemoteControlPresetGroup* EntityGroup = Preset->Layout.FindGroupFromField(Entity->GetId()))
				{
					const FString& EntityLabel = Entity->GetLabel().ToString();

					if (EntityLabel.Contains(*InSearchText.ToString()) &&
						(SelectedGroup->GetRCId() == EntityGroup->Id ||
							Preset->Layout.IsDefaultGroup(SelectedGroup->GetRCId())))
					{
						if (TSharedPtr<SRCPanelTreeNode>* FoundNode = FieldWidgetMap.Find(Entity->GetId()))
						{
							(*FoundNode)->SetHighlightText(InSearchText);

							FieldEntities.Add(*FoundNode);
						}
					}
				}
			}
		}
	}

	// Only apply filter if it is requested otherwise skip this.
	if (BackendFilter.HasAnyActiveFilters() && bApplyFilter)
	{
		bFilterApplicationRequested = true;
	}

	FieldsListView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::GenerateListWidgets()
{
	FieldWidgetMap.Reset();

	for (TWeakPtr<FRemoteControlEntity> WeakEntity : Preset->GetExposedEntities())
	{
		if (const TSharedPtr<FRemoteControlEntity> Entity = WeakEntity.Pin())
		{
			FGenerateWidgetArgs Args;
			Args.Entity = Entity;
			Args.Preset = Preset.Get();
			Args.WidgetRegistry = WidgetRegistry;
			Args.ColumnSizeData = ColumnSizeData;
			Args.bIsInEditMode = bIsInEditMode;

			FieldWidgetMap.Add(Entity->GetId(), FRemoteControlUIModule::Get().GenerateEntityWidget(Args));
		}
	}
}

void SRCPanelExposedEntitiesList::GenerateListWidgets(const FRemoteControlPresetGroup& FromGroup)
{
	FieldEntities.Reset();

	if (FieldWidgetMap.IsEmpty())
	{
		FieldsListView->RequestListRefresh();

		return;
	}

	if (Preset->Layout.IsDefaultGroup(FromGroup.Id))
	{
		FieldWidgetMap.GenerateValueArray(FieldEntities);
	}
	else if (TSharedPtr<SRCPanelGroup> SRCGroup = FindGroupById(FromGroup.Id))
	{
		SRCGroup->GetNodeChildren(FieldEntities);
	}

	FieldsListView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::RefreshGroups()
{
	FieldGroups.Reset(Preset->Layout.GetGroups().Num());

	for (const FRemoteControlPresetGroup& RCGroup : Preset->Layout.GetGroups())
	{
		TSharedRef<SRCPanelGroup> FieldGroup = SNew(SRCPanelGroup, Preset.Get(), ColumnSizeData)
			.Id(RCGroup.Id)
			.Name(RCGroup.Name)
			.OnFieldDropEvent(this, &SRCPanelExposedEntitiesList::OnDropOnGroup)
			.OnGetGroupId(this, &SRCPanelExposedEntitiesList::GetGroupId)
			.OnDeleteGroup(this, &SRCPanelExposedEntitiesList::OnDeleteGroup)
			.EditMode(bIsInEditMode);
		
		FieldGroups.Add(FieldGroup);
		FieldGroup->GetNodes().Reserve(RCGroup.GetFields().Num());

		for (const FGuid& FieldId : RCGroup.GetFields())
		{
			if (TSharedPtr<SRCPanelTreeNode>* Widget = FieldWidgetMap.Find(FieldId))
			{
				FieldGroup->GetNodes().Add(*Widget);
			}
		}
	}

	GroupsListView->RequestListRefresh();
}

TSharedRef<ITableRow> SRCPanelExposedEntitiesList::OnGenerateRow(TSharedPtr<SRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	const TSharedRef<SWidget> NodeWidget = Node->AsShared();
	
	auto OnDropLambda = [this, Node]
	(const FDragDropEvent& Event)
	{
		if (const TSharedPtr<FExposedEntityDragDrop> DragDropOp = Event.GetOperationAs<FExposedEntityDragDrop>())
		{
			if (const TSharedPtr<SRCPanelGroup> Group = FindGroupById(GetGroupId(Node->GetRCId())))
			{
				if (DragDropOp->IsOfType<FExposedEntityDragDrop>())
				{
					return OnDropOnGroup(DragDropOp, Node, Group);
				}
				else if (DragDropOp->IsOfType<FFieldGroupDragDropOp>())
				{
					return OnDropOnGroup(DragDropOp, nullptr, Group);
				}
			}
		}

		return FReply::Unhandled();
	};
	
	if (Node->GetRCType() == SRCPanelTreeNode::Group)
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
			.Padding(1.f)
			.Style(&RCPanelStyle->TableRowStyle)
			[
				NodeWidget
			];
	}
	else
	{
		constexpr float LeftPadding = 3.f;
		const FMargin Margin = Node->GetRCType() == SRCPanelTreeNode::FieldChild ? FMargin(LeftPadding + 10.f, 1.f, 1.f, 1.f) : FMargin(LeftPadding, 1.f, 1.f, 1.f);
		return SNew(SEntityRow, OwnerTable)
			.OnDragEnter_Lambda([Node](const FDragDropEvent& Event) { if (Node && Node->GetRCType() == SRCPanelTreeNode::Field) StaticCastSharedPtr<SRCPanelExposedField>(Node)->SetIsHovered(true); })
			.OnDragLeave_Lambda([Node](const FDragDropEvent& Event) { if (Node && Node->GetRCType() == SRCPanelTreeNode::Field) StaticCastSharedPtr<SRCPanelExposedField>(Node)->SetIsHovered(false); })
			.OnDrop_Lambda(OnDropLambda)
			.Padding(Margin)
			.Style(&RCPanelStyle->TableRowStyle)
			.Entity(Node);
	}
}

void SRCPanelExposedEntitiesList::OnGetNodeChildren(TSharedPtr<SRCPanelTreeNode> Node, TArray<TSharedPtr<SRCPanelTreeNode>>& OutNodes)
{
	if (Node.IsValid())
	{
		Node->GetNodeChildren(OutNodes);
	}
}

void SRCPanelExposedEntitiesList::OnSelectionChanged(TSharedPtr<SRCPanelTreeNode> Node, ESelectInfo::Type SelectInfo)
{
	if (!Node || SelectInfo != ESelectInfo::OnMouseClick)
	{
		FieldsListView->ClearSelection();

		OnSelectionChangeDelegate.Broadcast(nullptr);

		return;
	}

	if (Node->GetRCType() == SRCPanelTreeNode::Group)
	{
		if (FRemoteControlPresetGroup* RCGroup = Preset->Layout.GetGroup(Node->GetRCId()))
		{
			GenerateListWidgets(*RCGroup);
			
			if (BackendFilter.HasAnyActiveFilters())
			{
				bFilterApplicationRequested = true;
			}
		}
	
		FieldsListView->ClearSelection();
	}
	else
	{
		// todo call handler on node itself
		if (TSharedPtr<FRemoteControlField> RCField = Preset->GetExposedEntity<FRemoteControlField>(Node->GetRCId()).Pin())
		{
			TSet<UObject*> Objects = TSet<UObject*>{ RCField->GetBoundObjects() };

			TArray<UObject*> OwnerActors;
			for (UObject* Object : Objects)
			{
				if (Object->IsA<AActor>())
				{
					OwnerActors.Add(Object);
				}
				else
				{
					OwnerActors.Add(Object->GetTypedOuter<AActor>());
				}
			}

			SelectActorsInlevel(OwnerActors);
		}
	}

	OnSelectionChangeDelegate.Broadcast(Node);
}

FReply SRCPanelExposedEntitiesList::OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelTreeNode>& TargetEntity, const TSharedPtr<SRCPanelTreeNode>& DragTargetGroup)
{
	checkSlow(DragTargetGroup);

	if (DragDropOperation->IsOfType<FExposedEntityDragDrop>())
	{
		if (Preset->Layout.IsDefaultGroup(DragTargetGroup->GetRCId()))
		{
			// We do not add fields to the default group.
			return FReply::Unhandled();
		}

		if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = StaticCastSharedPtr<FExposedEntityDragDrop>(DragDropOperation))
		{
			FGuid DragOriginGroupId = GetGroupId(DragDropOp->GetId());
			if (!DragOriginGroupId.IsValid())
			{
				return FReply::Unhandled();
			}

			FRemoteControlPresetLayout::FFieldSwapArgs Args;
			Args.OriginGroupId = DragOriginGroupId;
			Args.TargetGroupId = DragTargetGroup->GetRCId();
			Args.DraggedFieldId = DragDropOp->GetId();

			if (TargetEntity)
			{
				Args.TargetFieldId = TargetEntity->GetRCId();
			}
			else
			{
				if (Args.OriginGroupId == Args.TargetGroupId)
				{
					// No-op if dragged from the same group.
					return FReply::Unhandled();
				}
			}

			FScopedTransaction Transaction(LOCTEXT("MoveField", "Move exposed field"));
			Preset->Modify();
			Preset->Layout.SwapFields(Args);
			constexpr bool bForceMouseClick = true;
			SetSelection(FindGroupById(Args.TargetGroupId), bForceMouseClick);
			return FReply::Handled();
		}
	}
	else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
	{
		if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
		{
			FGuid DragOriginGroupId = DragDropOp->GetGroupId();
			FGuid DragTargetGroupId = DragTargetGroup->GetRCId();

			if (DragOriginGroupId == DragTargetGroupId)
			{
				// No-op if dragged from the same group.
				return FReply::Unhandled();
			}

			FScopedTransaction Transaction(LOCTEXT("MoveGroup", "Move Group"));
			Preset->Modify();
			Preset->Layout.SwapGroups(DragOriginGroupId, DragTargetGroupId);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FGuid SRCPanelExposedEntitiesList::GetGroupId(const FGuid& EntityId)
{
	FGuid GroupId;
	if (FRemoteControlPresetGroup* Group = Preset->Layout.FindGroupFromField(EntityId))
	{
		GroupId = Group->Id;
	}

	return GroupId;
}

FReply SRCPanelExposedEntitiesList::OnCreateGroup()
{
	FScopedTransaction Transaction(LOCTEXT("CreateGroup", "Create Group"));
	Preset->Modify();
	Preset->Layout.CreateGroup();
	return FReply::Handled();
}

void SRCPanelExposedEntitiesList::OnDeleteGroup(const FGuid& GroupId)
{
	FScopedTransaction Transaction(LOCTEXT("DeleteGroup", "Delete Group"));
	Preset->Modify();
	Preset->Layout.DeleteGroup(GroupId);
}

void SRCPanelExposedEntitiesList::SelectActorsInlevel(const TArray<UObject*>& Objects)
{
	if (GEditor)
	{
		// Don't change selection if the target's component is already selected
		USelection* Selection = GEditor->GetSelectedComponents();
		
		if (Selection->Num() == 1
			&& Objects.Num() == 1
			&& Selection->GetSelectedObject(0) != nullptr
			&& Selection->GetSelectedObject(0)->GetTypedOuter<AActor>() == Objects[0])
		{
			return;
		}

		GEditor->SelectNone(false, true, false);

		for (UObject* Object : Objects)
		{
			if (AActor* Actor = Cast<AActor>(Object))
			{
				GEditor->SelectActor(Actor, true, true, true);
			}
		}
	}
}

void SRCPanelExposedEntitiesList::RegisterEvents()
{
	OnPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SRCPanelExposedEntitiesList::OnObjectPropertyChange);
}

void SRCPanelExposedEntitiesList::UnregisterEvents()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandle);
}

TSharedPtr<SRCPanelGroup> SRCPanelExposedEntitiesList::FindGroupById(const FGuid& Id)
{
	TSharedPtr<SRCPanelGroup> TargetGroup;
	if (TSharedPtr<SRCPanelGroup>* FoundGroup = FieldGroups.FindByPredicate([Id](const TSharedPtr<SRCPanelGroup>& InGroup) {return InGroup->GetRCId() == Id; }))
	{
		TargetGroup = *FoundGroup;
	}
	return TargetGroup;
}

TSharedPtr<SWidget> SRCPanelExposedEntitiesList::OnContextMenuOpening(SRCPanelTreeNode::ENodeType InType)
{
	if (TSharedPtr<SRCPanelTreeNode> SelectedNode = InType == SRCPanelTreeNode::Group ? SRCPanelExposedEntitiesList::GetSelectedGroup() : SRCPanelExposedEntitiesList::GetSelectedEntity())
	{
		return SelectedNode->GetContextMenu();
	}
	
	return nullptr;
}

void SRCPanelExposedEntitiesList::RegisterPresetDelegates()
{
	FRemoteControlPresetLayout& Layout = Preset->Layout;
	Layout.OnGroupAdded().AddSP(this, &SRCPanelExposedEntitiesList::OnGroupAdded);
	Layout.OnGroupDeleted().AddSP(this, &SRCPanelExposedEntitiesList::OnGroupDeleted);
	Layout.OnGroupOrderChanged().AddSP(this, &SRCPanelExposedEntitiesList::OnGroupOrderChanged);
	Layout.OnGroupRenamed().AddSP(this, &SRCPanelExposedEntitiesList::OnGroupRenamed);
	Layout.OnFieldAdded().AddSP(this, &SRCPanelExposedEntitiesList::OnFieldAdded);
	Layout.OnFieldDeleted().AddSP(this, &SRCPanelExposedEntitiesList::OnFieldDeleted);
	Layout.OnFieldOrderChanged().AddSP(this, &SRCPanelExposedEntitiesList::OnFieldOrderChanged);
	Preset->OnEntitiesUpdated().AddSP(this, &SRCPanelExposedEntitiesList::OnEntitiesUpdated);
}

void SRCPanelExposedEntitiesList::UnregisterPresetDelegates()
{
	if (Preset)
	{
		FRemoteControlPresetLayout& Layout = Preset->Layout;
		Preset->OnEntitiesUpdated().RemoveAll(this);
		Layout.OnFieldOrderChanged().RemoveAll(this);
		Layout.OnFieldDeleted().RemoveAll(this);
		Layout.OnFieldAdded().RemoveAll(this);
		Layout.OnGroupRenamed().RemoveAll(this);
		Layout.OnGroupOrderChanged().RemoveAll(this);
		Layout.OnGroupDeleted().RemoveAll(this);
		Layout.OnGroupAdded().RemoveAll(this);
	}
}

void SRCPanelExposedEntitiesList::OnEntityAdded(const FGuid& InEntityId)
{
	auto ExposeEntity = [this, InEntityId](TSharedPtr<SRCPanelTreeNode>&& Node)
	{
		if (Node)
		{
			FieldWidgetMap.Add(InEntityId, Node);

			if (TSharedPtr<SRCPanelGroup> SRCGroup = FindGroupById(GetGroupId(InEntityId)))
			{
				SRCGroup->GetNodes().Add(MoveTemp(Node));

				GenerateListWidgets(*Preset->Layout.GetGroup(GetGroupId(InEntityId)));
			}
		}
	};
	
	FGenerateWidgetArgs Args;
	Args.Preset = Preset.Get();
	Args.WidgetRegistry = WidgetRegistry;
	Args.ColumnSizeData = ColumnSizeData;
	Args.bIsInEditMode = bIsInEditMode;
	Args.Entity = Preset->GetExposedEntity(InEntityId).Pin();

	ExposeEntity(FRemoteControlUIModule::Get().GenerateEntityWidget(Args));
}

void SRCPanelExposedEntitiesList::OnEntityRemoved(const FGuid& InGroupId, const FGuid& InEntityId)
{
	if (TSharedPtr<SRCPanelGroup> PanelGroup = FindGroupById(InGroupId))
	{
		const int32 EntityIndex = PanelGroup->GetNodes().IndexOfByPredicate([InEntityId](const TSharedPtr<SRCPanelTreeNode>& Node) { return Node->GetRCId() == InEntityId; });
		if (EntityIndex != INDEX_NONE)
		{
			PanelGroup->GetNodes().RemoveAt(EntityIndex);
		}
	}

	if (const TSharedPtr<SRCPanelTreeNode> Node = GetSelectedEntity())
	{
		if (Node->GetRCId() == InEntityId)
		{
			OnSelectionChangeDelegate.Broadcast(nullptr);
		}
	}

	FieldWidgetMap.Remove(InEntityId);
	
	GenerateListWidgets(*Preset->Layout.GetGroup(InGroupId));
}

void SRCPanelExposedEntitiesList::OnGroupAdded(const FRemoteControlPresetGroup& Group)
{
	TSharedRef<SRCPanelGroup> FieldGroup = SNew(SRCPanelGroup, Preset.Get(), ColumnSizeData)
		.Id(Group.Id)
		.Name(Group.Name)
		.OnFieldDropEvent(this, &SRCPanelExposedEntitiesList::OnDropOnGroup)
		.OnGetGroupId(this, &SRCPanelExposedEntitiesList::GetGroupId)
		.OnDeleteGroup(this, &SRCPanelExposedEntitiesList::OnDeleteGroup)
		.EditMode(bIsInEditMode);
	
	FieldGroups.Add(FieldGroup);
	
	FieldGroup->GetNodes().Reserve(Group.GetFields().Num());

	for (FGuid FieldId : Group.GetFields())
	{
		if (TSharedPtr<SRCPanelTreeNode>* Widget = FieldWidgetMap.Find(FieldId))
		{
			FieldGroup->GetNodes().Add(*Widget);
		}
	}

	FieldGroup->EnterRenameMode();
	GroupsListView->SetSelection(FieldGroup, ESelectInfo::OnMouseClick);
	GroupsListView->ScrollToBottom();
	GroupsListView->RequestListRefresh();

	RequestSearchOrFilter();
}

void SRCPanelExposedEntitiesList::OnGroupDeleted(FRemoteControlPresetGroup DeletedGroup)
{
	int32 Index = FieldGroups.IndexOfByPredicate([&DeletedGroup](const TSharedPtr<SRCPanelGroup>& Group) { return Group->GetRCId() == DeletedGroup.Id; });

	if (TSharedPtr<SRCPanelTreeNode> Node = GetSelectedEntity())
	{
		if (DeletedGroup.GetFields().Contains(Node->GetRCId()))
		{
			OnSelectionChangeDelegate.Broadcast(nullptr);
		}
	}
	
	if (Index != INDEX_NONE)
	{
		FieldGroups.RemoveAt(Index);
		GroupsListView->RequestListRefresh();
	}

	RequestSearchOrFilter();
}

void SRCPanelExposedEntitiesList::OnGroupOrderChanged(const TArray<FGuid>& GroupIds)
{
	TMap<FGuid, int32> IndicesMap;
	IndicesMap.Reserve(GroupIds.Num());
	for (auto It = GroupIds.CreateConstIterator(); It; ++It)
	{
		IndicesMap.Add(*It, It.GetIndex());
	}

	auto SortFunc = [&IndicesMap]
	(const TSharedPtr<SRCPanelGroup>& A, const TSharedPtr<SRCPanelGroup>& B)
	{
		return IndicesMap.FindChecked(A->GetRCId()) < IndicesMap.FindChecked(B->GetRCId());
	};

	FieldGroups.Sort(SortFunc);
	GroupsListView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnGroupRenamed(const FGuid& GroupId, FName NewName)
{
	if (TSharedPtr<SRCPanelGroup> Group = FindGroupById(GroupId))
	{
		Group->SetName(NewName);
	}
}

void SRCPanelExposedEntitiesList::OnFieldAdded(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition)
{
	OnEntityAdded(FieldId);

	RequestSearchOrFilter();
}

void SRCPanelExposedEntitiesList::OnFieldDeleted(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition)
{
	OnEntityRemoved(GroupId, FieldId);

	RequestSearchOrFilter();
}

void SRCPanelExposedEntitiesList::OnFieldOrderChanged(const FGuid& GroupId, const TArray<FGuid>& Fields)
{
	if (TSharedPtr<SRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<SRCPanelGroup>& InGroup) {return InGroup->GetRCId() == GroupId; }))
	{
		// Sort the group's fields according to the fields array.
		TMap<FGuid, int32> OrderMap;
		OrderMap.Reserve(Fields.Num());
		for (auto It = Fields.CreateConstIterator(); It; ++It)
		{
			OrderMap.Add(*It, It.GetIndex());
		}

		if (*Group)
		{
			(*Group)->GetNodes().Sort(
			[&OrderMap]
			(const TSharedPtr<SRCPanelTreeNode>& A, const TSharedPtr<SRCPanelTreeNode>& B)
				{
					return OrderMap.FindChecked(A->GetRCId()) < OrderMap.FindChecked(B->GetRCId());
				});
		}
	}

	GroupsListView->RequestListRefresh();
	FieldsListView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnEntitiesUpdated(URemoteControlPreset*, const TSet<FGuid>& UpdatedEntities)
{
	for (const FGuid& EntityId : UpdatedEntities)
	{
		TSharedPtr<SRCPanelTreeNode>* Node = FieldWidgetMap.Find(EntityId);
		if (Node && *Node)
		{
			(*Node)->Refresh();
		}
	}
	
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakListPtr = TWeakPtr<SRCPanelExposedEntitiesList>(StaticCastSharedRef<SRCPanelExposedEntitiesList>(AsShared()))]()
	{
		if (TSharedPtr<SRCPanelExposedEntitiesList> ListPtr = WeakListPtr.Pin())
		{
			ListPtr->GroupsListView->RebuildList();
			ListPtr->FieldsListView->RebuildList();
		}
	}));

	OnEntityListUpdatedDelegate.ExecuteIfBound();

	RequestSearchOrFilter();
}

void SRCPanelExposedEntitiesList::ApplyFilters()
{
	if (BackendFilter.HasAnyActiveFilters())
	{
		// If we are not actively searching anything then include all the entities.
		if (SearchedText.IsValid() && SearchedText->IsEmpty())
		{
			for (TWeakPtr<FRemoteControlEntity> WeakEntity : Preset->GetExposedEntities())
			{
				if (const TSharedPtr<FRemoteControlEntity> Entity = WeakEntity.Pin())
				{
					if (TSharedPtr<SRCPanelTreeNode> SelectedGroup = GetSelectedGroup())
					{
						if (FRemoteControlPresetGroup* EntityGroup = Preset->Layout.FindGroupFromField(Entity->GetId()))
						{
							if (SelectedGroup->GetRCId() == EntityGroup->Id ||
								Preset->Layout.IsDefaultGroup(SelectedGroup->GetRCId()))
							{
								if (TSharedPtr<SRCPanelTreeNode>* FoundNode = FieldWidgetMap.Find(Entity->GetId()))
								{
									if (BackendFilter.DoesPassFilters(FoundNode->ToSharedRef()))
									{
										FieldEntities.AddUnique(*FoundNode);
									}
								}
							}
						}
					}
				}
			}
		}
		else
		{
			// Caution : Avoid filter during this phase of search as it would cause an endless loop between search and filters.
			constexpr bool bApplyFilter = false;

			TryRefreshingSearch(*SearchedText, bApplyFilter);
		}
	
		// Apply the filter (always operate on the active list of entities).
		FieldEntities.RemoveAll([&](TSharedPtr<const SRCPanelTreeNode> InEntity) { return !BackendFilter.DoesPassFilters(InEntity.ToSharedRef()); });

		FieldsListView->RequestListRefresh();
	}
	else // Do a one time refresh in case all filters are cleared.
	{
		Refresh();
	}
}

void SRCPanelExposedEntitiesList::RequestSearchOrFilter()
{
	bSearchRequested = SearchedText.IsValid() && !SearchedText->IsEmpty();

	bFilterApplicationRequested = BackendFilter.HasAnyActiveFilters();
}

FReply SRCPanelExposedEntitiesList::RequestDeleteAllEntities()
{
	if (!FieldsListView.IsValid() || !Preset.IsValid())
	{
		return FReply::Unhandled();
	}

	const FString WarningMessage = FString::Format(TEXT("You are about to delete '{0}' entities. This action might not be undone.\nAre you sure you want to proceed?"), { FString::FromInt(FieldWidgetMap.Num()) });

	EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(WarningMessage));

	if (UserResponse == EAppReturnType::Yes)
	{
		FScopedTransaction Transaction(LOCTEXT("UnexposeAll", "Unexpose all remote control entities"));
		Preset->Modify();

		for (TMap<FGuid, TSharedPtr<SRCPanelTreeNode>>::TConstIterator EntityItr = FieldWidgetMap.CreateConstIterator(); EntityItr; ++EntityItr)
		{
			Preset->Unexpose(EntityItr->Key);
		}
	}

	return FReply::Handled();
}

FReply SRCPanelExposedEntitiesList::RequestDeleteAllGroups()
{
	if (!GroupsListView.IsValid() || !Preset.IsValid())
	{
		return FReply::Unhandled();
	}

	const FString WarningMessage = FString::Format(TEXT("You are about to delete '{0}' groups. This action might not be undone.\nAre you sure you want to proceed?"), { FString::FromInt(FieldGroups.Num() - 1) });

	EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(WarningMessage));

	if (UserResponse == EAppReturnType::Yes)
	{
		FScopedTransaction Transaction(LOCTEXT("DeleteAllGroups", "Delete all remote control groups"));
		Preset->Modify();

		const TArray<FRemoteControlPresetGroup>& RCGroups = Preset->Layout.GetGroups();

		// Perform deletion in reverse to avoid "Ensure condition failed: Lhs.CurrentNum == Lhs.InitialNum"
		for (int32 GroupIndex = RCGroups.Num() - 1; GroupIndex >= 0; GroupIndex--)
		{
			const FRemoteControlPresetGroup& RCGroup = RCGroups[GroupIndex];

			if (Preset->Layout.IsDefaultGroup(RCGroup.Id))
			{
				continue;
			}
		
			Preset->Layout.DeleteGroup(RCGroup.Id);
		}

		Refresh();
	}

	return FReply::Handled();
}

bool FGroupDragEvent::IsDraggedFromSameGroup() const
{
	return DragOriginGroup->GetId() == DragTargetGroup->GetId();
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanelFieldList */