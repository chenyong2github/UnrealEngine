// Copyright Epic Games, Inc. All Rights Reserved.

#include "MirrorTool.h"

#include "AssetGenerationUtil.h"
#include "BaseBehaviors/KeyAsModifierInputBehavior.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "CompositionOps/MirrorOp.h"
#include "Drawing/MeshDebugDrawing.h"
#include "DynamicMeshToMeshDescription.h"
#include "InteractiveToolManager.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Misc/MessageDialog.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UMirrorTool"

// Local function forward declarations
void CheckAndDisplayWarnings(const TArray<TUniquePtr<FPrimitiveComponentTarget>>& ComponentTargets, UInteractiveToolManager& ToolsManager);


//Tool builder functions

bool UMirrorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UMirrorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMirrorTool* NewTool = NewObject<UMirrorTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if (MeshComponent)
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}


// Operator factory

TUniquePtr<FDynamicMeshOperator> UMirrorOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FMirrorOp> MirrorOp = MakeUnique<FMirrorOp>();

	// Set up inputs and settings
	MirrorOp->OriginalMesh = MirrorTool->MeshesToMirror[ComponentIndex]->GetMesh();
	MirrorOp->bAppendToOriginal = MirrorTool->Settings->OperationMode == EMirrorOperationMode::MirrorAndAppend;
	MirrorOp->bCropFirst = MirrorTool->Settings->bCropAlongMirrorPlaneFirst;
	MirrorOp->bWeldAlongPlane = MirrorTool->Settings->bWeldVerticesOnMirrorPlane;
	MirrorOp->bAllowBowtieVertexCreation = MirrorTool->Settings->bAllowBowtieVertexCreation;

	FTransform LocalToWorld = MirrorTool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	MirrorOp->SetTransform(LocalToWorld);

	// We also need WorldToLocal. Threshold the LocalToWorld scaling transform so we can get the inverse.
	FVector LocalToWorldScale = LocalToWorld.GetScale3D();
	for (int i = 0; i < 3; i++)
	{
		float DimScale = FMathf::Abs(LocalToWorldScale[i]);
		float Tolerance = KINDA_SMALL_NUMBER;
		if (DimScale < Tolerance)
		{
			LocalToWorldScale[i] = Tolerance * FMathf::SignNonZero(LocalToWorldScale[i]);
		}
	}
	LocalToWorld.SetScale3D(LocalToWorldScale);
	FTransform3d WorldToLocal = FTransform3d(LocalToWorld).Inverse();

	// Now we can get the plane parameters in local space.
	MirrorOp->LocalPlaneOrigin = WorldToLocal.TransformPosition(MirrorTool->MirrorPlaneOrigin);;

	FVector3d WorldNormal = MirrorTool->MirrorPlaneNormal;
	MirrorOp->LocalPlaneNormal = WorldToLocal.TransformNormal(MirrorTool->MirrorPlaneNormal);

	return MirrorOp;
}


// Tool property functions

void UMirrorToolActionPropertySet::PostAction(EMirrorToolAction Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


// Tool itself
UMirrorTool::UMirrorTool()
{
}

bool UMirrorTool::CanAccept() const
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		if (!Preview->HaveValidResult())
		{
			return false;
		}
	}
	return Super::CanAccept();
}

void UMirrorTool::SetWorld(UWorld* World)
{
	TargetWorld = World;
}

void UMirrorTool::SetAssetAPI(IToolsContextAssetAPI* NewAssetApi)
{
	AssetAPI = NewAssetApi;
}

void UMirrorTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	// Editing the "show preview" option changes whether we need to be displaying the preview or the original mesh.
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMirrorToolProperties, bShowPreview)))
	{
		for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
		{
			ComponentTarget->SetOwnerVisibility(!Settings->bShowPreview);
		}
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->SetVisibility(Settings->bShowPreview);
		}
	}

	// Regardless of what changed, update the previews.
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}

void UMirrorTool::OnTick(float DeltaTime)
{
	// Deal with any buttons that may have been clicked
	if (PendingAction != EMirrorToolAction::NoAction)
	{
		ApplyAction(PendingAction);
		PendingAction = EMirrorToolAction::NoAction;
	}

	if (PlaneMechanic != nullptr)
	{
		// Update snapping behavior based on modifier key.
		PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid ^ bSnappingToggle);

		PlaneMechanic->Tick(DeltaTime);
	}
	
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}

void UMirrorTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	// Have the plane draw itself.
	PlaneMechanic->Render(RenderAPI);
}

void UMirrorTool::Setup()
{
	UInteractiveTool::Setup();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartMirrorTool", "Mirror one or more meshes across a plane. Grid snapping behavior is swapped while the shift key is down. The plane can be set by using the preset buttons, moving the gizmo, or ctrl+clicking on a spot on the original mesh."),
		EToolMessageLevel::UserNotification);

	// Set up the properties
	Settings = NewObject<UMirrorToolProperties>(this, TEXT("Mirror Tool Settings"));
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	ToolActions = NewObject<UMirrorToolActionPropertySet>(this);
	ToolActions->Initialize(this);
	AddToolPropertySource(ToolActions);

	CheckAndDisplayWarnings(ComponentTargets, *GetToolManager());

	// Fill in the MeshesToMirror array with suitably converted meshes.
	for (int i = 0; i < ComponentTargets.Num(); i++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[i];

		// Convert into dynamic mesh
		TSharedPtr<FDynamicMesh3> DynamicMesh = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTarget->GetMesh(), *DynamicMesh);

		// Wrap the dynamic mesh in a replacement change target
		UDynamicMeshReplacementChangeTarget* WrappedTarget = MeshesToMirror.Add_GetRef(NewObject<UDynamicMeshReplacementChangeTarget>());

		// Set callbacks so previews are invalidated on undo/redo changing the meshes
		WrappedTarget->SetMesh(DynamicMesh);
		WrappedTarget->OnMeshChanged.AddLambda([this, i]() { Previews[i]->InvalidateResult(); });
	}

	// Set the visibility of the StaticMeshComponents depending on whether we are showing them or the preview.
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(!Settings->bShowPreview);
	}

	// Initialize the PreviewMesh and BackgroundCompute objects
	SetupPreviews();

	// Update the bounding box of the meshes.
	CombinedBounds.Init();
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		FVector ComponentOrigin, ComponentExtents;
		ComponentTarget->GetOwnerActor()->GetActorBounds(false, ComponentOrigin, ComponentExtents);
		CombinedBounds += FBox::BuildAABB(ComponentOrigin, ComponentExtents);
	}

	// Set the initial mirror plane. We want the plane to start in the middle if we're doing a simple
	// mirror (i.e., not appending, and not cropping). Otherwise, we want the plane to start to one side.
	MirrorPlaneOrigin = CombinedBounds.GetCenter();
	MirrorPlaneNormal = FVector3d(0, -1, 0);
	if (Settings->OperationMode == EMirrorOperationMode::MirrorAndAppend || Settings->bCropAlongMirrorPlaneFirst)
	{
		MirrorPlaneOrigin.Y = CombinedBounds.Min.Y;
	}

	// Set up the mirror plane mechanic, which manages the gizmo
	PlaneMechanic = NewObject<UConstructionPlaneMechanic>(this);
	PlaneMechanic->Setup(this);
	PlaneMechanic->Initialize(TargetWorld, FFrame3d(MirrorPlaneOrigin, MirrorPlaneNormal));

	// Have the plane mechanic update things properly
	PlaneMechanic->OnPlaneChanged.AddLambda([this]() {
		MirrorPlaneNormal = PlaneMechanic->Plane.Rotation.AxisZ();
		MirrorPlaneOrigin = PlaneMechanic->Plane.Origin;
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->InvalidateResult();
		}
		});

	// Modify the Ctrl+click set plane behavior to respond to our CtrlClickBehavior property
	PlaneMechanic->SetPlaneCtrlClickBehaviorTarget->OnClickedPositionFunc = [this](const FHitResult& Hit)
	{
		bool bIgnoreNormal = (Settings->CtrlClickBehavior == EMirrorCtrlClickBehavior::Reposition);
		PlaneMechanic->SetDrawPlaneFromWorldPos(Hit.ImpactPoint, Hit.ImpactNormal, bIgnoreNormal);
	};
	// Also include the original components in the ctrl+click hit testing even though we made them 
	// invisible, since we want to be able to reposition the plane onto the original mesh.
	for (const TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		PlaneMechanic->SetPlaneCtrlClickBehaviorTarget->InvisibleComponentsToHitTest.Add(Target->GetOwnerComponent());
	}


	// Add modifier button for snapping
	UKeyAsModifierInputBehavior* SnapToggleBehavior = NewObject<UKeyAsModifierInputBehavior>();
	SnapToggleBehavior->ModifierCheckFunc = FInputDeviceState::IsShiftKeyDown;
	SnapToggleBehavior->Initialize(this, SnappingToggleModifierId, EKeys::AnyKey);
	AddInputBehavior(SnapToggleBehavior);

	// Start the preview calculations
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}

void UMirrorTool::SetupPreviews()
{
	// Create a preview (with an op) for each selected component.
	int32 NumMeshes = MeshesToMirror.Num();
	for (int32 PreviewIndex = 0; PreviewIndex < NumMeshes; ++PreviewIndex)
	{
		UMirrorOperatorFactory* MirrorOpCreator = NewObject<UMirrorOperatorFactory>();
		MirrorOpCreator->MirrorTool = this;
		MirrorOpCreator->ComponentIndex = PreviewIndex;

		UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(
			NewObject<UMeshOpPreviewWithBackgroundCompute>(MirrorOpCreator, "Preview"));
		Preview->Setup(TargetWorld, MirrorOpCreator);
		Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

		FComponentMaterialSet MaterialSet;
		ComponentTargets[PreviewIndex]->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));

		// Set initial preview to unprocessed mesh, so that things don't disappear initially
		Preview->PreviewMesh->UpdatePreview(MeshesToMirror[PreviewIndex]->GetMesh().Get());
		Preview->PreviewMesh->SetTransform(ComponentTargets[PreviewIndex]->GetWorldTransform());
		Preview->SetVisibility(Settings->bShowPreview);
	}
}

void CheckAndDisplayWarnings(const TArray<TUniquePtr<FPrimitiveComponentTarget>>& ComponentTargets, 
	UInteractiveToolManager& ToolsManager)
{
	// We can have more than one warning, which makes this a bit more work.
	FText SameSourceWarning;
	FText NonUniformScaleWarning;

	// See if any of the selected components have the same source.
	bool bAnyHaveSameSource = false;
	for (int32 FirstComponentIndex = 0; !bAnyHaveSameSource && FirstComponentIndex < ComponentTargets.Num(); FirstComponentIndex++)
	{
		FPrimitiveComponentTarget* ComponentTarget = ComponentTargets[FirstComponentIndex].Get();
		for (int32 SecondComponentIndex = FirstComponentIndex + 1; SecondComponentIndex < ComponentTargets.Num(); SecondComponentIndex++)
		{
			if (ComponentTarget->HasSameSourceData(*ComponentTargets[SecondComponentIndex]))
			{
				bAnyHaveSameSource = true;
				break;
			}
		}
	}

	if (bAnyHaveSameSource)
	{
		SameSourceWarning = LOCTEXT("MirrorMultipleAssetsWithSameSource", "WARNING: Multiple meshes in your selection use the same source asset! Only the \"Create New Assets\" save mode is supported.");
		
		// We could forcefully set the save mode to CreateNewAssets, but the setting will persist on new invocations
		// of the tool, which may surprise the user. So, it's up to them to set it.
	}

	// See if any of the selected components have a nonuniform scaling transform.
	FPrimitiveComponentTarget* NonUniformScalingTarget = nullptr;
	for (int32 i = 0; i < ComponentTargets.Num(); ++i)
	{
		FVector Scaling = ComponentTargets[i]->GetWorldTransform().GetScale3D();
		if (Scaling.X != Scaling.Y || Scaling.Y != Scaling.Z)
		{
			NonUniformScalingTarget = ComponentTargets[i].Get();
			break;
		}
	}

	if (NonUniformScalingTarget)
	{
		NonUniformScaleWarning = FText::Format(
			LOCTEXT("MirrorNonUniformScaledAsset", "WARNING: The item \"{0}\" has a non-uniform scaling transform. This is not supported because mirroring acts on the underlying mesh, and mirroring is not commutative with non-uniform scaling. Consider deforming the mesh rather than scaling it non-uniformly."),
			FText::FromString(NonUniformScalingTarget->GetOwnerActor()->GetName()));
	}

	if (bAnyHaveSameSource && NonUniformScalingTarget)
	{
		// Concatenates the two warnings with an extra line in between.
		ToolsManager.DisplayMessage(FText::Format(LOCTEXT("CombinedWarnings", "{0}\n\n{1}"),
			SameSourceWarning, NonUniformScaleWarning), EToolMessageLevel::UserWarning);
	}
	else if (bAnyHaveSameSource)
	{
		ToolsManager.DisplayMessage(SameSourceWarning, EToolMessageLevel::UserWarning);
	}
	else if (NonUniformScalingTarget)
	{
		ToolsManager.DisplayMessage(NonUniformScaleWarning, EToolMessageLevel::UserWarning);
	}
}

void UMirrorTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);

	PlaneMechanic->Shutdown();

	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	// Swap in results, if appropriate
	if (ShutdownType == EToolShutdownType::Accept)
	{
		// Gather results
		TArray<FDynamicMeshOpResult> Results;
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Results.Emplace(Preview->Shutdown());
		}

		// Convert to output. This will also edit the selection.
		GenerateAsset(Results);
	}
	else
	{
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->Cancel();
		}
	}
}

void UMirrorTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	if (Results.Num() == 0)
	{
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("MirrorToolTransactionName", "Mirror Tool"));

	ensure(Results.Num() > 0);

	int32 NumSourceMeshes = MeshesToMirror.Num();

	// check if we entirely cut away any meshes
	bool bWantToDestroy = false;
	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		if (Results[OrigMeshIdx].Mesh->TriangleCount() == 0)
		{
			bWantToDestroy = true;
			break;
		}
	}
	// if so ask user what to do
	if (bWantToDestroy)
	{
		FText Title = LOCTEXT("MirrorDestroyTitle", "Delete mesh components?");
		EAppReturnType::Type Ret = FMessageDialog::Open(EAppMsgType::YesNo,
			LOCTEXT("PlaneCutDestroyQuestion", "The mirror plane cropping has entirely cut away at least one mesh.  Actually destroy these mesh components?"), &Title);
		if (Ret == EAppReturnType::No)
		{
			bWantToDestroy = false;
		}
	}

	// Properly deal with each result, setting up the selection at the same time.
	FSelectedOjectsChangeList NewSelection;
	NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		FDynamicMesh3* Mesh = Results[OrigMeshIdx].Mesh.Get();
		check(Mesh != nullptr);

		if (Mesh->TriangleCount() == 0)
		{
			if (bWantToDestroy)
			{
				ComponentTargets[OrigMeshIdx]->GetOwnerComponent()->DestroyComponent();
			}
			continue;
		}
		else if (Settings->SaveMode == EMirrorSaveMode::UpdateAssets)
		{
			NewSelection.Actors.Add(ComponentTargets[OrigMeshIdx]->GetOwnerActor());

			ComponentTargets[OrigMeshIdx]->CommitMesh([&Mesh](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
				{
					FDynamicMeshToMeshDescription Converter;
					Converter.Convert(Mesh, *CommitParams.MeshDescription);
				});
		}
		else
		{
			// Build array of materials from the original.
			TArray<UMaterialInterface*> Materials;
			TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[OrigMeshIdx];
			for (int MaterialIdx = 0, NumMaterials = ComponentTarget->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
			{
				Materials.Add(ComponentTarget->GetMaterial(MaterialIdx));
			}

			// Create the new actor
			AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
				AssetAPI, TargetWorld, Mesh, Results[OrigMeshIdx].Transform, TEXT("MirrorResult"), Materials);
			if (NewActor != nullptr)
			{
				NewSelection.Actors.Add(NewActor);
			}

			// Remove the original actor
			ComponentTarget->GetOwnerComponent()->DestroyComponent();
		}
	}

	// Update the selection
	if (NewSelection.Actors.Num() > 0)
	{
		GetToolManager()->RequestSelectionChange(NewSelection);
	}

	GetToolManager()->EndUndoTransaction();
}


// Action support

void UMirrorTool::RequestAction(EMirrorToolAction ActionType)
{
	if (PendingAction == EMirrorToolAction::NoAction)
	{
		PendingAction = ActionType;
	}
}

void UMirrorTool::ApplyAction(EMirrorToolAction ActionType)
{
	FVector3d ShiftedPlaneOrigin = CombinedBounds.GetCenter();

	if (ActionType == EMirrorToolAction::ShiftToCenter)
	{
		// We keep the same orientation here
		PlaneMechanic->SetDrawPlaneFromWorldPos(ShiftedPlaneOrigin, FVector3d(), true);
	}
	else
	{
		// We still start from the center, but adjust one of the coordinates and set direction.
		FVector3d DirectionVector;
		switch (ActionType)
		{
		case EMirrorToolAction::Left:
			ShiftedPlaneOrigin.Y = CombinedBounds.Min.Y;
			DirectionVector = FVector(0, -1, 0);
			break;
		case EMirrorToolAction::Right:
			ShiftedPlaneOrigin.Y = CombinedBounds.Max.Y;
			DirectionVector = FVector(0, 1, 0);
			break;
		case EMirrorToolAction::Up:
			ShiftedPlaneOrigin.Z = CombinedBounds.Max.Z;
			DirectionVector = FVector(0, 0, 1);
			break;
		case EMirrorToolAction::Down:
			ShiftedPlaneOrigin.Z = CombinedBounds.Min.Z;
			DirectionVector = FVector(0, 0, -1);
			break;
		case EMirrorToolAction::Forward:
			ShiftedPlaneOrigin.X = CombinedBounds.Max.X;
			DirectionVector = FVector(1, 0, 0);
			break;
		case EMirrorToolAction::Backward:
			ShiftedPlaneOrigin.X = CombinedBounds.Min.X;
			DirectionVector = FVector(-1, 0, 0);
			break;
		}

		// The user can optionally have the button change the direction only
		if (Settings->bButtonsOnlyChangeOrientation)
		{
			ShiftedPlaneOrigin = MirrorPlaneOrigin;	// Keeps the same
		}
		PlaneMechanic->SetDrawPlaneFromWorldPos(ShiftedPlaneOrigin, DirectionVector, false);
	}
}

void UMirrorTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == SnappingToggleModifierId)
	{
		bSnappingToggle = bIsOn;
	}
}

#undef LOCTEXT_NAMESPACE
