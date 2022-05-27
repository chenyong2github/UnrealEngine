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
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "RemoteControlUIModule.h"
#include "ScopedTransaction.h"
#include "SRCPanelFieldGroup.h"
#include "SRCPanelExposedField.h"
#include "UObject/Object.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelEntitiesList"

namespace FRemoteControlPresetColumns
{
	const FName DragDropHandle = TEXT("DragDropHandle");
	const FName IsBound = TEXT("IsBound");
	const FName Description = TEXT("Description");
	const FName Value = TEXT("Value");
	const FName Groups = TEXT("Groups");
}

void SRCPanelExposedEntitiesList::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TWeakPtr<FRCPanelWidgetRegistry> InWidgetRegistry)
{
	bIsInEditMode = InArgs._EditMode;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	OnEntityListUpdatedDelegate = InArgs._OnEntityListUpdated;
	WidgetRegistry = MoveTemp(InWidgetRegistry);

	ColumnSizeData.LeftColumnWidth = TAttribute<float>(this, &SRCPanelExposedEntitiesList::OnGetLeftColumnWidth);
	ColumnSizeData.RightColumnWidth = TAttribute<float>(this, &SRCPanelExposedEntitiesList::OnGetRightColumnWidth);
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SRCPanelExposedEntitiesList::OnSetColumnWidth);

	ChildSlot
	[
		SNew(SVerticalBox)
		
		+SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			
			+SHorizontalBox::Slot()
			.Padding(2.f, 4.f)
			.FillWidth(0.25f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				[
					SAssignNew(GroupsListView, SListView<TSharedPtr<SRCPanelTreeNode>>)
					.ItemHeight(24.f)
					.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
					.OnSelectionChanged(this, &SRCPanelExposedEntitiesList::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
					.ListItemsSource(reinterpret_cast<TArray<TSharedPtr<SRCPanelTreeNode>>*>(&FieldGroups))
					.OnContextMenuOpening(this, &SRCPanelExposedEntitiesList::OnContextMenuOpening, SRCPanelTreeNode::Group)
					.ClearSelectionOnClick(true)
					.HeaderRow(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(FRemoteControlPresetColumns::Groups)
						.DefaultLabel(LOCTEXT("RCPresetGroupsColumnHeader", "Groups"))
						.FillWidth(1.f)
						.ToolTipText(LOCTEXT("NewGroupToolTip", "Create new group."))
						[
							SNew(SHorizontalBox)
				
							+SHorizontalBox::Slot()
							.Padding(3.f)
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								SNew(STextBlock)
								.ColorAndOpacity(FColor::White)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12.f))
								.Text(LOCTEXT("GroupsLabel", "Groups"))
							]
	
							+SHorizontalBox::Slot()
							.Padding(3.f)
							.VAlign(VAlign_Center)
							.FillWidth(1.f)
							[
								SNew(SSpacer)
							]
	
							+ SHorizontalBox::Slot()
							.Padding(3.f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								SNew(SButton)
								.Visibility_Lambda([this]() { return bIsInEditMode.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
								.ButtonStyle(FAppStyle::Get(), "ToggleButton")
								.ToolTipText(LOCTEXT("NewGroupToolTip", "Create new group."))
								.OnClicked(this, &SRCPanelExposedEntitiesList::OnCreateGroup)
								[
									SNew(SOverlay)

									+SOverlay::Slot()
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
										.ColorAndOpacity(FColor::White)
										.Font(FAppStyle::Get().GetFontStyle("FontAwesome.14"))
										.Text(FEditorFontGlyphs::Circle_O)
									]

									+SOverlay::Slot()
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
										.ColorAndOpacity(FColor::White)
										.Font(FAppStyle::Get().GetFontStyle("FontAwesome.7"))
										.Text(FEditorFontGlyphs::Plus)
									]
								]
							]
						]
					)
				]
			]

			+SHorizontalBox::Slot()
			.Padding(2.f, 4.f)
			.FillWidth(0.75f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				[
					SAssignNew(FieldsListView, SListView<TSharedPtr<SRCPanelTreeNode>>)
					.ItemHeight(24.f)
					.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
					.OnSelectionChanged(this, &SRCPanelExposedEntitiesList::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
					.ListItemsSource(&FieldEntities)
					.OnContextMenuOpening(this, &SRCPanelExposedEntitiesList::OnContextMenuOpening, SRCPanelTreeNode::Field)
					.ClearSelectionOnClick(true)
					.HeaderRow(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(FRemoteControlPresetColumns::DragDropHandle)
						.DefaultLabel(LOCTEXT("RCPresetDragDropHandleColumnHeader", ""))
						.FixedWidth(36.0f)

						+ SHeaderRow::Column(FRemoteControlPresetColumns::Description)
						.DefaultLabel(LOCTEXT("RCPresetDescColumnHeader", "Description"))
						.FillWidth(0.7f)

						+ SHeaderRow::Column(FRemoteControlPresetColumns::Value)
						.DefaultLabel(LOCTEXT("RCPresetValueColumnHeader", "Value"))
						.FillWidth(0.7f)
					)
				]
			]
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
			.OnFieldDropEvent_Raw(this, &SRCPanelExposedEntitiesList::OnDropOnGroup)
			.OnGetGroupId_Raw(this, &SRCPanelExposedEntitiesList::GetGroupId)
			.OnDeleteGroup_Raw(this, &SRCPanelExposedEntitiesList::OnDeleteGroup)
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
			.Style(&FRemoteControlPanelStyle::Get()->GetWidgetStyle<FTableRowStyle>("RemoteControlPanel.GroupRow"))
			[
				NodeWidget
			];
	}
	else
	{
		constexpr float LeftPadding = 3.f;
		const FMargin Margin = Node->GetRCType() == SRCPanelTreeNode::FieldChild ? FMargin(LeftPadding + 10.f, 0.f, 0.f, 0.f) : FMargin(LeftPadding, 0.f, 0.f, 0.f);
		return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
			.OnDragEnter_Lambda([Node](const FDragDropEvent& Event) { if (Node && Node->GetRCType() == SRCPanelTreeNode::Field) StaticCastSharedPtr<SRCPanelExposedField>(Node)->SetIsHovered(true); })
			.OnDragLeave_Lambda([Node](const FDragDropEvent& Event) { if (Node && Node->GetRCType() == SRCPanelTreeNode::Field) StaticCastSharedPtr<SRCPanelExposedField>(Node)->SetIsHovered(false); })
			.OnDrop_Lambda(OnDropLambda)
			.Padding(Margin)
			[
				NodeWidget
			];
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
		return;
	}

	if (Node->GetRCType() == SRCPanelTreeNode::Group)
	{
		if (FRemoteControlPresetGroup* RCGroup = Preset->Layout.GetGroup(Node->GetRCId()))
		{
			GenerateListWidgets(*RCGroup);
		}
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
	Layout.OnGroupRenamed().AddRaw(this, &SRCPanelExposedEntitiesList::OnGroupRenamed);
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
		.OnFieldDropEvent_Raw(this, &SRCPanelExposedEntitiesList::OnDropOnGroup)
		.OnGetGroupId_Raw(this, &SRCPanelExposedEntitiesList::GetGroupId)
		.OnDeleteGroup_Raw(this, &SRCPanelExposedEntitiesList::OnDeleteGroup)
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
}

void SRCPanelExposedEntitiesList::OnFieldDeleted(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition)
{
	OnEntityRemoved(GroupId, FieldId);
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
			ListPtr->GroupsListView->RequestListRefresh();
			ListPtr->FieldsListView->RequestListRefresh();
		}
	}));

	OnEntityListUpdatedDelegate.ExecuteIfBound();
}

bool FGroupDragEvent::IsDraggedFromSameGroup() const
{
	return DragOriginGroup->GetId() == DragTargetGroup->GetId();
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanelFieldList */