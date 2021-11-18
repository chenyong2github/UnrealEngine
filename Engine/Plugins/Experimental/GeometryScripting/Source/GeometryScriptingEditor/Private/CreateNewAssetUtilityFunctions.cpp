// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/CreateNewAssetUtilityFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "RenderingThread.h"

#include "Engine/BlockingVolume.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "BSPOps.h"		// in UnrealEd
#include "Editor/EditorEngine.h"		// for FActorLabelUtilities
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Misc/Paths.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "ConversionUtils/DynamicMeshToVolume.h"
#include "AssetUtils/CreateStaticMeshUtil.h"
#include "ModelingObjectsCreationAPI.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_CreateNewAssetUtilityFunctions"






void UGeometryScriptLibrary_CreateNewAssetFunctions::CreateUniqueNewAssetPathName(
	FString AssetFolderPath,
	FString BaseAssetName,
	FString& UniqueAssetPathAndName,
	FString& UniqueAssetName,
	FGeometryScriptUniqueAssetNameOptions Options,
	TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
	UGeometryScriptDebug* Debug)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	Outcome = EGeometryScriptOutcomePins::Failure;
	int Attempts = 0;
	while (Attempts++ < 10)
	{
		FString UUID = UE::Modeling::GenerateRandomShortHexString(Options.UniqueIDDigits);
		UniqueAssetName = FString::Printf(TEXT("%s_%s"), *BaseAssetName, *UUID);
		UniqueAssetPathAndName = FPaths::Combine(AssetFolderPath, UniqueAssetName);

		// if asset does not exist at this path, we can use it
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*UniqueAssetPathAndName);
		if (AssetData.IsValid() == false)
		{
			Outcome = EGeometryScriptOutcomePins::Success;
			return;
		}
	}

	UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("CreateUniqueNewAssetPathName_Failed", "Failed to find available unique Asset Path/Name"));
}


AVolume* UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewVolumeFromMesh(
	UDynamicMesh* FromDynamicMesh,
	UWorld* CreateInWorld,
	FTransform ActorTransform,
	FString BaseActorName,
	FGeometryScriptCreateNewVolumeFromMeshOptions Options,
	TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
	UGeometryScriptDebug* Debug)
{
	Outcome = EGeometryScriptOutcomePins::Failure;

	if (FromDynamicMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CreateNewVolumeFromMesh_InvalidInput1", "CreateNewVolumeFromMesh: FromDynamicMesh is Null"));
		return nullptr;
	}
	if (CreateInWorld == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CreateNewVolumeFromMesh_InvalidInput2", "CreateNewVolumeFromMesh: CreateInWorld is Null"));
		return nullptr;
	}	
	if (FromDynamicMesh->GetTriangleCount() < 4)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CreateNewVolumeFromMesh_InvalidInput3", "CreateNewVolumeFromMesh: FromDynamicMesh does not define a valid Volume"));
		return nullptr;
	}
	// todo: other safety checks

	// spawn new actor

	UClass* VolumeClass = ABlockingVolume::StaticClass();
	if (Options.VolumeType != nullptr 
		&& Cast<AVolume>(Options.VolumeType->GetDefaultObject(false)) != nullptr )
	{
		VolumeClass = Options.VolumeType;
	}

	GEditor->BeginTransaction(LOCTEXT("CreateNewVolumeFromMesh_Transaction", "Create Volume"));

	FActorSpawnParameters SpawnInfo;
	FTransform NewActorTransform = FTransform::Identity;
	AVolume* NewVolumeActor = (AVolume*)CreateInWorld->SpawnActor(VolumeClass, &NewActorTransform, SpawnInfo);

	NewVolumeActor->BrushType = EBrushType::Brush_Add;
	UModel* Model = NewObject<UModel>(NewVolumeActor);
	NewVolumeActor->Brush = Model;
	NewVolumeActor->GetBrushComponent()->Brush = NewVolumeActor->Brush;

	UE::Conversion::FMeshToVolumeOptions ConvertOptions;
	ConvertOptions.bAutoSimplify = true;
	ConvertOptions.MaxTriangles = FMath::Max(1, Options.MaxTriangles);

	FromDynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		UE::Conversion::DynamicMeshToVolume(ReadMesh, NewVolumeActor, ConvertOptions);
	});

	NewVolumeActor->SetActorTransform(ActorTransform);
	FActorLabelUtilities::SetActorLabelUnique(NewVolumeActor, BaseActorName);
	NewVolumeActor->PostEditChange();

	GEditor->EndTransaction();

	Outcome = EGeometryScriptOutcomePins::Success;
	return NewVolumeActor;
}






UStaticMesh* UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewStaticMeshAssetFromMesh(
	UDynamicMesh* FromDynamicMesh, 
	FString AssetPathAndName,
	FGeometryScriptCreateNewStaticMeshAssetOptions Options,
	TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
	UGeometryScriptDebug* Debug)
{
	Outcome = EGeometryScriptOutcomePins::Failure;
	if (FromDynamicMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CreateNewStaticMeshAssetFromMesh_InvalidInput1", "CreateNewStaticMeshAssetFromMesh: FromDynamicMesh is Null"));
		return nullptr;
	}
	if (FromDynamicMesh->GetTriangleCount() == 0)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CreateNewStaticMeshAssetFromMesh_InvalidInput3", "CreateNewStaticMeshAssetFromMesh: FromDynamicMesh has zero triangles"));
		return nullptr;
	}
	// todo: other safety checks

	GEditor->BeginTransaction(LOCTEXT("CreateNewStaticMeshAssetFromMesh_Transaction", "Create StaticMesh"));

	UE::AssetUtils::FStaticMeshAssetOptions AssetOptions;
	AssetOptions.NewAssetPath = AssetPathAndName;

	AssetOptions.NumSourceModels = 1;

	// CreateStaticMeshAsset below will handle this, but we could allow passing in materials as an option...
	//AssetOptions.NumMaterialSlots = CreateMeshParams.Materials.Num();
	//AssetOptions.AssetMaterials = (CreateMeshParams.AssetMaterials.Num() == AssetOptions.NumMaterialSlots) ?
	//	FilterMaterials(CreateMeshParams.AssetMaterials) : FilterMaterials(CreateMeshParams.Materials);

	AssetOptions.bEnableRecomputeNormals = Options.bEnableRecomputeNormals;
	AssetOptions.bEnableRecomputeTangents = Options.bEnableRecomputeTangents;
	AssetOptions.bGenerateNaniteEnabledMesh = Options.bEnableNanite;
	AssetOptions.NaniteProxyTrianglePercent = Options.NaniteProxyTrianglePercent;

	AssetOptions.bCreatePhysicsBody = Options.bEnableCollision;
	AssetOptions.CollisionType = Options.CollisionMode;

	FDynamicMesh3 LODMesh;
	FromDynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		LODMesh = ReadMesh;
	});
	AssetOptions.SourceMeshes.DynamicMeshes.Add(&LODMesh);

	UE::AssetUtils::FStaticMeshResults ResultData;
	UE::AssetUtils::ECreateStaticMeshResult AssetResult = UE::AssetUtils::CreateStaticMeshAsset(AssetOptions, ResultData);

	if (AssetResult != UE::AssetUtils::ECreateStaticMeshResult::Ok)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("CreateNewStaticMeshAssetFromMesh_Failed", "CreateNewStaticMeshAssetFromMesh: Failed to create new Asset"));
		return nullptr;
	}

	UStaticMesh* NewStaticMesh = ResultData.StaticMesh;
	NewStaticMesh->PostEditChange();

	GEditor->EndTransaction();

	// publish new asset so that asset editor updates
	FAssetRegistryModule::AssetCreated(NewStaticMesh);

	Outcome = EGeometryScriptOutcomePins::Success;
	return NewStaticMesh;
}





#undef LOCTEXT_NAMESPACE