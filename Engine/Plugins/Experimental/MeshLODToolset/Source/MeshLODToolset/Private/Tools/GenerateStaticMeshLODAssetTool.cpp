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

}



void UGenerateStaticMeshLODAssetTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		CreateNewAsset();
		//UpdateExistingAsset();
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


void UGenerateStaticMeshLODAssetTool::CreateNewAsset()
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	//GetToolManager()->BeginUndoTransaction( LOCTEXT("GenerateStaticMeshLODAssetTransaction", "Generate LOD Asset") );

	GenerateProcess->CalculateDerivedPathName(BasicProperties->GeneratedSuffix);
	GenerateProcess->ComputeDerivedSourceData();
	GenerateProcess->WriteDerivedAssetData();

/*
	UStaticMesh* StaticMesh = GenerateProcess->GetSourceStaticMesh();
	const FDynamicMesh3& GeneratedLOD0Mesh = GenerateProcess->GetDerivedLOD0Mesh();

	// find existing path and generate AutoLOD path
	FString FullSMPathWithExtension = UEditorAssetLibrary::GetPathNameForLoadedAsset(StaticMesh);
	FString SMPathWithName = FPaths::GetBaseFilename(FullSMPathWithExtension, false);
	FString SMPath = FPaths::GetPath(SMPathWithName);
	FString UseSuffix = FPaths::MakeValidFileName(BasicProperties->GeneratedSuffix);
	if (UseSuffix.Len() == 0)
	{
		UseSuffix = TEXT("_AutoLOD");
	}
	FString GeneratedSMPath = FString::Printf(TEXT("%s%s"), *SMPathWithName, *UseSuffix);

	// [TODO] should we try to re-use existing asset here, or should we delete it? 
	// The source asset might have had any number of config changes that we want to
	// preserve in the duplicate...
	UStaticMesh* GeneratedStaticMesh = nullptr;
	if (UEditorAssetLibrary::DoesAssetExist(GeneratedSMPath))
	{
		UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(GeneratedSMPath);
		GeneratedStaticMesh = Cast<UStaticMesh>(LoadedAsset);
	}
	else
	{
		UObject* DupeAsset = UEditorAssetLibrary::DuplicateAsset(SMPathWithName, GeneratedSMPath);
		GeneratedStaticMesh = Cast<UStaticMesh>(DupeAsset);
	}

	// make sure transactional flag is on
	GeneratedStaticMesh->SetFlags(RF_Transactional);
	GeneratedStaticMesh->Modify();

	// update MeshDescription LOD0 mesh
	GeneratedStaticMesh->SetNumSourceModels(1);
	FMeshDescription* MeshDescription = GeneratedStaticMesh->GetMeshDescription(0);
	FConversionToMeshDescriptionOptions ConversionOptions;
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	Converter.Convert(&GeneratedLOD0Mesh, *MeshDescription);
	GeneratedStaticMesh->CommitMeshDescription(0);

	// done updating mesh
	GeneratedStaticMesh->PostEditChange();



	const TArray<FStaticMaterial>& OldMaterials = StaticMesh->GetStaticMaterials();
	TArray<FStaticMaterial>& NewMaterials = GeneratedStaticMesh->GetStaticMaterials();
	check(OldMaterials.Num() == NewMaterials.Num());

	for (int32 mi = 0; mi < OldMaterials.Num(); ++mi)
	{
		UMaterialInterface* MaterialInterface = OldMaterials[mi].MaterialInterface;
		bool bSourceIsMIC = (Cast<UMaterialInstanceConstant>(MaterialInterface) != nullptr);

		FString SourceMaterialPath = UEditorAssetLibrary::GetPathNameForLoadedAsset(MaterialInterface);
		FString MaterialName = FPaths::GetBaseFilename(SourceMaterialPath, true);
		FString NewMaterialName = FString::Printf(TEXT("%s%s"), *MaterialName, *UseSuffix);
		FString NewMaterialPath = FPaths::Combine(SMPath, NewMaterialName);


		// delete existing material so that we can have a clean duplicate
		bool bNewMaterialExists = UEditorAssetLibrary::DoesAssetExist(NewMaterialPath);
		if (bNewMaterialExists)
		{
			bool bDeleteOK = UEditorAssetLibrary::DeleteAsset(NewMaterialPath);
			ensure(bDeleteOK);
		}

		// If source is a MIC, we can just duplicate it. If it is a UMaterial, we want to
		// create a child MIC? Or we could dupe the Material and rewrite the textures.
		// Probably needs to be an option.
		UMaterialInstanceConstant* GeneratedMIC = nullptr;
		if (bSourceIsMIC)
		{
			UObject* DupeAsset = UEditorAssetLibrary::DuplicateAsset(SourceMaterialPath, NewMaterialPath);
			GeneratedMIC = Cast<UMaterialInstanceConstant>(DupeAsset);
		}
		else
		{
			UMaterial* SourceMaterial = MaterialInterface->GetBaseMaterial();
			if (ensure(SourceMaterial))
			{
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = SourceMaterial;

				UObject* NewAsset = AssetTools.CreateAsset(NewMaterialName, FPackageName::GetLongPackagePath(NewMaterialPath),
					UMaterialInstanceConstant::StaticClass(), Factory);

				GeneratedMIC = Cast<UMaterialInstanceConstant>(NewAsset);
			}
		}

		NewMaterials[mi].MaterialInterface = GeneratedMIC;
	}


	// update materials on generated mesh
	GeneratedStaticMesh->SetStaticMaterials(NewMaterials);

*/

	// I think this would let us jump to new assets...
	//ObjectsToSync.Add(NewAsset);
	//GEditor->SyncBrowserToObjects(ObjectsToSync);


	//GetToolManager()->EndUndoTransaction();
}







void UGenerateStaticMeshLODAssetTool::UpdateExistingAsset()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("GenerateStaticMeshLODAssetToolTransactionName", "Combine Meshes"));

	// note there is a MergeMeshUtilities.h w/ a very feature-filled mesh merging class, but for simplicity (and to fit modeling tool needs)
	// this tool currently converts things through dynamic mesh instead

	AActor* SkipActor = nullptr;

#if WITH_EDITOR

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

	int32 SkipIndex = 0;
	TUniquePtr<FPrimitiveComponentTarget>& UpdateTarget = ComponentTargets[SkipIndex];
	SkipActor = UpdateTarget->GetOwnerActor();

	FTransform3d TargetToWorld = (FTransform3d)UpdateTarget->GetWorldTransform();
	FTransform3d WorldToTarget = TargetToWorld.Inverse();

	{
		FScopedSlowTask SlowTask(ComponentTargets.Num() + 1,
			LOCTEXT("GenerateStaticMeshLODAssetBuild", "Building combined mesh ..."));
		SlowTask.MakeDialog();

		int MatIndexBase = 0;
		for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
		{
			SlowTask.EnterProgressFrame(1);
			TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[ComponentIdx];

			FMeshDescriptionToDynamicMesh Converter;
			FDynamicMesh3 ComponentDMesh;
			Converter.Convert(ComponentTarget->GetMesh(), ComponentDMesh);

			// update material IDs to account for combined material set
			FDynamicMeshMaterialAttribute* MatAttrib = ComponentDMesh.Attributes()->GetMaterialID();
			for (int TID : ComponentDMesh.TriangleIndicesItr())
			{
				MatAttrib->SetValue(TID, CombinedMatToOutMatIdx[MatIndexBase + MatAttrib->GetValue(TID)]);
			}
			MatIndexBase += ComponentTarget->GetNumMaterials();


			if (ComponentIdx != SkipIndex)
			{
				FTransform3d ComponentToWorld = (FTransform3d)ComponentTarget->GetWorldTransform();
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
			}

			FDynamicMeshEditor Editor(&AccumulateDMesh);
			FMeshIndexMappings IndexMapping;
			Editor.AppendMesh(&ComponentDMesh, IndexMapping);
		}

		SlowTask.EnterProgressFrame(1);

		UpdateTarget->CommitMesh([&](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(&AccumulateDMesh, *CommitParams.MeshDescription);
		});

		FComponentMaterialSet MaterialSet;
		MaterialSet.Materials = AllMaterials;
		UpdateTarget->CommitMaterialSetUpdate(MaterialSet, true);

		// select the new actor
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), SkipActor);
	}
#endif



	GetToolManager()->EndUndoTransaction();
}







#undef LOCTEXT_NAMESPACE
