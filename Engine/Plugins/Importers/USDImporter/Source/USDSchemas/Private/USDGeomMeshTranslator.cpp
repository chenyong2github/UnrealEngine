// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomMeshTranslator.h"

#if USE_USD_SDK

#include "MeshTranslationImpl.h"
#include "UnrealUSDWrapper.h"
#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDLog.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"

#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GeometryCache.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "RHI.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshResources.h"
#include "TessellationRendering.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "GeometryCacheTrackUSD.h"
#include "GeometryCacheUSDComponent.h"
#include "IMeshBuilderModule.h"
#endif // WITH_EDITOR

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/xformable.h"
	#include "pxr/usd/usdShade/material.h"
#include "USDIncludesEnd.h"

#if WITH_EDITOR  // The GeometryCacheStreamer module is editor-only
// Can toggle on/off to compare performance with StaticMesh instead of GeometryCache
static bool GUseGeometryCacheUSD = true;

static FAutoConsoleVariableRef CVarUsdUseGeometryCache(
	TEXT("USD.UseGeometryCache"),
	GUseGeometryCacheUSD,
	TEXT("Use GeometryCache instead of static meshes for loading animated meshes"));

#endif // WITH_EDITOR

namespace UsdGeomMeshTranslatorImpl
{
	bool IsAnimated( const pxr::UsdPrim& Prim )
	{
		FScopedUsdAllocs UsdAllocs;

		bool bHasAttributesTimeSamples = false;
		{
			constexpr bool bIncludeInherited = false;
			pxr::TfTokenVector GeomMeshAttributeNames = pxr::UsdGeomMesh::GetSchemaAttributeNames( bIncludeInherited );
			pxr::TfTokenVector GeomPointBasedAttributeNames = pxr::UsdGeomPointBased::GetSchemaAttributeNames( bIncludeInherited );

			GeomMeshAttributeNames.reserve( GeomMeshAttributeNames.size() + GeomPointBasedAttributeNames.size() );
			GeomMeshAttributeNames.insert( GeomMeshAttributeNames.end(), GeomPointBasedAttributeNames.begin(), GeomPointBasedAttributeNames.end() );

			for ( const pxr::TfToken& AttributeName : GeomMeshAttributeNames )
			{
				const pxr::UsdAttribute& Attribute = Prim.GetAttribute( AttributeName );

				if ( Attribute.ValueMightBeTimeVarying() )
				{
					bHasAttributesTimeSamples = true;
					break;
				}
			}
		}

		return bHasAttributesTimeSamples;
	}

	/** Returns true if material infos have changed on the StaticMesh */
	bool ProcessMaterials( const pxr::UsdPrim& UsdPrim, const TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& LODIndexToMaterialInfo, UStaticMesh& StaticMesh, UUsdAssetCache& AssetCache, float Time, EObjectFlags Flags )
	{
		bool bMaterialAssignementsHaveChanged = false;

		TArray<UMaterialInterface*> ExistingAssignments;
		for ( const FStaticMaterial& StaticMaterial : StaticMesh.GetStaticMaterials() )
		{
			ExistingAssignments.Add(StaticMaterial.MaterialInterface);
		}

		TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> ResolvedMaterials = MeshTranslationImpl::ResolveMaterialAssignmentInfo(UsdPrim, LODIndexToMaterialInfo, ExistingAssignments, AssetCache, Time, Flags );

		uint32 StaticMeshSlotIndex = 0;
		for ( int32 LODIndex = 0; LODIndex < LODIndexToMaterialInfo.Num(); ++LODIndex )
		{
			const TArray< UsdUtils::FUsdPrimMaterialSlot >& LODSlots = LODIndexToMaterialInfo[ LODIndex ].Slots;

			for ( int32 LODSlotIndex = 0; LODSlotIndex < LODSlots.Num(); ++LODSlotIndex, ++StaticMeshSlotIndex )
			{
				const UsdUtils::FUsdPrimMaterialSlot& Slot = LODSlots[ LODSlotIndex ];

				UMaterialInterface* Material = nullptr;
				if ( UMaterialInterface** FoundMaterial = ResolvedMaterials.Find( &Slot ) )
				{
					Material = *FoundMaterial;
				}
				else
				{
					UE_LOG(LogUsd, Error, TEXT("Failed to resolve material '%s' for slot '%d' of LOD '%d' for mesh '%s'"), *Slot.MaterialSource, LODSlotIndex, LODIndex, *UsdToUnreal::ConvertPath(UsdPrim.GetPath()));
					continue;
				}

				// Create and set the static material
				FStaticMaterial StaticMaterial( Material, *LexToString( StaticMeshSlotIndex ) );
				if ( !StaticMesh.GetStaticMaterials().IsValidIndex( StaticMeshSlotIndex ) )
				{
					StaticMesh.GetStaticMaterials().Add( MoveTemp( StaticMaterial ) );
					bMaterialAssignementsHaveChanged = true;
				}
				else if ( !( StaticMesh.GetStaticMaterials()[ StaticMeshSlotIndex ] == StaticMaterial ) )
				{
					StaticMesh.GetStaticMaterials()[ StaticMeshSlotIndex ] = MoveTemp( StaticMaterial );
					bMaterialAssignementsHaveChanged = true;
				}

#if WITH_EDITOR
				// Setup the section map so that our LOD material index is properly mapped to the static mesh material index
				// At runtime we don't ever parse these variants as LODs so we don't need this
				if ( StaticMesh.GetSectionInfoMap().IsValidSection( LODIndex, LODSlotIndex ) )
				{
					FMeshSectionInfo MeshSectionInfo = StaticMesh.GetSectionInfoMap().Get( LODIndex, LODSlotIndex );

					if ( MeshSectionInfo.MaterialIndex != StaticMeshSlotIndex )
					{
						MeshSectionInfo.MaterialIndex = StaticMeshSlotIndex;
						StaticMesh.GetSectionInfoMap().Set( LODIndex, LODSlotIndex, MeshSectionInfo );

						bMaterialAssignementsHaveChanged = true;
					}
				}
				else
				{
					FMeshSectionInfo MeshSectionInfo;
					MeshSectionInfo.MaterialIndex = StaticMeshSlotIndex;

					StaticMesh.GetSectionInfoMap().Set( LODIndex, LODSlotIndex, MeshSectionInfo );

					bMaterialAssignementsHaveChanged = true;
				}
#endif // WITH_EDITOR
			}
		}

#if WITH_EDITOR
		StaticMesh.GetOriginalSectionInfoMap().CopyFrom( StaticMesh.GetSectionInfoMap() );
#endif // WITH_EDITOR

		return bMaterialAssignementsHaveChanged;
	}

#if WITH_EDITOR
	// #ueent_todo: Merge the code with ProcessMaterials
	bool ProcessGeometryCacheMaterials( const pxr::UsdPrim& UsdPrim, const TArray< UsdUtils::FUsdPrimMaterialAssignmentInfo >& LODIndexToMaterialInfo, UGeometryCache& GeometryCache, UUsdAssetCache& AssetCache, float Time, EObjectFlags Flags)
	{
		bool bMaterialAssignementsHaveChanged = false;

		uint32 StaticMeshSlotIndex = 0;
		for ( int32 LODIndex = 0; LODIndex < LODIndexToMaterialInfo.Num(); ++LODIndex )
		{
			const TArray< UsdUtils::FUsdPrimMaterialSlot >& LODSlots = LODIndexToMaterialInfo[ LODIndex ].Slots;

			for ( int32 LODSlotIndex = 0; LODSlotIndex < LODSlots.Num(); ++LODSlotIndex, ++StaticMeshSlotIndex )
			{
				const UsdUtils::FUsdPrimMaterialSlot& Slot = LODSlots[ LODSlotIndex ];
				UMaterialInterface* Material = nullptr;

				switch ( Slot.AssignmentType )
				{
				case UsdUtils::EPrimAssignmentType::DisplayColor:
				{
					FScopedUsdAllocs Allocs;

					// Try reusing an already created DisplayColor material
					if ( UMaterialInstanceConstant* ExistingMaterial = Cast< UMaterialInstanceConstant >( AssetCache.GetCachedAsset( Slot.MaterialSource ) ) )
					{
						Material = ExistingMaterial;
					}

					// Need to create a new DisplayColor material
					if ( Material == nullptr )
					{
						if ( TOptional< UsdUtils::FDisplayColorMaterial > DisplayColorDesc = UsdUtils::FDisplayColorMaterial::FromString( Slot.MaterialSource ) )
						{
							UMaterialInstance* MaterialInstance = nullptr;

							if ( GIsEditor )  // Editor, PIE => true; Standlone, packaged => false
							{
								MaterialInstance = UsdUtils::CreateDisplayColorMaterialInstanceConstant( DisplayColorDesc.GetValue() );

								// Leave PrimPath as empty as it likely will be reused by many prims
								UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( MaterialInstance, TEXT( "USDAssetImportData" ) );
								MaterialInstance->AssetImportData = ImportData;
							}
							else
							{
								MaterialInstance = UsdUtils::CreateDisplayColorMaterialInstanceDynamic( DisplayColorDesc.GetValue() );
							}

							AssetCache.CacheAsset( Slot.MaterialSource, MaterialInstance );
							Material = MaterialInstance;
						}
					}

					break;
				}
				case UsdUtils::EPrimAssignmentType::MaterialPrim:
				{
					FScopedUsdAllocs Allocs;

					// Check first or else we may get a warning
					if ( pxr::SdfPath::IsValidPathString( UnrealToUsd::ConvertString( *Slot.MaterialSource ).Get() ) )
					{
						pxr::SdfPath MaterialPrimPath = UnrealToUsd::ConvertPath( *Slot.MaterialSource ).Get();

						// TODO: This may break if MaterialPrimPath targets a prim inside a LOD variant that is disabled...
						TUsdStore< pxr::UsdPrim > MaterialPrim = UsdPrim.GetStage()->GetPrimAtPath( MaterialPrimPath );
						if ( MaterialPrim.Get() )
						{
							Material = Cast< UMaterialInterface >( AssetCache.GetAssetForPrim( UsdToUnreal::ConvertPath( MaterialPrim.Get().GetPrimPath() ) ) );
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
				{
					// Check if there is a material already on the mesh
					UMaterialInstanceConstant* ExistingMaterialInstance = GeometryCache.Materials.IsValidIndex( StaticMeshSlotIndex ) ? Cast< UMaterialInstanceConstant >( GeometryCache.Materials[ StaticMeshSlotIndex ] ) : nullptr;

					// Assuming that we own the material instance and that we can change it as we wish, reuse it
					if ( ExistingMaterialInstance && ExistingMaterialInstance->GetOuter() == GetTransientPackage() )
					{
#if WITH_EDITOR
						UUsdAssetImportData* AssetImportData = Cast< UUsdAssetImportData >( ExistingMaterialInstance->AssetImportData );
						if ( AssetImportData && AssetImportData->PrimPath == UsdToUnreal::ConvertPath( UsdPrim.GetPrimPath() ) )
#endif // WITH_EDITOR
						{
							// If we have displayColor data on our prim, repurpose this material to show it
							if ( TOptional< UsdUtils::FDisplayColorMaterial > DisplayColorDescription = UsdUtils::ExtractDisplayColorMaterial( pxr::UsdGeomMesh( UsdPrim ) ) )
							{
								UsdToUnreal::ConvertDisplayColor( DisplayColorDescription.GetValue(), *ExistingMaterialInstance );
							}

							Material = ExistingMaterialInstance;
						}
					}
					break;
				}
				}

				// Fallback to this UsdGeomMesh DisplayColor material if present
				if ( Material == nullptr )
				{
					FScopedUsdAllocs Allocs;

					// Try reusing an already created DisplayColor material
					if ( UMaterialInstanceConstant* ExistingMaterial = Cast< UMaterialInstanceConstant >( AssetCache.GetCachedAsset( Slot.MaterialSource ) ) )
					{
						Material = ExistingMaterial;
					}

					// Need to create a new DisplayColor material
					if ( Material == nullptr )
					{
						if ( TOptional< UsdUtils::FDisplayColorMaterial > DisplayColorDesc = UsdUtils::ExtractDisplayColorMaterial( pxr::UsdGeomMesh( UsdPrim ) ) )
						{
							UMaterialInstance* MaterialInstance = nullptr;

							if ( GIsEditor )  // Editor, PIE => true; Standlone, packaged => false
							{
								MaterialInstance = UsdUtils::CreateDisplayColorMaterialInstanceConstant( DisplayColorDesc.GetValue() );

								// Leave PrimPath as empty as it likely will be reused by many prims
								UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( MaterialInstance, TEXT( "USDAssetImportData" ) );
								MaterialInstance->AssetImportData = ImportData;
							}
							else
							{
								MaterialInstance = UsdUtils::CreateDisplayColorMaterialInstanceDynamic( DisplayColorDesc.GetValue() );
							}

							AssetCache.CacheAsset( Slot.MaterialSource, MaterialInstance );
							Material = MaterialInstance;
						}
					}
				}

				if ( !GeometryCache.Materials.IsValidIndex( StaticMeshSlotIndex ) )
				{
					GeometryCache.Materials.Add( Material );
					bMaterialAssignementsHaveChanged = true;
				}
				else if ( !( GeometryCache.Materials[ StaticMeshSlotIndex ] == Material ) )
				{
					GeometryCache.Materials[ StaticMeshSlotIndex ] = Material;
					bMaterialAssignementsHaveChanged = true;
				}
			}
		}

		return bMaterialAssignementsHaveChanged;
	}
#endif // WITH_EDITOR

	// If UsdMesh is a LOD, will parse it and all of the other LODs, and and place them in OutLODIndexToMeshDescription and OutLODIndexToMaterialInfo.
	// Note that these other LODs will be hidden in other variants, and won't show up on traversal unless we actively switch the variants (which we do here).
	// We use a separate function for this because there is a very specific set of conditions where we successfully can do this, and we
	// want to fall back to just parsing UsdMesh as a simple single-LOD mesh if we fail.
	bool TryLoadingMultipleLODs( const pxr::UsdTyped& UsdMesh, TArray<FMeshDescription>& OutLODIndexToMeshDescription, TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& OutLODIndexToMaterialInfo, const TMap< FString, TMap< FString, int32 > >& InMaterialToPrimvarToUVIndex, const pxr::UsdTimeCode InTimeCode )
	{
		FScopedUsdAllocs Allocs;

		pxr::UsdPrim UsdMeshPrim = UsdMesh.GetPrim();
		if ( !UsdMeshPrim )
		{
			return false;
		}

		pxr::UsdPrim ParentPrim = UsdMeshPrim.GetParent();
		if ( !ParentPrim )
		{
			return false;
		}

		TMap<int32, FMeshDescription> LODIndexToMeshDescriptionMap;
		TMap<int32, UsdUtils::FUsdPrimMaterialAssignmentInfo> LODIndexToMaterialInfoMap;

		TFunction<bool( const pxr::UsdGeomMesh&, int32 )> ConvertLOD = [ & ]( const pxr::UsdGeomMesh& LODMesh, int32 LODIndex )
		{
			FMeshDescription TempMeshDescription;
			UsdUtils::FUsdPrimMaterialAssignmentInfo TempMaterialInfo;

			FStaticMeshAttributes StaticMeshAttributes( TempMeshDescription );
			StaticMeshAttributes.Register();

			bool bSuccess = UsdToUnreal::ConvertGeomMesh( LODMesh, TempMeshDescription, TempMaterialInfo, FTransform::Identity, InMaterialToPrimvarToUVIndex, InTimeCode );
			if ( bSuccess )
			{
				LODIndexToMeshDescriptionMap.Add( LODIndex, MoveTemp( TempMeshDescription ) );
				LODIndexToMaterialInfoMap.Add( LODIndex, MoveTemp( TempMaterialInfo ) );
			}

			return true;
		};
		bool bFoundLODs = UsdUtils::IterateLODMeshes( ParentPrim, ConvertLOD );

		// Place them in order as we can't have e.g. LOD0 and LOD2 without LOD1, and there's no reason downstream code needs to care about this
		OutLODIndexToMeshDescription.Reset( LODIndexToMeshDescriptionMap.Num() );
		OutLODIndexToMaterialInfo.Reset( LODIndexToMaterialInfoMap.Num() );
		LODIndexToMeshDescriptionMap.KeySort( TLess<int32>() );
		for ( TPair<int32, FMeshDescription>& Entry : LODIndexToMeshDescriptionMap )
		{
			const int32 OldLODIndex = Entry.Key;
			OutLODIndexToMeshDescription.Add( MoveTemp( Entry.Value ) );
			OutLODIndexToMaterialInfo.Add( MoveTemp( LODIndexToMaterialInfoMap[ OldLODIndex ] ) );
		}

		return bFoundLODs;
	}

	void LoadMeshDescriptions( const pxr::UsdTyped& UsdMesh, TArray<FMeshDescription>& OutLODIndexToMeshDescription, TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& OutLODIndexToMaterialInfo,
		const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarToUVIndex, const pxr::UsdTimeCode TimeCode, bool bInterpretLODs, const FName& RenderContext )
	{
		if ( !UsdMesh )
		{
			return;
		}

		bool bInterpretedLODs = false;
		if ( bInterpretLODs )
		{
			bInterpretedLODs = TryLoadingMultipleLODs( UsdMesh, OutLODIndexToMeshDescription, OutLODIndexToMaterialInfo, MaterialToPrimvarToUVIndex, TimeCode );
		}

		if ( !bInterpretedLODs )
		{
			FMeshDescription TempMeshDescription;
			UsdUtils::FUsdPrimMaterialAssignmentInfo TempMaterialInfo;

			FStaticMeshAttributes StaticMeshAttributes( TempMeshDescription );
			StaticMeshAttributes.Register();

			pxr::TfToken RenderContextToken = pxr::UsdShadeTokens->universalRenderContext;
			if ( !RenderContext.IsNone() )
			{
				RenderContextToken = UnrealToUsd::ConvertToken( *RenderContext.ToString() ).Get();
			}

			bool bSuccess = UsdToUnreal::ConvertGeomMesh( UsdMesh, TempMeshDescription, TempMaterialInfo, FTransform::Identity, MaterialToPrimvarToUVIndex, TimeCode, RenderContextToken );

			if ( bSuccess )
			{
				OutLODIndexToMeshDescription = { MoveTemp( TempMeshDescription ) };
				OutLODIndexToMaterialInfo = { MoveTemp( TempMaterialInfo ) };
			}
		}
	}

	UStaticMesh* CreateStaticMesh( TArray<FMeshDescription>& LODIndexToMeshDescription, FUsdSchemaTranslationContext& Context, bool& bOutIsNew )
	{
		UStaticMesh* StaticMesh = nullptr;

		bool bHasValidMeshDescription = false;

		FSHAHash AllLODHash;
		FSHA1 SHA1;
		for (const FMeshDescription& MeshDescription : LODIndexToMeshDescription )
		{
			FSHAHash LODHash = FStaticMeshOperations::ComputeSHAHash( MeshDescription );
			SHA1.Update( &LODHash.Hash[0], sizeof(LODHash.Hash) );

			if ( !MeshDescription.IsEmpty() )
			{
				bHasValidMeshDescription = true;
			}
		}
		SHA1.Final();
		SHA1.GetHash(&AllLODHash.Hash[0]);

		StaticMesh = Cast< UStaticMesh >( Context.AssetCache->GetCachedAsset( AllLODHash.ToString() ) );

		if ( !StaticMesh && bHasValidMeshDescription )
		{
			bOutIsNew = true;

			StaticMesh = NewObject< UStaticMesh >( GetTransientPackage(), NAME_None, Context.ObjectFlags | EObjectFlags::RF_Public );

#if WITH_EDITOR
			for ( int32 LODIndex = 0; LODIndex < LODIndexToMeshDescription.Num(); ++LODIndex )
			{
				FMeshDescription& MeshDescription = LODIndexToMeshDescription[LODIndex];

				// Make sure our normals/tangents can be rebuilt from MeshDescription::InitializeAutoGeneratedAttributes in case some tool needs them
				bool bHasInvalidNormals, bHasInvalidTangents;
				FStaticMeshOperations::AreNormalsAndTangentsValid( MeshDescription, bHasInvalidNormals, bHasInvalidTangents );

				FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
				SourceModel.BuildSettings.bGenerateLightmapUVs = false;
				SourceModel.BuildSettings.bRecomputeNormals = bHasInvalidNormals;
				SourceModel.BuildSettings.bRecomputeTangents = bHasInvalidTangents;
				SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
				SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;

				FMeshDescription* StaticMeshDescription = StaticMesh->CreateMeshDescription(LODIndex);
				check( StaticMeshDescription );
				*StaticMeshDescription = MoveTemp( MeshDescription );
			}
#endif // WITH_EDITOR

			StaticMesh->SetLightingGuid();

			Context.AssetCache->CacheAsset( AllLODHash.ToString(), StaticMesh );
		}
		else
		{
			//FPlatformMisc::LowLevelOutputDebugStringf( TEXT("Mesh found in cache %s\n"), *StaticMesh->GetName() );
			bOutIsNew = false;
		}

		return StaticMesh;
	}

	void PreBuildStaticMesh( UStaticMesh& StaticMesh )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomMeshTranslatorImpl::PreBuildStaticMesh );

		if ( StaticMesh.GetRenderData())
		{
			StaticMesh.ReleaseResources();
			StaticMesh.ReleaseResourcesFence.Wait();
		}

		StaticMesh.SetRenderData(MakeUnique< FStaticMeshRenderData >());
		StaticMesh.CreateBodySetup();
	}

	bool BuildStaticMesh( UStaticMesh& StaticMesh, const FStaticFeatureLevel& FeatureLevel, TArray<FMeshDescription*>& LODIndexToMeshDescription )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomMeshTranslatorImpl::BuildStaticMesh );

		if ( LODIndexToMeshDescription.Num() == 0 )
		{
			return false;
		}

#if WITH_EDITOR
		// Enable adjacency buffer if we're using a material that requires it
		for ( const FStaticMaterial& StaticMaterial : StaticMesh.GetStaticMaterials() )
		{
			if ( StaticMaterial.MaterialInterface )
			{
				if ( RequiresAdjacencyInformation( StaticMaterial.MaterialInterface, nullptr, FeatureLevel ) )
				{
					for ( FStaticMeshSourceModel& SourceModel : StaticMesh.GetSourceModels() )
					{
						SourceModel.BuildSettings.bBuildAdjacencyBuffer = true;
					}
					break;
				}
			}
		}

		ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
		ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
		check( RunningPlatform );

		const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();
		StaticMesh.GetRenderData()->Cache( RunningPlatform, &StaticMesh, LODSettings );
#else
		StaticMesh.GetRenderData()->AllocateLODResources( LODIndexToMeshDescription.Num() );

		// Build render data from each mesh description
		for ( int32 LODIndex = 0; LODIndex < LODIndexToMeshDescription.Num(); ++LODIndex )
		{
			FStaticMeshLODResources& LODResources = StaticMesh.GetRenderData()->LODResources[ LODIndex ];

			FMeshDescription* MeshDescription = LODIndexToMeshDescription[ LODIndex ];
			TVertexInstanceAttributesConstRef< FVector4 > MeshDescriptionColors = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>( MeshAttribute::VertexInstance::Color );

			// Compute normals here if necessary because they're not going to be computed via the regular static mesh build pipeline at runtime
			// (i.e. StaticMeshBuilder is not available at runtime)
			// We need polygon info because ComputeTangentsAndNormals uses it to repair the invalid vertex normals/tangents
			// Can't calculate just the required polygons as ComputeTangentsAndNormals is parallel and we can't guarantee thread-safe access patterns
			FStaticMeshOperations::ComputePolygonTangentsAndNormals( *MeshDescription );
			FStaticMeshOperations::ComputeTangentsAndNormals( *MeshDescription, EComputeNTBsFlags::UseMikkTSpace );

			// Manually set this as it seems the UStaticMesh only sets this whenever the mesh is serialized, which we won't do
			LODResources.bHasColorVertexData = MeshDescriptionColors.GetNumElements() > 0;

			StaticMesh.BuildFromMeshDescription( *MeshDescription, LODResources );
		}

#endif // WITH_EDITOR
		if ( StaticMesh.GetBodySetup())
		{
			StaticMesh.GetBodySetup()->CreatePhysicsMeshes();
		}
		return true;
	}

	void PostBuildStaticMesh( UStaticMesh& StaticMesh, const TArray<FMeshDescription*>& LODIndexToMeshDescription )
	{
		// For runtime builds, the analogue for this stuff is already done from within BuildFromMeshDescriptions
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomMeshTranslatorImpl::PostBuildStaticMesh );

		StaticMesh.InitResources();

#if WITH_EDITOR
		// Fetch the MeshDescription from the StaticMesh because we'll have moved it away from LODIndexToMeshDescription CreateStaticMesh
		if ( const FMeshDescription* MeshDescription = StaticMesh.GetMeshDescription( 0 ) )
		{
			StaticMesh.GetRenderData()->Bounds = MeshDescription->GetBounds();
		}
		StaticMesh.CalculateExtendedBounds();
		StaticMesh.ClearMeshDescriptions(); // Clear mesh descriptions to reduce memory usage, they are kept only in bulk data form
#else
		// Fetch the MeshDescription from the imported LODIndexToMeshDescription as StaticMesh.GetMeshDescription is editor-only
		StaticMesh.GetRenderData()->Bounds = LODIndexToMeshDescription[ 0 ]->GetBounds();
		StaticMesh.CalculateExtendedBounds();
#endif // WITH_EDITOR
	}

#if WITH_EDITOR
	void GeometryCacheDataForMeshDescription( FGeometryCacheMeshData& OutMeshData, FMeshDescription& MeshDescription );

	UGeometryCache* CreateGeometryCache( const FString& InPrimPath, TArray< FMeshDescription >& LODIndexToMeshDescription, TSharedRef< FUsdSchemaTranslationContext> Context, bool& bOutIsNew )
	{
		UGeometryCache* GeometryCache = nullptr;

		bool bHasValidMeshDescription = false;

		FSHAHash AllLODHash;
		FSHA1 SHA1;
		for ( const FMeshDescription& MeshDescription : LODIndexToMeshDescription )
		{
			FSHAHash LODHash = FStaticMeshOperations::ComputeSHAHash( MeshDescription );
			SHA1.Update( &LODHash.Hash[ 0 ], sizeof( LODHash.Hash ) );

			if ( !MeshDescription.IsEmpty() )
			{
				bHasValidMeshDescription = true;
			}
		}
		SHA1.Final();
		SHA1.GetHash( &AllLODHash.Hash[ 0 ] );

		GeometryCache = Cast< UGeometryCache >( Context->AssetCache->GetCachedAsset( AllLODHash.ToString() ) );

		if ( !GeometryCache && bHasValidMeshDescription )
		{
			bOutIsNew = true;

			GeometryCache = NewObject< UGeometryCache >( GetTransientPackage(), NAME_None, Context->ObjectFlags | EObjectFlags::RF_Public );

			// Create and configure a new USDTrack to be added to the GeometryCache
			UGeometryCacheTrackUsd* UsdTrack = NewObject< UGeometryCacheTrackUsd >( GeometryCache );

			UE::FUsdStage Stage = Context->Stage;
			TMap< FString, TMap< FString, int32 > > MaterialToPrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? *Context->MaterialToPrimvarToUVIndex : TMap< FString, TMap< FString, int32 > >();
			const bool bAllowInterpretingLODs = Context->bAllowInterpretingLODs;
			FName RenderContext = Context->RenderContext;
			UsdTrack->Initialize( [ = ]( FGeometryCacheMeshData& OutMeshData, const FString& InPrimPath, float Time )
				{
					// Get MeshDescription associated with the prim
					// #ueent_todo: Replace MeshDescription with RawMesh to optimize
					TArray< FMeshDescription > LODIndexToMeshDescription;
					TArray< UsdUtils::FUsdPrimMaterialAssignmentInfo > LODIndexToMaterialInfo;

					UE::FSdfPath PrimPath( *InPrimPath );
					UE::FUsdPrim Prim = Stage.GetPrimAtPath( PrimPath );

					UsdGeomMeshTranslatorImpl::LoadMeshDescriptions(
						pxr::UsdTyped( Prim ),
						LODIndexToMeshDescription,
						LODIndexToMaterialInfo,
						MaterialToPrimvarToUVIndex,
						pxr::UsdTimeCode( Time ),
						bAllowInterpretingLODs,
						RenderContext
					);

					// Convert the MeshDescription to MeshData
					for ( FMeshDescription& MeshDescription : LODIndexToMeshDescription )
					{
						if ( !MeshDescription.IsEmpty() )
						{
							// Compute the normals and tangents for the mesh
							const float ComparisonThreshold = THRESH_POINTS_ARE_SAME;

							// This function make sure the Polygon Normals Tangents Binormals are computed and also remove degenerated triangle from the render mesh description.
							FStaticMeshOperations::ComputePolygonTangentsAndNormals(MeshDescription, ComparisonThreshold);

							// Compute any missing normals or tangents.
							// Static meshes always blend normals of overlapping corners.
							EComputeNTBsFlags ComputeNTBsOptions = EComputeNTBsFlags::BlendOverlappingNormals;
							ComputeNTBsOptions |= EComputeNTBsFlags::IgnoreDegenerateTriangles;
							ComputeNTBsOptions |= EComputeNTBsFlags::UseMikkTSpace;

							FStaticMeshOperations::ComputeTangentsAndNormals( MeshDescription, ComputeNTBsOptions );

							UsdGeomMeshTranslatorImpl::GeometryCacheDataForMeshDescription( OutMeshData, MeshDescription );

							return true;
						}
					}
					return false;
				},
				InPrimPath,
				Stage.GetStartTimeCode(),
				Stage.GetEndTimeCode()
			);

			GeometryCache->AddTrack( UsdTrack );

			TArray< FMatrix > Mats;
			Mats.Add( FMatrix::Identity );
			Mats.Add( FMatrix::Identity );

			TArray< float > MatTimes;
			MatTimes.Add( 0.0f );
			MatTimes.Add( 0.0f );
			UsdTrack->SetMatrixSamples( Mats, MatTimes );

			Context->AssetCache->CacheAsset( AllLODHash.ToString(), GeometryCache );
		}
		else
		{
			bOutIsNew = false;
		}

		return GeometryCache;
	}

	// #ueent_todo: Replace MeshDescription with RawMesh and also make it work with StaticMesh
	void GeometryCacheDataForMeshDescription( FGeometryCacheMeshData& OutMeshData, FMeshDescription& MeshDescription )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( GeometryCacheDataForMeshDescription );

		OutMeshData.Positions.Reset();
		OutMeshData.TextureCoordinates.Reset();
		OutMeshData.TangentsX.Reset();
		OutMeshData.TangentsZ.Reset();
		OutMeshData.Colors.Reset();
		OutMeshData.Indices.Reset();

		OutMeshData.MotionVectors.Reset();
		OutMeshData.BatchesInfo.Reset();
		OutMeshData.BoundingBox.Init();

		OutMeshData.VertexInfo.bHasColor0 = true;
		OutMeshData.VertexInfo.bHasTangentX = true;
		OutMeshData.VertexInfo.bHasTangentZ = true;
		OutMeshData.VertexInfo.bHasUV0 = true;
		OutMeshData.VertexInfo.bHasMotionVectors = false;

		FStaticMeshAttributes MeshDescriptionAttributes( MeshDescription );

		TVertexAttributesConstRef< FVector > VertexPositions = MeshDescriptionAttributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef< FVector > VertexInstanceNormals = MeshDescriptionAttributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef< FVector > VertexInstanceTangents = MeshDescriptionAttributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesConstRef< float > VertexInstanceBinormalSigns = MeshDescriptionAttributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesConstRef< FVector4 > VertexInstanceColors = MeshDescriptionAttributes.GetVertexInstanceColors();
		TVertexInstanceAttributesConstRef< FVector2D > VertexInstanceUVs = MeshDescriptionAttributes.GetVertexInstanceUVs();

		const int32 NumVertices = MeshDescription.Vertices().Num();
		const int32 NumTriangles = MeshDescription.Triangles().Num();
		const int32 NumMeshDataVertices = NumTriangles * 3;

		OutMeshData.Positions.Reserve( NumVertices );
		OutMeshData.Indices.Reserve( NumMeshDataVertices );
		OutMeshData.TangentsZ.Reserve( NumMeshDataVertices );
		OutMeshData.Colors.Reserve( NumMeshDataVertices );
		OutMeshData.TextureCoordinates.Reserve( NumMeshDataVertices );

		FBox BoundingBox( EForceInit::ForceInitToZero );
		int32 MaterialIndex = 0;
		int32 VertexIndex = 0;
		for ( FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs() )
		{
			// Skip empty polygon groups
			if ( MeshDescription.GetNumPolygonGroupPolygons( PolygonGroupID ) == 0 )
			{
				continue;
			}

			FGeometryCacheMeshBatchInfo BatchInfo;
			BatchInfo.StartIndex = OutMeshData.Indices.Num();
			BatchInfo.MaterialIndex = MaterialIndex++;

			int32 TriangleCount = 0;
			for ( FPolygonID PolygonID : MeshDescription.GetPolygonGroupPolygons( PolygonGroupID ) )
			{
				for ( FTriangleID TriangleID : MeshDescription.GetPolygonTriangleIDs( PolygonID ) )
				{
					for ( FVertexInstanceID VertexInstanceID : MeshDescription.GetTriangleVertexInstances( TriangleID ) )
					{
						const FVector& Position = VertexPositions[ MeshDescription.GetVertexInstanceVertex( VertexInstanceID ) ];
						OutMeshData.Positions.Add( Position );
						BoundingBox += Position;

						OutMeshData.Indices.Add( VertexIndex++ );

						FPackedNormal Normal = VertexInstanceNormals[ VertexInstanceID ];
						Normal.Vector.W = VertexInstanceBinormalSigns[ VertexInstanceID ] < 0 ? -127 : 127;
						OutMeshData.TangentsZ.Add( Normal );
						OutMeshData.TangentsX.Add( VertexInstanceTangents[ VertexInstanceID ] );

						const bool bSRGB = true;
						OutMeshData.Colors.Add( FLinearColor( VertexInstanceColors[ VertexInstanceID ] ).ToFColor( bSRGB ) );

						// Supporting only one UV channel
						const int32 UVIndex = 0;
						OutMeshData.TextureCoordinates.Add( VertexInstanceUVs.Get( VertexInstanceID, UVIndex ) );
					}

					++TriangleCount;
				}
			}

			OutMeshData.BoundingBox = BoundingBox;

			BatchInfo.NumTriangles = TriangleCount;
			OutMeshData.BatchesInfo.Add( BatchInfo );
		}
	}
#endif // WITH_EDITOR
}

FBuildStaticMeshTaskChain::FBuildStaticMeshTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath )
	: PrimPath( InPrimPath )
	, Context( InContext )
{
}

void FBuildStaticMeshTaskChain::SetupTasks()
{
	// Ignore meshes from disabled purposes
	if ( !EnumHasAllFlags( Context->PurposesToLoad, IUsdPrim::GetPurpose( GetPrim() ) ) )
	{
		return;
	}

	// Create static mesh (Main thread)
	Do( ESchemaTranslationLaunchPolicy::Sync,
		[ this ]()
		{
			// Force load MeshBuilderModule so that it's ready for the async tasks
#if WITH_EDITOR
			FModuleManager::LoadModuleChecked< IMeshBuilderModule >( TEXT("MeshBuilder") );
#endif // WITH_EDITOR

			bool bIsNew = true;
			StaticMesh = UsdGeomMeshTranslatorImpl::CreateStaticMesh( LODIndexToMeshDescription, *Context, bIsNew );

			const FString PrimPathString = PrimPath.GetString();

			if ( StaticMesh )
			{
				Context->AssetCache->LinkAssetToPrim( PrimPathString, StaticMesh );

#if WITH_EDITOR
				if ( bIsNew )
				{
					UUsdAssetImportData* ImportData = NewObject<UUsdAssetImportData>( StaticMesh, TEXT( "UUSDAssetImportData" ) );
					ImportData->PrimPath = PrimPathString;
					StaticMesh->AssetImportData = ImportData;
				}
#endif // WITH_EDITOR


				// Only process the materials if we own the mesh. If it's new we know we do
#if WITH_EDITOR
				UUsdAssetImportData* ImportData = Cast<UUsdAssetImportData>( StaticMesh->AssetImportData );
				if ( ImportData && ImportData->PrimPath == PrimPathString )
#endif // WITH_EDITOR
				{
					if ( !bIsNew )
					{
						// We may change material assignments
						StaticMesh->Modify();
					}

					const bool bMaterialsHaveChanged = UsdGeomMeshTranslatorImpl::ProcessMaterials(
						GetPrim(),
						LODIndexToMaterialInfo,
						*StaticMesh,
						*Context->AssetCache.Get(),
						Context->Time,
						Context->ObjectFlags
					);

					if ( bMaterialsHaveChanged )
					{
						const bool bRebuildAll = true;

#if WITH_EDITOR
						StaticMesh->UpdateUVChannelData( bRebuildAll );
#else
						// UpdateUVChannelData doesn't do anything without the editor
						for ( FStaticMaterial& Material : StaticMesh->GetStaticMaterials() )
						{
							Material.UVChannelData.bInitialized = true;
						}
#endif // WITH_EDITOR
					}
				}
			}

			// Only need to continue building the mesh if we just created it
			return bIsNew;
		} );

#if WITH_EDITOR
	// Commit mesh description (Async)
	Then( ESchemaTranslationLaunchPolicy::Async,
		[ this ]()
		{
			UStaticMesh::FCommitMeshDescriptionParams Params;
			Params.bMarkPackageDirty = false;
			Params.bUseHashAsGuid = true;

			for ( int32 LODIndex = 0; LODIndex < LODIndexToMeshDescription.Num(); ++LODIndex )
			{
				StaticMesh->CommitMeshDescription( LODIndex, Params );
			}

			return true;
		} );
#endif // WITH_EDITOR

	// PreBuild static mesh (Main thread)
	Then( ESchemaTranslationLaunchPolicy::Sync,
		[ this ]()
		{
			RecreateRenderStateContextPtr = MakeShared<FStaticMeshComponentRecreateRenderStateContext>( StaticMesh, true, true );

			UsdGeomMeshTranslatorImpl::PreBuildStaticMesh( *StaticMesh );

			return true;
		} );

	// Build static mesh (Async)
	Then( ESchemaTranslationLaunchPolicy::Async,
		[ this ]() mutable
		{
			TArray<FMeshDescription*> MeshDescriptionPtrs;
			MeshDescriptionPtrs.Reserve( LODIndexToMeshDescription.Num() );
			for ( FMeshDescription& MeshDescription : LODIndexToMeshDescription )
			{
				MeshDescriptionPtrs.Add( &MeshDescription );
			}

			FStaticFeatureLevel FeatureLevel = GMaxRHIFeatureLevel;

			UWorld* World = Context->Level ? Context->Level->GetWorld() : nullptr;
			if ( !World )
			{
				World = GWorld;
			}
			if ( World )
			{
				FeatureLevel = World->FeatureLevel;
			}

			if ( !UsdGeomMeshTranslatorImpl::BuildStaticMesh( *StaticMesh, FeatureLevel, MeshDescriptionPtrs ) )
			{
				// Build failed, discard the mesh
				StaticMesh = nullptr;

				return false;
			}

			return true;
		} );

	// PostBuild static mesh (Main thread)
	Then( ESchemaTranslationLaunchPolicy::Sync,
		[ this ]()
		{
			TArray<FMeshDescription*> MeshDescriptionPtrs;
			MeshDescriptionPtrs.Reserve( LODIndexToMeshDescription.Num() );
			for ( FMeshDescription& MeshDescription : LODIndexToMeshDescription )
			{
				MeshDescriptionPtrs.Add( &MeshDescription );
			}

			UsdGeomMeshTranslatorImpl::PostBuildStaticMesh( *StaticMesh, MeshDescriptionPtrs );

			RecreateRenderStateContextPtr.Reset();

			return true;
		} );
}

FGeomMeshCreateAssetsTaskChain::FGeomMeshCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath )
	: FBuildStaticMeshTaskChain( InContext, InPrimPath )
{
	SetupTasks();
}

void FGeomMeshCreateAssetsTaskChain::SetupTasks()
{
	FScopedUnrealAllocs UnrealAllocs;

	// To parse all LODs we need to actively switch variant sets to other variants (triggering prim loading/unloading and notices),
	// which could cause race conditions if other async translation tasks are trying to access those prims
	ESchemaTranslationLaunchPolicy LaunchPolicy = ESchemaTranslationLaunchPolicy::Async;
	if ( Context->bAllowInterpretingLODs && UsdUtils::IsGeomMeshALOD( GetPrim() ) )
	{
		LaunchPolicy = ESchemaTranslationLaunchPolicy::ExclusiveSync;
	}

	// Create mesh descriptions (Async or ExclusiveSync)
	Do( LaunchPolicy,
		[ this ]() -> bool
		{
			TMap< FString, TMap< FString, int32 > > Unused;
			TMap< FString, TMap< FString, int32 > >* MaterialToPrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex : &Unused;

			UsdGeomMeshTranslatorImpl::LoadMeshDescriptions(
				pxr::UsdTyped( GetPrim() ),
				LODIndexToMeshDescription,
				LODIndexToMaterialInfo,
				*MaterialToPrimvarToUVIndex,
				pxr::UsdTimeCode( Context->Time ),
				Context->bAllowInterpretingLODs,
				Context->RenderContext
			);

			// If we have at least one valid LOD, we should keep going
			for ( const FMeshDescription& MeshDescription : LODIndexToMeshDescription )
			{
				if ( !MeshDescription.IsEmpty() )
				{
					return true;
				}
			}
			return false;
		} );

	FBuildStaticMeshTaskChain::SetupTasks();
}

#if WITH_EDITOR
class FGeometryCacheCreateAssetsTaskChain : public FBuildStaticMeshTaskChain
{
public:
	explicit FGeometryCacheCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath )
		: FBuildStaticMeshTaskChain( InContext, InPrimPath )
	{
		SetupTasks();
	}

protected:
	virtual void SetupTasks() override;
};

void FGeometryCacheCreateAssetsTaskChain::SetupTasks()
{
	FScopedUnrealAllocs UnrealAllocs;

	// To parse all LODs we need to actively switch variant sets to other variants (triggering prim loading/unloading and notices),
	// which could cause race conditions if other async translation tasks are trying to access those prims
	ESchemaTranslationLaunchPolicy LaunchPolicy = ESchemaTranslationLaunchPolicy::Async;
	if ( Context->bAllowInterpretingLODs && UsdUtils::IsGeomMeshALOD( GetPrim() ) )
	{
		LaunchPolicy = ESchemaTranslationLaunchPolicy::ExclusiveSync;
	}

	// Create mesh descriptions (Async or ExclusiveSync)
	Do( LaunchPolicy,
		[ this ]() -> bool
		{
			TMap< FString, TMap< FString, int32 > > Unused;
			TMap< FString, TMap< FString, int32 > >* MaterialToPrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex : &Unused;

			UsdGeomMeshTranslatorImpl::LoadMeshDescriptions(
				pxr::UsdTyped( GetPrim() ),
				LODIndexToMeshDescription,
				LODIndexToMaterialInfo,
				*MaterialToPrimvarToUVIndex,
				pxr::UsdTimeCode( Context->Time ),
				Context->bAllowInterpretingLODs,
				Context->RenderContext
			);

			// If we have at least one valid LOD, we should keep going
			for ( const FMeshDescription& MeshDescription : LODIndexToMeshDescription )
			{
				if ( !MeshDescription.IsEmpty() )
				{
					return true;
				}
			}
			return false;
		} );

	// Create the GeometryCache (Main thread)
	Then( ESchemaTranslationLaunchPolicy::Sync,
		[ this ]()
		{
			bool bIsNew = true;
			const FString PrimPathString = PrimPath.GetString();
			UGeometryCache* GeometryCache = UsdGeomMeshTranslatorImpl::CreateGeometryCache( PrimPathString, LODIndexToMeshDescription, Context, bIsNew );

			if ( bIsNew && GeometryCache )
			{
				UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( GeometryCache, TEXT( "UUSDAssetImportData" ) );
				ImportData->PrimPath = PrimPathString;
				GeometryCache->AssetImportData = ImportData;
			}

			bool bMaterialsHaveChanged = false;
			if ( GeometryCache )
			{
				Context->AssetCache->LinkAssetToPrim( PrimPathString, GeometryCache );

				// Only process the materials if we own the GeometryCache. If it's new we know we do
				UUsdAssetImportData* ImportData = Cast< UUsdAssetImportData >( GeometryCache->AssetImportData );
				if ( ImportData && ImportData->PrimPath == PrimPathString )
				{
					bMaterialsHaveChanged = UsdGeomMeshTranslatorImpl::ProcessGeometryCacheMaterials( GetPrim(), LODIndexToMaterialInfo, *GeometryCache, *Context->AssetCache.Get(), Context->Time, Context->ObjectFlags );
				}
			}

			const bool bContinueTaskChain = ( bIsNew || bMaterialsHaveChanged );
			return bContinueTaskChain;
		} );
}
#endif // WITH_EDITOR

void FUsdGeomMeshTranslator::CreateAssets()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdGeomMeshTranslator::CreateAssets );

#if WITH_EDITOR
	if ( GUseGeometryCacheUSD && UsdGeomMeshTranslatorImpl::IsAnimated( GetPrim() ) )
	{
		// Create the GeometryCache TaskChain
		TSharedRef< FGeometryCacheCreateAssetsTaskChain > AssetsTaskChain = MakeShared< FGeometryCacheCreateAssetsTaskChain >( Context, PrimPath );

		Context->TranslatorTasks.Add( MoveTemp( AssetsTaskChain ) );
	}
	else
#endif // WITH_EDITOR
	{
		TSharedRef< FGeomMeshCreateAssetsTaskChain > AssetsTaskChain = MakeShared< FGeomMeshCreateAssetsTaskChain >( Context, PrimPath );

		Context->TranslatorTasks.Add( MoveTemp( AssetsTaskChain ) );
	}
}

USceneComponent* FUsdGeomMeshTranslator::CreateComponents()
{
#if WITH_EDITOR
	// Animated meshes as GeometryCache
	if ( GUseGeometryCacheUSD && UsdGeomMeshTranslatorImpl::IsAnimated( GetPrim() ) )
	{
		TOptional< TSubclassOf< USceneComponent > > GeometryCacheComponent( UGeometryCacheUsdComponent::StaticClass() );
		return CreateComponentsEx( GeometryCacheComponent, {} );
	}
#endif // WITH_EDITOR

	// Animated and static meshes as StaticMesh
	USceneComponent* SceneComponent = CreateComponentsEx( {}, {} );

	// Handle material overrides
	// #ueent_todo: Do the analogue for geometry cache
	// Note: This can be here and not in USDGeomXformableTranslator because there is no way that a collapsed mesh prim could end up with a material override
	if ( UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>( SceneComponent ) )
	{
		if ( UStaticMesh* StaticMesh = Cast< UStaticMesh >( Context->AssetCache->GetAssetForPrim( PrimPath.GetString() ) ) )
		{
#if WITH_EDITOR
			// If the prim paths match, it means that it was this prim that created (and so "owns") the static mesh,
			// so its material assignments will already be directly on the mesh. If they differ, we're using some other prim's mesh,
			// so we may need material overrides on our component
			UUsdAssetImportData* UsdImportData = Cast<UUsdAssetImportData>( StaticMesh->AssetImportData );
			if ( UsdImportData && UsdImportData->PrimPath != PrimPath.GetString() )
#endif // WITH_EDITOR
			{
				TArray<UMaterialInterface*> ExistingAssignments;
				for ( FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials() )
				{
					ExistingAssignments.Add( StaticMaterial.MaterialInterface );
				}

				MeshTranslationImpl::SetMaterialOverrides(
					GetPrim(),
					ExistingAssignments,
					*StaticMeshComponent,
					*Context->AssetCache.Get(),
					Context->Time,
					Context->ObjectFlags,
					Context->bAllowInterpretingLODs,
					Context->RenderContext
				);

#if WITH_EDITOR
				FStaticFeatureLevel FeatureLevel = GMaxRHIFeatureLevel;

				UWorld* World = Context->Level ? Context->Level->GetWorld() : nullptr;
				if ( !World )
				{
					World = GWorld;
				}
				if ( World )
				{
					FeatureLevel = World->FeatureLevel;
				}

				// Enable adjacency buffer if we're using a material override that requires it.
				// This is currently editor-only because UStaticMesh::BuildFromMeshDescription doesn't
				// build the adjacency buffer anyway, and that's what we use to build at runtime.
				// Possibly because StaticMesh.cpp can't use MeshBuilderCommon stuff, which is where
				// ThirdPartyBuildOptimizationHelper.h is. That seems to be the procedure for building
				// adjacency buffer indices.
				// TODO: Whenever that is refactored, expand this to support detecting adjacency buffer rebuilds for
				// material overrides at runtime
				for ( UMaterialInterface* Material : StaticMeshComponent->OverrideMaterials )
				{
					if ( RequiresAdjacencyInformation( Material, nullptr, FeatureLevel ) )
					{
						FStaticMeshComponentRecreateRenderStateContext RecreateContext{ StaticMesh };

						TArray<FMeshDescription*> LODIndexToMeshDescriptionPtr;
						TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();
						for ( int32 Index = 0; Index < SourceModels.Num(); ++Index )
						{
							SourceModels[Index].BuildSettings.bBuildAdjacencyBuffer = true;
							LODIndexToMeshDescriptionPtr.Add( StaticMesh->GetMeshDescription( Index ) );
						}

						// Rebuild the mesh if we know we need adjacency buffer info
						UsdGeomMeshTranslatorImpl::PreBuildStaticMesh( *StaticMesh );
						UsdGeomMeshTranslatorImpl::BuildStaticMesh( *StaticMesh, FeatureLevel, LODIndexToMeshDescriptionPtr );
						UsdGeomMeshTranslatorImpl::PostBuildStaticMesh( *StaticMesh, LODIndexToMeshDescriptionPtr );

						break;
					}
				}
#endif // WITH_EDITOR
			}
		}
	}

	return SceneComponent;
}

void FUsdGeomMeshTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( SceneComponent )
	{
		SceneComponent->Modify();
	}

	if (
#if WITH_EDITOR
		!GUseGeometryCacheUSD &&
#endif // !WITH_EDITOR
		UsdGeomMeshTranslatorImpl::IsAnimated( GetPrim() )
	)
	{
		// The assets might have changed since our attributes are animated
		CreateAssets();
	}

#if WITH_EDITOR
	// Set the initial GeometryCache on the GeometryCacheUsdComponent
	if ( UGeometryCacheUsdComponent* GeometryCacheUsdComponent = Cast< UGeometryCacheUsdComponent >( SceneComponent ) )
	{
		UGeometryCache* GeometryCache = Cast< UGeometryCache >( Context->AssetCache->GetAssetForPrim( PrimPath.GetString() ) );

		if ( GeometryCache != GeometryCacheUsdComponent->GetGeometryCache() )
		{
			if ( GeometryCacheUsdComponent->IsRegistered() )
			{
				GeometryCacheUsdComponent->UnregisterComponent();
			}

			// Skip the extra handling in SetGeometryCache
			GeometryCacheUsdComponent->GeometryCache = GeometryCache;

			GeometryCacheUsdComponent->RegisterComponent();
		}
		else
		{
			GeometryCacheUsdComponent->SetManualTick( true );
			GeometryCacheUsdComponent->TickAtThisTime( Context->Time, true, false, true );
		}
	}
#endif // WITH_EDITOR

	Super::UpdateComponents( SceneComponent );
}

bool FUsdGeomMeshTranslator::CanBeCollapsed( ECollapsingType CollapsingType ) const
{
	if ( !Context->bAllowCollapsing )
	{
		return false;
	}

	UE::FUsdPrim Prim = GetPrim();

	// Don't collapse if our final UStaticMesh would have multiple LODs
	if ( Context->bAllowInterpretingLODs &&
		 CollapsingType == FUsdSchemaTranslator::ECollapsingType::Assets &&
		 UsdUtils::IsGeomMeshALOD( Prim ) )
	{
		return false;
	}

	return !UsdGeomMeshTranslatorImpl::IsAnimated( Prim );
}

#endif // #if USE_USD_SDK
