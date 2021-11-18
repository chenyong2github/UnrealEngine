// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshAssetFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "RenderingThread.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"


#if WITH_EDITOR
#include "Editor.h"
#endif

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshAssetFunctions"




UDynamicMesh*  UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(
	UStaticMesh* FromStaticMeshAsset, 
	UDynamicMesh* ToDynamicMesh, 
	FGeometryScriptCopyMeshFromAssetOptions AssetOptions,
	FGeometryScriptMeshReadLOD RequestedLOD,
	TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
	UGeometryScriptDebug* Debug)
{
	Outcome = EGeometryScriptOutcomePins::Failure;

	if (FromStaticMeshAsset == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromAsset_InvalidInput1", "CopyMeshFromStaticMesh: FromStaticMeshAsset is Null"));
		return ToDynamicMesh;
	}
	if (ToDynamicMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromAsset_InvalidInput2", "CopyMeshFromStaticMesh: ToDynamicMesh is Null"));
		return ToDynamicMesh;
	}
	if (RequestedLOD.LODType != EGeometryScriptLODType::MaxAvailable && RequestedLOD.LODType != EGeometryScriptLODType::SourceModel)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromAsset_LODNotAvailable", "CopyMeshFromStaticMesh: Requested LOD is not available"));
		return ToDynamicMesh;
	}

#if WITH_EDITOR
	int32 UseLODIndex = FMath::Clamp(RequestedLOD.LODIndex, 0, FromStaticMeshAsset->GetNumLODs() - 1);

	const FMeshDescription* SourceMesh = FromStaticMeshAsset->GetMeshDescription(UseLODIndex);
	const FStaticMeshSourceModel& SourceModel = FromStaticMeshAsset->GetSourceModel(UseLODIndex);
	const FMeshBuildSettings& BuildSettings = SourceModel.BuildSettings;

	bool bHasDirtyBuildSettings = BuildSettings.bRecomputeNormals
		|| (BuildSettings.bRecomputeTangents && AssetOptions.bRequestTangents);

	FMeshDescription LocalSourceMeshCopy;
	if (AssetOptions.bApplyBuildSettings && bHasDirtyBuildSettings )
	{
		LocalSourceMeshCopy = *SourceMesh;

		FStaticMeshAttributes Attributes(LocalSourceMeshCopy);
		if (!Attributes.GetTriangleNormals().IsValid() || !Attributes.GetTriangleTangents().IsValid())
		{
			// If these attributes don't exist, create them and compute their values for each triangle
			FStaticMeshOperations::ComputeTriangleTangentsAndNormals(LocalSourceMeshCopy);
		}

		EComputeNTBsFlags ComputeNTBsOptions = EComputeNTBsFlags::BlendOverlappingNormals;
		ComputeNTBsOptions |= BuildSettings.bRecomputeNormals ? EComputeNTBsFlags::Normals : EComputeNTBsFlags::None;
		if (AssetOptions.bRequestTangents)
		{
			ComputeNTBsOptions |= BuildSettings.bRecomputeTangents ? EComputeNTBsFlags::Tangents : EComputeNTBsFlags::None;
			ComputeNTBsOptions |= BuildSettings.bUseMikkTSpace ? EComputeNTBsFlags::UseMikkTSpace : EComputeNTBsFlags::None;
		}
		ComputeNTBsOptions |= BuildSettings.bComputeWeightedNormals ? EComputeNTBsFlags::WeightedNTBs : EComputeNTBsFlags::None;
		if (AssetOptions.bIgnoreRemoveDegenerates == false)
		{
			ComputeNTBsOptions |= BuildSettings.bRemoveDegenerates ? EComputeNTBsFlags::IgnoreDegenerateTriangles : EComputeNTBsFlags::None;
		}

		FStaticMeshOperations::ComputeTangentsAndNormals(LocalSourceMeshCopy, ComputeNTBsOptions);

		SourceMesh = &LocalSourceMeshCopy;
	}

	FDynamicMesh3 NewMesh;
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(SourceMesh, NewMesh, AssetOptions.bRequestTangents);

	ToDynamicMesh->SetMesh(MoveTemp(NewMesh));

	Outcome = EGeometryScriptOutcomePins::Success;

#else
	UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromAsset_EditorOnly", "CopyMeshFromStaticMesh: Not currently supported at Runtime"));
#endif

	return ToDynamicMesh;
}




UDynamicMesh*  UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
	UDynamicMesh* FromDynamicMesh,
	UStaticMesh* ToStaticMeshAsset,
	FGeometryScriptCopyMeshToAssetOptions Options,
	FGeometryScriptMeshWriteLOD TargetLOD,
	TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
	UGeometryScriptDebug* Debug)
{
	Outcome = EGeometryScriptOutcomePins::Failure;

	if (FromDynamicMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshToAsset_InvalidInput1", "CopyMeshToStaticMesh: FromDynamicMesh is Null"));
		return FromDynamicMesh;
	}
	if (ToStaticMeshAsset == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshToAsset_InvalidInput2", "CopyMeshToStaticMesh: ToStaticMeshAsset is Null"));
		return FromDynamicMesh;
	}
	if (TargetLOD.bWriteHiResSource)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshToAsset_Unsupported", "CopyMeshToStaticMesh: Writing HiResSource LOD is not yet supported"));
		return FromDynamicMesh;
	}

#if WITH_EDITOR

	int32 UseLODIndex = FMath::Clamp(TargetLOD.LODIndex, 0, 32);

	// don't allow build-in engine assets to be modified
	if (ToStaticMeshAsset->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshToAsset_EngineAsset", "CopyMeshToStaticMesh: Cannot modify built-in Engine asset"));
		return FromDynamicMesh;
	}

	// flush any pending rendering commands, which might want to touch this StaticMesh while we are rebuilding it
	FlushRenderingCommands();

	if (Options.bEmitTransaction)
	{
		GEditor->BeginTransaction(LOCTEXT("UpdateStaticMesh", "Update Static Mesh"));
	}

	// make sure transactional flag is on for the Asset
	ToStaticMeshAsset->SetFlags(RF_Transactional);
	// mark as modified
	ToStaticMeshAsset->Modify();

	if (ToStaticMeshAsset->GetNumSourceModels() < UseLODIndex+1)
	{
		ToStaticMeshAsset->SetNumSourceModels(UseLODIndex+1);
	}

	// configure build settings from options
	FStaticMeshSourceModel& LODSourceModel = ToStaticMeshAsset->GetSourceModel(UseLODIndex);
	FMeshBuildSettings& BuildSettings = LODSourceModel.BuildSettings;
	BuildSettings.bRecomputeNormals = Options.bEnableRecomputeNormals;
	BuildSettings.bRecomputeTangents = Options.bEnableRecomputeTangents;
	BuildSettings.bRemoveDegenerates = Options.bEnableRemoveDegenerates;

	FMeshDescription* MeshDescription = ToStaticMeshAsset->GetMeshDescription(UseLODIndex);
	if (MeshDescription == nullptr)
	{
		MeshDescription = ToStaticMeshAsset->CreateMeshDescription(UseLODIndex);
	}

	// mark mesh description for modify
	ToStaticMeshAsset->ModifyMeshDescription(UseLODIndex);

	if (!ensure(MeshDescription != nullptr))
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs,
			FText::Format(LOCTEXT("CopyMeshToAsset_EngineAsset", "CopyMeshToAsset: MeshDescription for LOD {0} is null?"), FText::AsNumber(UseLODIndex)));
		return FromDynamicMesh;
	}

	FConversionToMeshDescriptionOptions ConversionOptions;
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	FromDynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		Converter.Convert(&ReadMesh, *MeshDescription, !BuildSettings.bRecomputeTangents);
	});

	ToStaticMeshAsset->CommitMeshDescription(UseLODIndex);

	// Setting to prevent the standard static mesh reduction from running and replacing the render LOD.
	FStaticMeshSourceModel& ThisSourceModel = ToStaticMeshAsset->GetSourceModel(UseLODIndex);
	ThisSourceModel.ReductionSettings.PercentTriangles = 1.f;
	ThisSourceModel.ReductionSettings.PercentVertices = 1.f;

	if (Options.bDeferMeshPostEditChange == false)
	{
		ToStaticMeshAsset->PostEditChange();
	}

	if (Options.bEmitTransaction)
	{
		GEditor->EndTransaction();
	}

	Outcome = EGeometryScriptOutcomePins::Success;

#else
	UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshToAsset_EditorOnly", "CopyMeshToStaticMesh: Not currently supported at Runtime"));
#endif

	return FromDynamicMesh;
}




#undef LOCTEXT_NAMESPACE