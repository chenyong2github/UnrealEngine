// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntitiesList.h"

#include "Algo/ForEach.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "Engine/Selection.h"
#include "Editor/EditorEngine.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
#include "RemoteControlActor.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "SRCPanelFieldGroup.h"
#include "SRCPanelExposedActor.h"
#include "SRCPanelExposedField.h"
#include "SRCPanelTreeNode.h"
#include "UObject/Object.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelEntitiesList"

void SRCPanelExposedEntitiesList::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	bIsInEditMode = InArgs._EditMode;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	bDisplayValues = InArgs._DisplayValues;

	ChildSlot
	[
		SAssignNew(TreeView, STreeView<TSharedPtr<SRCPanelTreeNode>>)
		.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<SRCPanelTreeNode>>*>(&FieldGroups))
		.ItemHeight(24.0f)
		.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
		.OnGetChildren(this, &SRCPanelExposedEntitiesList::OnGetNodeChildren)
		.OnSelectionChanged(this, &SRCPanelExposedEntitiesList::OnSelectionChanged)
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

		if (TSharedPtr<FRCPanelGroup>* FoundGroup = FieldGroups.FindByPredicate([&Node](const TSharedPtr<SRCPanelTreeNode>& Item) { return Item->GetId() == Node->GetId(); }))
		{
			TreeView->SetSelection(*FoundGroup);
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

	if ((InChangeEvent.ChangeType & TypesNeedingRefresh) != 0 && InChangeEvent.MemberProperty && IsRelevantProperty(InChangeEvent.MemberProperty->GetClass()))
	{
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

	for (const TSharedPtr<FRCPanelGroup>& Group : FieldGroups)
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
	            SNew(SRCPanelExposedField, MoveTemp(WeakField))
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
				SNew(SRCPanelExposedActor, MoveTemp(WeakActor), Preset.Get())
				.EditMode(bIsInEditMode));
		}
	}
}

void SRCPanelExposedEntitiesList::RefreshGroups()
{
	FieldGroups.Reset(Preset->Layout.GetGroups().Num());

	for (const FRemoteControlPresetGroup& RCGroup : Preset->Layout.GetGroups())
	{
		TSharedPtr<FRCPanelGroup> NewGroup = MakeShared<FRCPanelGroup>(RCGroup.Name, RCGroup.Id);
		FieldGroups.Add(NewGroup);
		NewGroup->Nodes.Reserve(RCGroup.GetFields().Num());

		for (const FGuid& FieldId : RCGroup.GetFields())
		{
			if (TSharedPtr<SRCPanelTreeNode>* Widget = FieldWidgetMap.Find(FieldId))
			{
				NewGroup->Nodes.Add(*Widget);
			}
		}
	}

	TreeView->RequestListRefresh();
}

TSharedRef<ITableRow> SRCPanelExposedEntitiesList::OnGenerateRow(TSharedPtr<SRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Node->GetType() == SRCPanelTreeNode::Group)
	{
		return SNew(SFieldGroup, OwnerTable, Node->AsGroup(), Preset.Get())
			.OnFieldDropEvent_Raw(this, &SRCPanelExposedEntitiesList::OnDropOnGroup)
			.OnGetGroupId_Raw(this, &SRCPanelExposedEntitiesList::GetGroupId)
			.OnDeleteGroup_Raw(this, &SRCPanelExposedEntitiesList::OnDeleteGroup)
			.EditMode(bIsInEditMode);
	}
	else if (Node->GetType() == SRCPanelTreeNode::Field)
	{
		auto OnDropLambda = [this, Field = Node->AsField()]
		(const FDragDropEvent& Event)
		{
			if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = Event.GetOperationAs<FExposedEntityDragDrop>())
			{
				FGuid GroupId = GetGroupId(Field->GetId());
				if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& TargetGroup) { return TargetGroup->Id == GroupId; }))
				{
					if (DragDropOp->IsOfType<FExposedEntityDragDrop>())
					{
						return OnDropOnGroup(DragDropOp, Field, *Group);
					}
					else if (DragDropOp->IsOfType<FFieldGroupDragDropOp>())
					{
						return OnDropOnGroup(DragDropOp, nullptr, *Group);
					}
				}
			}

			return FReply::Unhandled();
		};

		return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
			.OnDragEnter_Lambda([Field = Node->AsField()](const FDragDropEvent& Event) { Field->SetIsHovered(true); })
			.OnDragLeave_Lambda([Field = Node->AsField()](const FDragDropEvent& Event) { Field->SetIsHovered(false); })
			.OnDrop_Lambda(OnDropLambda)
			.Padding(FMargin(20.f, 0.f, 0.f, 0.f))
			[
				Node->AsField().ToSharedRef()
			];
	}
	else if (Node->GetType() == SRCPanelTreeNode::FieldChild)
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
			.Padding(FMargin(30.f, 0.f, 0.f, 0.f))
			[
				Node->AsFieldChild().ToSharedRef()
			];
	}
	else
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
			.Padding(FMargin(30.f, 0.f, 0.f, 0.f))
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

FReply SRCPanelExposedEntitiesList::OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelTreeNode>& TargetEntity, const TSharedPtr<FRCPanelGroup>& DragTargetGroup)
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
			Args.TargetGroupId = DragTargetGroup->Id;
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
			FGuid DragTargetGroupId = DragTargetGroup->Id;

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

void SRCPanelExposedEntitiesList::OnDeleteGroup(const TSharedPtr<FRCPanelGroup>& PanelGroup)
{
	FScopedTransaction Transaction(LOCTEXT("DeleteGroup", "Delete Group"));
	Preset->Modify();
	Preset->Layout.DeleteGroup(PanelGroup->Id);
}

void SRCPanelExposedEntitiesList::SelectActorsInlevel(const TArray<UObject*>& Objects)
{
	if (GEditor)
	{
		// Don't change selection if the target's component is already selected
		USelection* Selection = GEditor->GetSelectedComponents();
		if (Selection->Num() == 1 && Objects.Num() == 1 && Selection->GetSelectedObject(0)->GetTypedOuter<AActor>() == Objects[0])
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
	MapChangedHandle = FEditorDelegates::MapChange.AddLambda([this](uint32) { Refresh(); });
}

void SRCPanelExposedEntitiesList::UnregisterEvents()
{
	FEditorDelegates::MapChange.Remove(MapChangedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandle);
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

		TSharedPtr<FRCPanelGroup>* SRCGroup = FieldGroups.FindByPredicate([GroupId = GetGroupId(InEntityId)](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; });
		if (SRCGroup && SRCGroup->IsValid())
		{
			(*SRCGroup)->Nodes.Add(MoveTemp(Node));
			TreeView->SetItemExpansion(*SRCGroup, true);
		}
	};

	if (TSharedPtr<FRemoteControlActor> Actor = Preset->GetExposedEntity<FRemoteControlActor>(InEntityId).Pin())
	{
		ExposeEntity(SNew(SRCPanelExposedActor, MoveTemp(Actor), Preset.Get())
			.EditMode(bIsInEditMode));
	}
	else if (TSharedPtr<FRemoteControlField> Field = Preset->GetExposedEntity<FRemoteControlField>(InEntityId).Pin())
	{
		ExposeEntity(SNew(SRCPanelExposedField, MoveTemp(Field))
			.Preset(Preset.Get())
			.EditMode(bIsInEditMode)
			.DisplayValues(bDisplayValues));
	}
	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnEntityRemoved(const FGuid& InGroupId, const FGuid& InEntityId)
{
	TSharedPtr<FRCPanelGroup>* PanelGroup = FieldGroups.FindByPredicate([InGroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == InGroupId; });
	if (PanelGroup && *PanelGroup)
	{
		int32 EntityIndex = (*PanelGroup)->Nodes.IndexOfByPredicate([InEntityId](const TSharedPtr<SRCPanelTreeNode>& Node) {return Node->GetId() == InEntityId; });
		if (EntityIndex != INDEX_NONE)
		{
			(*PanelGroup)->Nodes.RemoveAt(EntityIndex);
		}
	}

	FieldWidgetMap.Remove(InEntityId);
	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnGroupAdded(const FRemoteControlPresetGroup& Group)
{
	TSharedPtr<FRCPanelGroup> NewGroup = MakeShared<FRCPanelGroup>(Group.Name, Group.Id);
	FieldGroups.Add(NewGroup);
	NewGroup->Nodes.Reserve(Group.GetFields().Num());

	for (FGuid FieldId : Group.GetFields())
	{
		if (TSharedPtr<SRCPanelTreeNode>* Widget = FieldWidgetMap.Find(FieldId))
		{
			NewGroup->Nodes.Add(*Widget);
		}
	}
	TreeView->SetSelection(NewGroup);
	TreeView->ScrollToBottom();
	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnGroupDeleted(FRemoteControlPresetGroup DeletedGroup)
{
	int32 Index = FieldGroups.IndexOfByPredicate([&DeletedGroup](const TSharedPtr<FRCPanelGroup>& Group) { return Group->Id == DeletedGroup.Id; });
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
	(const TSharedPtr<FRCPanelGroup>& A, const TSharedPtr<FRCPanelGroup>& B)
	{
		return IndicesMap.FindChecked(A->Id) < IndicesMap.FindChecked(B->Id);
	};

	FieldGroups.Sort(SortFunc);
	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnGroupRenamed(const FGuid& GroupId, FName NewName)
{
	if (TSharedPtr<FRCPanelGroup>* TargetGroup = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& Group) {return Group->Id == GroupId; }))
	{
		if (*TargetGroup)
		{
			(*TargetGroup)->Name = NewName;
			if (TSharedPtr<SFieldGroup> GroupWidget = StaticCastSharedPtr<SFieldGroup>(TreeView->WidgetFromItem(*TargetGroup)))
			{
				GroupWidget->SetName(NewName);
			}
		}
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
	if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; }))
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
			(*Group)->Nodes.Sort(
			[&OrderMap]
			(const TSharedPtr<SRCPanelTreeNode>& A, const TSharedPtr<SRCPanelTreeNode>& B)
				{
					return OrderMap.FindChecked(A->GetId()) < OrderMap.FindChecked(B->GetId());
				});
		}
	}

	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnEntitiesUpdated(URemoteControlPreset*, const TArray<FGuid>& UpdatedEntities)
{
	for (const FGuid& EntityId : UpdatedEntities)
	{
		TSharedPtr<SRCPanelTreeNode>* Node = FieldWidgetMap.Find(EntityId);
		if (Node && *Node)
		{
			(*Node)->Refresh();
		}
	}

	TreeView->RequestListRefresh();
}

bool FGroupDragEvent::IsDraggedFromSameGroup() const
{
	return DragOriginGroup.Name == DragTargetGroup.Name;
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanelFieldList */