// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSkelRootTranslator.h"

#if USE_USD_SDK

#include "USDGeomMeshConversion.h"
#include "USDMemory.h"
#include "USDSkeletalDataConversion.h"
#include "USDTypesConversion.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdShade/material.h"
	#include "pxr/usd/usdSkel/binding.h"
	#include "pxr/usd/usdSkel/cache.h"
	#include "pxr/usd/usdSkel/root.h"
	#include "pxr/usd/usdSkel/skeletonQuery.h"
#include "USDIncludesEnd.h"


namespace UsdSkelRootTranslatorImpl
{
	void ProcessMaterials( const pxr::UsdPrim& UsdPrim, FSkeletalMeshImportData& SkelMeshImportData, TMap< FString, UObject* >& PrimPathsToAssets, bool bHasPrimDisplayColor, float Time )
	{
		for ( SkeletalMeshImportData::FMaterial& ImportedMaterial : SkelMeshImportData.Materials )
		{
			if ( !ImportedMaterial.Material.IsValid() )
			{
				UMaterialInterface* Material = nullptr;
				TUsdStore< pxr::UsdPrim > MaterialPrim = UsdPrim.GetStage()->GetPrimAtPath( UnrealToUsd::ConvertPath( *ImportedMaterial.MaterialImportName ).Get() );

				if ( MaterialPrim.Get() )
				{
					Material = Cast< UMaterialInterface >( PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( MaterialPrim.Get().GetPrimPath() ) ) );

					if ( Material )
					{
						bool bNeedsRecompile = false;
						Material->GetMaterial()->SetMaterialUsage( bNeedsRecompile, MATUSAGE_SkeletalMesh );
					}
				}

				if ( Material == nullptr && bHasPrimDisplayColor )
				{
					UMaterialInstanceConstant* MaterialInstance = NewObject< UMaterialInstanceConstant >();
					if ( UsdToUnreal::ConvertDisplayColor( pxr::UsdGeomMesh( UsdPrim ), *MaterialInstance, pxr::UsdTimeCode( Time ) ) )
					{
						if ( MaterialInstance && MaterialInstance->HasOverridenBaseProperties() )
						{
							MaterialInstance->ForceRecompileForRendering();
						}
						
						Material = MaterialInstance;
					}
				}

				ImportedMaterial.Material = Material;
			}
		}
	}

	FSHAHash ComputeSHAHash( const FSkeletalMeshImportData& SkeletalMeshImportData )
	{
		FSHA1 HashState;

		// We're only hashing the mesh points for now. Might need to refine that.
		HashState.Update( (uint8*)SkeletalMeshImportData.Points.GetData(), SkeletalMeshImportData.Points.Num() * SkeletalMeshImportData.Points.GetTypeSize() );

		FSHAHash OutHash;

		HashState.Final();
		HashState.GetHash( &OutHash.Hash[0] );

		return OutHash;
	}
}

void FUsdSkelRootTranslator::CreateAssets()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdSkelRootTranslator::CreateAssets );

	FSkeletalMeshImportData SkelMeshImportData;
	bool bIsSkeletalDataValid = true;

	// Retrieve the USD skeletal data under the SkeletonRoot into the SkeletalMeshImportData
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdPrim Prim = Schema.Get().GetPrim();

		pxr::UsdSkelCache SkeletonCache;
		pxr::UsdSkelRoot SkeletonRoot( Prim );
		SkeletonCache.Populate( SkeletonRoot );

		pxr::SdfPath PrimPath = Prim.GetPath();

		std::vector< pxr::UsdSkelBinding > SkeletonBindings;
		SkeletonCache.ComputeSkelBindings( SkeletonRoot, &SkeletonBindings );

		if ( SkeletonBindings.size() == 0 )
		{
			return;
		}

		const pxr::TfToken StageUpAxis = UsdUtils::GetUsdStageAxis( Prim.GetStage() );

		pxr::UsdGeomXformable Xformable( Prim );

		std::vector< double > TimeSamples;

		bool bHasPrimDisplayColor = false;

		// Note that there could be multiple skeleton bindings under the SkeletonRoot
		// For now, extract just the first one 
		for ( const pxr::UsdSkelBinding& Binding : SkeletonBindings )
		{
			const pxr::UsdSkelSkeleton& Skeleton = Binding.GetSkeleton();
			pxr::UsdSkelSkeletonQuery SkelQuery = SkeletonCache.GetSkelQuery( Skeleton );

			const bool bSkeletonValid = UsdToUnreal::ConvertSkeleton( SkelQuery, SkelMeshImportData );
			if ( !bSkeletonValid )
			{
				bIsSkeletalDataValid = false;
				break;
			}

			for (const pxr::UsdSkelSkinningQuery& SkinningQuery : Binding.GetSkinningTargets())
			{
				// In USD, the skinning target need not be a mesh, but for Unreal we are only interested in skinning meshes
				pxr::UsdGeomMesh SkinningMesh = pxr::UsdGeomMesh( SkinningQuery.GetPrim() );
				if (SkinningMesh)
				{
					UsdToUnreal::ConvertSkinnedMesh( SkinningQuery, SkelMeshImportData );
					bHasPrimDisplayColor = bHasPrimDisplayColor || SkinningMesh.GetDisplayColorPrimvar().IsDefined();
				}
			}

			UsdSkelRootTranslatorImpl::ProcessMaterials( Prim, SkelMeshImportData, Context->PrimPathsToAssets, bHasPrimDisplayColor, Context->Time );

			break;
		}
	}

	if ( !bIsSkeletalDataValid )
	{
		return;
	}

	FSHAHash SkeletalMeshHash = UsdSkelRootTranslatorImpl::ComputeSHAHash( SkelMeshImportData );

	USkeletalMesh* SkeletalMesh = Cast< USkeletalMesh >( Context->AssetsCache.FindRef( SkeletalMeshHash.ToString() ) );

	if ( !SkeletalMesh )
	{
		SkeletalMesh = UsdToUnreal::GetSkeletalMeshFromImportData( SkelMeshImportData, Context->ObjectFlags );
		Context->PrimPathsToAssets.Add( UsdToUnreal::ConvertPath( Schema.Get().GetPath() ), SkeletalMesh );
		Context->AssetsCache.Add( SkeletalMeshHash.ToString(), SkeletalMesh );
	}
}

USceneComponent* FUsdSkelRootTranslator::CreateComponents()
{
	USceneComponent* RootComponent = FUsdGeomXformableTranslator::CreateComponents();

	if ( USkinnedMeshComponent* SkinnedMeshComponent = Cast< USkinnedMeshComponent >( RootComponent ) )
	{
		SkinnedMeshComponent->SetSkeletalMesh( Cast< USkeletalMesh >( Context->PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( Schema.Get().GetPath() ) ) ) );
		UpdateComponents( SkinnedMeshComponent );
	}

	return RootComponent;
}

void FUsdSkelRootTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( !SceneComponent )
	{
		return;
	}

	Super::UpdateComponents( SceneComponent );

	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdPrim Prim = Schema.Get().GetPrim();

		pxr::UsdSkelCache SkeletonCache;
		pxr::UsdSkelRoot SkeletonRoot( Prim );
		SkeletonCache.Populate( SkeletonRoot );

		pxr::SdfPath PrimPath = Prim.GetPath();

		std::vector< pxr::UsdSkelBinding > SkeletonBindings;
		SkeletonCache.ComputeSkelBindings( SkeletonRoot, &SkeletonBindings );

		if ( SkeletonBindings.size() == 0 )
		{
			return;
		}

		const pxr::TfToken StageUpAxis = UsdUtils::GetUsdStageAxis( Prim.GetStage() );

		pxr::UsdGeomXformable Xformable( Prim );

		std::vector< double > TimeSamples;

		// Note that there could be multiple skeleton bindings under the SkeletonRoot
		// For now, extract just the first one 
		for ( const pxr::UsdSkelBinding& Binding : SkeletonBindings )
		{
			const pxr::UsdSkelSkeleton& Skeleton = Binding.GetSkeleton();
			pxr::UsdSkelSkeletonQuery SkelQuery = SkeletonCache.GetSkelQuery( Skeleton );

			const pxr::UsdSkelAnimQuery& AnimQuery = SkelQuery.GetAnimQuery();
			const bool bRes = AnimQuery.GetJointTransformTimeSamples( &TimeSamples );

			if ( TimeSamples.size() > 0 )
			{
				FScopedUnrealAllocs UnrealAllocs;

				UPoseableMeshComponent* PoseableMeshComponent = Cast< UPoseableMeshComponent >( SceneComponent );
				if ( !PoseableMeshComponent || !PoseableMeshComponent->SkeletalMesh || !SkelQuery )
				{
					return;
				}

				if ( PoseableMeshComponent->BoneSpaceTransforms.Num() != PoseableMeshComponent->SkeletalMesh->RefSkeleton.GetNum() )
				{
					PoseableMeshComponent->AllocateTransformData();
				}

				TArray< FTransform > BoneTransforms;
				TUsdStore< pxr::VtArray< pxr::GfMatrix4d > > UsdBoneTransforms;

				const bool bJointTransformsComputed = SkelQuery.ComputeJointLocalTransforms( &UsdBoneTransforms.Get(), pxr::UsdTimeCode( Context->Time ) );
				if ( bJointTransformsComputed )
				{
					BoneTransforms.Reserve( decltype( BoneTransforms )::SizeType( UsdBoneTransforms.Get().size() ) );

					for ( uint32 BoneIndex = 0; BoneIndex < UsdBoneTransforms.Get().size(); ++BoneIndex )
					{
						const pxr::GfMatrix4d& UsdMatrix = UsdBoneTransforms.Get()[ BoneIndex ];
						PoseableMeshComponent->BoneSpaceTransforms[ BoneIndex ] = UsdToUnreal::ConvertMatrix( StageUpAxis, UsdMatrix );
					}
				}

				PoseableMeshComponent->RefreshBoneTransforms();
			}
		}
	}
}

#endif // #if USE_USD_SDK
