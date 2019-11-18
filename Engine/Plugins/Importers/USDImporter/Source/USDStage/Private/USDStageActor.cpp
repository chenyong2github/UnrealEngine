// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDStageActor.h"

#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDListener.h"
#include "USDPrimConversion.h"
#include "USDSkeletalDataConversion.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "IMeshBuilderModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescriptionOperations.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshAttributes.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "MeshDescription.h"
#include "Misc/FrameNumber.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/stageCache.h"
#include "pxr/usd/usd/stageCacheContext.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdSkel/binding.h"
#include "pxr/usd/usdSkel/cache.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/usd/usdSkel/skeletonQuery.h"

#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDStageActor"

static const EObjectFlags DefaultObjFlag = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient;

AUsdStageActor::FOnActorLoaded AUsdStageActor::OnActorLoaded{};

#if USE_USD_SDK
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

void ProcessMaterials( const pxr::UsdStageRefPtr& Stage, UStaticMesh* StaticMesh, const FMeshDescription& MeshDescription, TMap< FString, UMaterial* >& MaterialsCache, bool bHasPrimDisplayColor )
{
	FStaticMeshConstAttributes StaticMeshAttributes( MeshDescription );

	for ( const FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs() )
	{
		const FName& ImportedMaterialSlotName = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[ PolygonGroupID ];
		const FName MaterialSlotName = ImportedMaterialSlotName;

		int32 MaterialIndex = INDEX_NONE;
		int32 MeshMaterialIndex = 0;

		for ( FStaticMaterial& StaticMaterial : StaticMesh->StaticMaterials )
		{
			if ( StaticMaterial.MaterialSlotName.IsEqual( ImportedMaterialSlotName ) )
			{
				MaterialIndex = MeshMaterialIndex;
				break;
			}

			++MeshMaterialIndex;
		}

		if ( MaterialIndex == INDEX_NONE )
		{
			MaterialIndex = PolygonGroupID.GetValue();
		}

		UMaterialInterface* Material = nullptr;
		pxr::UsdPrim MaterialPrim = Stage->GetPrimAtPath( UnrealToUsd::ConvertPath( *ImportedMaterialSlotName.ToString() ).Get() );

		if ( MaterialPrim )
		{
			UMaterial*& CachedMaterial = MaterialsCache.FindOrAdd( UsdToUnreal::ConvertPath( MaterialPrim.GetPrimPath() ) );

			if ( !CachedMaterial )
			{
				CachedMaterial = NewObject< UMaterial >();

				if ( UsdToUnreal::ConvertMaterial( pxr::UsdShadeMaterial( MaterialPrim ), *CachedMaterial ) )
				{
					//UMaterialEditingLibrary::RecompileMaterial( CachedMaterial ); // Too slow
					CachedMaterial->PostEditChange();
				}
				else
				{
					CachedMaterial = nullptr;
				}
			}

			Material = CachedMaterial;
		}

		if ( Material == nullptr && bHasPrimDisplayColor )
		{
			FSoftObjectPath VertexColorMaterialPath( TEXT("Material'/USDImporter/Materials/DisplayColor.DisplayColor'") );
			Material = Cast< UMaterialInterface >( VertexColorMaterialPath.TryLoad() );
		}

		FStaticMaterial StaticMaterial( Material, MaterialSlotName, ImportedMaterialSlotName );
		StaticMesh->StaticMaterials.Add( StaticMaterial );

		FMeshSectionInfo MeshSectionInfo;
		MeshSectionInfo.MaterialIndex = MaterialIndex;

		StaticMesh->GetSectionInfoMap().Set( 0, PolygonGroupID.GetValue(), MeshSectionInfo );
	}
}

void ProcessMaterials( const pxr::UsdStageRefPtr& Stage, FSkeletalMeshImportData& SkelMeshImportData, TMap< FString, UMaterial* >& MaterialsCache, bool bHasPrimDisplayColor )
{
	for (SkeletalMeshImportData::FMaterial& ImportedMaterial : SkelMeshImportData.Materials)
	{
		if (!ImportedMaterial.Material.IsValid())
		{
			UMaterialInterface* Material = nullptr;
			TUsdStore< pxr::UsdPrim > MaterialPrim = Stage->GetPrimAtPath( UnrealToUsd::ConvertPath( *ImportedMaterial.MaterialImportName ).Get() );

			if ( MaterialPrim.Get() )
			{
				UMaterial*& CachedMaterial = MaterialsCache.FindOrAdd( UsdToUnreal::ConvertPath( MaterialPrim.Get().GetPrimPath() ) );

				if ( !CachedMaterial )
				{
					CachedMaterial = NewObject< UMaterial >();

					if ( UsdToUnreal::ConvertMaterial( pxr::UsdShadeMaterial( MaterialPrim.Get() ), *CachedMaterial ) )
					{
						CachedMaterial->bUsedWithSkeletalMesh = true;
						bool bNeedsRecompile = false;
						CachedMaterial->GetMaterial()->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);

						CachedMaterial->PostEditChange();
					}
					else
					{
						CachedMaterial = nullptr;
					}
				}
				Material = CachedMaterial;
			}

			if ( Material == nullptr && bHasPrimDisplayColor )
			{
				FSoftObjectPath VertexColorMaterialPath( TEXT("Material'/USDImporter/Materials/DisplayColor.DisplayColor'") );
				Material = Cast< UMaterialInterface >( VertexColorMaterialPath.TryLoad() );
			}
			ImportedMaterial.Material = Material;
		}
	}
}
#endif // #if USE_USD_SDK

AUsdStageActor::AUsdStageActor()
	: InitialLoadSet( EUsdInitialLoadSet::LoadAll )
	, Time( 0.0f )
	, StartTimeCode( 0.f )
	, EndTimeCode( 100.f )
	, TimeCodesPerSecond( 24.f )
{
	SceneComponent = CreateDefaultSubobject< USceneComponent >( TEXT("SceneComponent0") );
	
	RootComponent = SceneComponent;

#if USE_USD_SDK
	UsdListener.OnPrimChanged.AddLambda(
		[ this ]( const FString& PrimPath, bool bResync )
			{
				TUsdStore< pxr::SdfPath > UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath );
				this->UpdatePrim( UsdPrimPath.Get(), bResync );

				this->OnPrimChanged.Broadcast( PrimPath, bResync );
			}
		);
#endif // #if USE_USD_SDK

	InitLevelSequence(TimeCodesPerSecond);
	SetupLevelSequence();

	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject( this, &AUsdStageActor::OnPrimObjectPropertyChanged );
}

AUsdStageActor::~AUsdStageActor()
{
#if USE_USD_SDK
	UnrealUSDWrapper::GetUsdStageCache().Erase( UsdStageStore.Get() );
#endif // #if USE_USD_SDK
}

#if USE_USD_SDK

bool AUsdStageActor::LoadStaticMesh( const pxr::UsdGeomMesh& UsdMesh, UStaticMeshComponent& MeshComponent )
{
	UStaticMesh* StaticMesh = nullptr;

	pxr::UsdGeomXformable Xformable( UsdMesh.GetPrim() );

	FString MeshPrimPath = UsdToUnreal::ConvertPath( UsdMesh.GetPrim().GetPrimPath() );

	FMeshDescription MeshDescription = LoadMeshDescription( UsdMesh, pxr::UsdTimeCode( Time ) );

	FSHAHash MeshHash = FMeshDescriptionOperations::ComputeSHAHash( MeshDescription );

	StaticMesh = MeshCache.FindRef( MeshHash.ToString() );

	if ( !StaticMesh && !MeshDescription.IsEmpty() )
	{
		StaticMesh = NewObject< UStaticMesh >( GetTransientPackage(), NAME_None, DefaultObjFlag | EObjectFlags::RF_Public );

		FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
		SourceModel.BuildSettings.bGenerateLightmapUVs = false;
		SourceModel.BuildSettings.bRecomputeNormals = false;
		SourceModel.BuildSettings.bRecomputeTangents = false;
		SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
		SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;

		FMeshDescription* StaticMeshDescription = StaticMesh->CreateMeshDescription(0);
		check( StaticMeshDescription );
		*StaticMeshDescription = MoveTemp( MeshDescription );
		StaticMesh->CommitMeshDescription(0);

		const bool bHasPrimDisplayColor = UsdMesh.GetDisplayColorPrimvar().IsDefined();
		ProcessMaterials( GetUsdStage(), StaticMesh, *StaticMeshDescription, MaterialsCache, bHasPrimDisplayColor );

		// Create render data
		if ( !StaticMesh->RenderData )
		{
			StaticMesh->RenderData.Reset(new(FMemory::Malloc(sizeof(FStaticMeshRenderData)))FStaticMeshRenderData());
		}

		ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
		ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
		check(RunningPlatform);
		const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();

		const FStaticMeshLODGroup& LODGroup = LODSettings.GetLODGroup(StaticMesh->LODGroup);

		IMeshBuilderModule& MeshBuilderModule = FModuleManager::LoadModuleChecked< IMeshBuilderModule >( TEXT("MeshBuilder") );
		if ( !MeshBuilderModule.BuildMesh( *StaticMesh->RenderData, StaticMesh, LODGroup ) )
		{
			UE_LOG(LogStaticMesh, Error, TEXT("Failed to build static mesh. See previous line(s) for details."));
			return false;
		}

		for ( FStaticMeshLODResources& LODResources : StaticMesh->RenderData->LODResources )
		{
			LODResources.bHasColorVertexData = true;
		}

		// Bounds
		StaticMesh->CalculateExtendedBounds();

		// Recreate resources
		StaticMesh->InitResources();

		// Other setup
		//StaticMesh->CreateBodySetup();

		// Collision
		//StaticMesh->BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;

		MeshCache.Add( MeshHash.ToString() ) = StaticMesh;
	}
	/*else
	{
		FPlatformMisc::LowLevelOutputDebugStringf( TEXT("Mesh found in cache %s\n"), *StaticMesh->GetName() );
	}*/

	if ( StaticMesh != MeshComponent.GetStaticMesh() )
	{
		if ( MeshComponent.IsRegistered() )
		{
			MeshComponent.UnregisterComponent();
		}

		MeshComponent.SetStaticMesh( StaticMesh );

		MeshComponent.RegisterComponent();
	}

	return true;
}

FUsdPrimTwin* AUsdStageActor::SpawnPrim( const pxr::SdfPath& UsdPrimPath )
{
	const FString PrimPath = UsdToUnreal::ConvertPath( UsdPrimPath );
	const FString ParentPrimPath = UsdToUnreal::ConvertPath( UsdPrimPath.GetParentPath() );

	FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimPath );
	FUsdPrimTwin* ParentUsdPrimTwin = RootUsdTwin.Find( ParentPrimPath );

	const pxr::UsdPrim Prim = GetUsdStage()->GetPrimAtPath( UsdPrimPath );

	if ( !Prim )
	{
		return nullptr;
	}

	if ( !ParentUsdPrimTwin )
	{
		ParentUsdPrimTwin = &RootUsdTwin;
	}

	if ( !UsdPrimTwin )
	{
		UsdPrimTwin = &ParentUsdPrimTwin->AddChild( *PrimPath );

		UsdPrimTwin->OnDestroyed.AddLambda(
			[ this ]( const FUsdPrimTwin& UsdPrimTwin )
			{
				this->OnUsdPrimTwinDestroyed( UsdPrimTwin );
			} );

		if ( !UsdPrimTwin->AnimationHandle.IsValid() )
		{
			bool bHasXformbaleTimeSamples = false;
			{
				pxr::UsdGeomXformable Xformable( Prim );

				if ( Xformable )
				{
					std::vector< double > TimeSamples;
					Xformable.GetTimeSamples( &TimeSamples );

					bHasXformbaleTimeSamples = TimeSamples.size() > 0;
				}
			}

			bool bHasAttributesTimeSamples = false;
			{
				std::vector< pxr::UsdAttribute > Attributes = Prim.GetAttributes();

				for ( pxr::UsdAttribute& Attribute : Attributes )
				{
					bHasAttributesTimeSamples = Attribute.ValueMightBeTimeVarying();
					if ( bHasAttributesTimeSamples )
					{
						break;
					}
				}
			}

			if ( ( bHasXformbaleTimeSamples || bHasAttributesTimeSamples ) )
			{
				PrimsToAnimate.Add( PrimPath );
			}
		}
	}

	UClass* ComponentClass = UsdUtils::GetComponentTypeForPrim( Prim );

	if ( !ComponentClass )
	{
		return nullptr;
	}

	FScopedUnrealAllocs UnrealAllocs;

	const bool bNeedsActor = ( ParentUsdPrimTwin->SceneComponent == nullptr || Prim.IsModel() || UsdUtils::HasCompositionArcs( Prim ) || Prim.IsGroup() || Prim.IsA< pxr::UsdGeomScope >() || Prim.IsA< pxr::UsdGeomCamera >() );

	if ( bNeedsActor && !UsdPrimTwin->SpawnedActor.IsValid() )
	{
		// Try to find an existing actor for the new prim
		AActor* ParentActor = ParentUsdPrimTwin->SpawnedActor.Get();

		if ( !ParentActor )
		{
			ParentActor = this;
		}

		if ( ParentActor )
		{
			TArray< AActor* > AttachedActors;
			ParentActor->GetAttachedActors( AttachedActors );

			FString PrimName = UsdToUnreal::ConvertString( Prim.GetName().GetText() );

			for ( AActor* AttachedActor : AttachedActors )
			{
				if ( AttachedActor->GetName() == PrimName )
				{
					// Assuming that this actor isn't used by another prim because USD names are unique
					UsdPrimTwin->SpawnedActor = AttachedActor;
					break;
				}
			}
		}

		if ( !UsdPrimTwin->SpawnedActor.IsValid() )
		{
			// Spawn actor
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags = DefaultObjFlag;
			SpawnParameters.OverrideLevel = GetLevel();

			UClass* ActorClass = UsdUtils::GetActorTypeForPrim( Prim );
			AActor* SpawnedActor = GetWorld()->SpawnActor( ActorClass, nullptr, SpawnParameters );
			UsdPrimTwin->SpawnedActor = SpawnedActor;
		}

		UsdPrimTwin->SpawnedActor->SetActorLabel( Prim.GetName().GetText() );
		UsdPrimTwin->SpawnedActor->Tags.AddUnique( TEXT("SequencerActor") );	// Hack to show transient actors in world outliner

		if ( UActorComponent* ExistingComponent = UsdPrimTwin->SpawnedActor->GetComponentByClass( ComponentClass ) )
		{
			UsdPrimTwin->SceneComponent = Cast< USceneComponent >( ExistingComponent );
		}
		else
		{
			UsdPrimTwin->SceneComponent = NewObject< USceneComponent >( UsdPrimTwin->SpawnedActor.Get(), ComponentClass, FName( Prim.GetName().GetText() ), DefaultObjFlag );
			UsdPrimTwin->SpawnedActor->SetRootComponent( UsdPrimTwin->SceneComponent.Get() );
		}
	}
	else if ( !bNeedsActor && UsdPrimTwin->SpawnedActor.IsValid() )
	{
		GetWorld()->DestroyActor( UsdPrimTwin->SpawnedActor.Get() );
		UsdPrimTwin->SpawnedActor = nullptr;
	}

	if ( !UsdPrimTwin->SceneComponent.IsValid() || !UsdPrimTwin->SceneComponent->IsA( ComponentClass ) )
	{
		if ( UsdPrimTwin->SceneComponent.IsValid() )
		{
			UsdPrimTwin->SceneComponent->DestroyComponent();
			UsdPrimTwin->SceneComponent = nullptr;
		}

		UObject* ComponentOwner = UsdPrimTwin->SpawnedActor.Get();

		if ( !ComponentOwner )
		{
			if ( ParentUsdPrimTwin->SceneComponent.IsValid() )
			{
				ComponentOwner = ParentUsdPrimTwin->SceneComponent.Get();
			}
			else
			{
				ComponentOwner = ParentUsdPrimTwin->SpawnedActor.Get();
			}
		}

		UsdPrimTwin->SceneComponent = NewObject< USceneComponent >( ComponentOwner, ComponentClass, FName( Prim.GetName().GetText() ), DefaultObjFlag );

		if ( UsdPrimTwin->SpawnedActor.IsValid() )
		{
			UsdPrimTwin->SpawnedActor->AddInstanceComponent( UsdPrimTwin->SceneComponent.Get() );

			if ( !UsdPrimTwin->SpawnedActor->GetRootComponent() )
			{
				UsdPrimTwin->SpawnedActor->SetRootComponent( UsdPrimTwin->SceneComponent.Get() );
			}
		}
	}

	return UsdPrimTwin;
}

FUsdPrimTwin* AUsdStageActor::LoadPrim( const pxr::SdfPath& Path )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadPrim );

	FScopedUsdAllocs UsdAllocs;

	if ( !GetUsdStage() )
	{
		return nullptr;
	}

	TUsdStore< pxr::UsdPrim > PrimStore = GetUsdStage()->GetPrimAtPath( Path.GetPrimPath() );
	if ( !PrimStore.Get() )
	{
		return nullptr;
	}

	pxr::UsdPrim& Prim = PrimStore.Get();

	FUsdPrimTwin* ParentUsdPrimTwin = RootUsdTwin.Find( UsdToUnreal::ConvertPath( Path.GetParentPath() ) );

	if ( !ParentUsdPrimTwin )
	{
		ParentUsdPrimTwin = &RootUsdTwin;
	}

	FUsdPrimTwin* UsdPrimTwin = SpawnPrim( Path );

	if ( !UsdPrimTwin || !UsdPrimTwin->SceneComponent.IsValid() )
	{
		return nullptr;
	}

	if ( UStaticMeshComponent* MeshComponent = Cast< UStaticMeshComponent >( UsdPrimTwin->SceneComponent.Get() ) )
	{
		LoadStaticMesh( pxr::UsdGeomMesh( Prim ), *MeshComponent );
	}
	else if ( UCineCameraComponent* CameraComponent = Cast< UCineCameraComponent >( UsdPrimTwin->SceneComponent.Get() ) )
	{
		UsdToUnreal::ConvertGeomCamera( GetUsdStage(), pxr::UsdGeomCamera( Prim ), *CameraComponent, pxr::UsdTimeCode( Time ) );
	}

	if ( Path.IsPrimPath() )
	{
		USceneComponent* AttachToComponent = ParentUsdPrimTwin->SceneComponent.Get();

		if ( !AttachToComponent )
		{
			AttachToComponent = SceneComponent;
		}

		USceneComponent* ComponentToAttach = UsdPrimTwin->SceneComponent.Get();
		if ( ComponentToAttach->GetOwner()->GetRootComponent() != ComponentToAttach && ComponentToAttach->GetOwner() != AttachToComponent->GetOwner() )
		{
			ComponentToAttach = ComponentToAttach->GetOwner()->GetRootComponent();
		}

		ComponentToAttach->AttachToComponent( AttachToComponent, FAttachmentTransformRules::KeepRelativeTransform );
		UsdPrimTwin->SceneComponent->SetMobility( EComponentMobility::Movable );
		UsdPrimTwin->SceneComponent->GetOwner()->AddInstanceComponent( UsdPrimTwin->SceneComponent.Get() );
	}

	if ( !Path.IsPropertyPath() || pxr::UsdGeomXformOp::IsXformOp( Path.GetNameToken() ) )
	{
		pxr::UsdGeomXformable Xformable( Prim );
		UsdToUnreal::ConvertXformable( Prim.GetStage(), Xformable, *UsdPrimTwin->SceneComponent );
	}

	if ( pxr::UsdGeomImageable GeomImageable = pxr::UsdGeomImageable( Prim ) )
	{
		bool bIsHidden = ( GeomImageable.ComputeVisibility() == pxr::UsdGeomTokens->invisible );

		if ( UsdPrimTwin->SpawnedActor.IsValid() )
		{
			UsdPrimTwin->SpawnedActor->SetIsTemporarilyHiddenInEditor( bIsHidden );
			UsdPrimTwin->SpawnedActor->SetActorHiddenInGame( bIsHidden );
		}
		else if ( UsdPrimTwin->SceneComponent.IsValid() )
		{
			UsdPrimTwin->SceneComponent->SetVisibility( !bIsHidden );
		}
	}

	if ( UsdPrimTwin->SpawnedActor.IsValid() )
	{
		ObjectsToWatch.Add( UsdPrimTwin->SpawnedActor.Get(), UsdPrimTwin->PrimPath );
	}
	else if ( UsdPrimTwin->SceneComponent.IsValid() )
	{
		ObjectsToWatch.Add( UsdPrimTwin->SceneComponent.Get(), UsdPrimTwin->PrimPath );
	}

	if ( !UsdPrimTwin->SceneComponent->IsRegistered() )
	{
		UsdPrimTwin->SceneComponent->RegisterComponent();
	}

	return UsdPrimTwin;
}

FUsdPrimTwin* AUsdStageActor::ExpandPrim( const pxr::UsdPrim& Prim )
{
	if ( !Prim )
	{
		return nullptr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ExpandPrim );

	FUsdPrimTwin* UsdPrimTwin = LoadPrim( Prim.GetPrimPath() );

	if ( Prim.IsA<pxr::UsdSkelRoot>() )
	{
		USkinnedMeshComponent* SkinnedMeshComponent = Cast< USkinnedMeshComponent >( UsdPrimTwin->SceneComponent.Get() );
		if ( SkinnedMeshComponent )
		{
			ProcessSkeletonRoot( Prim, *SkinnedMeshComponent );
		}
	}
	else if ( Prim )
	{
		pxr::UsdPrimSiblingRange PrimChildren = Prim.GetFilteredChildren( pxr::UsdTraverseInstanceProxies() );
		for ( TUsdStore< pxr::UsdPrim > ChildStore : PrimChildren )
		{
			ExpandPrim( ChildStore.Get() );
		}
	}

	return UsdPrimTwin;
}

bool AUsdStageActor::ProcessSkeletonRoot( const pxr::UsdPrim& Prim, USkinnedMeshComponent& SkinnedMeshComponent )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ProcessSkeletonRoot );

	FSkeletalMeshImportData SkelMeshImportData;
	bool bIsSkeletalDataValid = true;

	// Retrieve the USD skeletal data under the SkeletonRoot into the SkeletalMeshImportData
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdSkelCache SkeletonCache;
		pxr::UsdSkelRoot SkeletonRoot(Prim);
		SkeletonCache.Populate(SkeletonRoot);

		pxr::SdfPath PrimPath = Prim.GetPath();

		std::vector<pxr::UsdSkelBinding> SkeletonBindings;
		SkeletonCache.ComputeSkelBindings(SkeletonRoot, &SkeletonBindings);

		if (SkeletonBindings.size() == 0)
		{
			return false;
		}

		pxr::UsdGeomXformable Xformable(Prim);

		std::vector<double> TimeSamples;
		bool bHasTimeSamples = Xformable.GetTimeSamples(&TimeSamples);

		if (TimeSamples.size() > 0)
		{
			auto UpdateSkelRootTransform = [&, Prim]()
			{
				pxr::UsdGeomXformable Xformable(Prim);
				UsdToUnreal::ConvertXformable(Prim.GetStage(), Xformable, SkinnedMeshComponent, pxr::UsdTimeCode(Time));
			};

			FDelegateHandle Handle = OnTimeChanged.AddLambda(UpdateSkelRootTransform);
			PrimDelegates.Add(UsdToUnreal::ConvertPath(PrimPath), Handle);
		}

		bool bHasPrimDisplayColor = false;

		// Note that there could be multiple skeleton bindings under the SkeletonRoot
		// For now, extract just the first one 
		for (const pxr::UsdSkelBinding& Binding : SkeletonBindings)
		{
			const pxr::UsdSkelSkeleton& Skeleton = Binding.GetSkeleton();
			pxr::UsdSkelSkeletonQuery SkelQuery = SkeletonCache.GetSkelQuery(Skeleton);

			bool bSkeletonValid = UsdToUnreal::ConvertSkeleton(SkelQuery, SkelMeshImportData);
			if (!bSkeletonValid)
			{
				bIsSkeletalDataValid = false;
				break;
			}

			for (const pxr::UsdSkelSkinningQuery& SkinningQuery : Binding.GetSkinningTargets())
			{
				// In USD, the skinning target need not be a mesh, but for Unreal we are only interested in skinning meshes
				pxr::UsdGeomMesh SkinningMesh = pxr::UsdGeomMesh(SkinningQuery.GetPrim());
				if (SkinningMesh)
				{
					UsdToUnreal::ConvertSkinnedMesh(SkinningQuery, SkelMeshImportData);
					bHasPrimDisplayColor = bHasPrimDisplayColor || SkinningMesh.GetDisplayColorPrimvar().IsDefined();
				}
			}

			const pxr::UsdSkelAnimQuery& AnimQuery = SkelQuery.GetAnimQuery();
			bool bRes = AnimQuery.GetJointTransformTimeSamples(&TimeSamples);

			if (TimeSamples.size() > 0)
			{
				auto UpdateBoneTransforms = [&, SkelQuery]()
				{
					FScopedUnrealAllocs UnrealAllocs;

					UPoseableMeshComponent* PoseableMeshComponent = Cast<UPoseableMeshComponent>(&SkinnedMeshComponent);
					if (!PoseableMeshComponent || !SkelQuery)
					{
						return;
					}

					TArray<FTransform> BoneTransforms;
					pxr::VtArray<pxr::GfMatrix4d> UsdBoneTransforms;
					bool bJointTransformsComputed = SkelQuery.ComputeJointLocalTransforms(&UsdBoneTransforms, pxr::UsdTimeCode(Time));
					if (bJointTransformsComputed)
					{
						for (uint32 Index = 0; Index < UsdBoneTransforms.size(); ++Index)
						{
							const pxr::GfMatrix4d& UsdMatrix = UsdBoneTransforms[Index];
							FTransform BoneTransform = UsdToUnreal::ConvertMatrix(GetUsdStage(), UsdMatrix);
							BoneTransforms.Add(BoneTransform);
						}
					}

					for (int32 Index = 0; Index < BoneTransforms.Num(); ++Index)
					{
						PoseableMeshComponent->BoneSpaceTransforms[Index] = BoneTransforms[Index];
					}
					PoseableMeshComponent->RefreshBoneTransforms();
				};

				FDelegateHandle Handle = OnTimeChanged.AddLambda(UpdateBoneTransforms);
				PrimDelegates.Add(UsdToUnreal::ConvertPath(PrimPath), Handle);
			}

			ProcessMaterials(GetUsdStage(), SkelMeshImportData, MaterialsCache, bHasPrimDisplayColor);

			break;
		}
	}

	if (!bIsSkeletalDataValid)
	{
		return false;
	}

	USkeletalMesh* SkeletalMesh = UsdToUnreal::GetSkeletalMeshFromImportData(SkelMeshImportData, DefaultObjFlag);

	if (SkeletalMesh)
	{
		SkinnedMeshComponent.SetSkeletalMesh(SkeletalMesh);
	}

	return SkeletalMesh != nullptr;
}

void AUsdStageActor::UpdatePrim( const pxr::SdfPath& InUsdPrimPath, bool bResync )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::UpdatePrim );

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "UpdatingUSDPrim", "Updating USD Prim" ) );
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	pxr::SdfPath UsdPrimPath = InUsdPrimPath;

	if ( !UsdPrimPath.IsAbsoluteRootOrPrimPath() )
	{
		UsdPrimPath = UsdPrimPath.GetAbsoluteRootOrPrimPath();
	}

	if ( UsdPrimPath.IsAbsoluteRootOrPrimPath() )
	{
		if ( bResync )
		{
			FString PrimPath = UsdToUnreal::ConvertPath( UsdPrimPath );
			if ( FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimPath ) )
			{
				UsdPrimTwin->Clear();
			}
		}

		TUsdStore< pxr::UsdPrim > PrimToExpand = GetUsdStage()->GetPrimAtPath( UsdPrimPath );
		FUsdPrimTwin* UsdPrimTwin = ExpandPrim( PrimToExpand.Get() );

		GEditor->BroadcastLevelActorListChanged();
		GEditor->RedrawLevelEditingViewports();
	}
}

const pxr::UsdStageRefPtr& AUsdStageActor::GetUsdStage()
{
	OpenUsdStage();

	return UsdStageStore.Get();
}

#endif // #if USE_USD_SDK

void AUsdStageActor::SetTime(float InTime)
{
	Time = InTime;

	Refresh();
}

void AUsdStageActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;
	
	if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootLayer ) )
	{
#if USE_USD_SDK
		UnrealUSDWrapper::GetUsdStageCache().Erase( UsdStageStore.Get() );

		UsdStageStore = TUsdStore< pxr::UsdStageRefPtr >();
		LoadUsdStage();
#endif // #if USE_USD_SDK
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, Time ) )
	{
		Refresh();
	}
	else
	{
		Super::PostEditChangeProperty( PropertyChangedEvent );
	}
}

void AUsdStageActor::Clear()
{
	MeshCache.Reset();
	MaterialsCache.Reset();
	ObjectsToWatch.Reset();
}

void AUsdStageActor::OpenUsdStage()
{
#if USE_USD_SDK
	// Early exit if stage is already opened
	if ( UsdStageStore.Get() || RootLayer.FilePath.IsEmpty() )
	{
		return;
	}

	FString FilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *RootLayer.FilePath );
	FilePath = FPaths::GetPath(FilePath) + TEXT("/");
	FString CleanFilename = FPaths::GetCleanFilename( RootLayer.FilePath );

	if ( FPaths::FileExists( RootLayer.FilePath ) )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdStageCacheContext UsdStageCacheContext( UnrealUSDWrapper::GetUsdStageCache() );
		UsdStageStore = pxr::UsdStage::Open( UnrealToUsd::ConvertString( *RootLayer.FilePath ).Get(), pxr::UsdStage::InitialLoadSet( InitialLoadSet ) );
	}
	else
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdStageCacheContext UsdStageCacheContext( UnrealUSDWrapper::GetUsdStageCache() );
		UsdStageStore = pxr::UsdStage::CreateNew( UnrealToUsd::ConvertString( *RootLayer.FilePath ).Get() );

		// Create default prim
		pxr::UsdGeomXform RootPrim = pxr::UsdGeomXform::Define( UsdStageStore.Get(), UnrealToUsd::ConvertPath( TEXT("/Root") ).Get() );
		pxr::UsdModelAPI( RootPrim ).SetKind( pxr::TfToken("component") );
		
		// Set default prim
		UsdStageStore.Get()->SetDefaultPrim( RootPrim.GetPrim() );

		// Set up axis
		UsdUtils::SetUsdStageAxis( UsdStageStore.Get(), pxr::UsdGeomTokens->z );
	}

	if ( UsdStageStore.Get() )
	{
		UsdStageStore.Get()->SetEditTarget( UsdStageStore.Get()->GetRootLayer() );

		UsdListener.Register( UsdStageStore.Get() );

		OnStageChanged.Broadcast();
	}
#endif // #if USE_USD_SDK
}

void AUsdStageActor::InitLevelSequence(float FramesPerSecond)
{
	if (LevelSequence)
	{
		return;
	}

	// Create LevelSequence
	LevelSequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, DefaultObjFlag | EObjectFlags::RF_Public);
	LevelSequence->Initialize();

	UMovieScene* MovieScene = LevelSequence->MovieScene;

	FFrameRate FrameRate(FramesPerSecond, 1);
	MovieScene->SetDisplayRate(FrameRate);

	// Populate the Sequence
	FGuid ObjectBinding = MovieScene->AddPossessable(GetActorLabel(), this->GetClass());
	LevelSequence->BindPossessableObject(ObjectBinding, *this, GetWorld());

	// Add the Time track
	UMovieSceneFloatTrack* TimeTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(ObjectBinding);
	TimeTrack->SetPropertyNameAndPath(FName(TEXT("Time")), "Time");

	FFrameRate DestFrameRate = LevelSequence->MovieScene->GetTickResolution();
	FFrameNumber StartFrame = DestFrameRate.AsFrameNumber(StartTimeCode / TimeCodesPerSecond);

	bool bSectionAdded = false;
	UMovieSceneFloatSection* TimeSection = Cast<UMovieSceneFloatSection>(TimeTrack->FindOrAddSection(StartFrame, bSectionAdded));

	TimeSection->EvalOptions.CompletionMode = EMovieSceneCompletionMode::KeepState;
	MovieScene->SetEvaluationType(EMovieSceneEvaluationType::FrameLocked);
}

void AUsdStageActor::SetupLevelSequence()
{
	if (!LevelSequence)
	{
		return;
	}

	UMovieScene* MovieScene = LevelSequence->MovieScene;
	if (!MovieScene)
	{
		return;
	}

	FFrameRate DestFrameRate = MovieScene->GetTickResolution();

	TArray<UMovieSceneSection*> Sections = MovieScene->GetAllSections();
	if (Sections.Num() > 0)
	{
		UMovieSceneSection* TimeSection = Sections[0];
		FMovieSceneFloatChannel* TimeChannel = TimeSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);

		FFrameNumber StartFrame = DestFrameRate.AsFrameNumber(StartTimeCode / TimeCodesPerSecond);
		FFrameNumber EndFrame = DestFrameRate.AsFrameNumber(EndTimeCode / TimeCodesPerSecond);

		TArray<FFrameNumber> FrameNumbers;
		FrameNumbers.Add(StartFrame);
		FrameNumbers.Add(EndFrame);

		TArray<FMovieSceneFloatValue> FrameValues;
		FrameValues.Add_GetRef(FMovieSceneFloatValue(StartTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;
		FrameValues.Add_GetRef(FMovieSceneFloatValue(EndTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;

		TimeChannel->Set(FrameNumbers, FrameValues);

		TimeSection->SetRange(TRange<FFrameNumber>::All());

		float StartTime = StartTimeCode / TimeCodesPerSecond;
		float EndTime = EndTimeCode / TimeCodesPerSecond;

		TRange<FFrameNumber> TimeRange = TRange<FFrameNumber>::Inclusive(StartFrame, EndFrame);

		LevelSequence->MovieScene->SetPlaybackRange(TimeRange);
		LevelSequence->MovieScene->SetViewRange((StartTimeCode - 5.f) / TimeCodesPerSecond, (EndTimeCode + 5.f) / TimeCodesPerSecond);
		LevelSequence->MovieScene->SetWorkingRange((StartTimeCode - 5.f) / TimeCodesPerSecond, (EndTimeCode + 5.f) / TimeCodesPerSecond);
	}
}

void AUsdStageActor::LoadUsdStage()
{
#if USE_USD_SDK
	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "LoadingUDStage", "Loading USD Stage") );
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	Clear();

	RootUsdTwin.Clear();
	RootUsdTwin.PrimPath = TEXT("/");

	const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

	if ( !UsdStage )
	{
		OnStageChanged.Broadcast();
		return;
	}

	StartTimeCode = UsdStage->GetStartTimeCode();
	EndTimeCode = UsdStage->GetEndTimeCode();
	TimeCodesPerSecond = UsdStage->GetTimeCodesPerSecond();

	SetupLevelSequence();

	UpdatePrim( UsdStage->GetPseudoRoot().GetPrimPath(), true );

	SetTime( StartTimeCode );

	GEditor->BroadcastLevelActorListChanged();
	GEditor->RedrawLevelEditingViewports();

	auto AnimatePrims = [ this ]()
	{
		for ( const FString& PrimToAnimate : PrimsToAnimate )
		{
			this->LoadPrim( UnrealToUsd::ConvertPath( *PrimToAnimate ).Get() );
		}

		GEditor->BroadcastLevelActorListChanged();
		GEditor->RedrawLevelEditingViewports();
	};

	OnTimeChanged.AddLambda( AnimatePrims );
#endif // #if USE_USD_SDK
}

void AUsdStageActor::Refresh() const
{
	OnTimeChanged.Broadcast();
}

void AUsdStageActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if USE_USD_SDK
	const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

	if ( UsdStage )
	{
		UpdatePrim( UsdStage->GetPseudoRoot().GetPrimPath(), true );
	}
#endif // #if USE_USD_SDK
}

void AUsdStageActor::PostLoad()
{
	Super::PostLoad();

	OnActorLoaded.Broadcast( this );
}

void AUsdStageActor::OnUsdPrimTwinDestroyed( const FUsdPrimTwin& UsdPrimTwin )
{
	TArray<FDelegateHandle> DelegateHandles;
	PrimDelegates.MultiFind( UsdPrimTwin.PrimPath, DelegateHandles );

	for ( FDelegateHandle Handle : DelegateHandles )
	{
		OnTimeChanged.Remove( Handle );
	}

	PrimDelegates.Remove( UsdPrimTwin.PrimPath );
	PrimsToAnimate.Remove( UsdPrimTwin.PrimPath );

	UObject* WatchedObject = UsdPrimTwin.SpawnedActor.IsValid() ? (UObject*)UsdPrimTwin.SpawnedActor.Get() : (UObject*)UsdPrimTwin.SceneComponent.Get();
	ObjectsToWatch.Remove( WatchedObject );
}

void AUsdStageActor::OnPrimObjectPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent )
{
#if USE_USD_SDK
	if ( ObjectBeingModified == this || !ObjectsToWatch.Contains( ObjectBeingModified ) )
	{
		return;
	}

	FString PrimPath = ObjectsToWatch[ ObjectBeingModified ];

	if ( FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimPath ) )
	{
		// Update prim from UE
		USceneComponent* PrimSceneComponent = UsdPrimTwin->SceneComponent.Get();

		if ( !PrimSceneComponent && UsdPrimTwin->SpawnedActor.IsValid() )
		{
			PrimSceneComponent = UsdPrimTwin->SpawnedActor->GetRootComponent();
		}

		if ( PrimSceneComponent )
		{
			const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

			if ( UsdStage )
			{
				FScopedBlockNotices BlockNotices( UsdListener );

				TUsdStore< pxr::UsdPrim > UsdPrim = UsdStage->GetPrimAtPath( UnrealToUsd::ConvertPath( *PrimPath ).Get() );
				UnrealToUsd::ConvertSceneComponent( UsdStage, PrimSceneComponent, UsdPrim.Get() );
			}
		}
	}
#endif // #if USE_USD_SDK
}

#undef LOCTEXT_NAMESPACE
