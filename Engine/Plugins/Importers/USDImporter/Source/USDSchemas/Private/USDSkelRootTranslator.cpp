// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSkelRootTranslator.h"

#if USE_USD_SDK

#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDSkeletalDataConversion.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ObjectTools.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdShade/material.h"
	#include "pxr/usd/usdSkel/binding.h"
	#include "pxr/usd/usdSkel/bindingAPI.h"
	#include "pxr/usd/usdSkel/blendShapeQuery.h"
	#include "pxr/usd/usdSkel/cache.h"
	#include "pxr/usd/usdSkel/root.h"
	#include "pxr/usd/usdSkel/skeletonQuery.h"
	#include "pxr/usd/usdSkel/tokens.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "UsdSkelRoot"

namespace UsdSkelRootTranslatorImpl
{
	void ProcessMaterials(
		TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& LODIndexToMaterialInfo,
		USkeletalMesh* SkeletalMesh,
		pxr::UsdStageRefPtr& InStage,
		TMap< FString, UObject* >& PrimPathsToAssets,
		TMap< FString, UObject* >& AssetsCache,
		float Time, EObjectFlags Flags,
		bool bSkeletalMeshHasMorphTargets
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdSkelRootTranslatorImpl::ProcessMaterials );

		if ( !SkeletalMesh )
		{
			return;
		}

		uint32 SkeletalMeshSlotIndex = 0;
		for ( int32 LODIndex = 0; LODIndex < LODIndexToMaterialInfo.Num(); ++LODIndex )
		{
			const TArray< UsdUtils::FUsdPrimMaterialSlot >& LODSlots = LODIndexToMaterialInfo[ LODIndex ].Slots;

			// We need to fill this in with the mapping from LOD material slots (i.e. sections) to the skeletal mesh's material slots
			TArray<int32> LODMaterialMap;
			LODMaterialMap.Reserve(LODSlots.Num());

			for ( int32 LODSlotIndex = 0; LODSlotIndex < LODSlots.Num(); ++LODSlotIndex, ++SkeletalMeshSlotIndex )
			{
				const UsdUtils::FUsdPrimMaterialSlot& Slot = LODSlots[ LODSlotIndex ];

				UMaterialInterface* Material = nullptr;

				switch ( Slot.AssignmentType )
				{
				case UsdUtils::EPrimAssignmentType::DisplayColor:
				{
					// Try reusing an already created DisplayColor material
					if ( UObject** FoundAsset = AssetsCache.Find( Slot.MaterialSource ) )
					{
						if ( UMaterialInstanceConstant* ExistingMaterial = Cast<UMaterialInstanceConstant>( *FoundAsset ) )
						{
							Material = ExistingMaterial;
						}
					}

					// Need to actually create a new DisplayColor material
					if ( Material == nullptr )
					{
						UMaterialInstanceConstant* MaterialInstance = NewObject< UMaterialInstanceConstant >( GetTransientPackage(), NAME_None, Flags );

						// Leave PrimPath as empty as it likely will be reused by many prims
						UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( MaterialInstance, TEXT( "USDAssetImportData" ) );
						MaterialInstance->AssetImportData = ImportData;

						AssetsCache.Add( Slot.MaterialSource, MaterialInstance );

						if ( TOptional<UsdUtils::FDisplayColorMaterial> DisplayColor = UsdUtils::FDisplayColorMaterial::FromString( Slot.MaterialSource ) )
						{
							UsdToUnreal::ConvertDisplayColor( DisplayColor.GetValue(), *MaterialInstance );
						}

						Material = MaterialInstance;
					}

					break;
				}
				case UsdUtils::EPrimAssignmentType::MaterialPrim:
				{
					FScopedUsdAllocs Allocs;

					std::string PathString = UnrealToUsd::ConvertString( *Slot.MaterialSource ).Get();
					if ( pxr::SdfPath::IsValidPathString( PathString ) )
					{
						pxr::UsdPrim MaterialPrim = InStage->GetPrimAtPath( pxr::SdfPath( PathString ) );
						if ( MaterialPrim )
						{
							Material = Cast< UMaterialInterface >( PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( MaterialPrim.GetPrimPath() ) ) );
						}
					}
					break;
				}
				case UsdUtils::EPrimAssignmentType::UnrealMaterial:
				{
					Material = Cast< UMaterialInterface >( FSoftObjectPath( Slot.MaterialSource ).TryLoad() );
					break;
				}
				case UsdUtils::EPrimAssignmentType::None:
				default:
					break;
				}

				if ( Material )
				{
					bool bNeedsRecompile = false;
					Material->GetMaterial()->SetMaterialUsage( bNeedsRecompile, MATUSAGE_SkeletalMesh );
					if ( bSkeletalMeshHasMorphTargets )
					{
						Material->GetMaterial()->SetMaterialUsage( bNeedsRecompile, MATUSAGE_MorphTargets );
					}
				}

				FName MaterialSlotName = *LexToString( SkeletalMeshSlotIndex );
				const bool bEnableShadowCasting = true;
				const bool bRecomputeTangents = false;
				SkeletalMesh->Materials.Add( FSkeletalMaterial( Material, bEnableShadowCasting, bRecomputeTangents, MaterialSlotName, MaterialSlotName ) );

				LODMaterialMap.Add( SkeletalMeshSlotIndex );
			}

			if ( FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo( LODIndex ) )
			{
				LODInfo->LODMaterialMap = LODMaterialMap;
			}
			else
			{
				UE_LOG(LogUsd, Error, TEXT("When processing materials for SkeletalMesh '%s', encountered no LOD info for LOD index %d!"), *SkeletalMesh->GetName(), LODIndex);
			}
		}
	}

	FSHAHash ComputeSHAHash( const TArray<FSkeletalMeshImportData>& LODIndexToSkeletalMeshImportData )
	{
		FSHA1 HashState;

		for ( const FSkeletalMeshImportData& ImportData : LODIndexToSkeletalMeshImportData )
		{
			// We're only hashing the mesh points for now. Might need to refine that.
			HashState.Update( (uint8*)ImportData.Points.GetData(), ImportData.Points.Num() * ImportData.Points.GetTypeSize() );
		}

		FSHAHash OutHash;

		HashState.Final();
		HashState.GetHash( &OutHash.Hash[0] );

		return OutHash;
	}

	void SetMorphTargetWeight( UPoseableMeshComponent& PoseableMeshComponent, const FString& MorphTargetName, float Weight )
	{
		USkeletalMesh* SkeletalMesh = PoseableMeshComponent.SkeletalMesh;

		// We try keeping a perfect correspondence between SkeletalMesh->MorphTargets and PoseableMeshComponent.ActiveMorphTargets
		int32 IndexInSkeletalMesh = INDEX_NONE;
		PoseableMeshComponent.SkeletalMesh->FindMorphTargetAndIndex( *MorphTargetName, IndexInSkeletalMesh );
		if ( IndexInSkeletalMesh == INDEX_NONE )
		{
			return;
		}

		UMorphTarget* MorphTarget = PoseableMeshComponent.SkeletalMesh->MorphTargets[ IndexInSkeletalMesh ];
		if ( !MorphTarget )
		{
			return;
		}

		int32 WeightIndex = INDEX_NONE;
		if ( PoseableMeshComponent.ActiveMorphTargets.IsValidIndex( IndexInSkeletalMesh ) )
		{
			FActiveMorphTarget& ActiveMorphTarget = PoseableMeshComponent.ActiveMorphTargets[ IndexInSkeletalMesh ];
			if ( ActiveMorphTarget.MorphTarget == MorphTarget )
			{
				WeightIndex = ActiveMorphTarget.WeightIndex;
			}
		}

		// Morph target is not at expected location (i.e. after CreateComponents, duplicate for PIE or undo/redo) --> Rebuild ActiveMorphTargets
		// This may lead to one frame of glitchiness, as we'll reset all weights to zero...
		if ( WeightIndex == INDEX_NONE )
		{
			PoseableMeshComponent.ActiveMorphTargets.Reset();
			PoseableMeshComponent.MorphTargetWeights.Reset();

			for ( int32 MorphTargetIndex = 0; MorphTargetIndex < SkeletalMesh->MorphTargets.Num(); ++MorphTargetIndex )
			{
				FActiveMorphTarget ActiveMorphTarget;
				ActiveMorphTarget.MorphTarget = SkeletalMesh->MorphTargets[ MorphTargetIndex ];
				ActiveMorphTarget.WeightIndex = MorphTargetIndex;

				PoseableMeshComponent.ActiveMorphTargets.Add( ActiveMorphTarget );
				PoseableMeshComponent.MorphTargetWeights.Add( 0.0f ); // We'll update these right afterwards when we call UpdateComponents
			}

			WeightIndex = IndexInSkeletalMesh;
		}

		PoseableMeshComponent.MorphTargetWeights[ WeightIndex ] = Weight;
	}

	// Adapted from UsdSkel_CacheImpl::ReadScope::_FindOrCreateSkinningQuery because we need to manually create these on UsdGeomMeshes we already have
	pxr::UsdSkelSkinningQuery CreateSkinningQuery( const pxr::UsdGeomMesh& SkinnedMesh, const pxr::UsdSkelSkeletonQuery& SkeletonQuery )
	{
		pxr::UsdPrim SkinnedPrim = SkinnedMesh.GetPrim();
		if ( !SkinnedPrim )
		{
			return {};
		}

		const pxr::UsdSkelAnimQuery& AnimQuery = SkeletonQuery.GetAnimQuery();

		pxr::UsdSkelBindingAPI SkelBindingAPI{ SkinnedPrim };

		return pxr::UsdSkelSkinningQuery(
			SkinnedPrim,
			SkeletonQuery ? SkeletonQuery.GetJointOrder() : pxr::VtTokenArray(),
			AnimQuery ? AnimQuery.GetBlendShapeOrder() : pxr::VtTokenArray(),
			SkelBindingAPI.GetJointIndicesAttr(),
			SkelBindingAPI.GetJointWeightsAttr(),
			SkelBindingAPI.GetGeomBindTransformAttr(),
			SkelBindingAPI.GetJointsAttr(),
			SkelBindingAPI.GetBlendShapesAttr(),
			SkelBindingAPI.GetBlendShapeTargetsRel()
		);
	}

	bool LoadAllSkeletalData(
		pxr::UsdSkelCache& InSkeletonCache,
		const pxr::UsdSkelRoot& InSkeletonRoot,
		TArray<FSkeletalMeshImportData>& OutLODIndexToSkeletalMeshImportData,
		TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& OutLODIndexToMaterialInfo,
		TArray<SkeletalMeshImportData::FBone>& OutSkeletonBones,
		UsdUtils::FBlendShapeMap* OutBlendShapes,
		TSet<FString>& InOutUsedMorphTargetNames,
		const TMap< FString, TMap< FString, int32 > >& InMaterialToPrimvarsUVSetNames,
		float InTime,
		const TMap< FString, UObject* >& InPrimPathsToAssets,
		bool bInInterpretLODs
	)
	{
		if ( !InSkeletonRoot )
		{
			return false;
		}

		FScopedUsdAllocs UsdAllocs;

		pxr::UsdStageRefPtr Stage = InSkeletonRoot.GetPrim().GetStage();
		const FUsdStageInfo StageInfo( Stage );

		std::vector< pxr::UsdSkelBinding > SkeletonBindings;
		InSkeletonCache.Populate( InSkeletonRoot );
		InSkeletonCache.ComputeSkelBindings( InSkeletonRoot, &SkeletonBindings );
		if ( SkeletonBindings.size() < 1 )
		{
			FUsdLogManager::LogMessage( EMessageSeverity::Error,
										FText::Format( LOCTEXT("InvalidBinding", "SkelRoot {0} doesn't have any binding. No skinned mesh will be generated."),
										FText::FromString( UsdToUnreal::ConvertPath( InSkeletonRoot.GetPath() ) ) ) );
			return false;
		}

		// Note that there could be multiple skeleton bindings under the SkeletonRoot
		// For now, extract just the first one
		const pxr::UsdSkelBinding& SkeletonBinding = SkeletonBindings[0];
		const pxr::UsdSkelSkeleton& Skeleton = SkeletonBinding.GetSkeleton();
		if ( SkeletonBindings.size() > 1 )
		{
			UE_LOG(LogUsd, Warning, TEXT("Currently only a single skeleton is supported per UsdSkelRoot! '%s' will use skeleton '%s'"),
				*UsdToUnreal::ConvertPath( InSkeletonRoot.GetPrim().GetPath() ),
				*UsdToUnreal::ConvertPath( Skeleton.GetPrim().GetPath() )
			);
		}

		// Import skeleton data
		pxr::UsdSkelSkeletonQuery SkelQuery = InSkeletonCache.GetSkelQuery( Skeleton );
		{
			FSkeletalMeshImportData DummyImportData;
			const bool bSkeletonValid = UsdToUnreal::ConvertSkeleton( SkelQuery, DummyImportData );
			if ( !bSkeletonValid )
			{
				return false;
			}
			OutSkeletonBones = MoveTemp(DummyImportData.RefBonesBinary);
		}

		TMap<int32, FSkeletalMeshImportData> LODIndexToSkeletalMeshImportDataMap;
		TMap<int32, UsdUtils::FUsdPrimMaterialAssignmentInfo> LODIndexToMaterialInfoMap;
		TSet<FString> ProcessedLODParentPaths;

		// Since we may need to switch variants to parse LODs, we could invalidate references to SkinningQuery objects, so we need
		// to keep track of these by path and construct one whenever we need them
		TArray<pxr::SdfPath> PathsToSkinnedPrims;
		for ( const pxr::UsdSkelSkinningQuery& SkinningQuery : SkeletonBinding.GetSkinningTargets() )
		{
			// In USD, the skinning target need not be a mesh, but for Unreal we are only interested in skinning meshes
			if ( pxr::UsdGeomMesh SkinningMesh = pxr::UsdGeomMesh( SkinningQuery.GetPrim() ) )
			{
				PathsToSkinnedPrims.Add(SkinningMesh.GetPrim().GetPath());
			}
		}

		TFunction<bool( const pxr::UsdGeomMesh&, int32 )> ConvertLOD =
		[ &LODIndexToSkeletalMeshImportDataMap, &LODIndexToMaterialInfoMap, &SkelQuery, InTime, &InMaterialToPrimvarsUVSetNames, &InOutUsedMorphTargetNames, OutBlendShapes, &StageInfo ]
		( const pxr::UsdGeomMesh& LODMesh, int32 LODIndex )
		{
			pxr::UsdSkelSkinningQuery SkinningQuery = CreateSkinningQuery( LODMesh, SkelQuery );
			if ( !SkinningQuery )
			{
				return true; // Continue trying other LODs
			}

			pxr::GfMatrix4d GeomBindTransformUSD = SkinningQuery.GetGeomBindTransform( pxr::UsdTimeCode( InTime ) );
			FTransform AdditionalTransform = FTransform( UsdToUnreal::ConvertMatrix( StageInfo, GeomBindTransformUSD ) );

			FSkeletalMeshImportData& LODImportData = LODIndexToSkeletalMeshImportDataMap.FindOrAdd( LODIndex );
			TArray<UsdUtils::FUsdPrimMaterialSlot>& LODSlots = LODIndexToMaterialInfoMap.FindOrAdd( LODIndex ).Slots;

			// BlendShape data is respective to point indices for each mesh in isolation, but we combine all points
			// into one FSkeletalMeshImportData per LOD, so we need to remap the indices using this
			uint32 NumPointsBeforeThisMesh = static_cast< uint32 >( LODImportData.Points.Num() );

			bool bSuccess = UsdToUnreal::ConvertSkinnedMesh( SkinningQuery, AdditionalTransform, LODImportData, LODSlots, InMaterialToPrimvarsUVSetNames );
			if ( !bSuccess )
			{
				return true;
			}

			if ( OutBlendShapes )
			{
				pxr::UsdSkelBindingAPI SkelBindingAPI{ LODMesh.GetPrim() };
				pxr::UsdSkelBlendShapeQuery BlendShapeQuery{ SkelBindingAPI };
				if ( BlendShapeQuery )
				{
					for ( int32 BlendShapeIndex = 0; BlendShapeIndex < BlendShapeQuery.GetNumBlendShapes(); ++BlendShapeIndex )
					{
						UsdToUnreal::ConvertBlendShape(
							BlendShapeQuery.GetBlendShape( BlendShapeIndex ),
							StageInfo,
							LODIndex,
							AdditionalTransform,
							NumPointsBeforeThisMesh,
							InOutUsedMorphTargetNames,
							*OutBlendShapes
						);
					}
				}
			}

			return true;
		};

		// Actually parse all mesh data
		for ( const pxr::SdfPath& SkinnedPrimPath : PathsToSkinnedPrims )
		{
			pxr::UsdGeomMesh SkinnedMesh{ Stage->GetPrimAtPath( SkinnedPrimPath ) };
			if ( !SkinnedMesh )
			{
				continue;
			}

			pxr::UsdPrim ParentPrim = SkinnedMesh.GetPrim().GetParent();
			FString ParentPrimPath = UsdToUnreal::ConvertPath(ParentPrim.GetPath());

			bool bInterpretedLODs = false;
			if ( bInInterpretLODs && ParentPrim && !ProcessedLODParentPaths.Contains(ParentPrimPath))
			{
				// At the moment we only consider a single mesh per variant, so if multiple meshes tell us to process the same parent prim, we skip.
				// This check would also prevent us from getting in here in case we just have many meshes children of a same prim, outside
				// of a variant. In this case they don't fit the "one mesh per variant" pattern anyway, and we want to fallback to ignoring LODs
				ProcessedLODParentPaths.Add(ParentPrimPath);

				// WARNING: After this is called, references to objects that were inside any of the LOD Meshes will be invalidated!
				bInterpretedLODs = UsdUtils::IterateLODMeshes(ParentPrim, ConvertLOD);
			}

			if ( !bInterpretedLODs )
			{
				// Refresh reference to this prim as it could have been inside a variant that was temporarily switched by IterateLODMeshes
				ConvertLOD( pxr::UsdGeomMesh{ Stage->GetPrimAtPath( SkinnedPrimPath ) }, 0);
			}
		}

		// Place the LODs in order as we can't have e.g. LOD0 and LOD2 without LOD1, and there's no reason downstream code needs to care about
		// what LOD number these data originally wanted to be
		TMap<int32, int32> OldLODIndexToNewLODIndex;
		LODIndexToSkeletalMeshImportDataMap.KeySort( TLess<int32>() );
		OutLODIndexToSkeletalMeshImportData.Reset( LODIndexToSkeletalMeshImportDataMap.Num() );
		OutLODIndexToMaterialInfo.Reset( LODIndexToMaterialInfoMap.Num() );
		for ( TPair<int32, FSkeletalMeshImportData>& Entry : LODIndexToSkeletalMeshImportDataMap )
		{
			FSkeletalMeshImportData& ImportData = Entry.Value;
			if ( Entry.Value.Points.Num() == 0 )
			{
				continue;
			}

			const int32 OldLODIndex = Entry.Key;
			const int32 NewLODIndex = OutLODIndexToSkeletalMeshImportData.Add( MoveTemp( ImportData ) );
			OutLODIndexToMaterialInfo.Add( LODIndexToMaterialInfoMap[ OldLODIndex ] );

			// Keep track of these to remap blendshapes
			OldLODIndexToNewLODIndex.Add( OldLODIndex, NewLODIndex );
		}
		if ( OutBlendShapes )
		{
			for ( auto& Pair : *OutBlendShapes )
			{
				UsdUtils::FUsdBlendShape& BlendShape = Pair.Value;

				TSet<int32> NewLODIndexUsers;
				NewLODIndexUsers.Reserve(BlendShape.LODIndicesThatUseThis.Num());

				for ( int32 OldLODIndexUser : BlendShape.LODIndicesThatUseThis )
				{
					if ( int32* FoundNewLODIndex = OldLODIndexToNewLODIndex.Find( OldLODIndexUser ) )
					{
						NewLODIndexUsers.Add(*FoundNewLODIndex);
					}
					else
					{
						UE_LOG(LogUsd, Error, TEXT("Failed to remap blend shape '%s's LOD index '%d'"), *BlendShape.Name, OldLODIndexUser );
					}
				}

				BlendShape.LODIndicesThatUseThis = MoveTemp(NewLODIndexUsers);
			}
		}

		return true;
	}

	class FSkelRootCreateAssetsTaskChain : public FUsdSchemaTranslatorTaskChain
	{
	public:
		explicit FSkelRootCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath );

	protected:
		// Inputs
		UE::FSdfPath PrimPath;
		TSharedRef< FUsdSchemaTranslationContext > Context;

		// Outputs
		TArray<FSkeletalMeshImportData> LODIndexToSkeletalMeshImportData;
		TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo> LODIndexToMaterialInfo;
		TArray<SkeletalMeshImportData::FBone> SkeletonBones;
		UsdUtils::FBlendShapeMap NewBlendShapes;

		// Note that we want this to be case insensitive so that our UMorphTarget FNames are unique not only due to case differences
		TSet<FString> UsedMorphTargetNames;
		TUsdStore< pxr::UsdSkelCache > SkeletonCache;

		// Don't keep a live reference to the prim because other translators may mutate the stage in an ExclusiveSync translation step, invalidating the reference
		UE::FUsdPrim GetPrim() const { return Context->Stage.GetPrimAtPath( PrimPath ); }

		void SetupTasks();
	};

	FSkelRootCreateAssetsTaskChain::FSkelRootCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath )
		: PrimPath( InPrimPath )
		, Context( InContext )
	{
		SetupTasks();
	}

	void FSkelRootCreateAssetsTaskChain::SetupTasks()
	{
		// Ignore prims from disabled purposes
		if ( !EnumHasAllFlags( Context->PurposesToLoad, IUsdPrim::GetPurpose( GetPrim() ) ) )
		{
			return;
		}

		// To parse all LODs we need to actively switch variant sets to other variants (triggering prim loading/unloading and notices),
		// which could cause race conditions if other async translation tasks are trying to access those prims
		ESchemaTranslationLaunchPolicy LaunchPolicy = ESchemaTranslationLaunchPolicy::Async;
		if ( Context->bAllowInterpretingLODs && UsdUtils::IsGeomMeshALOD( pxr::UsdGeomMesh( GetPrim() ) ) )
		{
			LaunchPolicy = ESchemaTranslationLaunchPolicy::ExclusiveSync;
		}

		// Create SkeletalMeshImportData (Async or ExclusiveSync)
		Do( LaunchPolicy,
			[ this ]()
			{
				// No point in importing blend shapes if the import context doesn't want them
				UsdUtils::FBlendShapeMap* OutBlendShapes = Context->BlendShapesByPath ? &NewBlendShapes : nullptr;

				const bool bContinueTaskChain = UsdSkelRootTranslatorImpl::LoadAllSkeletalData(
					SkeletonCache.Get(),
					pxr::UsdSkelRoot( GetPrim() ),
					LODIndexToSkeletalMeshImportData,
					LODIndexToMaterialInfo,
					SkeletonBones,
					OutBlendShapes,
					UsedMorphTargetNames,
					Context->MaterialToPrimvarToUVIndex,
					Context->Time,
					Context->PrimPathsToAssets,
					Context->bAllowInterpretingLODs
				);

				return bContinueTaskChain;
			} );

		// Create USkeletalMesh (Main thread)
		Then( ESchemaTranslationLaunchPolicy::Sync,
			[ this ]()
			{
				FSHAHash SkeletalMeshHash = UsdSkelRootTranslatorImpl::ComputeSHAHash( LODIndexToSkeletalMeshImportData );

				USkeletalMesh* SkeletalMesh = Cast< USkeletalMesh >( Context->AssetsCache.FindRef( SkeletalMeshHash.ToString() ) );

				if ( !SkeletalMesh )
				{
					SkeletalMesh = UsdToUnreal::GetSkeletalMeshFromImportData( LODIndexToSkeletalMeshImportData, SkeletonBones, NewBlendShapes, Context->ObjectFlags );
				}

				if ( SkeletalMesh )
				{
					UsdSkelRootTranslatorImpl::ProcessMaterials(
						LODIndexToMaterialInfo,
						SkeletalMesh,
						Context->Stage,
						Context->PrimPathsToAssets,
						Context->AssetsCache,
						Context->Time,
						Context->ObjectFlags,
						Context->BlendShapesByPath && Context->BlendShapesByPath->Num() > 0
					);

					// Now that the materials are in the mesh, we need to update the UV data
					// We couldn't have had any materials before, so the previous call did nothing, meaning we must rebuild all
					const bool bRebuildAll = true;
					SkeletalMesh->UpdateUVChannelData( bRebuildAll );

					FString SkelRootPath = PrimPath.GetString();

					UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( SkeletalMesh, TEXT( "USDAssetImportData" ) );
					ImportData->PrimPath = SkelRootPath;
					SkeletalMesh->AssetImportData = ImportData;

					Context->AssetsCache.Add( SkeletalMeshHash.ToString(), SkeletalMesh );
					Context->AssetsCache.Add( SkeletalMeshHash.ToString() + TEXT( "_Skeleton" ), SkeletalMesh->Skeleton );
					Context->PrimPathsToAssets.Add( SkelRootPath, SkeletalMesh );

					if ( Context->BlendShapesByPath )
					{
						Context->BlendShapesByPath->Append( NewBlendShapes );
					}
				}

				return true;
			} );

		// Create UAnimSequences (requires a completed USkeleton. Main thread as some steps of the animation compression require it)
		Then( ESchemaTranslationLaunchPolicy::Sync,
			[ this ]()
			{
				if ( !Context->bAllowParsingSkeletalAnimations )
				{
					return false;
				}

				USkeletalMesh* SkeletalMesh = Cast< USkeletalMesh >( Context->PrimPathsToAssets.FindRef( PrimPath.GetString() ) );
				if ( !SkeletalMesh )
				{
					return false;
				}

				FScopedUsdAllocs Allocs;

				if ( pxr::UsdSkelRoot SkeletonRoot{ GetPrim() } )
				{
					std::vector< pxr::UsdSkelBinding > SkeletonBindings;
					SkeletonCache.Get().ComputeSkelBindings( SkeletonRoot, &SkeletonBindings );

					for ( const pxr::UsdSkelBinding& Binding : SkeletonBindings )
					{
						const pxr::UsdSkelSkeleton& Skeleton = Binding.GetSkeleton();
						pxr::UsdSkelSkeletonQuery SkelQuery = SkeletonCache.Get().GetSkelQuery( Skeleton );
						pxr::UsdSkelAnimQuery AnimQuery = SkelQuery.GetAnimQuery();
						if ( !AnimQuery )
						{
							continue;
						}

						if ( !AnimQuery.JointTransformsMightBeTimeVarying() )
						{
							continue;
						}

						pxr::UsdPrim SkelAnimPrim = AnimQuery.GetPrim();

						FScopedUnrealAllocs UEAllocs;

						UAnimSequence* AnimSequence = NewObject<UAnimSequence>( GetTransientPackage(), *UsdToUnreal::ConvertString( SkelAnimPrim.GetName() ), Context->ObjectFlags );
						AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);

						UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( AnimSequence, TEXT( "USDAssetImportData" ) );
						ImportData->PrimPath = GetPrim().GetPrimPath().GetString(); // Point to the SkelRoot so that it ends up next to the skeletal mesh
						AnimSequence->AssetImportData = ImportData;

						UsdToUnreal::ConvertSkelAnim( SkelQuery, &NewBlendShapes, AnimSequence );

						Context->AssetsCache.Add( AnimSequence->GetRawDataGuid().ToString(), AnimSequence );
					}
				}

				return true;
			});
	}
}

void FUsdSkelRootTranslator::CreateAssets()
{
	TSharedRef< UsdSkelRootTranslatorImpl::FSkelRootCreateAssetsTaskChain > AssetsTaskChain =
		MakeShared< UsdSkelRootTranslatorImpl::FSkelRootCreateAssetsTaskChain >( Context, PrimPath );

	Context->TranslatorTasks.Add( MoveTemp( AssetsTaskChain ) );
}

USceneComponent* FUsdSkelRootTranslator::CreateComponents()
{
	USceneComponent* RootComponent = FUsdGeomXformableTranslator::CreateComponents();

	if ( USkinnedMeshComponent* SkinnedMeshComponent = Cast< USkinnedMeshComponent >( RootComponent ) )
	{
		USkeletalMesh* SkeletalMesh = Cast< USkeletalMesh >( Context->PrimPathsToAssets.FindRef( PrimPath.GetString() ) );
		SkinnedMeshComponent->SetSkeletalMesh( SkeletalMesh );

		UpdateComponents( SkinnedMeshComponent );
	}

	return RootComponent;
}

void FUsdSkelRootTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	UPoseableMeshComponent* PoseableMeshComponent = Cast< UPoseableMeshComponent >( SceneComponent );
	if ( !PoseableMeshComponent || !PoseableMeshComponent->SkeletalMesh )
	{
		return;
	}

	Super::UpdateComponents( SceneComponent );

	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdPrim Prim = GetPrim();

		pxr::UsdSkelCache SkeletonCache;
		pxr::UsdSkelRoot SkeletonRoot( Prim );
		SkeletonCache.Populate( SkeletonRoot );

		std::vector< pxr::UsdSkelBinding > SkeletonBindings;
		SkeletonCache.ComputeSkelBindings( SkeletonRoot, &SkeletonBindings );

		if ( SkeletonBindings.size() == 0 )
		{
			return;
		}

		const FUsdStageInfo StageInfo( Prim.GetStage() );

		pxr::UsdGeomXformable Xformable( Prim );

		std::vector< double > TimeSamples;

		// Note that there could be multiple skeleton bindings under the SkeletonRoot
		// For now, extract just the first one
		for ( const pxr::UsdSkelBinding& Binding : SkeletonBindings )
		{
			const pxr::UsdSkelSkeleton& Skeleton = Binding.GetSkeleton();
			pxr::UsdSkelSkeletonQuery SkelQuery = SkeletonCache.GetSkelQuery( Skeleton );
			if ( !SkelQuery )
			{
				continue;
			}

			const pxr::UsdSkelAnimQuery& AnimQuery = SkelQuery.GetAnimQuery();
			if ( !AnimQuery )
			{
				continue;
			}

			const bool bRes = AnimQuery.GetJointTransformTimeSamples( &TimeSamples );

			if ( TimeSamples.size() > 0 )
			{
				FScopedUnrealAllocs UnrealAllocs;

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

			// Update blend shape weights
			pxr::VtArray< float > Weights;
			if ( Context->BlendShapesByPath && AnimQuery.ComputeBlendShapeWeights( &Weights, pxr::UsdTimeCode( Context->Time ) ) )
			{
				for ( const pxr::UsdSkelSkinningQuery& SkinningQuery : Binding.GetSkinningTargets() )
				{
					if ( !SkinningQuery )
					{
						continue;
					}

					// Our SkelAnimation may have blend shapes ["A", "B', "C", "D"], but our Mesh may only use blendShapes ["D", "A"],
					// so we use this to map the weights into the right order for this prim
					const pxr::UsdSkelAnimMapperRefPtr& BlendShapeMapper = SkinningQuery.GetBlendShapeMapper();
					if ( !BlendShapeMapper )
					{
						continue;
					}
					pxr::VtFloatArray WeightsInPrimOrder;
					BlendShapeMapper->Remap( Weights, &WeightsInPrimOrder );

					// Each prim may "listen" to the SkelAnim's 'skel:blendShapes' properties ["D", "A"], but actually map
					// these like this: 'rel skel:blendShapeTargets = [<BlendShape1>, <BlendShape4>]'
					// Here we get the BlendShapeTargets array, that will look like [<BlendShape1>, <BlendShape4>]
					pxr::SdfPathVector BlendShapeTargets;
					const pxr::UsdRelationship& BlendShapeTargetsRel = SkinningQuery.GetBlendShapeTargetsRel();
					BlendShapeTargetsRel.GetTargets( &BlendShapeTargets );

					int32 NumWeights = static_cast< int32 >( WeightsInPrimOrder.size() );
					if ( NumWeights != static_cast< int32 >( BlendShapeTargets.size() ) )
					{
						continue;
					}

					pxr::SdfPath MeshPath = SkinningQuery.GetPrim().GetPath();

					for ( int32 WeightIndex = 0; WeightIndex < NumWeights; ++WeightIndex )
					{
						FString PrimaryBlendShapePath = UsdToUnreal::ConvertPath( BlendShapeTargets[ WeightIndex ].MakeAbsolutePath( MeshPath ) );
						float InputWeight = WeightsInPrimOrder[ WeightIndex ];

						if ( UsdUtils::FUsdBlendShape* FoundPrimaryBlendShape = Context->BlendShapesByPath->Find( PrimaryBlendShapePath ) )
						{
							float PrimaryWeight = 0.0f;
							TArray<float> InbetweenWeights;
							UsdUtils::ResolveWeightsForBlendShape( *FoundPrimaryBlendShape, InputWeight, PrimaryWeight, InbetweenWeights );

							for ( int32 InbetweenIndex = 0; InbetweenIndex < FoundPrimaryBlendShape->Inbetweens.Num(); ++InbetweenIndex )
							{
								const UsdUtils::FUsdBlendShapeInbetween& Inbetween = FoundPrimaryBlendShape->Inbetweens[ InbetweenIndex ];
								float InbetweenWeight = InbetweenWeights[ InbetweenIndex ];

								UsdSkelRootTranslatorImpl::SetMorphTargetWeight( *PoseableMeshComponent, Inbetween.Name, InbetweenWeight );
							}

							UsdSkelRootTranslatorImpl::SetMorphTargetWeight( *PoseableMeshComponent, FoundPrimaryBlendShape->Name, PrimaryWeight );
						}
					}
				}

				if ( Weights.size() > 0 )
				{
					PoseableMeshComponent->MarkRenderDynamicDataDirty();
				}
			}

		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
