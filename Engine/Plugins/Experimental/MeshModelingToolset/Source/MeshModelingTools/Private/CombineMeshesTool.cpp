// Copyright Epic Games, Inc. All Rights Reserved.

#include "CombineMeshesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"
#include "Physics/ComponentCollisionUtil.h"
#include "ShapeApproximation/SimpleShapeSet3.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif


#define LOCTEXT_NAMESPACE "UCombineMeshesTool"


/*
 * ToolBuilder
 */


const FToolTargetTypeRequirements& UCombineMeshesToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UMaterialProvider::StaticClass()
		});
	return TypeRequirements;
}

bool UCombineMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (bIsDuplicateTool) ?
		  (AssetAPI != nullptr && SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1)
		: (AssetAPI != nullptr && SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) > 1);
}

UInteractiveTool* UCombineMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UCombineMeshesTool* NewTool = NewObject<UCombineMeshesTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	NewTool->SetDuplicateMode(bIsDuplicateTool);

	return NewTool;
}




/*
 * Tool
 */


void UCombineMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UCombineMeshesTool::SetDuplicateMode(bool bDuplicateModeIn)
{
	this->bDuplicateMode = bDuplicateModeIn;
}

void UCombineMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<UCombineMeshesToolProperties>(this);
	AddToolPropertySource(BasicProperties);
	BasicProperties->RestoreProperties(this);
	BasicProperties->bIsDuplicateMode = this->bDuplicateMode;
	BasicProperties->WatchProperty(BasicProperties->WriteOutputTo, [&](ECombineTargetType NewType)
	{
		if (NewType == ECombineTargetType::NewAsset)
		{
			BasicProperties->OutputAsset = TEXT("");
		}
		else
		{
			int32 Index = (BasicProperties->WriteOutputTo == ECombineTargetType::FirstInputAsset) ? 0 : Targets.Num() - 1;
			BasicProperties->OutputAsset = AssetGenerationUtil::GetComponentAssetBaseName(TargetComponentInterface(Index)->GetOwnerComponent(), false);
		}
	});

	if (bDuplicateMode)
	{
		SetToolDisplayName(LOCTEXT("DuplicateMeshesToolName", "Duplicate"));
		BasicProperties->OutputName = AssetGenerationUtil::GetComponentAssetBaseName(TargetComponentInterface(0)->GetOwnerComponent());
	}
	else
	{
		SetToolDisplayName(LOCTEXT("CombineMeshesToolName", "Append"));
		BasicProperties->OutputName = FString("Combined");
	}


	HandleSourceProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	AddToolPropertySource(HandleSourceProperties);
	HandleSourceProperties->RestoreProperties(this);


	if (bDuplicateMode)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartToolDuplicate", "This Tool duplicates input Asset into a new Asset, and optionally replaces the input Actor with a new Actor containing the new Asset."),
			EToolMessageLevel::UserNotification);
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartToolCombine", "This Tool appends the meshes from the input Assets into a new Asset, and optionally replaces the source Actors with a new Actor containing the new Asset."),
			EToolMessageLevel::UserNotification);
	}
}



void UCombineMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);
	HandleSourceProperties->SaveProperties(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		if (bDuplicateMode || BasicProperties->WriteOutputTo == ECombineTargetType::NewAsset)
		{
			CreateNewAsset();
		}
		else
		{
			UpdateExistingAsset();
		}
	}
}

void UCombineMeshesTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UCombineMeshesTool::CreateNewAsset()
{
	// Make sure mesh descriptions are deserialized before we open transaction.
	// This is to avoid potential stability issues related to creation/load of
	// mesh descriptions inside a transaction.
	TArray<const FMeshDescription*> MeshDescriptions;
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		MeshDescriptions.Add(TargetMeshProviderInterface(ComponentIdx)->GetMeshDescription());
	}

	GetToolManager()->BeginUndoTransaction(
		bDuplicateMode ? 
		LOCTEXT("DuplicateMeshToolTransactionName", "Duplicate Mesh") :
		LOCTEXT("CombineMeshesToolTransactionName", "Combine Meshes"));

	// note there is a MergeMeshUtilities.h w/ a very feature-filled mesh merging class, but for simplicity (and to fit modeling tool needs)
	// this tool currently converts things through dynamic mesh instead

#if WITH_EDITOR
	FBox Box(ForceInit);
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		Box += TargetComponentInterface(ComponentIdx)->GetOwnerComponent()->Bounds.GetBox();
	}

	bool bMergeSameMaterials = true;
	TArray<UMaterialInterface*> AllMaterials;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<int> CombinedMatToOutMatIdx;
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		IMaterialProvider* ComponentMaterial = TargetMaterialInterface(ComponentIdx);
		for (int MaterialIdx = 0, NumMaterials = ComponentMaterial->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
		{
			UMaterialInterface* Mat = ComponentMaterial->GetMaterial(MaterialIdx);
			int32 OutMatIdx = -1;
			if (!bMergeSameMaterials || !KnownMaterials.Contains(Mat))
			{
				OutMatIdx = AllMaterials.Num();
				if (bMergeSameMaterials)
				{
					KnownMaterials.Add(Mat, AllMaterials.Num());
				}
				AllMaterials.Add(Mat);
			}
			else
			{
				OutMatIdx = KnownMaterials[Mat];
			}
			CombinedMatToOutMatIdx.Add(OutMatIdx);
		}
	}

	FDynamicMesh3 AccumulateDMesh;
	AccumulateDMesh.EnableTriangleGroups();
	AccumulateDMesh.EnableAttributes();
	AccumulateDMesh.Attributes()->EnableMaterialID();
	FTransform AccumToWorld(Box.GetCenter());
	FTransform ToAccum(-Box.GetCenter());

	FSimpleShapeSet3d SimpleCollision;
	UE::Geometry::FComponentCollisionSettings CollisionSettings;

	{
		FScopedSlowTask SlowTask(Targets.Num()+1, 
			bDuplicateMode ? 
			LOCTEXT("DuplicateMeshBuild", "Building duplicate mesh ...") :
			LOCTEXT("CombineMeshesBuild", "Building combined mesh ..."));
		SlowTask.MakeDialog();

		int MatIndexBase = 0;
		for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
			SlowTask.EnterProgressFrame(1);
			IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(ComponentIdx);
			IMeshDescriptionProvider* TargetMeshProvider = TargetMeshProviderInterface(ComponentIdx);

			FMeshDescriptionToDynamicMesh Converter;
			FDynamicMesh3 ComponentDMesh;
			const FMeshDescription* MeshDescription = MeshDescriptions[ComponentIdx];
			Converter.Convert(MeshDescription, ComponentDMesh);

			FTransform3d XF = (FTransform3d)(TargetComponentInterface(ComponentIdx)->GetWorldTransform() * ToAccum);
			if (XF.GetDeterminant() < 0)
			{
				ComponentDMesh.ReverseOrientation(false);
			}

			// update material IDs to account for combined material set
			FDynamicMeshMaterialAttribute* MatAttrib = ComponentDMesh.Attributes()->GetMaterialID();
			for (int TID : ComponentDMesh.TriangleIndicesItr())
			{
				MatAttrib->SetValue(TID, CombinedMatToOutMatIdx[MatIndexBase + MatAttrib->GetValue(TID)]);
			}

			FDynamicMeshEditor Editor(&AccumulateDMesh);
			FMeshIndexMappings IndexMapping;
			if (bDuplicateMode) // no transform if duplicating
			{
				Editor.AppendMesh(&ComponentDMesh, IndexMapping);
				CollisionSettings = UE::Geometry::GetCollisionSettings(TargetComponent->GetOwnerComponent());
				UE::Geometry::AppendSimpleCollision(TargetComponent->GetOwnerComponent(), &SimpleCollision, FTransform3d::Identity());
			}
			else
			{
				Editor.AppendMesh(&ComponentDMesh, IndexMapping,
					[&XF](int Unused, const FVector3d P) { return XF.TransformPosition(P); },
					[&XF](int Unused, const FVector3d N) { return XF.TransformNormal(N); });
				UE::Geometry::AppendSimpleCollision(TargetComponent->GetOwnerComponent(), &SimpleCollision, XF);
			}

			MatIndexBase += TargetMaterialInterface(ComponentIdx)->GetNumMaterials();
		}

		SlowTask.EnterProgressFrame(1);

		if (bDuplicateMode)
		{
			// TODO: will need to refactor this when we support duplicating multiple
			check(Targets.Num() == 1);
			AccumToWorld = TargetComponentInterface(0)->GetWorldTransform();
		}

		// max len explicitly enforced here, would ideally notify user
		FString UseBaseName = BasicProperties->OutputName.Left(250);
		if (UseBaseName.IsEmpty())
		{
			UseBaseName = (bDuplicateMode) ? TEXT("Duplicate") : TEXT("Combined");
		}
		AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
			AssetAPI, TargetWorld,
			&AccumulateDMesh, (FTransform3d)AccumToWorld, UseBaseName, AllMaterials);
		if (NewActor != nullptr)
		{
			// copy the component materials onto the new static mesh asset too
			// (note: GenerateStaticMeshActor defaults to just putting blank slots on the asset)
			UStaticMeshComponent* NewMeshComponent = NewActor->FindComponentByClass<UStaticMeshComponent>();
			UStaticMesh* NewMesh = NewMeshComponent->GetStaticMesh();
			for (int32 MatIdx = 0; MatIdx < AllMaterials.Num(); MatIdx++)
			{
				NewMesh->SetMaterial(MatIdx, AllMaterials[MatIdx]);
			}

			// if any inputs have Simple Collision geometry we will forward it to new mesh.
			if (SimpleCollision.TotalElementsNum() > 0)
			{
				UE::Geometry::SetSimpleCollision(NewMeshComponent, &SimpleCollision, CollisionSettings);
			}

			// select the new actor
			ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
		}
	}
#endif

	
	TArray<AActor*> Actors;
	for (int32 Idx = 0; Idx < Targets.Num(); Idx++)
	{
		Actors.Add(TargetComponentInterface(Idx)->GetOwnerActor());
	}
	HandleSourceProperties->ApplyMethod(Actors, GetToolManager());


	GetToolManager()->EndUndoTransaction();
}










void UCombineMeshesTool::UpdateExistingAsset()
{
	TArray<const FMeshDescription*> MeshDescriptions;
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		MeshDescriptions.Add(TargetMeshProviderInterface(ComponentIdx)->GetMeshDescription());
	}

	check(!bDuplicateMode);
	GetToolManager()->BeginUndoTransaction(LOCTEXT("CombineMeshesToolTransactionName", "Combine Meshes"));

	// note there is a MergeMeshUtilities.h w/ a very feature-filled mesh merging class, but for simplicity (and to fit modeling tool needs)
	// this tool currently converts things through dynamic mesh instead

	AActor* SkipActor = nullptr;

#if WITH_EDITOR

	bool bMergeSameMaterials = true;
	TArray<UMaterialInterface*> AllMaterials;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<int> CombinedMatToOutMatIdx;
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		IMaterialProvider* TargetMaterial = TargetMaterialInterface(ComponentIdx);
		for (int MaterialIdx = 0, NumMaterials = TargetMaterial->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
		{
			UMaterialInterface* Mat = TargetMaterial->GetMaterial(MaterialIdx);
			int32 OutMatIdx = -1;
			if (!bMergeSameMaterials || !KnownMaterials.Contains(Mat))
			{
				OutMatIdx = AllMaterials.Num();
				if (bMergeSameMaterials)
				{
					KnownMaterials.Add(Mat, AllMaterials.Num());
				}
				AllMaterials.Add(Mat);
			}
			else
			{
				OutMatIdx = KnownMaterials[Mat];
			}
			CombinedMatToOutMatIdx.Add(OutMatIdx);
		}
	}

	FDynamicMesh3 AccumulateDMesh;
	AccumulateDMesh.EnableTriangleGroups();
	AccumulateDMesh.EnableAttributes();
	AccumulateDMesh.Attributes()->EnableMaterialID();

	int32 SkipIndex = (BasicProperties->WriteOutputTo == ECombineTargetType::FirstInputAsset) ? 0 : (Targets.Num() - 1);
	IPrimitiveComponentBackedTarget* UpdateTarget = TargetComponentInterface(SkipIndex);
	IMeshDescriptionCommitter* UpdateTargetCommitter = TargetMeshCommitterInterface(SkipIndex);
	IMaterialProvider* UpdateTargetMaterial = TargetMaterialInterface(SkipIndex);
	SkipActor = UpdateTarget->GetOwnerActor();

	FTransform3d TargetToWorld = (FTransform3d)UpdateTarget->GetWorldTransform();
	FTransform3d WorldToTarget = TargetToWorld.Inverse();

	FSimpleShapeSet3d SimpleCollision;
	UE::Geometry::FComponentCollisionSettings CollisionSettings = UE::Geometry::GetCollisionSettings(UpdateTarget->GetOwnerComponent());
	TArray<FTransform3d> Transforms;
	Transforms.SetNum(2);

	{
		FScopedSlowTask SlowTask(Targets.Num()+1, 
			bDuplicateMode ? 
			LOCTEXT("DuplicateMeshBuild", "Building duplicate mesh ...") :
			LOCTEXT("CombineMeshesBuild", "Building combined mesh ..."));
		SlowTask.MakeDialog();

		int MatIndexBase = 0;
		for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
			SlowTask.EnterProgressFrame(1);

			IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(ComponentIdx);

			FMeshDescriptionToDynamicMesh Converter;
			FDynamicMesh3 ComponentDMesh;
			Converter.Convert(MeshDescriptions[ComponentIdx], ComponentDMesh);

			// update material IDs to account for combined material set
			FDynamicMeshMaterialAttribute* MatAttrib = ComponentDMesh.Attributes()->GetMaterialID();
			for (int TID : ComponentDMesh.TriangleIndicesItr())
			{
				MatAttrib->SetValue(TID, CombinedMatToOutMatIdx[MatIndexBase + MatAttrib->GetValue(TID)]);
			}
			MatIndexBase += TargetMaterialInterface(ComponentIdx)->GetNumMaterials();


			if (ComponentIdx != SkipIndex)
			{
				FTransform3d ComponentToWorld = (FTransform3d)TargetComponent->GetWorldTransform();
				MeshTransforms::ApplyTransform(ComponentDMesh, ComponentToWorld);
				if (ComponentToWorld.GetDeterminant() < 0)
				{
					ComponentDMesh.ReverseOrientation(true);
				}
				MeshTransforms::ApplyTransform(ComponentDMesh, WorldToTarget);
				if (WorldToTarget.GetDeterminant() < 0)
				{
					ComponentDMesh.ReverseOrientation(true);
				}
				Transforms[0] = ComponentToWorld;
				Transforms[1] = WorldToTarget;
				UE::Geometry::AppendSimpleCollision(TargetComponent->GetOwnerComponent(), &SimpleCollision, Transforms);
			}
			else
			{
				UE::Geometry::AppendSimpleCollision(TargetComponent->GetOwnerComponent(), &SimpleCollision, FTransform3d::Identity());
			}

			FDynamicMeshEditor Editor(&AccumulateDMesh);
			FMeshIndexMappings IndexMapping;
			Editor.AppendMesh(&ComponentDMesh, IndexMapping);
		}

		SlowTask.EnterProgressFrame(1);

		UpdateTargetCommitter->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(&AccumulateDMesh, *CommitParams.MeshDescriptionOut);
		});

		UE::Geometry::SetSimpleCollision(UpdateTarget->GetOwnerComponent(), &SimpleCollision, CollisionSettings);

		FComponentMaterialSet MaterialSet;
		MaterialSet.Materials = AllMaterials;
		UpdateTargetMaterial->CommitMaterialSetUpdate(MaterialSet, true);

		// select the new actor
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), SkipActor);
	}
#endif

	
	TArray<AActor*> Actors;
	for (int Idx = 0; Idx < Targets.Num(); Idx++)
	{
		AActor* Actor = TargetComponentInterface(Idx)->GetOwnerActor();
		if (Actor != SkipActor)
		{
			Actors.Add(Actor);
		}
	}
	HandleSourceProperties->ApplyMethod(Actors, GetToolManager());


	GetToolManager()->EndUndoTransaction();
}







#undef LOCTEXT_NAMESPACE
