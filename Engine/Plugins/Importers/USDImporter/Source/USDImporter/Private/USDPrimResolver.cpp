// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimResolver.h"

#include "USDConversionUtils.h"
#include "USDImportOptions.h"
#include "USDImporter.h"
#include "USDLog.h"
#include "USDPrimConversion.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdTyped.h"

#include "ActorFactories/ActorFactory.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "AssetSelection.h"
#include "Engine/StaticMesh.h"
#include "IUSDImporterModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"


#define LOCTEXT_NAMESPACE "USDImportPlugin"


void UDEPRECATED_UUSDPrimResolver::Init()
{
	AssetRegistry = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
}

void UDEPRECATED_UUSDPrimResolver::FindMeshAssetsToImport(FUsdImportContext& ImportContext, const UE::FUsdPrim& StartPrim, const UE::FUsdPrim& ModelPrim, TArray<FUsdAssetPrimToImport>& OutAssetsToImport, bool bRecursive) const
{
#if USE_USD_SDK
	const FName PrimName = StartPrim.GetName();

	bool bHasUnrealAssetPath = IUsdPrim::GetUnrealAssetPath( StartPrim ).size() > 0;
	bool bHasUnrealActorClass = IUsdPrim::GetUnrealActorClass( StartPrim ).size() > 0;

	EUsdPurpose EnabledPurposes = EUsdPurpose::Render;
	if (UDEPRECATED_UUSDSceneImportOptions* SceneImportOptions = Cast<UDEPRECATED_UUSDSceneImportOptions>(ImportContext.ImportOptions_DEPRECATED))
	{
		EnabledPurposes = (EUsdPurpose)SceneImportOptions->PurposesToImport;
	}

	if (EnumHasAllFlags(EnabledPurposes, IUsdPrim::GetPurpose(StartPrim)) &&
		IUsdPrim::HasGeometryDataOrLODVariants(StartPrim))
	{
		FUsdAssetPrimToImport NewTopLevelPrim;

		FString FinalPrimName;
		// if the prim has a path use that as the final name
		if (bHasUnrealAssetPath)
		{
			FinalPrimName = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealAssetPath( StartPrim ).c_str() );
		}
		else
		{
			FinalPrimName = PrimName.ToString();
		}

		NewTopLevelPrim.Prim = StartPrim;
		NewTopLevelPrim.AssetPath = FinalPrimName;

		FindMeshChildren(ImportContext, StartPrim, true, NewTopLevelPrim.MeshPrims);

		for ( const UE::FUsdPrim& MeshPrim : NewTopLevelPrim.MeshPrims)
		{
			NewTopLevelPrim.NumLODs = FMath::Max(NewTopLevelPrim.NumLODs, IUsdPrim::GetNumLODs( MeshPrim ));
		}

		OutAssetsToImport.Add(NewTopLevelPrim);
	}
	else if(bRecursive)
	{
		const bool bTraverseInstanceProxies = true;
		for ( const UE::FUsdPrim& Child : StartPrim.GetFilteredChildren( bTraverseInstanceProxies ) )
		{
			FindMeshAssetsToImport(ImportContext, Child, StartPrim, OutAssetsToImport);
		}
	}
#endif // #if USE_USD_SDK
}

void UDEPRECATED_UUSDPrimResolver::FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnDatas) const
{
#if USE_USD_SDK
	if ( IUsdPrim::HasTransform( ImportContext.RootPrim ) )
	{
		FindActorsToSpawn_Recursive(ImportContext, ImportContext.RootPrim, UE::FUsdPrim(), OutActorSpawnDatas);
	}
	else
	{
		const bool bTraverseInstanceProxies = true;
		for (const UE::FUsdPrim& Child : ImportContext.RootPrim.GetFilteredChildren( bTraverseInstanceProxies ))
		{
			FindActorsToSpawn_Recursive(ImportContext, Child, UE::FUsdPrim(), OutActorSpawnDatas);
		}
	}
#endif // #if USE_USD_SDK
}

AActor* UDEPRECATED_UUSDPrimResolver::SpawnActor(FUSDSceneImportContext& ImportContext, const FActorSpawnData& SpawnData)
{
	AActor* ModifiedActor = nullptr;

#if USE_USD_SDK
	UDEPRECATED_UUSDImporter* USDImporter = IUSDImporterModule::Get().GetImporter();

	UDEPRECATED_UUSDSceneImportOptions* ImportOptions = Cast<UDEPRECATED_UUSDSceneImportOptions>(ImportContext.ImportOptions_DEPRECATED);

	const bool bFlattenHierarchy = ImportOptions->bFlattenHierarchy;

	// Look for an existing actor and decide what to do based on the users choice
	AActor* ExistingActor = ImportContext.ExistingActors.FindRef(SpawnData.ActorName);

	bool bShouldSpawnNewActor = true;
	EExistingActorPolicy ExistingActorPolicy = ImportOptions->ExistingActorPolicy;

	const FTransform ActorTransform = SpawnData.WorldTransform;

	if (ExistingActor && ExistingActorPolicy == EExistingActorPolicy::UpdateTransform)
	{
		ExistingActor->Modify();
		ModifiedActor = ExistingActor;

		ExistingActor->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
		ExistingActor->SetActorRelativeTransform(ActorTransform);

		bShouldSpawnNewActor = false;
	}
	else if (ExistingActor && ExistingActorPolicy == EExistingActorPolicy::Ignore)
	{
		// Ignore this actor and do nothing
		bShouldSpawnNewActor = false;
	}

	if (bShouldSpawnNewActor)
	{
		UActorFactory* ActorFactory = ImportContext.EmptyActorFactory;

		AActor* SpawnedActor = nullptr;

		// The asset which should be used to spawn the actor
		UObject* ActorAsset = nullptr;

		TArray<UObject*> ImportedAssets;

		// Note: an asset path on the actor is multually exclusive with importing geometry.
		if (SpawnData.AssetPath.IsEmpty() && SpawnData.AssetsToImport.Num() > 0)
		{
			ImportedAssets = USDImporter->ImportMeshes(ImportContext, SpawnData.AssetsToImport);

			// If there is more than one asset imported just use the first one.  This may be invalid if the actor only supports one mesh
			// so we will warn about that below if we can.
			ActorAsset = ImportedAssets.Num() > 0 ? ImportedAssets[0] : nullptr;
		}
		else if(!SpawnData.AssetPath.IsEmpty() && SpawnData.AssetsToImport.Num() > 0)
		{
			ImportContext.AddErrorMessage(EMessageSeverity::Warning,
				FText::Format(
					LOCTEXT("ConflictWithAssetPathAndMeshes", "Actor has an asset path '{0} but also contains meshes {1} to import. Meshes will be ignored"),
					FText::FromString(SpawnData.AssetPath),
					FText::AsNumber(SpawnData.AssetsToImport.Num())
				)
			);
		}

		if (!SpawnData.ActorClassName.IsEmpty())
		{
			TSubclassOf<AActor> ActorClass = FindActorClass(ImportContext, SpawnData);
			if (ActorClass)
			{
				SpawnedActor = ImportContext.World->SpawnActor<AActor>(ActorClass);
			}

		}
		else if (!SpawnData.AssetPath.IsEmpty())
		{
			ActorAsset = LoadObject<UObject>(nullptr, *SpawnData.AssetPath);
			if (!ActorAsset)
			{
				ImportContext.AddErrorMessage(
					EMessageSeverity::Error, FText::Format(LOCTEXT("CouldNotFindUnrealAssetPath", "Could not find Unreal Asset '{0}' for USD prim '{1}'"),
						FText::FromString(SpawnData.AssetPath),
						FText::FromString(SpawnData.ActorPrim.GetPrimPath().GetString())));

				UE_LOG(LogUsd, Error, TEXT("Could not find Unreal Asset '%s' for USD prim '%s'"), *SpawnData.AssetPath, *SpawnData.ActorName.ToString());
			}
		}

		if (SpawnData.ActorClassName.IsEmpty() && ActorAsset)
		{
			UClass* AssetClass = ActorAsset->GetClass();

			UActorFactory* Factory = ImportContext.UsedFactories.FindRef(AssetClass);
			if (!Factory)
			{
				Factory = FActorFactoryAssetProxy::GetFactoryForAssetObject(ActorAsset);
				if (Factory)
				{
					ImportContext.UsedFactories.Add(AssetClass, Factory);
				}
				else
				{
					ImportContext.AddErrorMessage(
						EMessageSeverity::Error, FText::Format(LOCTEXT("CouldNotFindActorFactory", "Could not find an actor type to spawn for '{0}'"),
							FText::FromString(ActorAsset->GetName()))
					);
				}
			}

			ActorFactory = Factory;

		}

		if (!SpawnedActor && ActorFactory)
		{
			if ( ImportedAssets.Num() > 0 )
			{
				if ( ImportedAssets.Num() > 1 )
				{
					SpawnedActor = ImportContext.World->SpawnActor( AActor::StaticClass() );
					USceneComponent* SceneComponent = NewObject< USceneComponent >( SpawnedActor, SpawnData.ActorName );
					SpawnedActor->AddInstanceComponent( SceneComponent );
					SpawnedActor->SetRootComponent( SceneComponent );
				}
				else
				{
					SpawnedActor = ImportContext.World->SpawnActor( AActor::StaticClass() );
				}

				const FUsdStageInfo StageInfo( ImportContext.Stage );

				int32 AssetIndex = 0;
				for ( UObject* ImportedAsset : ImportedAssets )
				{
					FUsdAssetPrimToImport UsdAssetPrimToImport = SpawnData.AssetsToImport[ AssetIndex ];
					UStaticMesh* ImportedStaticMesh = Cast< UStaticMesh >( ImportedAsset );

					UE::FUsdPrim ParentPrim = UsdAssetPrimToImport.Prim.GetParent();

					TArray< UE::FUsdPrim > ParentPrims;

					while ( ParentPrim && ParentPrim != SpawnData.ActorPrim )
					{
						ParentPrims.Add( ParentPrim );
						ParentPrim = ParentPrim.GetParent();
					}

					FTransform LocalTransform;

					for ( int32 ParentPrimIndex = ParentPrims.Num() - 1; ParentPrimIndex >= 0; --ParentPrimIndex )
					{
						ParentPrim = ParentPrims[ ParentPrimIndex ];

						FTransform ParentTransform;
						UsdToUnreal::ConvertXformable( ImportContext.Stage, UE::FUsdTyped( ParentPrim ), ParentTransform, 0.0 );
						LocalTransform = ParentTransform * LocalTransform;
					}

					FName ComponentBaseName = UsdAssetPrimToImport.Prim.GetName();
					FName ComponentName = MakeUniqueObjectName( SpawnedActor, UStaticMeshComponent::StaticClass(), ComponentBaseName );

					UStaticMeshComponent* StaticMeshComponent = NewObject< UStaticMeshComponent >( SpawnedActor, ComponentName );
					StaticMeshComponent->SetStaticMesh( ImportedStaticMesh );

					// Don't add the prim transform if its the same prim used for the actor as it's already accounted for in the ActorTransform
					if ( UsdAssetPrimToImport.Prim != SpawnData.ActorPrim )
					{
						FTransform AssetTransform;
						UsdToUnreal::ConvertXformable( ImportContext.Stage, UE::FUsdTyped( UsdAssetPrimToImport.Prim ), AssetTransform, 0.0 );
						LocalTransform = AssetTransform * LocalTransform;
					}

					StaticMeshComponent->SetRelativeTransform( LocalTransform );

					SpawnedActor->AddInstanceComponent( StaticMeshComponent );

					USceneComponent* AttachComponent = SpawnedActor->GetRootComponent();
					if ( !AttachComponent )
					{
						SpawnedActor->SetRootComponent( StaticMeshComponent );
					}
					else
					{
						StaticMeshComponent->AttachToComponent( AttachComponent, FAttachmentTransformRules::KeepRelativeTransform );
					}

					++AssetIndex;
				}
			}
			else
			{
				// We'll set the name later by using the ActorLabels cache
				SpawnedActor = ActorFactory->CreateActor(ActorAsset, ImportContext.World->GetCurrentLevel(), FTransform::Identity);

				// For empty group actors set their initial mobility to static
				if ( ActorFactory == ImportContext.EmptyActorFactory )
				{
					SpawnedActor->GetRootComponent()->SetMobility(EComponentMobility::Static);
				}
			}
		}

		if(SpawnedActor)
		{
			SpawnedActor->SetActorRelativeTransform( SpawnedActor->GetActorTransform() * ActorTransform );

			if (SpawnData.AttachParentPrim)
			{
				// Spawned actor should be attached to a parent
				AActor* AttachPrim = nullptr;
				const FString ParentPrimName = SpawnData.AttachParentPrim.GetName().ToString();

				if ( SpawnData.AttachParentPrim && PrimToActorMap.Contains( ParentPrimName ) )
				{
					AttachPrim = PrimToActorMap[ ParentPrimName ];
				}

				if ( !bFlattenHierarchy )
				{
					if (AttachPrim)
					{
						SpawnedActor->AttachToActor(AttachPrim, FAttachmentTransformRules::KeepRelativeTransform);
					}
				}
				else
				{
					SpawnedActor->SetActorTransform( SpawnedActor->GetActorTransform() * AttachPrim->GetActorTransform() );
				}
			}

			FActorLabelUtilities::SetActorLabelUnique(SpawnedActor, SpawnData.ActorName.ToString(), &ImportContext.ActorLabels);
			ImportContext.ActorLabels.Add(SpawnedActor->GetActorLabel());
		}


		ModifiedActor = SpawnedActor;
	}

	const FString PrimName = SpawnData.ActorPrim.GetName().ToString();
	PrimToActorMap.Add( PrimName ) = ModifiedActor;
#endif // #if USE_USD_SDK

	return ModifiedActor;
}


TSubclassOf<AActor> UDEPRECATED_UUSDPrimResolver::FindActorClass(FUSDSceneImportContext& ImportContext, const FActorSpawnData& SpawnData) const
{
	TSubclassOf<AActor> ActorClass = nullptr;

	FString ActorClassName = SpawnData.ActorClassName;
	FName ActorClassFName = *ActorClassName;

	// Attempt to use the fully qualified path first.  If not use the expensive slow path.
	{
		ActorClass = LoadClass<AActor>(nullptr, *ActorClassName, nullptr);
	}

	if (!ActorClass)
	{
		UObject* TestObject = nullptr;
		TArray<FAssetData> AssetDatas;

		AssetRegistry->GetAssetsByClass(UBlueprint::StaticClass()->GetFName(), AssetDatas);

		UClass* TestClass = nullptr;
		for (const FAssetData& AssetData : AssetDatas)
		{
			if (AssetData.AssetName == ActorClassFName)
			{
				TestClass = Cast<UBlueprint>(AssetData.GetAsset())->GeneratedClass;
				break;
			}
		}

		if (TestClass && TestClass->IsChildOf<AActor>())
		{
			ActorClass = TestClass;
		}

		if (!ActorClass)
		{
			ImportContext.AddErrorMessage(
				EMessageSeverity::Error, FText::Format(LOCTEXT("CouldNotFindUnrealActorClass", "Could not find Unreal Actor Class '{0}' for USD prim '{1}'"),
					FText::FromString(ActorClassName),
					FText::FromString(SpawnData.ActorPrim.GetPrimPath().GetString())));

		}
	}

	return ActorClass;
}

void UDEPRECATED_UUSDPrimResolver::FindMeshChildren(FUsdImportContext& ImportContext, const UE::FUsdPrim& ParentPrim, bool bOnlyLODRoots, TArray< UE::FUsdPrim >& OutMeshChildren) const
{
#if USE_USD_SDK
	const bool bIncludeLODs = bOnlyLODRoots;

	EUsdPurpose EnabledPurposes = EUsdPurpose::Default | EUsdPurpose::Render;
	if (UDEPRECATED_UUSDSceneImportOptions* SceneImportOptions = Cast<UDEPRECATED_UUSDSceneImportOptions>(ImportContext.ImportOptions_DEPRECATED))
	{
		EnabledPurposes = (EUsdPurpose)SceneImportOptions->PurposesToImport;
	}
	bool bValidPurpose = EnumHasAllFlags(EnabledPurposes, IUsdPrim::GetPurpose(ParentPrim));

	if(bOnlyLODRoots && IUsdPrim::GetNumLODs( ParentPrim ) > 0 && bValidPurpose)
	{
		// We're only looking for lod roots and this prim has LODs so add the prim and dont recurse into children
		OutMeshChildren.Add(ParentPrim);
	}
	else
	{
		if (IUsdPrim::HasGeometryData(ParentPrim) && bValidPurpose)
		{
			OutMeshChildren.Add(ParentPrim);
		}

		const bool bTraverseInstanceProxies = true;
		for ( const UE::FUsdPrim& Child : ParentPrim.GetFilteredChildren( bTraverseInstanceProxies ) )
		{
			if (!IUsdPrim::IsKindChildOf(Child, USDKindTypes::Component))
			{
				FindMeshChildren(ImportContext, Child, bOnlyLODRoots, OutMeshChildren);
			}
		}
	}
#endif // #if USE_USD_SDK
}

void UDEPRECATED_UUSDPrimResolver::FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const UE::FUsdPrim& Prim, const UE::FUsdPrim& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const
{
#if USE_USD_SDK
	TArray<FActorSpawnData>* SpawnDataArray = &OutSpawnDatas;

	UDEPRECATED_UUSDSceneImportOptions* ImportOptions = Cast<UDEPRECATED_UUSDSceneImportOptions>(ImportContext.ImportOptions_DEPRECATED);

	FActorSpawnData SpawnData;

	FString AssetPath;
	FName ActorClassName;
	if ( IUsdPrim::HasTransform(Prim) )
	{
		if ( IUsdPrim::GetUnrealActorClass( Prim ).size() > 0 )
		{
			SpawnData.ActorClassName = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealActorClass( Prim ) );
		}

		if ( IUsdPrim::GetUnrealAssetPath( Prim ).size() > 0 )
		{
			SpawnData.AssetPath = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealAssetPath( Prim ) );
		}

		FindMeshAssetsToImport(ImportContext, Prim, Prim, SpawnData.AssetsToImport, false);

		FName PrimName = Prim.GetName();
		SpawnData.ActorName = PrimName;
		UsdToUnreal::ConvertXformable( ImportContext.Stage, UE::FUsdTyped( Prim ), SpawnData.WorldTransform, 0.0 );
		SpawnData.AttachParentPrim = ParentPrim;
		SpawnData.ActorPrim = Prim;

		if (ImportOptions->ExistingActorPolicy == EExistingActorPolicy::Replace && ImportContext.ExistingActors.Contains(SpawnData.ActorName))
		{
			ImportContext.ActorsToDestroy.Add(SpawnData.ActorName);
		}

		OutSpawnDatas.Add(SpawnData);
	}

	if (!ImportContext.bFindUnrealAssetReferences || AssetPath.IsEmpty())
	{
		const bool bbTraverseInstanceProxies = true;
		for (UE::FUsdPrim Child : Prim.GetFilteredChildren( bbTraverseInstanceProxies ))
		{
			FindActorsToSpawn_Recursive(ImportContext, Child, Prim, *SpawnDataArray);
		}
	}
#endif // #if USE_USD_SDK
}


bool UDEPRECATED_UUSDPrimResolver::IsValidPathForImporting(const FString& TestPath) const
{
	return FPackageName::GetPackageMountPoint(TestPath) != NAME_None;
}

#undef LOCTEXT_NAMESPACE
