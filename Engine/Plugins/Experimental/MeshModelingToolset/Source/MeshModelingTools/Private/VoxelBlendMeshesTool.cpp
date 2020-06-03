// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelBlendMeshesTool.h"
#include "CompositionOps/VoxelBlendMeshesOp.h"
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

#include "CompositionOps/VoxelBlendMeshesOp.h"


#define LOCTEXT_NAMESPACE "UVoxelBlendMeshesTool"


/*
 * ToolBuilder
 */


bool UVoxelBlendMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= 2;
}

UInteractiveTool* UVoxelBlendMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVoxelBlendMeshesTool* NewTool = NewObject<UVoxelBlendMeshesTool>(SceneState.ToolManager);

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
UVoxelBlendMeshesTool::UVoxelBlendMeshesTool()
{
}

void UVoxelBlendMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UVoxelBlendMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}


	// initialize our properties
	BlendProperties = NewObject<UVoxelBlendMeshesToolProperties>(this);
	BlendProperties->RestoreProperties(this);
	AddToolPropertySource(BlendProperties);
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





void UVoxelBlendMeshesTool::SetupPreview()
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
			const FVoxelBlendMeshesOp* VoxOp = (const FVoxelBlendMeshesOp*)(Op);
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

void UVoxelBlendMeshesTool::UpdateVisualization()
{
	// TODO: remove this fn or do something with it
}

void UVoxelBlendMeshesTool::UpdateGizmoVisibility()
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->SetVisibility(BlendProperties->bShowTransformUI);
	}
}

void UVoxelBlendMeshesTool::SetTransformGizmos()
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
		Proxy->OnTransformChanged.AddUObject(this, &UVoxelBlendMeshesTool::TransformChanged);
	}
	UpdateGizmoVisibility();
}

void UVoxelBlendMeshesTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	Preview->InvalidateResult();
}

void UVoxelBlendMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	VoxProperties->SaveProperties(this);
	BlendProperties->SaveProperties(this);
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
			GetToolManager()->BeginUndoTransaction(LOCTEXT("VoxelBlendMeshes", "Boolean Meshes"));

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

void UVoxelBlendMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UVoxelBlendMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelBlendMeshesOp> Op = MakeUnique<FVoxelBlendMeshesOp>();

	Op->Transforms.SetNum(ComponentTargets.Num());
	Op->Meshes.SetNum(ComponentTargets.Num());
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		Op->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		Op->Transforms[Idx] = TransformProxies[Idx]->GetTransform();
	}

	Op->BlendFalloff = BlendProperties->BlendFalloff;
	Op->BlendPower = BlendProperties->BlendPower;
	Op->InputVoxelCount = VoxProperties->VoxelCount;
	Op->OutputVoxelCount = VoxProperties->VoxelCount;
	Op->SimplifyMaxErrorFactor = VoxProperties->SimplifyMaxErrorFactor;
	Op->bAutoSimplify = VoxProperties->bAutoSimplify;
	Op->MinComponentVolume = VoxProperties->CubeRootMinComponentVolume * VoxProperties->CubeRootMinComponentVolume * VoxProperties->CubeRootMinComponentVolume;

	return Op;
}



void UVoxelBlendMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UVoxelBlendMeshesTool::OnTick(float DeltaTime)
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->bSnapToWorldGrid = BlendProperties->bSnapToWorldGrid;
	}

	Preview->Tick(DeltaTime);
}


#if WITH_EDITOR
void UVoxelBlendMeshesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Preview->InvalidateResult();
	UpdateGizmoVisibility();
}
#endif

void UVoxelBlendMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelBlendMeshesToolProperties, bShowTransformUI)))
	{
		UpdateGizmoVisibility();
	}
	else if (Property && 
		(  PropertySet == HandleSourcesProperties
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelBlendMeshesToolProperties, bSnapToWorldGrid)
		))
	{
		// nothing
	}
	else
	{
		Preview->InvalidateResult();
	}
}


bool UVoxelBlendMeshesTool::HasAccept() const
{
	return true;
}

bool UVoxelBlendMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}


void UVoxelBlendMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
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
	
	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), CenteredTransform, TEXT("Blend Mesh"), Preview->StandardMaterials);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}




#undef LOCTEXT_NAMESPACE
