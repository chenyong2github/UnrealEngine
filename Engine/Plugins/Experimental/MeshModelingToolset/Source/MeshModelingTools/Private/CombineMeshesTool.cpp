// Copyright Epic Games, Inc. All Rights Reserved.

#include "CombineMeshesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"
#include "DynamicMeshEditor.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif


#define LOCTEXT_NAMESPACE "UCombineMeshesTool"


/*
 * ToolBuilder
 */


bool UCombineMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 1;
}

UInteractiveTool* UCombineMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UCombineMeshesTool* NewTool = NewObject<UCombineMeshesTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 1);

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

UCombineMeshesToolProperties::UCombineMeshesToolProperties()
{
}



UCombineMeshesTool::UCombineMeshesTool()
{
}

void UCombineMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UCombineMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<UCombineMeshesToolProperties>(this);
	AddToolPropertySource(BasicProperties);
}



void UCombineMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (ShutdownType == EToolShutdownType::Accept)
	{
		UpdateAssets();
	}
}

void UCombineMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UCombineMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UCombineMeshesTool::Tick(float DeltaTime)
{
}



bool UCombineMeshesTool::HasAccept() const
{
	return true;
}

bool UCombineMeshesTool::CanAccept() const
{
	return true;
}


void UCombineMeshesTool::UpdateAssets()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("CombineMeshesToolTransactionName", "Combine Meshes"));


	// note there is a MergeMeshUtilities.h w/ a very feature-filled mesh merging class, but for simplicity (and to fit modeling tool needs)
	// this tool currently converts things through dynamic mesh instead

#if WITH_EDITOR
	FBox Box(ForceInit);
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		Box += ComponentTargets[ComponentIdx]->GetOwnerComponent()->Bounds.GetBox();
	}

	bool bMergeSameMaterials = true;
	TArray<UMaterialInterface*> AllMaterials;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<int> CombinedMatToOutMatIdx;
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[ComponentIdx];
		for (int MaterialIdx = 0, NumMaterials = ComponentTarget->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
		{
			UMaterialInterface* Mat = ComponentTarget->GetMaterial(MaterialIdx);
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

	{
		FScopedSlowTask SlowTask(ComponentTargets.Num()+1, 
			LOCTEXT("CombineMeshesBuild", "Building combined mesh ..."));
		SlowTask.MakeDialog();

		int MatIndexBase = 0;
		for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
		{
			SlowTask.EnterProgressFrame(1);
			TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[ComponentIdx];

			FMeshDescriptionToDynamicMesh Converter;
			Converter.bPrintDebugMessages = true;
			FDynamicMesh3 ComponentDMesh;
			Converter.Convert(ComponentTarget->GetMesh(), ComponentDMesh);

			FTransform3d XF = (FTransform3d)(ComponentTarget->GetWorldTransform() * ToAccum);
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
			Editor.AppendMesh(&ComponentDMesh, IndexMapping, 
				[&XF](int Unused, const FVector3d P) { return XF.TransformPosition(P); },
				[&XF](int Unused, const FVector3d N) { return XF.TransformNormal(N); });

			MatIndexBase += ComponentTarget->GetNumMaterials();
		}

		SlowTask.EnterProgressFrame(1);

		AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
			AssetAPI, TargetWorld,
			&AccumulateDMesh, (FTransform3d)AccumToWorld, TEXT("Combined Meshes"), AllMaterials);
		if (NewActor != nullptr)
		{
			ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
		}
	}


#endif

	
	if (BasicProperties->bDeleteSourceActors)
	{
		TargetWorld->Modify();
		for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
		{
			ComponentTargets[ComponentIdx]->GetOwnerActor()->Destroy();
		}
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
