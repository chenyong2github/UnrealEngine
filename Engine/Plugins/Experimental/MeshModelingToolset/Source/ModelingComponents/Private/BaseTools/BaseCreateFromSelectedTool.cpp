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

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "UBaseCreateFromSelectedTool"

using namespace UE::Geometry;

/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UBaseCreateFromSelectedToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UMaterialProvider::StaticClass()
		});
	return TypeRequirements;
}

bool UBaseCreateFromSelectedToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	int32 ComponentCount = SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements());
	return AssetAPI != nullptr && ComponentCount >= MinComponentsSupported() && (!MaxComponentsSupported().IsSet() || ComponentCount <= MaxComponentsSupported().GetValue());
}


UInteractiveTool* UBaseCreateFromSelectedToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBaseCreateFromSelectedTool* NewTool = MakeNewToolInstance(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
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
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetComponentInterface(ComponentIdx)->SetOwnerVisibility(false);
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
	Preview->OnMeshUpdated.AddLambda(
		[this](const UMeshOpPreviewWithBackgroundCompute* UpdatedPreview)
		{
			UpdateAcceptWarnings(UpdatedPreview->HaveEmptyResult() ? EAcceptWarning::EmptyForbidden : EAcceptWarning::NoWarning);
		}
	);

	SetTransformGizmos();

	ConvertInputsAndSetPreviewMaterials(true);

	// output name fields
	HandleSourcesProperties->OutputName = PrefixWithSourceNameIfSingleSelection(GetCreatedAssetName());
	HandleSourcesProperties->WatchProperty(HandleSourcesProperties->WriteOutputTo, [&](EBaseCreateFromSelectedTargetType NewType)
	{
		if (NewType == EBaseCreateFromSelectedTargetType::NewAsset)
		{
			HandleSourcesProperties->OutputAsset = TEXT("");
			UpdateGizmoVisibility();
		}
		else
		{
			int32 Index = (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::FirstInputAsset) ? 0 : Targets.Num() - 1;
			HandleSourcesProperties->OutputAsset = AssetGenerationUtil::GetComponentAssetBaseName(TargetComponentInterface(Index)->GetOwnerComponent(), false);

			// Reset the hidden gizmo to its initial position
			FTransform ComponentTransform = TargetComponentInterface(Index)->GetWorldTransform();
			TransformGizmos[Index]->SetNewGizmoTransform(ComponentTransform, true);
			UpdateGizmoVisibility();
		}
	});

	Preview->InvalidateResult();
}


int32 UBaseCreateFromSelectedTool::GetHiddenGizmoIndex() const
{
	if (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::NewAsset)
	{
		return -1;
	}
	else
	{
		return (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::FirstInputAsset) ? 0 : Targets.Num() - 1;
	}
}


void UBaseCreateFromSelectedTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UBaseCreateFromSelectedTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
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
	for (int32 GizmoIndex = 0; GizmoIndex < TransformGizmos.Num(); GizmoIndex++)
	{
		UTransformGizmo* Gizmo = TransformGizmos[GizmoIndex];
		Gizmo->SetVisibility(TransformProperties->bShowTransformUI && GizmoIndex != GetHiddenGizmoIndex());
	}
}



void UBaseCreateFromSelectedTool::SetTransformGizmos()
{
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();

	for (int ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		UTransformProxy* Proxy = TransformProxies.Add_GetRef(NewObject<UTransformProxy>(this));
		UTransformGizmo* Gizmo = TransformGizmos.Add_GetRef(GizmoManager->Create3AxisTransformGizmo(this));
		Proxy->SetTransform(TargetComponentInterface(ComponentIdx)->GetWorldTransform());
		Gizmo->SetActiveTarget(Proxy, GetToolManager());
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
	if (Targets.Num() == 1) // in the single-selection case, shove the result back into the original component space
	{
		FTransform3d ToSourceComponentSpace = (FTransform3d)TargetComponentInterface(0)->GetWorldTransform().Inverse();
		MeshTransforms::ApplyTransform(*Result.Mesh, ToSourceComponentSpace);
		NewTransform = (FTransform3d)TargetComponentInterface(0)->GetWorldTransform();
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



void UBaseCreateFromSelectedTool::UpdateAsset(const FDynamicMeshOpResult& Result, UToolTarget* UpdateTarget)
{
	check(Result.Mesh.Get() != nullptr);

	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(UpdateTarget);
	IMeshDescriptionCommitter* TargetMeshCommitter = Cast<IMeshDescriptionCommitter>(UpdateTarget);
	IMaterialProvider* TargetMaterial = Cast<IMaterialProvider>(UpdateTarget);

	FTransform3d TargetToWorld = (FTransform3d)TargetComponent->GetWorldTransform();

	FTransform3d ResultTransform = Result.Transform;
	MeshTransforms::ApplyTransform(*Result.Mesh, ResultTransform);
	MeshTransforms::ApplyTransformInverse(*Result.Mesh, TargetToWorld);

	TargetMeshCommitter->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescriptionOut);
	});

	FComponentMaterialSet MaterialSet;
	MaterialSet.Materials = GetOutputMaterials();
	TargetMaterial->CommitMaterialSetUpdate(MaterialSet, true);
}


FString UBaseCreateFromSelectedTool::PrefixWithSourceNameIfSingleSelection(const FString& AssetName) const
{
	if (Targets.Num() == 1)
	{
		FString CurName = AssetGenerationUtil::GetComponentAssetBaseName(TargetComponentInterface(0)->GetOwnerComponent());
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
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetComponentInterface(ComponentIdx)->SetOwnerVisibility(true);
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
			int32 TargetIndex = (HandleSourcesProperties->WriteOutputTo == EBaseCreateFromSelectedTargetType::FirstInputAsset) ? 0 : (Targets.Num() - 1);
			KeepActor = TargetComponentInterface(TargetIndex)->GetOwnerActor();

			UpdateAsset(Result, Targets[TargetIndex]);
		}

		TArray<AActor*> Actors;
		for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
			AActor* Actor = TargetComponentInterface(ComponentIdx)->GetOwnerActor();
			if (Actor != KeepActor)
			{
				Actors.Add(Actor);
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
	return Super::CanAccept() && Preview->HaveValidNonEmptyResult();
}




#undef LOCTEXT_NAMESPACE
