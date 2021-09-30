// Copyright Epic Games, Inc. All Rights Reserved.

#include "CombineMeshesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "DynamicMesh/MeshTransforms.h"

#include "ModelingObjectsCreationAPI.h"
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
#include "ModelingToolTargetUtil.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

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
		  (SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1)
		: (SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) > 1);
}

UInteractiveTool* UCombineMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UCombineMeshesTool* NewTool = NewObject<UCombineMeshesTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
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

	OutputTypeProperties = NewObject<UCreateMeshObjectTypeProperties>(this);
	OutputTypeProperties->InitializeDefaultWithAuto();
	OutputTypeProperties->OutputType = UCreateMeshObjectTypeProperties::AutoIdentifier;
	OutputTypeProperties->RestoreProperties(this, TEXT("OutputTypeFromInputTool"));
	OutputTypeProperties->WatchProperty(OutputTypeProperties->OutputType, [this](FString) { OutputTypeProperties->UpdatePropertyVisibility(); });
	AddToolPropertySource(OutputTypeProperties);

	BasicProperties->WatchProperty(BasicProperties->WriteOutputTo, [&](ECombineTargetType NewType)
	{
		if (NewType == ECombineTargetType::NewAsset)
		{
			BasicProperties->OutputAsset = TEXT("");
			SetToolPropertySourceEnabled(OutputTypeProperties, true);
		}
		else
		{
			int32 Index = (BasicProperties->WriteOutputTo == ECombineTargetType::FirstInputAsset) ? 0 : Targets.Num() - 1;
			BasicProperties->OutputAsset = UE::Modeling::GetComponentAssetBaseName(UE::ToolTarget::GetTargetComponent(Targets[Index]), false);
			SetToolPropertySourceEnabled(OutputTypeProperties, false);
		}
	});

	SetToolPropertySourceEnabled(OutputTypeProperties, BasicProperties->WriteOutputTo == ECombineTargetType::NewAsset);

	if (bDuplicateMode)
	{
		SetToolDisplayName(LOCTEXT("DuplicateMeshesToolName", "Duplicate"));
		BasicProperties->OutputName = UE::Modeling::GetComponentAssetBaseName(UE::ToolTarget::GetTargetComponent(Targets[0]));
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
	OutputTypeProperties->SaveProperties(this, TEXT("OutputTypeFromInputTool"));
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

void UCombineMeshesTool::CreateNewAsset()
{
	// Make sure meshes are available before we open transaction. This is to avoid potential stability issues related 
	// to creation/load of meshes inside a transaction, for assets that possibly do not have bulk data currently loaded.
	TArray<FDynamicMesh3> InputMeshes;
	InputMeshes.Reserve(Targets.Num());
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		InputMeshes.Add(UE::ToolTarget::GetDynamicMeshCopy(Targets[ComponentIdx], true));
	}

	GetToolManager()->BeginUndoTransaction( bDuplicateMode ? 
		LOCTEXT("DuplicateMeshToolTransactionName", "Duplicate Mesh") :
		LOCTEXT("CombineMeshesToolTransactionName", "Combine Meshes"));

	FBox Box(ForceInit);
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		Box += UE::ToolTarget::GetTargetComponent(Targets[ComponentIdx])->Bounds.GetBox();
	}

	TArray<UMaterialInterface*> AllMaterials;
	TArray<TArray<int32>> MaterialIDRemaps;
	BuildCombinedMaterialSet(AllMaterials, MaterialIDRemaps);

	FDynamicMesh3 AccumulateDMesh;
	AccumulateDMesh.EnableTriangleGroups();
	AccumulateDMesh.EnableAttributes();
	AccumulateDMesh.Attributes()->EnableTangents();
	AccumulateDMesh.Attributes()->EnableMaterialID();
	AccumulateDMesh.Attributes()->EnablePrimaryColors();
	FTransform AccumToWorld(Box.GetCenter());
	FTransform ToAccum(-Box.GetCenter());

	FSimpleShapeSet3d SimpleCollision;
	UE::Geometry::FComponentCollisionSettings CollisionSettings;

	{
#if WITH_EDITOR
		FScopedSlowTask SlowTask(Targets.Num()+1, 
			bDuplicateMode ? 
			LOCTEXT("DuplicateMeshBuild", "Building duplicate mesh ...") :
			LOCTEXT("CombineMeshesBuild", "Building combined mesh ..."));
		SlowTask.MakeDialog();
#endif
		bool bNeedColorAttr = false;
		int MatIndexBase = 0;
		for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1);
#endif
			UPrimitiveComponent* PrimitiveComponent = UE::ToolTarget::GetTargetComponent(Targets[ComponentIdx]);

			FDynamicMesh3& ComponentDMesh = InputMeshes[ComponentIdx];
			bNeedColorAttr = bNeedColorAttr || (ComponentDMesh.HasAttributes() && ComponentDMesh.Attributes()->HasPrimaryColors());

			if (ComponentDMesh.HasAttributes() && ComponentDMesh.Attributes()->NumUVLayers() > AccumulateDMesh.Attributes()->NumUVLayers())
			{
				AccumulateDMesh.Attributes()->SetNumUVLayers(ComponentDMesh.Attributes()->NumUVLayers());
			}

			UE::Geometry::FTransform3d XF = (UE::Geometry::FTransform3d)((FTransform)UE::ToolTarget::GetLocalToWorldTransform(Targets[ComponentIdx]) * ToAccum);
			if (XF.GetDeterminant() < 0)
			{
				ComponentDMesh.ReverseOrientation(false);
			}

			// update material IDs to account for combined material set
			FDynamicMeshMaterialAttribute* MatAttrib = ComponentDMesh.Attributes()->GetMaterialID();
			for (int TID : ComponentDMesh.TriangleIndicesItr())
			{
				int MatID = MatAttrib->GetValue(TID);
				MatAttrib->SetValue(TID, MaterialIDRemaps[ComponentIdx][MatID]);
			}

			FDynamicMeshEditor Editor(&AccumulateDMesh);
			FMeshIndexMappings IndexMapping;
			if (bDuplicateMode) // no transform if duplicating
			{
				Editor.AppendMesh(&ComponentDMesh, IndexMapping);

				if (UE::Geometry::ComponentTypeSupportsCollision(PrimitiveComponent))
				{
					CollisionSettings = UE::Geometry::GetCollisionSettings(PrimitiveComponent);
					UE::Geometry::AppendSimpleCollision(PrimitiveComponent, &SimpleCollision, UE::Geometry::FTransform3d::Identity());
				}
			}
			else
			{
				Editor.AppendMesh(&ComponentDMesh, IndexMapping,
					[&XF](int Unused, const FVector3d P) { return XF.TransformPosition(P); },
					[&XF](int Unused, const FVector3d N) { return XF.TransformNormal(N); });
				if (UE::Geometry::ComponentTypeSupportsCollision(PrimitiveComponent))
				{
					UE::Geometry::AppendSimpleCollision(PrimitiveComponent, &SimpleCollision, XF);
				}
			}

			FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Targets[ComponentIdx]);
			MatIndexBase += MaterialSet.Materials.Num();
		}

		if (!bNeedColorAttr)
		{
			AccumulateDMesh.Attributes()->DisablePrimaryColors();
		}

#if WITH_EDITOR
		SlowTask.EnterProgressFrame(1);
#endif

		if (bDuplicateMode)
		{
			// TODO: will need to refactor this when we support duplicating multiple
			check(Targets.Num() == 1);
			AccumToWorld = (FTransform)UE::ToolTarget::GetLocalToWorldTransform(Targets[0]);
		}

		// max len explicitly enforced here, would ideally notify user
		FString UseBaseName = BasicProperties->OutputName.Left(250);
		if (UseBaseName.IsEmpty())
		{
			UseBaseName = (bDuplicateMode) ? TEXT("Duplicate") : TEXT("Combined");
		}

		FCreateMeshObjectParams NewMeshObjectParams;
		NewMeshObjectParams.TargetWorld = TargetWorld;
		NewMeshObjectParams.Transform = AccumToWorld;
		NewMeshObjectParams.BaseName = UseBaseName;
		NewMeshObjectParams.Materials = AllMaterials;
		NewMeshObjectParams.SetMesh(&AccumulateDMesh);
		if (OutputTypeProperties->OutputType == UCreateMeshObjectTypeProperties::AutoIdentifier)
		{
			UE::ToolTarget::ConfigureCreateMeshObjectParams(Targets[0], NewMeshObjectParams);
		}
		else
		{
			OutputTypeProperties->ConfigureCreateMeshObjectParams(NewMeshObjectParams);
		}
		FCreateMeshObjectResult Result = UE::Modeling::CreateMeshObject(GetToolManager(), MoveTemp(NewMeshObjectParams));
		if (Result.IsOK() && Result.NewActor != nullptr)
		{
			// if any inputs have Simple Collision geometry we will forward it to new mesh.
			if (UE::Geometry::ComponentTypeSupportsCollision(Result.NewComponent) && SimpleCollision.TotalElementsNum() > 0)
			{
				UE::Geometry::SetSimpleCollision(Result.NewComponent, &SimpleCollision, CollisionSettings);
			}

			// select the new actor
			ToolSelectionUtil::SetNewActorSelection(GetToolManager(), Result.NewActor);
		}
	}
	
	TArray<AActor*> Actors;
	for (int32 Idx = 0; Idx < Targets.Num(); Idx++)
	{
		Actors.Add(UE::ToolTarget::GetTargetActor(Targets[Idx]));
	}
	HandleSourceProperties->ApplyMethod(Actors, GetToolManager());


	GetToolManager()->EndUndoTransaction();
}







void UCombineMeshesTool::UpdateExistingAsset()
{
	// Make sure meshes are available before we open transaction. This is to avoid potential stability issues related 
	// to creation/load of meshes inside a transaction, for assets that possibly do not have bulk data currently loaded.
	TArray<FDynamicMesh3> InputMeshes;
	InputMeshes.Reserve(Targets.Num());
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		InputMeshes.Add(UE::ToolTarget::GetDynamicMeshCopy(Targets[ComponentIdx], true));
	}

	check(!bDuplicateMode);
	GetToolManager()->BeginUndoTransaction(LOCTEXT("CombineMeshesToolTransactionName", "Combine Meshes"));

	AActor* SkipActor = nullptr;

	TArray<UMaterialInterface*> AllMaterials;
	TArray<TArray<int32>> MaterialIDRemaps;
	BuildCombinedMaterialSet(AllMaterials, MaterialIDRemaps);

	FDynamicMesh3 AccumulateDMesh;
	AccumulateDMesh.EnableTriangleGroups();
	AccumulateDMesh.EnableAttributes();
	AccumulateDMesh.Attributes()->EnableTangents();
	AccumulateDMesh.Attributes()->EnableMaterialID();
	AccumulateDMesh.Attributes()->EnablePrimaryColors();

	int32 SkipIndex = (BasicProperties->WriteOutputTo == ECombineTargetType::FirstInputAsset) ? 0 : (Targets.Num() - 1);
	UPrimitiveComponent* UpdateComponent = UE::ToolTarget::GetTargetComponent(Targets[SkipIndex]);
	SkipActor = UE::ToolTarget::GetTargetActor(Targets[SkipIndex]);

	UE::Geometry::FTransform3d TargetToWorld = (UE::Geometry::FTransform3d)UE::ToolTarget::GetLocalToWorldTransform(Targets[SkipIndex]);
	UE::Geometry::FTransform3d WorldToTarget = TargetToWorld.Inverse();

	FSimpleShapeSet3d SimpleCollision;
	UE::Geometry::FComponentCollisionSettings CollisionSettings;
	bool bOutputComponentSupportsCollision = UE::Geometry::ComponentTypeSupportsCollision(UpdateComponent);
	if (bOutputComponentSupportsCollision)
	{
		CollisionSettings = UE::Geometry::GetCollisionSettings(UpdateComponent);
	}
	TArray<UE::Geometry::FTransform3d> Transforms;
	Transforms.SetNum(2);

	{
#if WITH_EDITOR
		FScopedSlowTask SlowTask(Targets.Num()+1, 
			bDuplicateMode ? 
			LOCTEXT("DuplicateMeshBuild", "Building duplicate mesh ...") :
			LOCTEXT("CombineMeshesBuild", "Building combined mesh ..."));
		SlowTask.MakeDialog();
#endif
		bool bNeedColorAttr = false;
		for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
#if WITH_EDITOR
			SlowTask.EnterProgressFrame(1);
#endif
			UPrimitiveComponent* PrimitiveComponent = UE::ToolTarget::GetTargetComponent(Targets[ComponentIdx]);

			FDynamicMesh3& ComponentDMesh = InputMeshes[ComponentIdx];
			bNeedColorAttr = bNeedColorAttr || (ComponentDMesh.HasAttributes() && ComponentDMesh.Attributes()->HasPrimaryColors());

			// update material IDs to account for combined material set
			FDynamicMeshMaterialAttribute* MatAttrib = ComponentDMesh.Attributes()->GetMaterialID();
			for (int TID : ComponentDMesh.TriangleIndicesItr())
			{
				int MatID = MatAttrib->GetValue(TID);
				MatAttrib->SetValue(TID, MaterialIDRemaps[ComponentIdx][MatID]);
			}

			if (ComponentIdx != SkipIndex)
			{
				UE::Geometry::FTransform3d ComponentToWorld = (UE::Geometry::FTransform3d)UE::ToolTarget::GetLocalToWorldTransform(Targets[ComponentIdx]);
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
				if (bOutputComponentSupportsCollision && UE::Geometry::ComponentTypeSupportsCollision(PrimitiveComponent))
				{
					UE::Geometry::AppendSimpleCollision(PrimitiveComponent, &SimpleCollision, Transforms);
				}
			}
			else
			{
				if (bOutputComponentSupportsCollision && UE::Geometry::ComponentTypeSupportsCollision(PrimitiveComponent))
				{
					UE::Geometry::AppendSimpleCollision(PrimitiveComponent, &SimpleCollision, UE::Geometry::FTransform3d::Identity());
				}
			}

			FDynamicMeshEditor Editor(&AccumulateDMesh);
			FMeshIndexMappings IndexMapping;
			Editor.AppendMesh(&ComponentDMesh, IndexMapping);
		}

		if (!bNeedColorAttr)
		{
			AccumulateDMesh.Attributes()->DisablePrimaryColors();
		}

#if WITH_EDITOR
		SlowTask.EnterProgressFrame(1);
#endif

		FComponentMaterialSet NewMaterialSet;
		NewMaterialSet.Materials = AllMaterials;
		UE::ToolTarget::CommitDynamicMeshUpdate(Targets[SkipIndex], AccumulateDMesh, true, FConversionToMeshDescriptionOptions(), &NewMaterialSet);

		if (bOutputComponentSupportsCollision)
		{
			UE::Geometry::SetSimpleCollision(UpdateComponent, &SimpleCollision, CollisionSettings);
		}

		// select the new actor
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), SkipActor);
	}

	
	TArray<AActor*> Actors;
	for (int Idx = 0; Idx < Targets.Num(); Idx++)
	{
		AActor* Actor = UE::ToolTarget::GetTargetActor(Targets[Idx]);
		if (Actor != SkipActor)
		{
			Actors.Add(Actor);
		}
	}
	HandleSourceProperties->ApplyMethod(Actors, GetToolManager());

	GetToolManager()->EndUndoTransaction();
}







void UCombineMeshesTool::BuildCombinedMaterialSet(TArray<UMaterialInterface*>& NewMaterialsOut, TArray<TArray<int32>>& MaterialIDRemapsOut)
{
	NewMaterialsOut.Reset();

	TMap<UMaterialInterface*, int> KnownMaterials;

	MaterialIDRemapsOut.SetNum(Targets.Num());
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Targets[ComponentIdx]);
		int32 NumMaterials = MaterialSet.Materials.Num();
		for (int MaterialIdx = 0; MaterialIdx < NumMaterials; MaterialIdx++)
		{
			UMaterialInterface* Mat = MaterialSet.Materials[MaterialIdx];
			int32 NewMaterialIdx = 0;
			if (KnownMaterials.Contains(Mat) == false)
			{
				NewMaterialIdx = NewMaterialsOut.Num();
				KnownMaterials.Add(Mat, NewMaterialIdx);
				NewMaterialsOut.Add(Mat);
			}
			else
			{
				NewMaterialIdx = KnownMaterials[Mat];
			}
			MaterialIDRemapsOut[ComponentIdx].Add(NewMaterialIdx);
		}
	}
}








#undef LOCTEXT_NAMESPACE
