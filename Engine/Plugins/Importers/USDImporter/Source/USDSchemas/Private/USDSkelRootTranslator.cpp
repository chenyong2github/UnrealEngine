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
#include "MaterialEditingLibrary.h"
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
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdSkelRootTranslatorImpl::ProcessMaterials );

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

					FSoftObjectPath MaterialPath( TEXT("Material'/USDImporter/Materials/DisplayColor.DisplayColor'") );

					UMaterialInterface* DisplayColorMaterial = Cast< UMaterialInterface >( MaterialPath.TryLoad() );
					UMaterialEditingLibrary::SetMaterialInstanceParent( MaterialInstance, DisplayColorMaterial );

					Material = MaterialInstance;
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

	bool LoadSkelMeshImportData( FSkeletalMeshImportData& SkelMeshImportData, pxr::UsdSkelCache& SkeletonCache, const pxr::UsdSkelRoot& SkeletonRoot, const float Time, TMap< FString, UObject* > PrimPathsToAssets )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdSkelRootTranslatorImpl::LoadSkelMeshImportData );

		bool bIsSkeletalDataValid = true;

		// Retrieve the USD skeletal data under the SkeletonRoot into the SkeletalMeshImportData
		{
			FScopedUsdAllocs UsdAllocs;

			SkeletonCache.Populate( SkeletonRoot );

			std::vector< pxr::UsdSkelBinding > SkeletonBindings;
			SkeletonCache.ComputeSkelBindings( SkeletonRoot, &SkeletonBindings );

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
					}
				}

				break;
			}
		}

		return bIsSkeletalDataValid;
	}

	class FSkelRootCreateAssetsTaskChain : public FUsdSchemaTranslatorTaskChain
	{
	public:
		explicit FSkelRootCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdTyped >& InSchema );

	protected:
		// Inputs
		TUsdStore< pxr::UsdTyped > Schema;
		TSharedRef< FUsdSchemaTranslationContext > Context;

		// Outputs
		FSkeletalMeshImportData SkeletalMeshImportData;

		TUsdStore< pxr::UsdSkelCache > SkeletonCache;

		void SetupTasks();
	};

	FSkelRootCreateAssetsTaskChain::FSkelRootCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdTyped >& InSchema )
		: Schema( InSchema )
		, Context( InContext )
	{
		SetupTasks();
	}

	void FSkelRootCreateAssetsTaskChain::SetupTasks()
	{
		// Ignore prims from disabled purposes
		if ( !EnumHasAllFlags( Context->PurposesToLoad, IUsdPrim::GetPurpose( Schema.Get().GetPrim() ) ) )
		{
			return;
		}

		{
			const bool bIsAsyncTask = true;

			// Create SkeletalMeshImportData (Async)
			Do( bIsAsyncTask,
				[ this ]()
				{
					const bool bContinueTaskChain = LoadSkelMeshImportData( SkeletalMeshImportData, SkeletonCache.Get(), pxr::UsdSkelRoot( Schema.Get() ), Context->Time, Context->PrimPathsToAssets );

					return bContinueTaskChain;
				} );

			// Process materials (Main thread)
			Then( !bIsAsyncTask,
				[ this ]()
				{
					FScopedUsdAllocs UsdAllocs;

					std::vector< pxr::UsdSkelBinding > SkeletonBindings;
					SkeletonCache.Get().ComputeSkelBindings( pxr::UsdSkelRoot( Schema.Get() ), &SkeletonBindings );

					bool bHasPrimDisplayColor = false;

					for ( const pxr::UsdSkelBinding& Binding : SkeletonBindings )
					{
						for ( const pxr::UsdSkelSkinningQuery& SkinningQuery : Binding.GetSkinningTargets() )
						{
							// In USD, the skinning target need not be a mesh, but for Unreal we are only interested in skinning meshes
							pxr::UsdGeomMesh SkinningMesh = pxr::UsdGeomMesh( SkinningQuery.GetPrim() );
							if ( SkinningMesh )
							{
								bHasPrimDisplayColor = bHasPrimDisplayColor || SkinningMesh.GetDisplayColorPrimvar().IsDefined();
							}
						}

						UsdSkelRootTranslatorImpl::ProcessMaterials( Schema.Get().GetPrim(), SkeletalMeshImportData, Context->PrimPathsToAssets, bHasPrimDisplayColor, Context->Time );
						break;
					}

					return true;
				} );

			// Create USkeletalMesh (Main thread)
			Then( !bIsAsyncTask,
				[ this ]()
				{
					FSHAHash SkeletalMeshHash = UsdSkelRootTranslatorImpl::ComputeSHAHash( SkeletalMeshImportData );

					USkeletalMesh* SkeletalMesh = Cast< USkeletalMesh >( Context->AssetsCache.FindRef( SkeletalMeshHash.ToString() ) );

					if ( !SkeletalMesh )
					{
						SkeletalMesh = UsdToUnreal::GetSkeletalMeshFromImportData( SkeletalMeshImportData, Context->ObjectFlags );
						Context->AssetsCache.Add( SkeletalMeshHash.ToString(), SkeletalMesh );
					}

					Context->PrimPathsToAssets.Add( UsdToUnreal::ConvertPath( Schema.Get().GetPath() ), SkeletalMesh );

					return true;
				} );
		}
	}
}

void FUsdSkelRootTranslator::CreateAssets()
{
	TSharedRef< UsdSkelRootTranslatorImpl::FSkelRootCreateAssetsTaskChain > AssetsTaskChain =
		MakeShared< UsdSkelRootTranslatorImpl::FSkelRootCreateAssetsTaskChain >( Context, Schema );

	Context->TranslatorTasks.Add( MoveTemp( AssetsTaskChain ) );
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

		const UsdToUnreal::FUsdStageInfo StageInfo( Prim.GetStage() );

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
						PoseableMeshComponent->BoneSpaceTransforms[ BoneIndex ] = UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix );
					}
				}

				PoseableMeshComponent->RefreshBoneTransforms();
			}
		}
	}
}

#endif // #if USE_USD_SDK
