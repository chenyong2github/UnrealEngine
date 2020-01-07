// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimResolver.h"

#include "USDConversionUtils.h"
#include "USDImportOptions.h"
#include "USDImporter.h"
#include "USDTypesConversion.h"

#include "ActorFactories/ActorFactory.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "AssetSelection.h"
#include "Engine/StaticMesh.h"
#include "IUSDImporterModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"


#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usd/prim.h"

#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDImportPlugin"


void UUSDPrimResolver::Init()
{
	AssetRegistry = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
}

void UUSDPrimResolver::FindMeshAssetsToImport(FUsdImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& StartPrim, const TUsdStore< pxr::UsdPrim >& ModelPrim, TArray<FUsdAssetPrimToImport>& OutAssetsToImport, bool bRecursive) const
{
	const FString PrimName = UsdToUnreal::ConvertString(StartPrim.Get().GetName().GetString());

	const FString KindName = UsdToUnreal::ConvertString(IUsdPrim::GetKind( StartPrim.Get() ).GetString());

	bool bHasUnrealAssetPath = IUsdPrim::GetUnrealAssetPath( StartPrim.Get() ).size() > 0;
	bool bHasUnrealActorClass = IUsdPrim::GetUnrealActorClass( StartPrim.Get() ).size() > 0;

	if ( !IUsdPrim::IsProxyOrGuide(StartPrim.Get()) )
	{
		if (IUsdPrim::HasGeometryDataOrLODVariants(StartPrim.Get()))
		{
			FUsdAssetPrimToImport NewTopLevelPrim;

			FString FinalPrimName;
			// if the prim has a path use that as the final name
			if (bHasUnrealAssetPath)
			{
				FinalPrimName = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealAssetPath( StartPrim.Get() ).c_str() );
			}
			else
			{
				FinalPrimName = PrimName;
			}

			NewTopLevelPrim.Prim = StartPrim;
			NewTopLevelPrim.AssetPath = FinalPrimName;

			FindMeshChildren(ImportContext, StartPrim, true, NewTopLevelPrim.MeshPrims);

			for ( const TUsdStore< pxr::UsdPrim >& MeshPrim : NewTopLevelPrim.MeshPrims)
			{
				NewTopLevelPrim.NumLODs = FMath::Max(NewTopLevelPrim.NumLODs, IUsdPrim::GetNumLODs( *MeshPrim ));
			}

			OutAssetsToImport.Add(NewTopLevelPrim);
		}
		else if(bRecursive)
		{
			for ( pxr::UsdPrim Child : StartPrim.Get().GetChildren() )
			{
				FindMeshAssetsToImport(ImportContext, Child, StartPrim, OutAssetsToImport);
			}
		}
	}
}

void UUSDPrimResolver::FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnDatas) const
{
	if ( IUsdPrim::HasTransform( *ImportContext.RootPrim ) )
	{
		FindActorsToSpawn_Recursive(ImportContext, ImportContext.RootPrim, pxr::UsdPrim(), OutActorSpawnDatas);
	}
	else
	{
		for (pxr::UsdPrim Child : ImportContext.RootPrim.Get().GetChildren())
		{
			FindActorsToSpawn_Recursive(ImportContext, Child, pxr::UsdPrim(), OutActorSpawnDatas);
		}
	}
}

AActor* UUSDPrimResolver::SpawnActor(FUSDSceneImportContext& ImportContext, const FActorSpawnData& SpawnData)
{
	UUSDImporter* USDImporter = IUSDImporterModule::Get().GetImporter();

	UUSDSceneImportOptions* ImportOptions = Cast<UUSDSceneImportOptions>(ImportContext.ImportOptions);

	const bool bFlattenHierarchy = ImportOptions->bFlattenHierarchy;

	AActor* ModifiedActor = nullptr;

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
						FText::FromString(UsdToUnreal::ConvertString(SpawnData.ActorPrim.Get().GetPath().GetString()))));

				UE_LOG(LogUSDImport, Error, TEXT("Could not find Unreal Asset '%s' for USD prim '%s'"), *SpawnData.AssetPath, *SpawnData.ActorName.ToString());
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

				int32 AssetIndex = 0;
				for ( UObject* ImportedAsset : ImportedAssets )
				{
					FUsdAssetPrimToImport UsdAssetPrimToImport = SpawnData.AssetsToImport[ AssetIndex ];
					UStaticMesh* ImportedStaticMesh = Cast< UStaticMesh >( ImportedAsset );

					pxr::UsdPrim ParentPrim = UsdAssetPrimToImport.Prim.Get().GetParent();

					TArray< pxr::UsdPrim > ParentPrims;

					while ( ParentPrim && ParentPrim != SpawnData.ActorPrim.Get() )
					{
						ParentPrims.Add( ParentPrim );
						ParentPrim = ParentPrim.GetParent();
					}

					FTransform LocalTransform;

					for ( int32 ParentPrimIndex = ParentPrims.Num() - 1; ParentPrimIndex >= 0; --ParentPrimIndex )
					{
						ParentPrim = ParentPrims[ ParentPrimIndex ];
						LocalTransform = UsdToUnreal::ConvertMatrix( *ImportContext.Stage, IUsdPrim::GetLocalTransform( ParentPrim ) ) * LocalTransform;
					}

					FName ComponentBaseName = *UsdToUnreal::ConvertString( UsdAssetPrimToImport.Prim.Get().GetName().GetString().c_str() );
					FName ComponentName = MakeUniqueObjectName( SpawnedActor, UStaticMeshComponent::StaticClass(), ComponentBaseName );

					UStaticMeshComponent* StaticMeshComponent = NewObject< UStaticMeshComponent >( SpawnedActor, ComponentName );
					StaticMeshComponent->SetStaticMesh( ImportedStaticMesh );

					// Don't add the prim transform if its the same prim used for the actor as it's already accounted for in the ActorTransform
					if ( UsdAssetPrimToImport.Prim.Get() != SpawnData.ActorPrim.Get() )
					{
						LocalTransform = UsdToUnreal::ConvertMatrix( *ImportContext.Stage, IUsdPrim::GetLocalTransform( UsdAssetPrimToImport.Prim.Get() ) ) * LocalTransform;
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
				SpawnedActor = ActorFactory->CreateActor(ActorAsset, ImportContext.World->GetCurrentLevel(), FTransform::Identity, RF_Transactional, SpawnData.ActorName);

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

			if (SpawnData.AttachParentPrim.Get())
			{
				// Spawned actor should be attached to a parent
				AActor* AttachPrim = nullptr;
				const FString ParentPrimName = UsdToUnreal::ConvertString( SpawnData.AttachParentPrim.Get().GetName().GetString().c_str() );

				if ( SpawnData.AttachParentPrim.Get() && PrimToActorMap.Contains( ParentPrimName ) )
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

	const FString PrimName = UsdToUnreal::ConvertString( SpawnData.ActorPrim.Get().GetName().GetString().c_str() );
	PrimToActorMap.Add( PrimName ) = ModifiedActor;

	return ModifiedActor;
}


TSubclassOf<AActor> UUSDPrimResolver::FindActorClass(FUSDSceneImportContext& ImportContext, const FActorSpawnData& SpawnData) const
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
					FText::FromString(UsdToUnreal::ConvertString(SpawnData.ActorPrim.Get().GetPath().GetString()))));

		}
	}

	return ActorClass;
}

void UUSDPrimResolver::FindMeshChildren(FUsdImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& ParentPrim, bool bOnlyLODRoots, TArray< TUsdStore< pxr::UsdPrim > >& OutMeshChildren) const
{
	const FString PrimName = UsdToUnreal::ConvertString(ParentPrim.Get().GetName().GetString());

	const FString KindName = UsdToUnreal::ConvertString(IUsdPrim::GetKind( ParentPrim.Get() ).GetString() );

	const bool bIncludeLODs = bOnlyLODRoots;

	if(bOnlyLODRoots && IUsdPrim::GetNumLODs( ParentPrim.Get() ) > 0)
	{
		// We're only looking for lod roots and this prim has LODs so add the prim and dont recurse into children
		OutMeshChildren.Add(ParentPrim);
	}
	else
	{
		if (IUsdPrim::HasGeometryData(ParentPrim.Get()))
		{
			OutMeshChildren.Add(ParentPrim);
		}

		for ( pxr::UsdPrim Child : ParentPrim.Get().GetChildren() )
		{
			if (!IUsdPrim::IsProxyOrGuide(Child) && !IUsdPrim::IsKindChildOf(Child, USDKindTypes::Component))
			{
				FindMeshChildren(ImportContext, Child, bOnlyLODRoots, OutMeshChildren);
			}
		}
	}
}

void UUSDPrimResolver::FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& Prim, const TUsdStore< pxr::UsdPrim >& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const
{
	TArray<FActorSpawnData>* SpawnDataArray = &OutSpawnDatas;

	UUSDSceneImportOptions* ImportOptions = Cast<UUSDSceneImportOptions>(ImportContext.ImportOptions);

	FActorSpawnData SpawnData;

	FString AssetPath;
	FName ActorClassName;
	if ( IUsdPrim::HasTransform(*Prim) )
	{
		if ( IUsdPrim::GetUnrealActorClass( *Prim ).size() > 0 )
		{
			SpawnData.ActorClassName = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealActorClass( *Prim ) );
		}

		if ( IUsdPrim::GetUnrealAssetPath( *Prim ).size() > 0 )
		{
			SpawnData.AssetPath = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealAssetPath( *Prim ) );
		}

		FindMeshAssetsToImport(ImportContext, Prim, Prim, SpawnData.AssetsToImport, false);

		FName PrimName = UsdToUnreal::ConvertName(Prim.Get().GetName().GetString());
		SpawnData.ActorName = PrimName;
		SpawnData.WorldTransform = UsdToUnreal::ConvertMatrix( *ImportContext.Stage, IUsdPrim::GetLocalTransform( *Prim ) );
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
		for (pxr::UsdPrim Child : Prim.Get().GetChildren())
		{
			FindActorsToSpawn_Recursive(ImportContext, Child, Prim, *SpawnDataArray);
		}
	}
}


bool UUSDPrimResolver::IsValidPathForImporting(const FString& TestPath) const
{
	return FPackageName::GetPackageMountPoint(TestPath) != NAME_None;
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
