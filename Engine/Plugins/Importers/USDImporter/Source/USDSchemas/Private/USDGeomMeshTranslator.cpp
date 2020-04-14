// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomMeshTranslator.h"

#if USE_USD_SDK

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDTypesConversion.h"

#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "IMeshBuilderModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshResources.h"
#include "UObject/SoftObjectPath.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/xformable.h"
	#include "pxr/usd/usdShade/material.h"
#include "USDIncludesEnd.h"


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
	bool ProcessMaterials( const pxr::UsdPrim& UsdPrim, UStaticMesh& StaticMesh, const TMap< FString, UObject* >& PrimPathsToAssets, float Time )
	{
		const FMeshDescription* MeshDescription = StaticMesh.GetMeshDescription( 0 );

		if ( !MeshDescription )
		{
			return false ;
		}

		bool bMaterialAssignementsHaveChanged = false;

		FStaticMeshConstAttributes StaticMeshAttributes( *MeshDescription );

		auto FetchUEMaterialsAttribute = []( const pxr::UsdPrim& UsdPrim, float Time ) -> TArray< FString >
		{
			if ( !UsdPrim )
			{
				return {};
			}

			TArray< FString > UEMaterialsAttribute;

			FScopedUsdAllocs UsdAllocs;

			if ( pxr::UsdAttribute MaterialsAttribute = UsdPrim.GetAttribute( UnrealIdentifiers::MaterialAssignments ) )
			{
				pxr::VtStringArray UEMaterials;
				MaterialsAttribute.Get( &UEMaterials, pxr::UsdTimeCode( Time ) );

				for ( const std::string& UEMaterial : UEMaterials )
				{
					UEMaterialsAttribute.Emplace( ANSI_TO_TCHAR( UEMaterial.c_str() ) );
				}
			}

			return UEMaterialsAttribute;
		};

		TArray< FString > MainPrimUEMaterialsAttribute = FetchUEMaterialsAttribute( UsdPrim, Time );

		TPolygonGroupAttributesConstRef< FName > PolygonGroupUsdPrimPaths = MeshDescription->PolygonGroupAttributes().GetAttributesRef< FName >( "UsdPrimPath" );

		int32 PolygonGroupPrimMaterialIndex = 0;

		for ( const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs() )
		{
			const FName& ImportedMaterialSlotName = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[ PolygonGroupID ];
			const FName MaterialSlotName = ImportedMaterialSlotName;

			const int32 MaterialIndex = PolygonGroupID.GetValue();

			TUsdStore< pxr::UsdPrim > PolygonGroupPrim = UsdPrim;

			if ( PolygonGroupUsdPrimPaths.IsValid() )
			{
				const FName& UsdPrimPath = PolygonGroupUsdPrimPaths[ PolygonGroupID ];

				TUsdStore< pxr::SdfPath > PrimPath = UnrealToUsd::ConvertPath( *UsdPrimPath.ToString() );

				if ( PolygonGroupPrim.Get() && PolygonGroupPrim.Get().GetPrimPath() != PrimPath.Get() )
				{
					PolygonGroupPrim = UsdPrim.GetStage()->GetPrimAtPath( PrimPath.Get() );
					PolygonGroupPrimMaterialIndex = 0; // We've moved to a new sub prim
				}
				else
				{
					++PolygonGroupPrimMaterialIndex; // This polygon group is part of the same sub prim
				}
			}

			UMaterialInterface* Material = nullptr;

			// If there was a UE4 material stored in the the main prim for this polygon group, try fetching it
			if ( MainPrimUEMaterialsAttribute.IsValidIndex( MaterialIndex ) )
			{
				Material = Cast< UMaterialInterface >( FSoftObjectPath( MainPrimUEMaterialsAttribute[ MaterialIndex ] ).TryLoad() );
			}
			else
			{
				// It could be that our UE4 material is stored in the subprim instead, so try fetching that
				TArray< FString > PolygonGroupPrimUEMaterialsAttribute = FetchUEMaterialsAttribute( PolygonGroupPrim.Get(), Time );

				if ( PolygonGroupPrimUEMaterialsAttribute.IsValidIndex( PolygonGroupPrimMaterialIndex ) )
				{
					Material = Cast< UMaterialInterface >( FSoftObjectPath( PolygonGroupPrimUEMaterialsAttribute[ PolygonGroupPrimMaterialIndex ] ).TryLoad() );
				}
				else
				{
					// We don't have a UE4 material stored, but we may have imported this material already, so check PrimPathsToAssets

					FScopedUsdAllocs Allocs;

					// We may have just a material slot index as the slot name (in case we have default displayColor materials) or a path to a valid
					// material prim (in case we point to a shade material)
					pxr::SdfPath MaterialPrimPath;
					FString SlotNameString = ImportedMaterialSlotName.ToString();
					if (!SlotNameString.IsNumeric() && pxr::SdfPath::IsValidPathString(UnrealToUsd::ConvertString(*SlotNameString).Get()))
					{
						MaterialPrimPath = UnrealToUsd::ConvertPath( *SlotNameString ).Get();
					}
					else
					{
						MaterialPrimPath = PolygonGroupPrim.Get().GetPrimPath();
					}

					TUsdStore< pxr::UsdPrim > MaterialPrim = UsdPrim.GetStage()->GetPrimAtPath( MaterialPrimPath );

					if ( MaterialPrim.Get() )
					{
						Material = Cast< UMaterialInterface >( PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( MaterialPrim.Get().GetPrimPath() ) ) );
					}
				}
			}

			// Need to create a new material or reuse what is already on the mesh
			if ( Material == nullptr )
			{
				UMaterialInstanceConstant* MaterialInstance =
					[&]()
					{
						UMaterialInstanceConstant* ExistingMaterialInstance = Cast< UMaterialInstanceConstant >( StaticMesh.GetMaterial( MaterialIndex ) );
						const FString& SourcePrimPath = UsdToUnreal::ConvertPath( PolygonGroupPrim.Get().GetPrimPath() );

						// Assuming that we own the material instance and that we can change it as we wish, reuse it
						if ( ExistingMaterialInstance && ExistingMaterialInstance->GetOuter() == GetTransientPackage() )
						{
							if ( ExistingMaterialInstance->AssetImportData )
							{
								if ( ExistingMaterialInstance->AssetImportData->GetFirstFilename() == SourcePrimPath )
								{
									return ExistingMaterialInstance;
								}
							}
						}

						// Create new material
						UMaterialInstanceConstant* NewMaterialInstance = NewObject< UMaterialInstanceConstant >();

						UAssetImportData* ImportData = NewObject< UAssetImportData >( NewMaterialInstance, TEXT("AssetImportData") );
						ImportData->UpdateFilenameOnly( SourcePrimPath );
						NewMaterialInstance->AssetImportData = ImportData;

						return NewMaterialInstance;
					}();

				if ( UsdToUnreal::ConvertDisplayColor( pxr::UsdGeomMesh( PolygonGroupPrim.Get() ), *MaterialInstance, pxr::UsdTimeCode( Time ) ) )
				{
					Material = MaterialInstance;
				}
			}

			FStaticMaterial StaticMaterial( Material, MaterialSlotName );

			if ( !StaticMesh.StaticMaterials.IsValidIndex( MaterialIndex ) )
			{
				StaticMesh.StaticMaterials.Add( MoveTemp( StaticMaterial ) );
				bMaterialAssignementsHaveChanged = true;
			}
			else if ( !( StaticMesh.StaticMaterials[ MaterialIndex ] == StaticMaterial ) )
			{
				StaticMesh.StaticMaterials[ MaterialIndex ] = MoveTemp( StaticMaterial );
				bMaterialAssignementsHaveChanged = true;
			}

			if ( StaticMesh.GetSectionInfoMap().IsValidSection( 0, PolygonGroupID.GetValue() ) )
			{
				FMeshSectionInfo MeshSectionInfo = StaticMesh.GetSectionInfoMap().Get( 0, PolygonGroupID.GetValue() );

				if ( MeshSectionInfo.MaterialIndex != MaterialIndex )
				{
					MeshSectionInfo.MaterialIndex = MaterialIndex;
					StaticMesh.GetSectionInfoMap().Set( 0, PolygonGroupID.GetValue(), MeshSectionInfo );

					bMaterialAssignementsHaveChanged = true;
				}
			}
			else
			{
				FMeshSectionInfo MeshSectionInfo;
				MeshSectionInfo.MaterialIndex = MaterialIndex;

				StaticMesh.GetSectionInfoMap().Set( 0, PolygonGroupID.GetValue(), MeshSectionInfo );

				bMaterialAssignementsHaveChanged = true;
			}
		}

		return bMaterialAssignementsHaveChanged;
	}

	FMeshDescription LoadMeshDescription( const pxr::UsdGeomMesh& UsdMesh, const pxr::UsdTimeCode TimeCode )
	{
		if ( !UsdMesh )
		{
			return {};
		}

		FMeshDescription MeshDescription;
		FStaticMeshAttributes StaticMeshAttributes( MeshDescription );
		StaticMeshAttributes.Register();

		UsdToUnreal::ConvertGeomMesh( UsdMesh, MeshDescription, TimeCode );

		return MeshDescription;
	}

	UStaticMesh* CreateStaticMesh( FMeshDescription&& MeshDescription, FUsdSchemaTranslationContext& Context, bool& bOutIsNew )
	{
		UStaticMesh* StaticMesh = nullptr;

		FSHAHash MeshHash = FStaticMeshOperations::ComputeSHAHash( MeshDescription );

		StaticMesh = Cast< UStaticMesh >( Context.AssetsCache.FindRef( MeshHash.ToString() ) );

		if ( !StaticMesh && !MeshDescription.IsEmpty() )
		{
			bOutIsNew = true;

			StaticMesh = NewObject< UStaticMesh >( GetTransientPackage(), NAME_None, Context.ObjectFlags | EObjectFlags::RF_Public );

			FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
			SourceModel.BuildSettings.bGenerateLightmapUVs = false;
			SourceModel.BuildSettings.bRecomputeNormals = false;
			SourceModel.BuildSettings.bRecomputeTangents = false;
			SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
			SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;

			FMeshDescription* StaticMeshDescription = StaticMesh->CreateMeshDescription(0);
			check( StaticMeshDescription );
			*StaticMeshDescription = MoveTemp( MeshDescription );

			Context.AssetsCache.Add( MeshHash.ToString() ) = StaticMesh;
		}
		else
		{
			//FPlatformMisc::LowLevelOutputDebugStringf( TEXT("Mesh found in cache %s\n"), *StaticMesh->GetName() );
			bOutIsNew = false;
		}

		return StaticMesh;
	}

	void PreBuildStaticMesh( const pxr::UsdPrim& RootPrim, UStaticMesh& StaticMesh, TMap< FString, UObject* >& PrimPathsToAssets, float Time )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomMeshTranslatorImpl::PreBuildStaticMesh );

		if ( StaticMesh.RenderData )
		{
			StaticMesh.ReleaseResources();
			StaticMesh.ReleaseResourcesFence.Wait();
		}

		StaticMesh.RenderData = MakeUnique< FStaticMeshRenderData >();
		StaticMesh.CreateBodySetup();
	}

	bool BuildStaticMesh( UStaticMesh& StaticMesh )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomMeshTranslatorImpl::BuildStaticMesh );

		ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
		ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
		check(RunningPlatform);

		const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();
		StaticMesh.RenderData->Cache(RunningPlatform, &StaticMesh, LODSettings );

		if ( StaticMesh.BodySetup )
		{
			StaticMesh.BodySetup->CreatePhysicsMeshes();
		}

		return true;
	}

	void PostBuildStaticMesh( UStaticMesh& StaticMesh )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomMeshTranslatorImpl::PostBuildStaticMesh );

		StaticMesh.InitResources();

		if ( const FMeshDescription* MeshDescription = StaticMesh.GetMeshDescription( 0 ) )
		{
			StaticMesh.RenderData->Bounds = MeshDescription->GetBounds();
		}

		StaticMesh.CalculateExtendedBounds();
		StaticMesh.ClearMeshDescriptions(); // Clear mesh descriptions to reduce memory usage, they are kept only in bulk data form
	}
}

FBuildStaticMeshTaskChain::FBuildStaticMeshTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdTyped >& InSchema, FMeshDescription&& InMeshDescription )
	: Schema( InSchema )
	, Context( InContext )
	, MeshDescription( MoveTemp( InMeshDescription ) )
{
	SetupTasks();
}

void FBuildStaticMeshTaskChain::SetupTasks()
{
	// Ignore meshes from disabled purposes
	if ( !EnumHasAllFlags( Context->PurposesToLoad, IUsdPrim::GetPurpose( Schema.Get().GetPrim() ) ) )
	{
		return;
	}

	{
		constexpr bool bIsAsyncTask = true;

		// Create static mesh (Main thread)
		Do( !bIsAsyncTask,
			[ this ]()
			{
				// Force load MeshBuilderModule so that it's ready for the async tasks
				FModuleManager::LoadModuleChecked< IMeshBuilderModule >( TEXT("MeshBuilder") );

				bool bIsNew = true;
				StaticMesh = UsdGeomMeshTranslatorImpl::CreateStaticMesh( MoveTemp( MeshDescription ), *Context, bIsNew );

				const FString PrimPath = UsdToUnreal::ConvertPath( Schema.Get().GetPath() );

				FScopeLock Lock( &Context->CriticalSection );
				{
					Context->PrimPathsToAssets.Add( PrimPath, StaticMesh );
				}

				if ( bIsNew && StaticMesh )
				{
					UAssetImportData* ImportData = NewObject< UAssetImportData >( StaticMesh, TEXT("AssetImportData") );
					ImportData->UpdateFilenameOnly( PrimPath );
					StaticMesh->AssetImportData = ImportData;
				}

				bool bMaterialsHaveChanged = false;
				if ( StaticMesh && StaticMesh->AssetImportData )
				{
					// Only process the materials if we have own the mesh
					if ( StaticMesh->AssetImportData->GetFirstFilename() == PrimPath )
					{
						bMaterialsHaveChanged = UsdGeomMeshTranslatorImpl::ProcessMaterials( Schema.Get().GetPrim(), *StaticMesh, Context->PrimPathsToAssets, Context->Time );
					}
				}

				const bool bContinueTaskChain = ( bIsNew || bMaterialsHaveChanged );
				return bContinueTaskChain;
			} );

		// Commit mesh description (Async)
		Then( bIsAsyncTask,
			[ this ]()
			{
				UStaticMesh::FCommitMeshDescriptionParams Params;
				Params.bMarkPackageDirty = false;
				Params.bUseHashAsGuid = true;

				StaticMesh->CommitMeshDescription( 0, Params );

				return true;
			} );

		// PreBuild static mesh (Main thread)
		Then( !bIsAsyncTask,
			[ this ]()
			{
				RecreateRenderStateContextPtr = MakeShared<FStaticMeshComponentRecreateRenderStateContext>( StaticMesh, true, true );

				UsdGeomMeshTranslatorImpl::PreBuildStaticMesh( Schema.Get().GetPrim(), *StaticMesh, Context->PrimPathsToAssets, Context->Time );

				return true;
			} );

		// Build static mesh (Async)
		Then( bIsAsyncTask,
			[ this ]() mutable
			{
				if ( !UsdGeomMeshTranslatorImpl::BuildStaticMesh( *StaticMesh ) )
				{
					// Build failed, discard the mesh
					StaticMesh = nullptr;

					return false;
				}

				return true;
			} );

		// PostBuild static mesh (Main thread)
		Then( !bIsAsyncTask,
			[ this ]()
			{
				UsdGeomMeshTranslatorImpl::PostBuildStaticMesh( *StaticMesh );

				RecreateRenderStateContextPtr.Reset();

				return true;
			} );
	}
}

void FGeomMeshCreateAssetsTaskChain::SetupTasks()
{
	FScopedUnrealAllocs UnrealAllocs;

	// Create mesh description (Async)
	constexpr bool bIsAsyncTask = true;
	Do( bIsAsyncTask,
		[ this ]() -> bool
		{
			MeshDescription = UsdGeomMeshTranslatorImpl::LoadMeshDescription( pxr::UsdGeomMesh( Schema.Get() ), pxr::UsdTimeCode( Context->Time ) );

			return !MeshDescription.IsEmpty();
		} );

	FBuildStaticMeshTaskChain::SetupTasks();
}

void FUsdGeomMeshTranslator::CreateAssets()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdGeomMeshTranslator::CreateAssets );

	TSharedRef< FGeomMeshCreateAssetsTaskChain > AssetsTaskChain = MakeShared< FGeomMeshCreateAssetsTaskChain >( Context, pxr::UsdGeomMesh( Schema.Get() ) );

	Context->TranslatorTasks.Add( MoveTemp( AssetsTaskChain ) );
}

void FUsdGeomMeshTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( UsdGeomMeshTranslatorImpl::IsAnimated( Schema.Get().GetPrim() ) )
	{
		// The assets might have changed since our attributes are animated
		CreateAssets();
	}

	Super::UpdateComponents( SceneComponent );
}

bool FUsdGeomMeshTranslator::CanBeCollapsed( ECollapsingType CollapsingType ) const
{
	return !UsdGeomMeshTranslatorImpl::IsAnimated( Schema.Get().GetPrim() );
}

#endif // #if USE_USD_SDK
