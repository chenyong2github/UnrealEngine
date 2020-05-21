// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimResolverKind.h"

#include "USDConversionUtils.h"
#include "USDImportOptions.h"
#include "USDImporter.h"
#include "USDLog.h"
#include "USDPrimConversion.h"
#include "USDSceneImportFactory.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "UsdWrappers/UsdTyped.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Modules/ModuleManager.h"


#if USE_USD_SDK
void UDEPRECATED_UUSDPrimResolverKind::FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnData) const
{
	FindActorsToSpawn_Recursive(ImportContext, ImportContext.RootPrim, UE::FUsdPrim(), OutActorSpawnData);
}

void UDEPRECATED_UUSDPrimResolverKind::FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const UE::FUsdPrim& Prim, const UE::FUsdPrim& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const
{
	FString PrimName = Prim.GetName().ToString();

	UDEPRECATED_UUSDSceneImportOptions* ImportOptions = Cast<UDEPRECATED_UUSDSceneImportOptions>(ImportContext.ImportOptions_DEPRECATED);

	// Parent/child hierarchy will be ignored unless the kind is a group.  Keep track of the current parent and use it
	UE::FUsdPrim GroupParent = ParentPrim;

	if (IUsdPrim::IsKindChildOf( Prim, USDKindTypes::Component ))
	{
		bool bHasUnrealAssetPath = IUsdPrim::GetUnrealAssetPath( Prim ).size() > 0;
		bool bHasUnrealActorClass = IUsdPrim::GetUnrealActorClass( Prim ).size() > 0;

		FActorSpawnData SpawnData;

		if (bHasUnrealActorClass)
		{
			SpawnData.ActorClassName = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealActorClass( Prim ) );

			UE_LOG(LogUsd, Log, TEXT("Adding %s Actor with custom actor class to spawn"), *PrimName);
		}
		else if(bHasUnrealAssetPath)
		{
			SpawnData.AssetPath = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealAssetPath( Prim ) );
			FAssetData AssetData = AssetRegistry->GetAssetByObjectPath(FName(*SpawnData.AssetPath));
			if ( !AssetData.IsValid() || AssetData.AssetClass == UStaticMesh::StaticClass()->GetFName() )
			{
				// If the object is a static mesh allow it to be imported.  Import settings may override this though
				// Find the asset associated with this object that should be imported
				FindMeshAssetsToImport(ImportContext, Prim, GroupParent, SpawnData.AssetsToImport);
			}

			UE_LOG(LogUsd, Log, TEXT("Adding %s Actor with custom asset path to spawn"), *PrimName);
		}
		else
		{
			FindMeshAssetsToImport(ImportContext, Prim, GroupParent, SpawnData.AssetsToImport);

			UE_LOG(LogUsd, Log, TEXT("Adding %s Actor with %d meshes"), *PrimName, SpawnData.AssetsToImport.Num());
		}

		UsdToUnreal::ConvertXformable( ImportContext.Stage, UE::FUsdTyped( Prim ), SpawnData.WorldTransform, 0.0 );
		SpawnData.ActorPrim = Prim;
		SpawnData.ActorName = *PrimName;
		SpawnData.AttachParentPrim = GroupParent;

		if (ImportOptions->ExistingActorPolicy == EExistingActorPolicy::Replace && ImportContext.ExistingActors.Contains(SpawnData.ActorName))
		{
			ImportContext.ActorsToDestroy.Add(SpawnData.ActorName);
		}

		OutSpawnDatas.Add(SpawnData);


	}
	else if ( IUsdPrim::IsKindChildOf( Prim, USDKindTypes::Group ))
	{
		// Blank actor for group prims
		FActorSpawnData SpawnData;

		SpawnData.ActorPrim = Prim;
		SpawnData.ActorName = *PrimName;

		UsdToUnreal::ConvertXformable( ImportContext.Stage, UE::FUsdTyped( Prim ), SpawnData.WorldTransform, 0.0 );
		SpawnData.AttachParentPrim = GroupParent;

		// New parent of all children is this prim
		GroupParent = Prim;
		OutSpawnDatas.Add(SpawnData);

		if (ImportOptions->ExistingActorPolicy == EExistingActorPolicy::Replace && ImportContext.ExistingActors.Contains(SpawnData.ActorName))
		{
			ImportContext.ActorsToDestroy.Add(SpawnData.ActorName);
		}

		UE_LOG(LogUsd, Log, TEXT("Adding %s Group Actor to spawn"), *PrimName);
	}

	const bool bTraverseInstanceProxies = true;
	for (const UE::FUsdPrim& Child : Prim.GetFilteredChildren( bTraverseInstanceProxies ))
	{
		FindActorsToSpawn_Recursive(ImportContext, Child, GroupParent, OutSpawnDatas);
	}
}
#endif // #if USE_USD_SDK
