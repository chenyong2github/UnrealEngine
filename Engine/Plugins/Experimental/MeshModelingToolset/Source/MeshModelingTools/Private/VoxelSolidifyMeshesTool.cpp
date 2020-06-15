// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelSolidifyMeshesTool.h"
#include "CompositionOps/VoxelSolidifyMeshesOp.h"
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

#include "CompositionOps/VoxelSolidifyMeshesOp.h"


#define LOCTEXT_NAMESPACE "UVoxelSolidifyMeshesTool"


/*
 * ToolBuilder
 */


bool UVoxelSolidifyMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= 1;
}

UInteractiveTool* UVoxelSolidifyMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVoxelSolidifyMeshesTool* NewTool = NewObject<UVoxelSolidifyMeshesTool>(SceneState.ToolManager);

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
UVoxelSolidifyMeshesTool::UVoxelSolidifyMeshesTool()
{
}

void UVoxelSolidifyMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UVoxelSolidifyMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}


	// initialize our properties
	SolidifyProperties = NewObject<UVoxelSolidifyMeshesToolProperties>(this);
	SolidifyProperties->RestoreProperties(this);
	AddToolPropertySource(SolidifyProperties);
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





void UVoxelSolidifyMeshesTool::SetupPreview()
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
			const FVoxelSolidifyMeshesOp* VoxOp = (const FVoxelSolidifyMeshesOp*)(Op);
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

void UVoxelSolidifyMeshesTool::UpdateVisualization()
{
	// TODO: remove this fn or do something with it
}

void UVoxelSolidifyMeshesTool::UpdateGizmoVisibility()
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->SetVisibility(SolidifyProperties->bShowTransformUI);
	}
}

void UVoxelSolidifyMeshesTool::SetTransformGizmos()
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
		Proxy->OnTransformChanged.AddUObject(this, &UVoxelSolidifyMeshesTool::TransformChanged);
	}
	UpdateGizmoVisibility();
}

void UVoxelSolidifyMeshesTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	Preview->InvalidateResult();
}

void UVoxelSolidifyMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	VoxProperties->SaveProperties(this);
	SolidifyProperties->SaveProperties(this);
	HandleSourcesProperties->SaveProperties(this);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	// Restore (unhide) the source meshes
	for (auto& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		// Generate the result
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("VoxelSolidifyMeshes", "Boolean Meshes"));

			GenerateAsset(Result);

			GetToolManager()->EndUndoTransaction();
		}

		TArray<AActor*> Actors;
		for (auto& ComponentTarget : ComponentTargets)
		{
			Actors.Add(ComponentTarget->GetOwnerActor());
		}
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());
	}

	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);
}

void UVoxelSolidifyMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UVoxelSolidifyMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelSolidifyMeshesOp> Op = MakeUnique<FVoxelSolidifyMeshesOp>();

	Op->Transforms.SetNum(ComponentTargets.Num());
	Op->Meshes.SetNum(ComponentTargets.Num());
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		Op->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		Op->Transforms[Idx] = TransformProxies[Idx]->GetTransform();
	}

	Op->bSolidAtBoundaries = SolidifyProperties->bSolidAtBoundaries;
	Op->WindingThreshold = SolidifyProperties->WindingThreshold;
	Op->bMakeOffsetSurfaces = SolidifyProperties->bMakeOffsetSurfaces;
	Op->OffsetThickness = SolidifyProperties->OffsetThickness;
	Op->SurfaceSearchSteps = SolidifyProperties->SurfaceSearchSteps;
	Op->ExtendBounds = SolidifyProperties->ExtendBounds;

	VoxProperties->SetPropertiesOnOp(*Op);
	
	return Op;
}



void UVoxelSolidifyMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UVoxelSolidifyMeshesTool::OnTick(float DeltaTime)
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->bSnapToWorldGrid = SolidifyProperties->bSnapToWorldGrid;
	}

	Preview->Tick(DeltaTime);
}


#if WITH_EDITOR
void UVoxelSolidifyMeshesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Preview->InvalidateResult();
	UpdateGizmoVisibility();
}
#endif

void UVoxelSolidifyMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelSolidifyMeshesToolProperties, bShowTransformUI)))
	{
		UpdateGizmoVisibility();
	}
	else if (Property && 
		(  PropertySet == HandleSourcesProperties
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelSolidifyMeshesToolProperties, bSnapToWorldGrid)
		))
	{
		// nothing
	}
	else
	{
		Preview->InvalidateResult();
	}
}


bool UVoxelSolidifyMeshesTool::HasAccept() const
{
	return true;
}

bool UVoxelSolidifyMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}


void UVoxelSolidifyMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
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
		Result.Mesh.Get(), CenteredTransform, TEXT("Solidify Mesh"), Materials);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}




#undef LOCTEXT_NAMESPACE
