// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticToSkeletalMeshConverter.h"

#if WITH_EDITOR

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "LODUtilities.h"
#include "MeshDescription.h"
#include "MeshUtilities.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"


DEFINE_LOG_CATEGORY_STATIC(LogStaticToSkeletalMeshConverter, Log, All);

static const FName RootBoneName("Root");
static const TCHAR* JointBaseName(TEXT("Joint"));


USkeleton* FStaticToSkeletalMeshConverter::CreateSkeletonFromStaticMesh(
	UObject *InOuter,
	const FName InName,
	const EObjectFlags InFlags,
	const UStaticMesh* InStaticMesh,
	const FVector& InRelativeRootPosition
	)
{
	if (!ensure(InStaticMesh))
	{
		return nullptr;
	}
	
	const FBox Bounds = InStaticMesh->GetBoundingBox();
	const FVector RootPosition = Bounds.Min + (Bounds.Max - Bounds.Min) * InRelativeRootPosition;
	FTransform RootTransform(FTransform::Identity);
	RootTransform.SetTranslation(RootPosition);

	USkeleton* Skeleton = NewObject<USkeleton>(InOuter, InName, InFlags);

	FReferenceSkeletonModifier Modifier(Skeleton);
	Modifier.Add(FMeshBoneInfo(RootBoneName, RootBoneName.ToString(), INDEX_NONE), RootTransform);

	return Skeleton;
}


USkeleton* FStaticToSkeletalMeshConverter::CreateSkeletonFromStaticMesh(
	UObject *InOuter,
	const FName InName,
	const EObjectFlags InFlags,
	const UStaticMesh* InStaticMesh,
	const FVector& InRelativeRootPosition,
	const FVector& InRelativeEndEffectorPosition,
	const int32 InIntermediaryJointCount
	)
{
	if (FMath::IsNearlyZero(FVector::DistSquared(InRelativeEndEffectorPosition, InRelativeRootPosition)))
	{
		return CreateSkeletonFromStaticMesh(InOuter, InName, InFlags, InStaticMesh, InRelativeRootPosition);
	}

	if (!ensure(InStaticMesh))
	{
		return nullptr;
	}
	
	const FBox Bounds = InStaticMesh->GetBoundingBox();
	const FVector RootPosition = Bounds.Min + (Bounds.Max - Bounds.Min) * InRelativeRootPosition;
	const FVector EndEffectorPosition = Bounds.Min + (Bounds.Max - Bounds.Min) * InRelativeEndEffectorPosition;

	// Find a rough rotation we can use
	const FQuat Rotation = FQuat::FindBetweenVectors(FVector::ZAxisVector, EndEffectorPosition - RootPosition).GetNormalized(); 
	
	FTransform ParentTransform(FTransform::Identity);
	ParentTransform.SetTranslation(RootPosition);
	ParentTransform.SetRotation(Rotation);

	USkeleton* Skeleton = NewObject<USkeleton>(InOuter, InName, InFlags);
	FReferenceSkeletonModifier Modifier(Skeleton);
	Modifier.Add(FMeshBoneInfo(RootBoneName, RootBoneName.ToString(), INDEX_NONE), ParentTransform);

	for (int32 JointIndex = 0; JointIndex <= InIntermediaryJointCount; JointIndex++)
	{
		const double T = (JointIndex + 1.0) / (InIntermediaryJointCount + 2.0);
		FTransform PointTransform(ParentTransform);
		PointTransform.SetTranslation(RootPosition + (EndEffectorPosition - RootPosition) * T);

		FString JointName = FString::Printf(TEXT("%s_%d"), JointBaseName, JointIndex + 1);
		Modifier.Add(FMeshBoneInfo(FName(JointName), JointName, JointIndex), PointTransform * ParentTransform.Inverse());
		ParentTransform = PointTransform;
	}

	return Skeleton;
}

static void CopyBuildSettings(
	const FMeshBuildSettings& InStaticMeshBuildSettings,
	FSkeletalMeshBuildSettings& OutSkeletalMeshBuildSettings
	)
{
	OutSkeletalMeshBuildSettings.bRecomputeNormals = InStaticMeshBuildSettings.bRecomputeNormals;
	OutSkeletalMeshBuildSettings.bRecomputeTangents = InStaticMeshBuildSettings.bRecomputeTangents;
	OutSkeletalMeshBuildSettings.bUseMikkTSpace = InStaticMeshBuildSettings.bUseMikkTSpace;
	OutSkeletalMeshBuildSettings.bComputeWeightedNormals = InStaticMeshBuildSettings.bComputeWeightedNormals;
	OutSkeletalMeshBuildSettings.bRemoveDegenerates = InStaticMeshBuildSettings.bRemoveDegenerates;
	OutSkeletalMeshBuildSettings.bUseHighPrecisionTangentBasis = InStaticMeshBuildSettings.bUseHighPrecisionTangentBasis;
	OutSkeletalMeshBuildSettings.bUseFullPrecisionUVs = InStaticMeshBuildSettings.bUseFullPrecisionUVs;
	OutSkeletalMeshBuildSettings.bUseBackwardsCompatibleF16TruncUVs = InStaticMeshBuildSettings.bUseBackwardsCompatibleF16TruncUVs;
	// The rest we leave at defaults.
}

static SkeletalMeshOptimizationImportance ConvertOptimizationImportance(
	 EMeshFeatureImportance::Type InStaticMeshImportance)
{
	switch(InStaticMeshImportance)
	{
	default:
	case EMeshFeatureImportance::Off:		return SMOI_Highest;
	case EMeshFeatureImportance::Lowest:	return SMOI_Lowest;
	case EMeshFeatureImportance::Low:		return SMOI_Low;
	case EMeshFeatureImportance::Normal:	return SMOI_Normal;
	case EMeshFeatureImportance::High:		return SMOI_High;
	case EMeshFeatureImportance::Highest:	return SMOI_Highest;
	}
}

static void CopyReductionSettings(
	const FMeshReductionSettings& InStaticMeshReductionSettings,
	FSkeletalMeshOptimizationSettings& OutSkeletalMeshReductionSettings
	)
{
	// Copy the reduction settings as closely as we can. 
	OutSkeletalMeshReductionSettings.NumOfTrianglesPercentage = InStaticMeshReductionSettings.PercentTriangles;
	OutSkeletalMeshReductionSettings.NumOfVertPercentage = InStaticMeshReductionSettings.PercentVertices;
	
	OutSkeletalMeshReductionSettings.WeldingThreshold = InStaticMeshReductionSettings.WeldingThreshold;
	OutSkeletalMeshReductionSettings.NormalsThreshold = InStaticMeshReductionSettings.HardAngleThreshold;
	OutSkeletalMeshReductionSettings.bRecalcNormals = InStaticMeshReductionSettings.bRecalculateNormals;
	
	OutSkeletalMeshReductionSettings.BaseLOD = InStaticMeshReductionSettings.BaseLODModel;
	
	OutSkeletalMeshReductionSettings.SilhouetteImportance = ConvertOptimizationImportance(InStaticMeshReductionSettings.SilhouetteImportance);
	OutSkeletalMeshReductionSettings.TextureImportance = ConvertOptimizationImportance(InStaticMeshReductionSettings.TextureImportance);
	OutSkeletalMeshReductionSettings.ShadingImportance = ConvertOptimizationImportance(InStaticMeshReductionSettings.ShadingImportance);
	
	switch(InStaticMeshReductionSettings.TerminationCriterion)
	{
	case EStaticMeshReductionTerimationCriterion::Triangles:
		OutSkeletalMeshReductionSettings.TerminationCriterion = SMTC_NumOfTriangles;
		break;
	case EStaticMeshReductionTerimationCriterion::Vertices:
		OutSkeletalMeshReductionSettings.TerminationCriterion = SMTC_NumOfVerts;
		break;
	case EStaticMeshReductionTerimationCriterion::Any:
		OutSkeletalMeshReductionSettings.TerminationCriterion = SMTC_TriangleOrVert;
		break;
	}
}



static bool AddLODFromMeshDescription(
	const FStaticMeshSourceModel& InStaticMeshSourceModel,
	USkeletalMesh* InSkeletalMesh,
	const int32 InLODIndex,
	const FBoneIndexType InBoneIndex,
	IMeshUtilities& InMeshUtilities
	)
{
	// Always copy the build and reduction settings. 
	FSkeletalMeshLODInfo& SkeletalLODInfo = InSkeletalMesh->AddLODInfo();

	SkeletalLODInfo.ScreenSize = InStaticMeshSourceModel.ScreenSize;
	CopyBuildSettings(InStaticMeshSourceModel.BuildSettings, SkeletalLODInfo.BuildSettings);
	CopyReductionSettings(InStaticMeshSourceModel.ReductionSettings, SkeletalLODInfo.ReductionSettings);

	FSkeletalMeshModel* ImportedModels = InSkeletalMesh->GetImportedModel();
	ImportedModels->LODModels.Add(new FSkeletalMeshLODModel);
	FSkeletalMeshLODModel& SkeletalMeshModel = ImportedModels->LODModels.Last(); 
	
	if (InStaticMeshSourceModel.IsMeshDescriptionValid())
	{
		FMeshDescription SkeletalMeshGeometry;
		if (!InStaticMeshSourceModel.CloneMeshDescription(SkeletalMeshGeometry))
		{
			return false;
		}
		
		FSkeletalMeshAttributes SkeletalMeshAttributes(SkeletalMeshGeometry);
		SkeletalMeshAttributes.Register();

		// Full binding to the root bone.
		FSkinWeightsVertexAttributesRef SkinWeights = SkeletalMeshAttributes.GetVertexSkinWeights();
		UE::AnimationCore::FBoneWeight RootInfluence(InBoneIndex, 1.0f);
		UE::AnimationCore::FBoneWeights RootBinding = UE::AnimationCore::FBoneWeights::Create({RootInfluence});
		
		for (const FVertexID VertexID: SkeletalMeshGeometry.Vertices().GetElementIDs())
		{
			SkinWeights.Set(VertexID, RootBinding);
		}

		FSkeletalMeshImportData SkeletalMeshImportGeometry = FSkeletalMeshImportData::CreateFromMeshDescription(SkeletalMeshGeometry);

		// We need at least one set of texture coordinates. Always.
		SkeletalMeshModel.NumTexCoords = FMath::Max<uint32>(1, SkeletalMeshImportGeometry.NumTexCoords);
		
		// Data needed by BuildSkeletalMesh
		TArray<FVector3f> LODPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
		TArray<int32> LODPointToRawMap;
		SkeletalMeshImportGeometry.CopyLODImportData( LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap );

		IMeshUtilities::MeshBuildOptions BuildOptions;
		BuildOptions.TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		BuildOptions.FillOptions(SkeletalLODInfo.BuildSettings);

		TArray<FText> WarningMessages;
		if (!InMeshUtilities.BuildSkeletalMesh(SkeletalMeshModel, InSkeletalMesh->GetPathName(), InSkeletalMesh->GetRefSkeleton(), LODInfluences, LODWedges, LODFaces, LODPoints, LODPointToRawMap, BuildOptions, &WarningMessages, nullptr))
		{
			for(const FText& Message: WarningMessages)
			{
				UE_LOG(LogStaticToSkeletalMeshConverter, Warning, TEXT("%s"), *Message.ToString());
			}
			return false;
		}
		
		InSkeletalMesh->SaveLODImportedData(InLODIndex, SkeletalMeshImportGeometry);
	}
	else
	{
		FSkeletalMeshUpdateContext UpdateContext;
		UpdateContext.SkeletalMesh = InSkeletalMesh;
		
		FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, InLODIndex, GetTargetPlatformManagerRef().GetRunningTargetPlatform());
	}
	
	return true;
}



USkeletalMesh* FStaticToSkeletalMeshConverter::CreateSkeletalMeshFromStaticMesh(
	UObject *InOuter,
	const FName InName,
	const EObjectFlags InFlags,
	const UStaticMesh* InStaticMesh,
	const FReferenceSkeleton& InReferenceSkeleton,
	const FName InBindBone 
)
{
	if (!ensure(InStaticMesh))
	{
		return nullptr;
	}

	int32 BoneIndex = 0;
	if (!InBindBone.IsNone())
	{
		BoneIndex = InReferenceSkeleton.FindRawBoneIndex(InBindBone);
		if (!ensure(BoneIndex != INDEX_NONE))
		{
			return nullptr;
		}
	}

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(InOuter, InName, InFlags);

	// This ensures that the render data gets built before we return, by calling PostEditChange when we fall out of scope.
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange( SkeletalMesh );
	SkeletalMesh->PreEditChange( nullptr );
	SkeletalMesh->SetRefSkeleton(InReferenceSkeleton);
	
	// Calculate the initial pose from the reference skeleton.
	SkeletalMesh->CalculateInvRefMatrices();

	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>( "MeshUtilities" );
	
	// Copy the LODs and LOD settings over (as close as we can).
	for (int32 Index = 0; Index < InStaticMesh->GetNumSourceModels(); Index++)
	{
		const FStaticMeshSourceModel& StaticMeshSourceModel = InStaticMesh->GetSourceModel(Index);
		
		if (!AddLODFromMeshDescription(
			StaticMeshSourceModel, SkeletalMesh, Index, static_cast<FBoneIndexType>(BoneIndex), MeshUtilities))
		{
			// If we didn't get a model for LOD index 0, we don't have a mesh. Bail out.
			if (Index == 0)
			{
				return nullptr;
			}

			// Otherwise, we have a model, so let's continue with what we have.
			break;
		}
	}
	
	// Convert the materials over.
	TArray<FSkeletalMaterial> Materials;
	for (const FStaticMaterial& StaticMaterial: InStaticMesh->GetStaticMaterials())
	{
		FSkeletalMaterial Material(
			StaticMaterial.MaterialInterface,
			StaticMaterial.MaterialSlotName);
		
		Materials.Add(Material);
	}

	SkeletalMesh->SetMaterials(Materials);
	
	// Set the bounds from the static mesh, including the extensions, otherwise it won't render properly (among other things).
	SkeletalMesh->SetImportedBounds( InStaticMesh->GetBounds() );
	SkeletalMesh->SetPositiveBoundsExtension(InStaticMesh->GetPositiveBoundsExtension());
	SkeletalMesh->SetNegativeBoundsExtension(InStaticMesh->GetNegativeBoundsExtension());

	return SkeletalMesh;
}

#endif
