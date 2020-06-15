// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelMorphologyMeshesTool.h"

#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "AssetGenerationUtil.h"

#include "CompositionOps/VoxelMorphologyMeshesOp.h"


#define LOCTEXT_NAMESPACE "UVoxelMorphologyMeshesTool"


/*
 * ToolBuilder
 */


bool UVoxelMorphologyMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= 1;
}

UInteractiveTool* UVoxelMorphologyMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVoxelMorphologyMeshesTool* NewTool = NewObject<UVoxelMorphologyMeshesTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}



/*
 * Tool
 */
UVoxelMorphologyMeshesTool::UVoxelMorphologyMeshesTool()
{
}

void UVoxelMorphologyMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UVoxelMorphologyMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}


	// initialize our properties
	MorphologyProperties = NewObject<UVoxelMorphologyMeshesToolProperties>(this);
	MorphologyProperties->RestoreProperties(this);
	AddToolPropertySource(MorphologyProperties);
	
	VoxProperties = NewObject<UVoxelProperties>(this);
	VoxProperties->RestoreProperties(this);
	AddToolPropertySource(VoxProperties);

	HandleSourcesProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreview();

	Preview->InvalidateResult();
}





void UVoxelMorphologyMeshesTool::SetupPreview()
{
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);

	OriginalDynamicMeshes.SetNum(ComponentTargets.Num());

	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		OriginalDynamicMeshes[ComponentIdx] = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTargets[ComponentIdx]->GetMesh(), *OriginalDynamicMeshes[ComponentIdx]);
	}

	// TODO: create a low quality preview result for initial display?

	Preview->ConfigureMaterials(
		ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
		{
			const FVoxelMorphologyMeshesOp* VoxOp = (const FVoxelMorphologyMeshesOp*)(Op);
			// TODO: remove this function or do something with it
		}
	);
	Preview->OnMeshUpdated.AddLambda(
		[this](const UMeshOpPreviewWithBackgroundCompute*)
		{
			GetToolManager()->PostInvalidation();
			UpdateVisualization(); // TODO: remove this fn or do something with it
		}
	);
	SetTransformGizmos();
}

void UVoxelMorphologyMeshesTool::UpdateVisualization()
{
	// TODO: remove this fn or do something with it
}

void UVoxelMorphologyMeshesTool::UpdateGizmoVisibility()
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->SetVisibility(MorphologyProperties->bShowTransformUI);
	}
}

void UVoxelMorphologyMeshesTool::SetTransformGizmos()
{
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();

	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& Target = ComponentTargets[ComponentIdx];
		UTransformProxy* Proxy = TransformProxies.Add_GetRef(NewObject<UTransformProxy>(this));
		UTransformGizmo* Gizmo = TransformGizmos.Add_GetRef(GizmoManager->Create3AxisTransformGizmo(this));
		Gizmo->SetActiveTarget(Proxy);
		FTransform InitialTransform = Target->GetWorldTransform();
		Gizmo->SetNewGizmoTransform(InitialTransform);
		Proxy->OnTransformChanged.AddUObject(this, &UVoxelMorphologyMeshesTool::TransformChanged);
	}
	UpdateGizmoVisibility();
}

void UVoxelMorphologyMeshesTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	Preview->InvalidateResult();
}

void UVoxelMorphologyMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	VoxProperties->SaveProperties(this);
	MorphologyProperties->SaveProperties(this);
	HandleSourcesProperties->SaveProperties(this);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	// Restore (unhide) the source meshes
	for (auto& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("VoxelMorphologyMeshes", "Morphology Meshes"));

		// Generate the result
		GenerateAsset(Result);

		TArray<AActor*> Actors;
		for (auto& ComponentTarget : ComponentTargets)
		{
			Actors.Add(ComponentTarget->GetOwnerActor());
		}
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());

		GetToolManager()->EndUndoTransaction();
	}

	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);
}

void UVoxelMorphologyMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UVoxelMorphologyMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelMorphologyMeshesOp> Op = MakeUnique<FVoxelMorphologyMeshesOp>();

	Op->Transforms.SetNum(ComponentTargets.Num());
	Op->Meshes.SetNum(ComponentTargets.Num());
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		Op->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		Op->Transforms[Idx] = TransformProxies[Idx]->GetTransform();
	}

	VoxProperties->SetPropertiesOnOp(*Op);
	
	Op->bSolidifyInput = MorphologyProperties->bSolidifyInput;
	Op->OffsetSolidifySurface = MorphologyProperties->OffsetSolidifySurface;
	Op->bRemoveInternalsAfterSolidify = MorphologyProperties->bRemoveInternalsAfterSolidify;
	Op->Distance = MorphologyProperties->Distance;
	Op->Operation = MorphologyProperties->Operation;

	return Op;
}



void UVoxelMorphologyMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UVoxelMorphologyMeshesTool::OnTick(float DeltaTime)
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->bSnapToWorldGrid = MorphologyProperties->bSnapToWorldGrid;
	}

	Preview->Tick(DeltaTime);
}


#if WITH_EDITOR
void UVoxelMorphologyMeshesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Preview->InvalidateResult();
	UpdateGizmoVisibility();
}
#endif

void UVoxelMorphologyMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelMorphologyMeshesToolProperties, bShowTransformUI)))
	{
		UpdateGizmoVisibility();
	}
	else if (Property && 
		(  PropertySet == HandleSourcesProperties
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelMorphologyMeshesToolProperties, bSnapToWorldGrid)
		))
	{
		// nothing
	}
	else
	{
		Preview->InvalidateResult();
	}
}


bool UVoxelMorphologyMeshesTool::HasAccept() const
{
	return true;
}

bool UVoxelMorphologyMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}


void UVoxelMorphologyMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	check(Result.Mesh.Get() != nullptr);

	FVector3d Center = Result.Mesh->GetCachedBounds().Center();
	double Rescale = Result.Transform.GetScale().X;
	FTransform3d LocalTransform(-Center * Rescale);
	LocalTransform.SetScale(FVector3d(Rescale, Rescale, Rescale));
	MeshTransforms::ApplyTransform(*Result.Mesh, LocalTransform);
	FTransform3d CenteredTransform = Result.Transform;
	CenteredTransform.SetScale(FVector3d::One());
	CenteredTransform.SetTranslation(CenteredTransform.GetTranslation() + CenteredTransform.TransformVector(Center * Rescale));
	
	TArray<UMaterialInterface*> Materials;
	Materials.Add(LoadObject<UMaterial>(nullptr, TEXT("MATERIAL")));
	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), CenteredTransform, TEXT("Morphology Mesh"), Materials);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}




#undef LOCTEXT_NAMESPACE
