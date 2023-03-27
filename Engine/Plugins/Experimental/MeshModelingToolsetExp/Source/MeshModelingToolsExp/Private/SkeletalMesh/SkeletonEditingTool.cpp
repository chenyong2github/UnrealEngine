// Copyright Epic Games, Inc. All Rights Reserved.


#include "SkeletalMesh/SkeletonEditingTool.h"

#include "ContextObjectStore.h"
#include "InteractiveToolManager.h"
#include "ModelingToolTargetUtil.h"
#include "SkeletalDebugRendering.h"
#include "ToolTargetManager.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "UnrealClient.h"
#include "HitProxies.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseGizmos/GizmoViewContext.h"

#include "Algo/Count.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#define LOCTEXT_NAMESPACE "USkeletonEditingTool"

namespace SkeletonEditingTool
{
	
FRefSkeletonChange::FRefSkeletonChange(const USkeletonEditingTool* InTool)
	: FToolCommandChange()
	, PreChangeSkeleton(InTool->SkeletonModifier.GetReferenceSkeleton())
	, PreBoneTracker(InTool->SkeletonModifier.GetBoneIndexTracker())
	, PostChangeSkeleton(InTool->SkeletonModifier.GetReferenceSkeleton())
	, PostBoneTracker(InTool->SkeletonModifier.GetBoneIndexTracker())
{}

void FRefSkeletonChange::StoreSkeleton(const USkeletonEditingTool* InTool)
{
	PostChangeSkeleton = InTool->SkeletonModifier.GetReferenceSkeleton();
	PostBoneTracker = InTool->SkeletonModifier.GetBoneIndexTracker();
}

void FRefSkeletonChange::Apply(UObject* Object)
{ // redo
	USkeletonEditingTool* Tool = CastChecked<USkeletonEditingTool>(Object);
	Tool->SkeletonModifier.ExternalUpdate(PostChangeSkeleton, PostBoneTracker);
}

void FRefSkeletonChange::Revert(UObject* Object)
{ // undo
	USkeletonEditingTool* Tool = CastChecked<USkeletonEditingTool>(Object);
	Tool->SkeletonModifier.ExternalUpdate(PreChangeSkeleton, PreBoneTracker);
}

}

/**
 * USkeletonEditingToolBuilder
 */

bool USkeletonEditingToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1;
}

const FToolTargetTypeRequirements& USkeletonEditingToolBuilder::GetTargetRequirements() const
{
	static const FToolTargetTypeRequirements TypeRequirements(UPrimitiveComponentBackedTarget::StaticClass());
	return TypeRequirements;
}

UInteractiveTool* USkeletonEditingToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USkeletonEditingTool* NewTool = NewObject<USkeletonEditingTool>(SceneState.ToolManager);
	
	UToolTarget* Target = SceneState.TargetManager->BuildFirstSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTarget(Target);
	NewTool->Init(SceneState);
	
	return NewTool;
}

/**
 * USkeletonEditingTool
 */

void USkeletonEditingTool::Init(const FToolBuilderState& InSceneState)
{
	TargetWorld = InSceneState.World;
	ViewContext = InSceneState.ToolManager->GetContextObjectStore()->FindContext<UGizmoViewContext>();
}

void USkeletonEditingTool::Setup()
{
	Super::Setup();

	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	const USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(TargetComponent->GetOwnerComponent());

	USkeletalMesh* SkeletalMesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
	if (!SkeletalMesh)
	{
		return;
	}

	// setup modifier
	SkeletonModifier.Init(SkeletalMesh);

	// setup current bone
	const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();
	const FName& RootBoneName = NumBones ? RefSkeleton.GetBoneName(0) : NAME_None;

	if (NumBones)
	{
		CurrentBone = RootBoneName;
	}

	// setup preview
	{
		PreviewMesh = NewObject<UPreviewMesh>(this);
		PreviewMesh->bBuildSpatialDataStructure = true;
		PreviewMesh->CreateInWorld(TargetWorld.Get(), FTransform::Identity);

		PreviewMesh->SetTransform(UE::ToolTarget::GetLocalToWorldTransform(Target));

		PreviewMesh->ReplaceMesh(UE::ToolTarget::GetDynamicMeshCopy(Target));

		const FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Target);
		PreviewMesh->SetMaterials(MaterialSet.Materials);

		// hide the skeletal mesh component
		UE::ToolTarget::HideSourceObject(Target);
	}

	// setup properties
	{
		Properties = NewObject<USkeletonEditingProperties>();
		Properties->Initialize(this);
		Properties->RestoreProperties(this);
		AddToolPropertySource(Properties);

		ProjectionProperties = NewObject<UProjectionProperties>();
		ProjectionProperties->Initialize(PreviewMesh);
		ProjectionProperties->RestoreProperties(this);
		AddToolPropertySource(ProjectionProperties);

		MirroringProperties = NewObject<UMirroringProperties>();
		MirroringProperties->Initialize(this);
		MirroringProperties->RestoreProperties(this);
		AddToolPropertySource(MirroringProperties);

		OrientingProperties = NewObject<UOrientingProperties>();
		OrientingProperties->Initialize(this);
		OrientingProperties->RestoreProperties(this);
		AddToolPropertySource(OrientingProperties);
	}

	// setup drag & drop behaviour
	{
		UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
		ClickDragBehavior->Initialize(this);
		AddInputBehavior(ClickDragBehavior);
	}
}

void USkeletonEditingTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("SkeletonEditingTool", "Commit Skeleton Editing"));
		SkeletonModifier.CommitSkeletonToSkeletalMesh();
		GetToolManager()->EndUndoTransaction();

		// to force to refresh the tree
		if (NeedsNotification())
		{
			GetNotifier().Notify({}, ESkeletalMeshNotifyType::BonesAdded);
		}
	}
	
	Super::Shutdown(ShutdownType);

	// remove preview mesh
	if (PreviewMesh != nullptr)
	{
		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

	// show the skeletal mesh component
	UE::ToolTarget::ShowSourceObject(Target);

	//save properties	
	Properties->SaveProperties(this);
	ProjectionProperties->SaveProperties(this);
	MirroringProperties->SaveProperties(this);
	OrientingProperties->SaveProperties(this);
}

void USkeletonEditingTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	Super::RegisterActions(ActionSet);

	int32 ActionId = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 400;
	auto GetActionId = [&ActionId]
	{
		return ActionId++;
	};
	
	// register New key
	ActionSet.RegisterAction(this, GetActionId(), TEXT("CreateNewBone"),
		LOCTEXT("CreateNewBone", "Create New Bone"),
		LOCTEXT("CreateNewBoneDesc", "Create New Bone"),
		EModifierKey::None, EKeys::N,
		[this]()
		{
			Operation = EEditingOperation::Create;
			GetToolManager()->DisplayMessage(LOCTEXT("Create", "Click & Drag to place a new bone."), EToolMessageLevel::UserNotification);
		});
	
	// register Delete key
	ActionSet.RegisterAction(this, GetActionId(), TEXT("DeleteSelectedBones"),
		LOCTEXT("DeleteSelectedBones", "Delete Selected Bone(s)"),
		LOCTEXT("DeleteSelectedBonesDesc", "Delete Selected Bone(s)"),
		EModifierKey::None, EKeys::Delete,
		[this]()
		{
			RemoveBones();
		});

	// register Select key
	ActionSet.RegisterAction(this, GetActionId(), TEXT("SelectBones"),
		LOCTEXT("SelectBone", "Select Bone"),
		LOCTEXT("SelectDesc", "Select Bone"),
		EModifierKey::None, EKeys::Escape,
		[this]()
		{
			Operation = EEditingOperation::Select;
			GetToolManager()->DisplayMessage(LOCTEXT("Select", "Click on a bone to select it."), EToolMessageLevel::UserNotification);
		});

	// register UnParent key
	ActionSet.RegisterAction(this, GetActionId(), TEXT("UnparentBones"),
		LOCTEXT("UnparentBones", "Unparent Bones"),
		LOCTEXT("UnparentBonesDesc", "Unparent Bones"),
		EModifierKey::Shift, EKeys::P,
		[this]()
		{
			UnParentBones();
		});
		
	// register Parent key
	ActionSet.RegisterAction(this, GetActionId(), TEXT("ParentBones"),
		LOCTEXT("ParentBones", "Parent Bones"),
		LOCTEXT("ParentBonesDesc", "Parent Bones"),
		EModifierKey::None, EKeys::B, // FIXME find another shortcut
		[this]()
		{
			Operation = EEditingOperation::Parent;
			GetToolManager()->DisplayMessage(LOCTEXT("Parent", "Click on a bone to be set as the new parent."), EToolMessageLevel::UserNotification);
		});
}

void USkeletonEditingTool::CreateNewBone()
{
	if (Operation != EEditingOperation::Create)
	{
		return;
	}

	BeginChange();

	const FName BoneName = SkeletonModifier.GetUniqueName(Properties->DefaultName);
	const bool bBoneAdded = SkeletonModifier.AddBone(BoneName, CurrentBone, Properties->Transform);
	if (bBoneAdded)
	{
		if (NeedsNotification())
		{
			GetNotifier().Notify({BoneName}, ESkeletalMeshNotifyType::BonesAdded);
		}
	
		CurrentBone = BoneName;
		Properties->Name = CurrentBone;

		EndChange();
		return;
	}

	CancelChange();
}

void USkeletonEditingTool::MirrorBones()
{
	TGuardValue OperationGuard(Operation, EEditingOperation::Mirror);
	BeginChange();

	const bool bBonesMirrored = SkeletonModifier.MirrorBones(GetSelectedBones(), MirroringProperties->Options);
	if (bBonesMirrored)
	{
		EndChange();
		return;		
	}
	
	CancelChange();
}

void USkeletonEditingTool::RemoveBones()
{
	const TArray<FName> BonesToRemove = GetSelectedBones();
	
	TGuardValue OperationGuard(Operation, EEditingOperation::Remove);
	BeginChange();

	const bool bBonesRemoved = SkeletonModifier.RemoveBones(BonesToRemove, true);
	if (bBonesRemoved)
	{
		// if (NeedsNotification())
		// {
		// 	GetNotifier().Notify(BonesToRemove, ESkeletalMeshNotifyType::BonesRemoved);
		// }
		
		EndChange();
		return;
	}

	CancelChange();
}

void USkeletonEditingTool::UnParentBones()
{
	static const TArray<FName> Dummy;

	TGuardValue OperationGuard(Operation, EEditingOperation::Parent);
	BeginChange();

	const bool bBonesUnParented = SkeletonModifier.ParentBones(GetSelectedBones(), Dummy);
	if (bBonesUnParented)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("Unparent", "Selected bones have been unparented."), EToolMessageLevel::UserNotification);
		
		EndChange();
		return;
	}
	
	CancelChange();
}

void USkeletonEditingTool::ParentBones(const FName& InParentName)
{
	if (Operation != EEditingOperation::Parent)
	{
		return;
	}

	BeginChange();
	const bool bBonesParented = SkeletonModifier.ParentBones(GetSelectedBones(), {InParentName});
	if (bBonesParented)
	{
		Operation = EEditingOperation::Select;
		EndChange();
		return;
	}

	Operation = EEditingOperation::Select;
	CancelChange();
}

void USkeletonEditingTool::MoveBones()
{
	const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();

	const TArray<FName> Bones = GetSelectedBones();
	const bool bHasValidBone = Bones.ContainsByPredicate([&](const FName& InBoneName)
	{
		return RefSkeleton.FindRawBoneIndex(InBoneName) > INDEX_NONE;
	});

	if (!bHasValidBone)
	{
		return;
	}

	TGuardValue OperationGuard(Operation, EEditingOperation::Transform);
	BeginChange();

	const bool bBonesMoved = SkeletonModifier.SetBoneTransform(Bones[0], Properties->Transform, Properties->bUpdateChildren);
	if (bBonesMoved)
	{
		// if (NeedsNotification())
		// {
		// 	GetNotifier().Notify(Bones, ESkeletalMeshNotifyType::BonesMoved);
		// }
		
		EndChange();
		return;
	}

	CancelChange();
}

void USkeletonEditingTool::RenameBones()
{
	if (CurrentBone == Properties->Name || Properties->Name == NAME_None)
	{
		return;
	}
	
	const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();
	if (RefSkeleton.FindRawBoneIndex(CurrentBone) == INDEX_NONE)
	{
		return;
	}

	TGuardValue OperationGuard(Operation, EEditingOperation::Rename);
	BeginChange();

	const bool bBoneRenamed = SkeletonModifier.RenameBone(CurrentBone, Properties->Name);
	if (bBoneRenamed)
	{
		CurrentBone = Properties->Name;

		// if (NeedsNotification())
		// {
		// 	GetNotifier().Notify({CurrentBone}, ESkeletalMeshNotifyType::BonesRenamed);
		// }
		
		EndChange();
		return;
	}

	CancelChange();
}

void USkeletonEditingTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	BeginChange();
}

void USkeletonEditingTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	FVector HitPoint;
	if (ProjectionProperties->GetProjectionPoint(DragPos, HitPoint))
	{
		const FTransform& ParentGlobal = SkeletonModifier.GetTransform(ParentIndex, true);
		Properties->Transform.SetLocation(ParentGlobal.InverseTransformPosition(HitPoint));

		if (!ActiveChange)
		{
			TGuardValue OperationGuard(Operation, EEditingOperation::Transform);
			BeginChange();
		}

		const bool bBoneMoved = SkeletonModifier.SetBoneTransform(CurrentBone, Properties->Transform, Properties->bUpdateChildren);
		if (!bBoneMoved)
		{
			CancelChange();
			return;
		}

		const bool bOrient = Operation == EEditingOperation::Create && OrientingProperties->bAutoOrient;
		if (bOrient && ParentIndex != INDEX_NONE)
		{
			const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();
			const FName ParentName = RefSkeleton.GetRawRefBoneInfo()[ParentIndex].Name;
			SkeletonModifier.OrientBone(ParentName, OrientingProperties->Options);
		}
	}
}

void USkeletonEditingTool::OrientBones()
{
	const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();

	const TArray<FName> Bones = GetSelectedBones();
	const bool bHasValidBone = Bones.ContainsByPredicate([&](const FName& InBoneName)
	{
		return RefSkeleton.FindRawBoneIndex(InBoneName) > INDEX_NONE;
	});

	if (!bHasValidBone)
	{
		return;
	}

	TGuardValue OperationGuard(Operation, EEditingOperation::Transform);
	BeginChange();
	
	const bool bBoneOriented = SkeletonModifier.OrientBones(Bones, OrientingProperties->Options);
	if (bBoneOriented)
	{
		// if (NeedsNotification())
		// {
		// 	GetNotifier().Notify(Bones, ESkeletalMeshNotifyType::BonesMoved);
		// }
		
		EndChange();
		return;
	}

	CancelChange();
}

void USkeletonEditingTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	TGuardValue OperationGuard(Operation, EEditingOperation::Transform);
	EndChange();
}

void USkeletonEditingTool::OnTerminateDragSequence()
{}

FInputRayHit USkeletonEditingTool::CanBeginClickDragSequence(const FInputDeviceRay& ClickPos)
{
	auto PickBone = [&]() -> int32
	{
		FViewport* FocusedViewport = GetToolManager()->GetContextQueriesAPI()->GetFocusedViewport();
		if (HHitProxy* HitProxy = FocusedViewport->GetHitProxy(ClickPos.ScreenPosition.X, ClickPos.ScreenPosition.Y))
		{
			if (TOptional<FName> OptBoneName = GetBoneName(HitProxy))
			{
				const FReferenceSkeleton& ReferenceSkeleton = SkeletonModifier.GetReferenceSkeleton();
				return ReferenceSkeleton.FindRawBoneIndex(*OptBoneName);
			}
		}
		return INDEX_NONE;
	};
	
	// pick bone in viewport
	const int32 BoneIndex = PickBone();

	// update parent
	const FReferenceSkeleton& ReferenceSkeleton = SkeletonModifier.GetReferenceSkeleton();
	ParentIndex = INDEX_NONE;

	// update projection properties
	const FVector GlobalPosition = SkeletonModifier.GetTransform(BoneIndex, true).GetTranslation();
	ProjectionProperties->UpdatePlane(*ViewContext, GlobalPosition);

	// if we picked a new bone
	if (BoneIndex > INDEX_NONE)
	{
		// parent selection without changing the selection
		if (Operation == EEditingOperation::Parent)
		{	
			ParentBones(ReferenceSkeleton.GetBoneName(BoneIndex));
			return FInputRayHit();
		}
		
		// otherwise, update current selection
		CurrentBone = ReferenceSkeleton.GetBoneName(BoneIndex);

		Properties->Name = CurrentBone;
		Properties->Transform = ReferenceSkeleton.GetRefBonePose()[BoneIndex];
		ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
		
		if (NeedsNotification())
		{
			GetNotifier().Notify({CurrentBone}, ESkeletalMeshNotifyType::BonesSelected);
		}
	
		return FInputRayHit(0.0);
	}

	// if we didn't pick anything
	if (Operation == EEditingOperation::Select)
	{
		// unselect all
		CurrentBone = NAME_None;
		Properties->Name = CurrentBone; 
		
		if (NeedsNotification())
		{
			GetNotifier().Notify({CurrentBone}, ESkeletalMeshNotifyType::BonesSelected);
		}
		
		return FInputRayHit();
	}

	// if we're in creation mode then create a new bone
	if (Operation == EEditingOperation::Create)
	{
		FVector HitPoint;
		if (ProjectionProperties->GetProjectionPoint(ClickPos, HitPoint))
		{
			// CurrentBone is gonna be the parent
			ParentIndex = ReferenceSkeleton.FindRawBoneIndex(CurrentBone);
			const FTransform& ParentGlobalTransform = SkeletonModifier.GetTransform(ParentIndex, true);

			// Create the new bone under mouse
			Properties->Transform.SetLocation(ParentGlobalTransform.InverseTransformPosition(HitPoint));
			CreateNewBone();
			
			return FInputRayHit(0.0);
		}
	}
	
	return FInputRayHit();
}

void USkeletonEditingTool::HandleSkeletalMeshModified(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	const FName BoneName = BoneNames.IsEmpty() ? NAME_None : BoneNames[0];
	switch (InNotifyType)
	{
		case ESkeletalMeshNotifyType::BonesAdded:
			CurrentBone = BoneName;
			break;
		case ESkeletalMeshNotifyType::BonesRemoved:
			if (BoneNames.Contains(CurrentBone))
			{
				CurrentBone = NAME_None;
			}
			break;
		case ESkeletalMeshNotifyType::BonesMoved:
			CurrentBone = BoneName;
			break;
		case ESkeletalMeshNotifyType::BonesSelected:
			CurrentBone = BoneName;
			break;
		case ESkeletalMeshNotifyType::BonesRenamed:
			CurrentBone = BoneName;
			break;
		default:
			break;
	}
	Properties->Name = CurrentBone;
}

void USkeletonEditingTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	// FIXME many things could be caches here and updated lazilly
	if (!Target)
	{
		return;
	}

	static const FLinearColor DefaultBoneColor(0.0f,0.0f,0.025f,1.0f);
	static const FLinearColor SelectedBoneColor(0.2f,1.0f,0.2f,1.0f);
	static const FLinearColor AffectedBoneColor(1.0f,1.0f,1.0f,1.0f);
	static const FLinearColor ParentOfSelectedBoneColor(0.85f,0.45f,0.12f,1.0f);
	static FSkelDebugDrawConfig DrawConfig;
		DrawConfig.BoneDrawMode = EBoneDrawMode::Type::All;
		DrawConfig.BoneDrawSize = 1.f;
		DrawConfig.bAddHitProxy = true;
		DrawConfig.bForceDraw = false;
		DrawConfig.DefaultBoneColor = DefaultBoneColor;
		DrawConfig.AffectedBoneColor = AffectedBoneColor;
		DrawConfig.SelectedBoneColor = SelectedBoneColor;
		DrawConfig.ParentOfSelectedBoneColor = ParentOfSelectedBoneColor;
		DrawConfig.AxisConfig.Thickness = Properties->AxisThickness;
		DrawConfig.AxisConfig.Length = Properties->AxisLength;
	
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	const FTransform ComponentTransform = TargetComponent->GetWorldTransform();
	
	const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();

	const int32 NumBones = RefSkeleton.GetRawBoneNum();
	TArray<TRefCountPtr<HHitProxy>> HitProxies; HitProxies.Reserve(NumBones);
	TArray<FBoneIndexType> RequiredBones; RequiredBones.AddUninitialized(NumBones);
	TArray<FTransform> WorldTransforms; WorldTransforms.AddUninitialized(NumBones);
	TArray<FLinearColor> BoneColors; BoneColors.AddUninitialized(NumBones);
	
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FTransform& BoneTransform = SkeletonModifier.GetTransform(Index, true);
		WorldTransforms[Index] = BoneTransform;
		RequiredBones[Index] = Index;
		BoneColors[Index] = DefaultBoneColor;
		HitProxies.Add(new HBoneHitProxy(Index, RefSkeleton.GetBoneName(Index)));
	}

	// FIXME cache this
	TArray<int32> SelectedBones;
	for (const FName& BoneName: GetSelectedBones())
	{
		int32 SelectedIndex = RefSkeleton.FindRawBoneIndex(CurrentBone);
		if (SelectedIndex > INDEX_NONE)
		{
			SelectedBones.Add(SelectedIndex);
		}
	}

	SkeletalDebugRendering::DrawBones(
		PDI,
		ComponentTransform.GetLocation(),
		RequiredBones,
		RefSkeleton,
		WorldTransforms,
		SelectedBones,
		BoneColors,
		HitProxies,
		DrawConfig
	);
}

FBox USkeletonEditingTool::GetWorldSpaceFocusBox()
{
	const TArray<FName> Selection = GetSelectedBones();
	if (!Selection.IsEmpty())
	{
		FBox Box(EForceInit::ForceInit);
		TSet<int32> AllChildren;

		const FReferenceSkeleton& RefSkeleton = SkeletonModifier.GetReferenceSkeleton();
		
		for (const FName& BoneName: Selection)
		{
			const int32 BoneIndex = RefSkeleton.FindRawBoneIndex(BoneName);
			Box += SkeletonModifier.GetTransform(BoneIndex, true).GetTranslation();

			// get direct children
			TArray<int32> Children;
			RefSkeleton.GetDirectChildBones(BoneIndex, Children);
			AllChildren.Append(Children);
		}

		for (const int32 ChildIndex: AllChildren)
		{
			Box += SkeletonModifier.GetTransform(ChildIndex, true).GetTranslation();
		}
		
		return Box;
	}

	if (PreviewMesh && PreviewMesh->GetActor())
	{
		return PreviewMesh->GetActor()->GetComponentsBoundingBox();
	}

	return USingleSelectionTool::GetWorldSpaceFocusBox();
}

TArray<FName> USkeletonEditingTool::GetSelectedBones() const
{
	if (Binding.IsValid())
	{
		const TArray<FName> Selection = Binding.Pin()->GetSelectedBones();
		if (!Selection.IsEmpty())
		{
			return Selection;
		}
	}

	if (CurrentBone != NAME_None)
	{
		return {CurrentBone};
	}

	const TArray<FName> Dummy;
	return Dummy;
}

void USkeletonEditingTool::BeginChange()
{
	if (Operation == EEditingOperation::Select)
	{
		return;
	}
	
	ensure( ActiveChange == nullptr );
	ActiveChange = MakeUnique<SkeletonEditingTool::FRefSkeletonChange>(this); 
}

void USkeletonEditingTool::EndChange()
{
	if (!ActiveChange.IsValid())
	{
		return;
	}

	if (Operation == EEditingOperation::Select)
	{
		return CancelChange();
	}
	
	ActiveChange->StoreSkeleton(this);

	static const UEnum* OperationEnum = StaticEnum<EEditingOperation>();
	const FName OperationName = OperationEnum->GetNameByValue(static_cast<int64>(Operation));
	const FText TransactionDesc = FText::Format(LOCTEXT("RefSkeletonChanged", "Skeleton Edit - {0}"), FText::FromName(OperationName));
	
	UInteractiveToolManager* ToolManager = GetToolManager();
	ToolManager->BeginUndoTransaction(TransactionDesc);
	ToolManager->EmitObjectChange(this, MoveTemp(ActiveChange), TransactionDesc);
	ToolManager->EndUndoTransaction();
}

void USkeletonEditingTool::CancelChange()
{
	ActiveChange.Reset();
}

void USkeletonEditingProperties::Initialize(USkeletonEditingTool* ParentToolIn)
{
	ParentTool = ParentToolIn;
	Name = ParentTool->CurrentBone;
}

#if WITH_EDITOR
void USkeletonEditingProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
	{
		const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(USkeletonEditingProperties, Name))
		{
			ParentTool->RenameBones();
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(USkeletonEditingProperties, Transform))
		{
			ParentTool->MoveBones();
		}
	}
}
#endif

void UMirroringProperties::Initialize(USkeletonEditingTool* ParentToolIn)
{
	ParentTool = ParentToolIn;
}

void UMirroringProperties::MirrorBones()
{
	ParentTool->MirrorBones();
}

void UOrientingProperties::Initialize(USkeletonEditingTool* ParentToolIn)
{
	ParentTool = ParentToolIn;
}

void UOrientingProperties::OrientBones()
{
	ParentTool->OrientBones();
}

#if WITH_EDITOR

void UOrientingProperties::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
	{
		auto CheckAxis = [&](const TEnumAsByte<EAxis::Type>& InRef, TEnumAsByte<EAxis::Type>& OutOther)
		{
			if (OutOther != InRef)
			{
				return;
			}

			switch (InRef)
			{
			case EAxis::X:
				OutOther = EAxis::Y;
				break;
			case EAxis::Y:
				OutOther = EAxis::Z;
				break;
			case EAxis::Z:
				OutOther = EAxis::X;
				break;
			default:
				break;
			}
		};
		
		const FName PropertyName = PropertyChangedEvent.GetPropertyName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FOrientOptions, Primary))
		{
			if (Options.Primary == EAxis::None)
			{
				Options.Primary = EAxis::X;
				Options.Secondary = EAxis::Y;
				return;
			}
			CheckAxis(Options.Primary, Options.Secondary);
			return;
		}
		
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FOrientOptions, Secondary))
		{
			CheckAxis(Options.Secondary, Options.Primary);
			return;
		}
	}
}

#endif

void UProjectionProperties::Initialize(TObjectPtr<UPreviewMesh> InPreviewMesh) 
{
	PreviewMesh = InPreviewMesh;
}

void UProjectionProperties::UpdatePlane(const UGizmoViewContext& InViewContext, const FVector& InOrigin)
{
	PlaneNormal = -InViewContext.GetViewDirection();
	PlaneOrigin = InOrigin;
}

bool UProjectionProperties::GetProjectionPoint(const FInputDeviceRay& InRay, FVector& OutHitPoint) const
{
	const FRay& WorldRay = InRay.WorldRay;

	if (PreviewMesh.IsValid())
	{
		if (ProjectionType == EProjectionType::OnMesh)
		{
			FHitResult Hit;
			if (PreviewMesh->FindRayIntersection(WorldRay, Hit))
			{
				OutHitPoint = WorldRay.Origin + WorldRay.Direction * Hit.Distance;
				return true;
			}
		}

		if (ProjectionType == EProjectionType::WithinMesh)
		{
			using namespace UE::Geometry;

			if (FDynamicMeshAABBTree3* MeshAABBTree = PreviewMesh->GetSpatial())
			{
				using HitResult = MeshIntersection::FHitIntersectionResult;
				TArray<HitResult> Hits;
				
				if (MeshAABBTree->FindAllHitTriangles(WorldRay, Hits))
				{
					if (Hits.Num() == 1)
					{
						OutHitPoint = WorldRay.Origin + WorldRay.Direction * Hits[0].Distance;
						return true;						
					}

					// const double AverageDistance = Algo::Accumulate(Distances, 0.0) / static_cast<double>(Distances.Num());
					// const int32 Index0 = Distances.IndexOfByPredicate([AverageDistance](double Distance) {return Distance <= AverageDistance;} );
					// const int32 Index1 = Distances.IndexOfByPredicate([AverageDistance](double Distance) {return Distance >= AverageDistance;} );

					static constexpr int32 Index0 = 0;
					static constexpr int32 Index1 = 1;

					const double d0 = Hits[Index0].Distance;
					const double d1 = Hits[Index1].Distance;
					OutHitPoint = WorldRay.Origin + WorldRay.Direction * ((d0+d1)*0.5);
					return true;
				}
			}

			FHitResult Hit;
			if (PreviewMesh->FindRayIntersection(WorldRay, Hit))
			{
				OutHitPoint = WorldRay.Origin + WorldRay.Direction * Hit.Distance;
				return true;
			}
		}
	}
	
	// if ray is parallel to plane, nothing has been hit
	if (FMath::IsNearlyZero(FVector::DotProduct(PlaneNormal, WorldRay.Direction)))
	{
		return false;
	}

	const FPlane Plane(PlaneOrigin, PlaneNormal);
	const float HitDepth = FMath::RayPlaneIntersectionParam(WorldRay.Origin, WorldRay.Direction, Plane);
	if (HitDepth < 0)
	{
		return false;
	}

	OutHitPoint = WorldRay.Origin + WorldRay.Direction * HitDepth;
	
	return true;
}

#undef LOCTEXT_NAMESPACE