// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/GenerateStaticMeshLODAssetTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Generators/SphereGenerator.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"
#include "MeshTangents.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Misc/Paths.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

#include "Graphs/GenerateStaticMeshLODProcess.h"


#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif


#define LOCTEXT_NAMESPACE "UGenerateStaticMeshLODAssetTool"


/*
 * ToolBuilder
 */


bool UGenerateStaticMeshLODAssetToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// hack to make multi-tool look like single-tool
	return (AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1);
}

UInteractiveTool* UGenerateStaticMeshLODAssetToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UGenerateStaticMeshLODAssetTool* NewTool = NewObject<UGenerateStaticMeshLODAssetTool>(SceneState.ToolManager);

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


void UGenerateStaticMeshLODAssetTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UGenerateStaticMeshLODAssetTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<UGenerateStaticMeshLODAssetToolProperties>(this);
	AddToolPropertySource(BasicProperties);
	BasicProperties->RestoreProperties(this);

	BasicProperties->OutputName = AssetGenerationUtil::GetComponentAssetBaseName(ComponentTargets[0]->GetOwnerComponent());

	BasicProperties->GeneratedSuffix = TEXT("_AutoLOD");


	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartStaticMeshLODAssetTool", "This tool creates a new LOD asset"),
		EToolMessageLevel::UserNotification);

	GenerateProcess = MakePimpl<FGenerateStaticMeshLODProcess>();

	TUniquePtr<FPrimitiveComponentTarget>& SourceComponent = ComponentTargets[0];
	UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(SourceComponent->GetOwnerComponent());
	UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	if (StaticMesh)
	{
		GenerateProcess->Initialize(StaticMesh);
	}

	FBoxSphereBounds Bounds = StaticMeshComponent->Bounds;
	FTransform PreviewTransform = SourceComponent->GetWorldTransform();
	PreviewTransform.AddToTranslation(FVector(0, 2.0f*Bounds.SphereRadius, 0));

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->CreateInWorld(TargetWorld, PreviewTransform);
	PreviewMesh->SetVisible(true);
	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::ExternallyCalculated);

	bPreviewValid = false;
	ValidatePreview();
}



void UGenerateStaticMeshLODAssetTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);

	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	if (ShutdownType == EToolShutdownType::Accept)
	{
		if (BasicProperties->OutputMode == EGenerateLODAssetOutputMode::UpdateExistingAsset)
		{
			UpdateExistingAsset();
		}
		else
		{
			CreateNewAsset();
		}

	}
}

void UGenerateStaticMeshLODAssetTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

bool UGenerateStaticMeshLODAssetTool::HasAccept() const
{
	return true;
}

bool UGenerateStaticMeshLODAssetTool::CanAccept() const
{
	return true;
}

void UGenerateStaticMeshLODAssetTool::ValidatePreview()
{
	if (bPreviewValid) return;

	GenerateProcess->ComputeDerivedSourceData();
	const FDynamicMesh3& ResultMesh = GenerateProcess->GetDerivedLOD0Mesh();
	const FMeshTangentsd& ResultTangents = GenerateProcess->GetDerivedLOD0MeshTangents();
	
	PreviewMesh->EditMesh([&](FDynamicMesh3& MeshToUpdate)
	{
		MeshToUpdate = GenerateProcess->GetDerivedLOD0Mesh();
	});
	PreviewMesh->UpdateTangents(&ResultTangents, true);

	FGenerateStaticMeshLODProcess::FPreviewMaterials PreviewMaterialSet;
	GenerateProcess->GetDerivedMaterialsPreview(PreviewMaterialSet);
	if (PreviewMaterialSet.Materials.Num() > 0)
	{
		PreviewTextures = PreviewMaterialSet.Textures;
		PreviewMaterials = PreviewMaterialSet.Materials;
		PreviewMesh->SetMaterials(PreviewMaterials);

		BasicProperties->PreviewTextures = PreviewTextures;
	}

	bPreviewValid = true;
}


void UGenerateStaticMeshLODAssetTool::CreateNewAsset()
{
	GenerateProcess->CalculateDerivedPathName(BasicProperties->GeneratedSuffix);
	//GenerateProcess->ComputeDerivedSourceData();
	GenerateProcess->WriteDerivedAssetData();
}



void UGenerateStaticMeshLODAssetTool::UpdateExistingAsset()
{
	GenerateProcess->CalculateDerivedPathName(BasicProperties->GeneratedSuffix);
	//GenerateProcess->ComputeDerivedSourceData();
	GenerateProcess->UpdateSourceAsset();
}







#undef LOCTEXT_NAMESPACE
