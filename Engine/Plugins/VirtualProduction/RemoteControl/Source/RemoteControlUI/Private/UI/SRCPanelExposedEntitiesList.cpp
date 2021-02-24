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

SRemoteControlTarget::SRemoteControlTarget(FName Alias, URemoteControlPreset* Preset, TAttribute<bool> bInIsInEditMode, bool bInDisplayValues)
	: TargetAlias(Alias)
	, Preset(Preset)
	, bIsInEditMode(MoveTemp(bInIsInEditMode))
{
	Algo::ForEach(GetUnderlyingTarget().ExposedProperties, [this, bInDisplayValues](const FRemoteControlProperty& RCProperty) {AddExposedProperty(RCProperty, bInDisplayValues); });
	Algo::ForEach(GetUnderlyingTarget().ExposedFunctions, [this, bInDisplayValues](const FRemoteControlFunction& RCFunction) {AddExposedFunction(RCFunction, bInDisplayValues); });
}

void SRemoteControlTarget::RefreshTargetWidgets()
{
	for (TSharedRef<SRCPanelExposedField>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->Refresh();
	}
}

TSharedPtr<SRCPanelExposedField> SRemoteControlTarget::AddExposedProperty(const FRemoteControlProperty& RCProperty, bool bDisplayValues)
{
	return ExposedFieldWidgets.Add_GetRef(SNew(SRCPanelExposedField, RCProperty)
		.Preset(MakeAttributeRaw(this, &SRemoteControlTarget::GetPreset))
		.EditMode(MakeAttributeRaw(this, &SRemoteControlTarget::GetPanelEditMode))
		.DisplayValues(bDisplayValues));
}

TSharedPtr<SRCPanelExposedField> SRemoteControlTarget::AddExposedFunction(const FRemoteControlFunction& RCFunction, bool bDisplayValues)
{
	return ExposedFieldWidgets.Add_GetRef(SNew(SRCPanelExposedField, RCFunction)
		.Preset(MakeAttributeRaw(this, &SRemoteControlTarget::GetPreset))
		.EditMode(MakeAttributeRaw(this, &SRemoteControlTarget::GetPanelEditMode))
		.DisplayValues(bDisplayValues));
}

TSet<UObject*> SRemoteControlTarget::GetBoundObjects() const
{
	TSet<UObject*> Objects;
	for (const TSharedRef<SRCPanelExposedField>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->GetBoundObjects(Objects);
	}
	return Objects;
}

void SRemoteControlTarget::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	for (TSharedRef<SRCPanelExposedField>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->OnObjectsReplaced(ReplacementObjectMap);
	}
}

FRemoteControlTarget& SRemoteControlTarget::GetUnderlyingTarget()
{
	check(Preset.IsValid());
	return Preset->GetRemoteControlTargets().FindChecked(TargetAlias);
}

UClass* SRemoteControlTarget::GetTargetClass()
{
	return GetUnderlyingTarget().Class;
}

EVisibility SRemoteControlTarget::GetVisibilityAccordingToEditMode() const
{
	return GetPanelEditMode() ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SRemoteControlTarget::GetPanelEditMode() const
{
	return bIsInEditMode.Get();
}

URemoteControlPreset* SRemoteControlTarget::GetPreset()
{
	return Preset.Get();
}

void SRCPanelExposedEntitiesList::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	bIsInEditMode = InArgs._EditMode;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	OnSelectionChangeDelegate = InArgs._OnSelectionChange;
	bDisplayValues = InArgs._DisplayValues;

	ChildSlot
	[
		SAssignNew(TreeView, STreeView<TSharedPtr<SRCPanelTreeNode>>)
		.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<SRCPanelTreeNode>>*>(&FieldGroups))
		.ItemHeight(24.0f)
		.OnGenerateRow(this, &SRCPanelExposedEntitiesList::OnGenerateRow)
		.OnGetChildren(this, &SRCPanelExposedEntitiesList::OnGetGroupChildren)
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

void SRCPanelExposedEntitiesList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bTriggerRefreshForPIE)
	{
		for (TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
		{
			for (TSharedPtr<SRCPanelExposedField> Field : Target->GetFieldWidgets())
			{
				if (TOptional<FExposedProperty> Property = Preset->ResolveExposedProperty(Field->GetFieldLabel()))
				{
					Field->SetBoundObjects(Property->OwnerObjects);
				}
			}
		}

		TreeView->RequestListRefresh();
		bTriggerRefreshForPIE = false;
	}
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

void SRCPanelExposedEntitiesList::SetSelection(const FGuid& Id)
{
	if (TSharedPtr<SRCPanelTreeNode>* FoundTreeNode = FieldWidgetMap.Find(Id))
	{
		TreeView->SetSelection(*FoundTreeNode);
		return;
	}

	if (TSharedPtr<FRCPanelGroup>* FoundGroup = FieldGroups.FindByPredicate([&Id](const TSharedPtr<SRCPanelTreeNode>& Item) { return Item->GetId() == Id; }))
	{
		TreeView->SetSelection(*FoundGroup);
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
		for (const TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
		{
			if (Target->GetBoundObjects().Contains(InObject))
			{
				Target->RefreshTargetWidgets();
			}
		}

		TreeView->RequestTreeRefresh();
	}
}

void SRCPanelExposedEntitiesList::ReplaceObjects(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	for (TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
	{
		Target->OnObjectsReplaced(ReplacementObjectMap);
	}
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
	TMap<FName, FRemoteControlTarget>& TargetMap = Preset->GetRemoteControlTargets();

	RemoteControlTargets.Reset(TargetMap.Num());
	FieldWidgetMap.Reset();

	for (TTuple<FName, FRemoteControlTarget>& MapEntry : TargetMap)
	{
		TSharedRef<SRemoteControlTarget> Target = MakeShared<SRemoteControlTarget>(MapEntry.Key, Preset.Get(), bIsInEditMode, bDisplayValues);
		RemoteControlTargets.Add(Target);
		for (const TSharedRef<SRCPanelExposedField>& Widget : Target->GetFieldWidgets())
		{
			FieldWidgetMap.Add(Widget->GetFieldId(), Widget);
		}
	}

	for (const TWeakPtr<FRemoteControlActor>& WeakActor : Preset->GetExposedEntities<FRemoteControlActor>())
	{
		if (TSharedPtr<FRemoteControlActor> Actor = WeakActor.Pin())
		{
			FieldWidgetMap.Add(Actor->GetId(),
				SNew(SRCPanelExposedActor, *Actor, Preset.Get())
				.EditMode(bIsInEditMode)
			);
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

void SRCPanelExposedEntitiesList::OnGetGroupChildren(TSharedPtr<SRCPanelTreeNode> Node, TArray<TSharedPtr<SRCPanelTreeNode>>& OutNodes)
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

	OnSelectionChangeDelegate.ExecuteIfBound(Node);
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
	GEngine->OnLevelActorDeleted().AddSP(this, &SRCPanelExposedEntitiesList::OnActorDeleted);

	if (GEditor)
	{
		FEditorDelegates::PostPIEStarted.AddSP(this, &SRCPanelExposedEntitiesList::OnPieEvent);
		FEditorDelegates::EndPIE.AddSP(this, &SRCPanelExposedEntitiesList::OnPieEvent);

 		GEditor->OnObjectsReplaced().AddSP(this, &SRCPanelExposedEntitiesList::ReplaceObjects);
	}
}

void SRCPanelExposedEntitiesList::UnregisterEvents()
{
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);

		FEditorDelegates::EndPIE.RemoveAll(this);
		FEditorDelegates::PostPIEStarted.RemoveAll(this);
	}

	GEngine->OnLevelActorDeleted().RemoveAll(this);

	FEditorDelegates::MapChange.Remove(MapChangedHandle);

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandle);
}

void SRCPanelExposedEntitiesList::OnActorDeleted(AActor* Actor)
{
	UWorld* World = Actor->GetWorld();
	if (World && !World->IsPreviewWorld())
	{
		TSet<FName> TargetsToRemove;

		for (TTuple<FName, FRemoteControlTarget>& Tuple : Preset->GetRemoteControlTargets())
		{
			if (Tuple.Value.HasBoundObjects({ Actor }))
			{
				TargetsToRemove.Add(Tuple.Key);
			}
		}

		Preset->Modify();
		for (FName TargetName : TargetsToRemove)
		{
			Preset->DeleteTarget(TargetName);
		}

		if (TargetsToRemove.Num())
		{
			Refresh();
		}
	}
}

void SRCPanelExposedEntitiesList::OnPieEvent(bool)
{
	// Trigger the refresh on the next tick to make sure the PIE world exists when starting and doesn't exist when stopping.
	bTriggerRefreshForPIE = true;
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
	Preset->OnFieldRenamed().AddSP(this, &SRCPanelExposedEntitiesList::OnFieldRenamed);
}

void SRCPanelExposedEntitiesList::UnregisterPresetDelegates()
{
	if (Preset)
	{
		FRemoteControlPresetLayout& Layout = Preset->Layout;
		Preset->OnFieldRenamed().RemoveAll(this);
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
	const UScriptStruct* Entity = Preset->GetExposedEntityType(InEntityId);
	if (Entity)
	{
		if (TSharedPtr<FRemoteControlActor> Actor = Preset->GetExposedEntity<FRemoteControlActor>(InEntityId).Pin())
		{
			TSharedRef<SRCPanelTreeNode> ActorWidget =
				SNew(SRCPanelExposedActor, *Actor, Preset.Get())
				.EditMode(bIsInEditMode);

			FieldWidgetMap.Add(InEntityId, ActorWidget);

			TSharedPtr<FRCPanelGroup>* SRCGroup = FieldGroups.FindByPredicate([GroupId = GetGroupId(InEntityId)](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; });
			if (SRCGroup && SRCGroup->IsValid())
			{
				(*SRCGroup)->Nodes.Add(ActorWidget);
				TreeView->SetItemExpansion(*SRCGroup, true);
				TreeView->RequestListRefresh();
			}
		}
	}
}

void SRCPanelExposedEntitiesList::OnEntityRemoved(const FGuid& InGroupId, const FGuid& InEntityId)
{
	TSharedPtr<FRCPanelGroup>* PanelGroup = FieldGroups.FindByPredicate([InGroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == InGroupId; });
	if (PanelGroup && *PanelGroup)
	{
		int32 ActorIndex = (*PanelGroup)->Nodes.IndexOfByPredicate([InEntityId](const TSharedPtr<SRCPanelTreeNode>& Node) {return Node->GetId() == InEntityId; });
		if (ActorIndex != INDEX_NONE)
		{
			(*PanelGroup)->Nodes.RemoveAt(ActorIndex);
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
	FName TargetName = Preset->GetOwnerAlias(FieldId);
	if (TargetName == NAME_None)
	{
		OnEntityAdded(FieldId);
		return;
	}

	auto GetFieldWidget = [this, &FieldId](const TSharedRef<SRemoteControlTarget>& Target)
	{
		TSharedPtr<SRCPanelExposedField> FieldWidget;
		if (TOptional<FRemoteControlProperty> Property = Preset->GetProperty(FieldId))
		{
			FieldWidget = Target->AddExposedProperty(*Property, bDisplayValues);
		}
		else if (TOptional<FRemoteControlFunction> Function = Preset->GetFunction(FieldId))
		{
			FieldWidget = Target->AddExposedFunction(*Function, bDisplayValues);
		}

		return FieldWidget;
	};

	if (TOptional<FRemoteControlField> Field = Preset->GetField(FieldId))
	{
		TSharedPtr<SRCPanelExposedField> FieldWidget;

		// If target already exists in the panel.
		if (TSharedRef<SRemoteControlTarget>* Target = RemoteControlTargets.FindByPredicate([TargetName](const TSharedRef<SRemoteControlTarget>& InTarget) { return InTarget->GetTargetAlias() == TargetName; }))
		{
			FieldWidget = GetFieldWidget(*Target);
		}
		else
		{
			const FRemoteControlTarget& FieldOwnerTarget = Preset->GetRemoteControlTargets().FindChecked(TargetName);

			TSharedRef<SRemoteControlTarget> PanelTarget = MakeShared<SRemoteControlTarget>(TargetName, Preset.Get(), bIsInEditMode);
			RemoteControlTargets.Add(PanelTarget);
			FieldWidget = GetFieldWidget(PanelTarget);
		}

		FieldWidgetMap.Add(FieldId, FieldWidget);
		if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; }))
		{
			if (*Group)
			{
				(*Group)->Nodes.Insert(FieldWidget, FieldPosition);
				TreeView->SetItemExpansion(*Group, true);
			}
		}
	}

	TreeView->RequestListRefresh();
}

void SRCPanelExposedEntitiesList::OnFieldDeleted(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition)
{
	FName TargetName = Preset->GetOwnerAlias(FieldId);
	if (TargetName == NAME_None)
	{
		OnEntityRemoved(GroupId, FieldId);
		return;
	}

	if (TSharedRef<SRemoteControlTarget>* Target = RemoteControlTargets.FindByPredicate([TargetName](const TSharedRef<SRemoteControlTarget>& InTarget) { return InTarget->GetTargetAlias() == TargetName; }))
	{
		if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; }))
		{
			if (*Group)
			{
				(*Group)->Nodes.RemoveAt(FieldPosition);
			}
		}

		TArray<TSharedRef<SRCPanelExposedField>>& FieldWdigets = (*Target)->GetFieldWidgets();
		int32 Index = FieldWdigets.IndexOfByPredicate([&FieldId](const TSharedRef<SRCPanelExposedField>& Widget) {return Widget->GetFieldId() == FieldId; });
		if (Index != INDEX_NONE)
		{
			FieldWdigets.RemoveAt(Index);
			FieldWidgetMap.Remove(FieldId);
		}
	}

	TreeView->RequestListRefresh();
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

void SRCPanelExposedEntitiesList::OnFieldRenamed(URemoteControlPreset*, FName OldName, FName NewName)
{
	TreeView->RequestListRefresh();
}

bool FGroupDragEvent::IsDraggedFromSameGroup() const
{
	return DragOriginGroup.Name == DragTargetGroup.Name;
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanelFieldList */