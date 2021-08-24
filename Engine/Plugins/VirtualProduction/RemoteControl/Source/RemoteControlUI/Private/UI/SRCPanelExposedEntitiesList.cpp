// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntitiesList.h"

#include "Algo/ForEach.h"
#include "Editor.h"
#include "ISettingsModule.h"
#include "RCPanelWidgetRegistry.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "Engine/Selection.h"
#include "Editor/EditorEngine.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
#include "RemoteControlActor.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "RemoteControlSettings.h"
#include "ScopedTransaction.h"
#include "SRCPanelFieldGroup.h"
#include "SRCPanelExposedActor.h"
#include "SRCPanelExposedField.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelEntitiesList"

void SRCPanelExposedEntitiesList::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TWeakPtr<FRCPanelWidgetRegistry> InWidgetRegistry)
{
	bIsInEditMode = InArgs._EditMode;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	bDisplayValues = InArgs._DisplayValues;
	OnEntityListUpdatedDelegate = InArgs._OnEntityListUpdated;
	WidgetRegistry = MoveTemp(InWidgetRegistry);

	ColumnSizeData.LeftColumnWidth = TAttribute<float>(this, &SRCPanelExposedEntitiesList::OnGetLeftColumnWidth);
	ColumnSizeData.RightColumnWidth = TAttribute<float>(this, &SRCPanelExposedEntitiesList::OnGetRightColumnWidth);
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SRCPanelExposedEntitiesList::OnSetColumnWidth);

	ChildSlot
	[
		SAssignNew(TreeView, STreeView<TSharedPtr<SRCPanelTreeNode>>)
			.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<SRCPanelTreeNode>>*>(&FieldGroups))
			.ItemHeight(24.0f)
			.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
			.OnGetChildren(this, &SRCPanelExposedEntitiesList::OnGetNodeChildren)
			.OnSelectionChanged(this, &SRCPanelExposedEntitiesList::OnSelectionChanged)
			.OnContextMenuOpening(this, &SRCPanelExposedEntitiesList::OnContextMenuOpening)
			.ClearSelectionOnClick(false)
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

TSharedPtr<SRCPanelTreeNode> SRCPanelExposedEntitiesList::GetSelection() const
{
	TArray<TSharedPtr<SRCPanelTreeNode>> SelectedNodes;
	TreeView->GetSelectedItems(SelectedNodes);
	if (SelectedNodes.Num())
	{
		return SelectedNodes[0];
	}
	return nullptr;
}

void SRCPanelExposedEntitiesList::SetSelection(const TSharedPtr<SRCPanelTreeNode>& Node)
{
	if (Node)
	{
		if (TSharedPtr<SRCPanelTreeNode>* FoundTreeNode = FieldWidgetMap.Find(Node->GetId()))
		{
			TreeView->SetSelection(*FoundTreeNode);
			return;
		}

		if (TSharedPtr<SRCPanelGroup> SRCGroup = FindGroupById(Node->GetId()))
		{
			TreeView->SetSelection(SRCGroup);
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

	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::Refresh()
{
	GenerateListWidgets();
	RefreshGroups();

	for (const TSharedPtr<SRCPanelGroup>& Group : FieldGroups)
	{
		TreeView->SetItemExpansion(Group, true);
	}

	for (const TTuple<FGuid, TSharedPtr<SRCPanelTreeNode>>& FieldTuple : FieldWidgetMap)
	{
		TreeView->SetItemExpansion(FieldTuple.Value, false);
	}
}

void SRCPanelExposedEntitiesList::GenerateListWidgets()
{
	FieldWidgetMap.Reset();

	for (TWeakPtr<FRemoteControlField> WeakField : Preset->GetExposedEntities<FRemoteControlField>())
	{
		if (TSharedPtr<FRemoteControlField> Field = WeakField.Pin())
		{
			FieldWidgetMap.Add(Field->GetId(),
	            SNew(SRCPanelExposedField, MoveTemp(WeakField), ColumnSizeData, WidgetRegistry)
	            .Preset(Preset.Get())
	            .EditMode(bIsInEditMode)
	            .DisplayValues(bDisplayValues)
	        );
		}
	}

	for (TWeakPtr<FRemoteControlActor> WeakActor : Preset->GetExposedEntities<FRemoteControlActor>())
	{
		if (TSharedPtr<FRemoteControlActor> Actor = WeakActor.Pin())
		{
			FieldWidgetMap.Add(Actor->GetId(),
				SNew(SRCPanelExposedActor, MoveTemp(WeakActor), Preset.Get(), ColumnSizeData)
				.EditMode(bIsInEditMode));
		}
	}
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

	TreeView->RequestListRefresh();
}

TSharedRef<ITableRow> SRCPanelExposedEntitiesList::OnGenerateRow(TSharedPtr<SRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	constexpr float LeftPadding = 3.f;
	if (Node->GetType() == SRCPanelTreeNode::Group)
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
		.Padding(FMargin(0.5f, 0.5f, 0.5f, 0.5f))
		.Style(&FRemoteControlPanelStyle::Get()->GetWidgetStyle<FTableRowStyle>("RemoteControlPanel.GroupRow"))
		[
			Node->AsGroup().ToSharedRef()
		];
	}
	else if (Node->GetType() == SRCPanelTreeNode::Field)
	{
		auto OnDropLambda = [this, Field = Node->AsField()]
		(const FDragDropEvent& Event)
		{
			if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = Event.GetOperationAs<FExposedEntityDragDrop>())
			{
				if (TSharedPtr<SRCPanelGroup> Group = FindGroupById(GetGroupId(Field->GetId())))
				{
					if (DragDropOp->IsOfType<FExposedEntityDragDrop>())
					{
						return OnDropOnGroup(DragDropOp, Field, Group);
					}
					else if (DragDropOp->IsOfType<FFieldGroupDragDropOp>())
					{
						return OnDropOnGroup(DragDropOp, nullptr, Group);
					}
				}
			}

			return FReply::Unhandled();
		};

		return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
			.OnDragEnter_Lambda([Field = Node->AsField()](const FDragDropEvent& Event) { Field->SetIsHovered(true); })
			.OnDragLeave_Lambda([Field = Node->AsField()](const FDragDropEvent& Event) { Field->SetIsHovered(false); })
			.OnDrop_Lambda(OnDropLambda)
			.Padding(FMargin(LeftPadding, 0.f, 0.f, 0.f))
			[
				Node->AsField().ToSharedRef()
			];
	}
	else if (Node->GetType() == SRCPanelTreeNode::FieldChild)
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
			.Padding(FMargin(LeftPadding + 10.f, 0.f, 0.f, 0.f))
			[
				Node->AsFieldChild().ToSharedRef()
			];
	}
	else
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
			.Padding(FMargin(LeftPadding, 0.f, 0.f, 0.f))
			[
				Node->AsActor().ToSharedRef()
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

	if (TSharedPtr<SRCPanelExposedField> Field = Node->AsField())
	{
		TSet<UObject*> Objects;
		Field->GetBoundObjects(Objects);

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

		for (UObject* Object : Objects)
		{
			SelectActorsInlevel(OwnerActors);
		}
	}

	OnSelectionChangeDelegate.Broadcast(Node);
}

FReply SRCPanelExposedEntitiesList::OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelTreeNode>& TargetEntity, const TSharedPtr<SRCPanelGroup>& DragTargetGroup)
{
	checkSlow(DragTargetGroup);

	if (DragDropOperation->IsOfType<FExposedEntityDragDrop>())
	{
		if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = StaticCastSharedPtr<FExposedEntityDragDrop>(DragDropOperation))
		{
			FGuid DragOriginGroupId = GetGroupId(DragDropOp->GetId());
			if (!DragOriginGroupId.IsValid())
			{
				return FReply::Unhandled();
			}

			FRemoteControlPresetLayout::FFieldSwapArgs Args;
			Args.OriginGroupId = DragOriginGroupId;
			Args.TargetGroupId = DragTargetGroup->GetId();
			Args.DraggedFieldId = DragDropOp->GetId();

			if (TargetEntity)
			{
				Args.TargetFieldId = TargetEntity->GetId();
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
			return FReply::Handled();
		}
	}
	else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
	{
		if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
		{
			FGuid DragOriginGroupId = DragDropOp->GetGroupId();
			FGuid DragTargetGroupId = DragTargetGroup->GetId();

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

void SRCPanelExposedEntitiesList::OnDeleteGroup(const TSharedPtr<SRCPanelGroup>& PanelGroup)
{
	FScopedTransaction Transaction(LOCTEXT("DeleteGroup", "Delete Group"));
	Preset->Modify();
	Preset->Layout.DeleteGroup(PanelGroup->GetId());
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
	if (TSharedPtr<SRCPanelGroup>* FoundGroup = FieldGroups.FindByPredicate([Id](const TSharedPtr<SRCPanelGroup>& InGroup) {return InGroup->GetId() == Id; }))
	{
		TargetGroup = *FoundGroup;
	}
	return TargetGroup;
}

TSharedPtr<SWidget> SRCPanelExposedEntitiesList::OnContextMenuOpening()
{
	if (TSharedPtr<SRCPanelTreeNode> SelectedNode = SRCPanelExposedEntitiesList::GetSelection())
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
	auto ExposeEntity = [this, InEntityId](TSharedRef<SRCPanelTreeNode>&& Node)
	{
		FieldWidgetMap.Add(InEntityId, Node);

		if (TSharedPtr<SRCPanelGroup> SRCGroup = FindGroupById(GetGroupId(InEntityId)))
		{
			SRCGroup->GetNodes().Add(MoveTemp(Node));
			TreeView->SetItemExpansion(SRCGroup, true);
		}
	};

	if (TSharedPtr<FRemoteControlActor> Actor = Preset->GetExposedEntity<FRemoteControlActor>(InEntityId).Pin())
	{
		ExposeEntity(SNew(SRCPanelExposedActor, MoveTemp(Actor), Preset.Get(), ColumnSizeData)
			.EditMode(bIsInEditMode));
	}
	else if (TSharedPtr<FRemoteControlField> Field = Preset->GetExposedEntity<FRemoteControlField>(InEntityId).Pin())
	{
		ExposeEntity(SNew(SRCPanelExposedField, MoveTemp(Field), ColumnSizeData, WidgetRegistry)
			.Preset(Preset.Get())
			.EditMode(bIsInEditMode)
			.DisplayValues(bDisplayValues));
	}
	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnEntityRemoved(const FGuid& InGroupId, const FGuid& InEntityId)
{
	if (TSharedPtr<SRCPanelGroup> PanelGroup = FindGroupById(InGroupId))
	{
		int32 EntityIndex = PanelGroup->GetNodes().IndexOfByPredicate([InEntityId](const TSharedPtr<SRCPanelTreeNode>& Node) { return Node->GetId() == InEntityId; });
		if (EntityIndex != INDEX_NONE)
		{
			PanelGroup->GetNodes().RemoveAt(EntityIndex);
		}
	}

	if (TSharedPtr<SRCPanelTreeNode> Node = GetSelection())
	{
		if (Node->GetId() == InEntityId)
		{
			OnSelectionChangeDelegate.Broadcast(nullptr);
		}
	}

	FieldWidgetMap.Remove(InEntityId);
	TreeView->RequestListRefresh();
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
	TreeView->SetSelection(FieldGroup);
	TreeView->ScrollToBottom();
	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnGroupDeleted(FRemoteControlPresetGroup DeletedGroup)
{
	int32 Index = FieldGroups.IndexOfByPredicate([&DeletedGroup](const TSharedPtr<SRCPanelGroup>& Group) { return Group->GetId() == DeletedGroup.Id; });

	if (TSharedPtr<SRCPanelTreeNode> Node = GetSelection())
	{
		if (DeletedGroup.GetFields().Contains(Node->GetId()))
		{
			OnSelectionChangeDelegate.Broadcast(nullptr);
		}
	}
	
	if (Index != INDEX_NONE)
	{
		FieldGroups.RemoveAt(Index);
		TreeView->RequestListRefresh();
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
		return IndicesMap.FindChecked(A->GetId()) < IndicesMap.FindChecked(B->GetId());
	};

	FieldGroups.Sort(SortFunc);
	TreeView->RequestListRefresh();
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
	if (TSharedPtr<SRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<SRCPanelGroup>& InGroup) {return InGroup->GetId() == GroupId; }))
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
					return OrderMap.FindChecked(A->GetId()) < OrderMap.FindChecked(B->GetId());
				});
		}
	}

	TreeView->RequestListRefresh();
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
			ListPtr->TreeView->RequestListRefresh();
		}
	}));

	OnEntityListUpdatedDelegate.ExecuteIfBound();
}

bool FGroupDragEvent::IsDraggedFromSameGroup() const
{
	return DragOriginGroup->GetId() == DragTargetGroup->GetId();
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanelFieldList */