// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTools/BaseCreateFromSelectedTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "SimpleDynamicMeshComponent.h"
#include "Async/Async.h"

#include "MeshNormals.h"
#include "MeshTransforms.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"



#define LOCTEXT_NAMESPACE "UBaseCreateFromSelectedTool"


/*
 * ToolBuilder
 */


bool UBaseCreateFromSelectedToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	int32 ComponentCount = ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget);
	return AssetAPI != nullptr && ComponentCount >= MinComponentsSupported() && (!MaxComponentsSupported().IsSet() || ComponentCount <= MaxComponentsSupported().GetValue());
}


UInteractiveTool* UBaseCreateFromSelectedToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBaseCreateFromSelectedTool* NewTool = MakeNewToolInstance(SceneState.ToolManager);

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




/*
 * Tool
 */

void UBaseCreateFromSelectedTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	// initialize our properties

	SetupProperties();
	TransformProperties = NewObject<UTransformInputsToolProperties>(this);
	TransformProperties->RestoreProperties(this);
	AddToolPropertySource(TransformProperties);
	
	HandleSourcesProperties = NewObject<UBaseCreateFromSelectedHandleSourceProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);

	SetPreviewCallbacks();

	SetTransformGizmos();

	ConvertInputsAndSetPreviewMaterials(true);

	// output name fields
	HandleSourcesProperties->OutputName = PrefixWithSourceNameIfSingleSelection(GetCreatedAssetName());
	HandleSourcesProperties->WatchProperty(HandleSourcesProperties->WriteOutputTo, [&](EBaseCreateFromSelectedTargetType NewType)
	{
		if (NewType == EBaseCreateFromSelectedTargetType::NewAsset)
		{
			HandleSourcesProperties->OutputAsset = TEXT("");
		}
		else
		{
			int32 Index = (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::FirstInputAsset) ? 0 : ComponentTargets.Num() - 1;
			HandleSourcesProperties->OutputAsset = AssetGenerationUtil::GetComponentAssetBaseName(ComponentTargets[Index]->GetOwnerComponent(), false);
		}
	});

	Preview->InvalidateResult();
}


void UBaseCreateFromSelectedTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UBaseCreateFromSelectedTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void UBaseCreateFromSelectedTool::OnTick(float DeltaTime)
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->bSnapToWorldGrid = TransformProperties->bSnapToWorldGrid;
	}

	Preview->Tick(DeltaTime);
}



void UBaseCreateFromSelectedTool::UpdateGizmoVisibility()
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->SetVisibility(TransformProperties->bShowTransformUI);
	}
}


void UBaseCreateFromSelectedTool::SetTransformGizmos()
{
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();

	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& Target = ComponentTargets[ComponentIdx];
		UTransformProxy* Proxy = TransformProxies.Add_GetRef(NewObject<UTransformProxy>(this));
		UTransformGizmo* Gizmo = TransformGizmos.Add_GetRef(GizmoManager->Create3AxisTransformGizmo(this));
		Gizmo->SetActiveTarget(Proxy, GetToolManager());
		FTransform InitialTransform = Target->GetWorldTransform();
		TransformInitialScales.Add(InitialTransform.GetScale3D());
		InitialTransform.SetScale3D(FVector::OneVector);
		Gizmo->ReinitializeGizmoTransform(InitialTransform);
		Proxy->OnTransformChanged.AddUObject(this, &UBaseCreateFromSelectedTool::TransformChanged);
	}
	UpdateGizmoVisibility();
}


void UBaseCreateFromSelectedTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	Preview->InvalidateResult();
}


FText UBaseCreateFromSelectedTool::GetActionName() const
{
	return LOCTEXT("BaseCreateFromSelectedTool", "Generated Mesh");
}


TArray<UMaterialInterface*> UBaseCreateFromSelectedTool::GetOutputMaterials() const
{
	return Preview->StandardMaterials;
}


void UBaseCreateFromSelectedTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	check(Result.Mesh.Get() != nullptr);

	FTransform3d NewTransform;
	if (ComponentTargets.Num() == 1) // in the single-selection case, shove the result back into the original component space
	{
		FTransform3d ToSourceComponentSpace = (FTransform3d)ComponentTargets[0]->GetWorldTransform().Inverse();
		MeshTransforms::ApplyTransform(*Result.Mesh, ToSourceComponentSpace);
		NewTransform = (FTransform3d)ComponentTargets[0]->GetWorldTransform();
	}
	else // in the multi-selection case, center the pivot for the combined result
	{
		FVector3d Center = Result.Mesh->GetCachedBounds().Center();
		double Rescale = Result.Transform.GetScale().X;
		FTransform3d LocalTransform(-Center * Rescale);
		LocalTransform.SetScale(FVector3d(Rescale, Rescale, Rescale));
		MeshTransforms::ApplyTransform(*Result.Mesh, LocalTransform);
		NewTransform = Result.Transform;
		NewTransform.SetScale(FVector3d::One());
		NewTransform.SetTranslation(NewTransform.GetTranslation() + NewTransform.TransformVector(Center * Rescale));
	}

	// max len explicitly enforced here, would ideally notify user
	FString UseBaseName = HandleSourcesProperties->OutputName.Left(250);
	if (UseBaseName.IsEmpty())
	{
		UseBaseName = PrefixWithSourceNameIfSingleSelection(GetCreatedAssetName());
	}

	TArray<UMaterialInterface*> Materials = GetOutputMaterials();
	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), NewTransform, UseBaseName, Materials);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}



void UBaseCreateFromSelectedTool::UpdateAsset(const FDynamicMeshOpResult& Result, TUniquePtr<FPrimitiveComponentTarget>& UpdateTarget)
{
	check(Result.Mesh.Get() != nullptr);

	FTransform3d TargetToWorld = (FTransform3d)UpdateTarget->GetWorldTransform();
	FTransform3d WorldToTarget = TargetToWorld.Inverse();

	FTransform3d NewTransform;
	if (ComponentTargets.Num() == 1) // in the single-selection case, shove the result back into the original component space
	{
		MeshTransforms::ApplyTransform(*Result.Mesh, WorldToTarget);
		UpdateTarget->CommitMesh([&](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescription);
		});
	}
	else // in the multi-selection case, center the pivot for the combined result
	{
		FTransform3d ResultTransform = Result.Transform;
		MeshTransforms::ApplyTransform(*Result.Mesh, ResultTransform);
		MeshTransforms::ApplyTransform(*Result.Mesh, WorldToTarget);

		UpdateTarget->CommitMesh([&](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescription);
		});
	}

	FComponentMaterialSet MaterialSet;
	MaterialSet.Materials = GetOutputMaterials();
	UpdateTarget->CommitMaterialSetUpdate(MaterialSet, true);

}


FString UBaseCreateFromSelectedTool::PrefixWithSourceNameIfSingleSelection(const FString& AssetName) const
{
	if (ComponentTargets.Num() == 1)
	{
		FString CurName = AssetGenerationUtil::GetComponentAssetBaseName(ComponentTargets[0]->GetOwnerComponent());
		return FString::Printf(TEXT("%s_%s"), *CurName, *AssetName);
	}
	else
	{
		return AssetName;
	}
}


void UBaseCreateFromSelectedTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformInputsToolProperties, bShowTransformUI)))
	{
		UpdateGizmoVisibility();
	}
	else if (Property && 
		(  PropertySet == HandleSourcesProperties
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformInputsToolProperties, bSnapToWorldGrid)
		))
	{
		// nothing
	}
	else
	{
		Preview->InvalidateResult();
	}
}


void UBaseCreateFromSelectedTool::Shutdown(EToolShutdownType ShutdownType)
{
	SaveProperties();
	HandleSourcesProperties->SaveProperties(this);
	TransformProperties->SaveProperties(this);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	// Restore (unhide) the source meshes
	for (auto& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(GetActionName());

		// Generate the result
		AActor* KeepActor = nullptr;
		if (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::NewAsset)
		{
			GenerateAsset(Result);
		}
		else
		{
			int32 TargetIndex = (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::FirstInputAsset) ? 0 : (ComponentTargets.Num() - 1);
			TUniquePtr<FPrimitiveComponentTarget>& UpdateTarget = ComponentTargets[TargetIndex];
			KeepActor = UpdateTarget->GetOwnerActor();

			UpdateAsset(Result, ComponentTargets[TargetIndex]);
		}

		TArray<AActor*> Actors;
		for (auto& ComponentTarget : ComponentTargets)
		{
			AActor* Actor = ComponentTarget->GetOwnerActor();
			if (Actor != KeepActor)
			{
				Actors.Add(ComponentTarget->GetOwnerActor());
			}
		}
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());

		if (KeepActor != nullptr)
		{
			// select the actor we kept
			ToolSelectionUtil::SetNewActorSelection(GetToolManager(), KeepActor);
		}

		GetToolManager()->EndUndoTransaction();
	}

	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);
}


bool UBaseCreateFromSelectedTool::CanAccept() const
{
	return Super::CanAccept() && Preview->HaveValidResult();
}




#undef LOCTEXT_NAMESPACE
