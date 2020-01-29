// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimResolverKind.h"

#include "USDConversionUtils.h"
#include "USDImportOptions.h"
#include "USDImporter.h"
#include "USDSceneImportFactory.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Modules/ModuleManager.h"


#if USE_USD_SDK
void UUSDPrimResolverKind::FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnData) const
{
	FindActorsToSpawn_Recursive(ImportContext, *ImportContext.RootPrim, pxr::UsdPrim(), OutActorSpawnData);
}

void UUSDPrimResolverKind::FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& Prim, const TUsdStore< pxr::UsdPrim >& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const
{
	FName PrimName = UsdToUnreal::ConvertName(Prim.Get().GetName().GetString());

	UUSDSceneImportOptions* ImportOptions = Cast<UUSDSceneImportOptions>(ImportContext.ImportOptions);

	// Parent/child hierarchy will be ignored unless the kind is a group.  Keep track of the current parent and use it 
	TUsdStore< pxr::UsdPrim > GroupParent = ParentPrim;

	if (IUsdPrim::IsKindChildOf( *Prim, USDKindTypes::Component ))
	{
		bool bHasUnrealAssetPath = IUsdPrim::GetUnrealAssetPath( *Prim ).size() > 0;
		bool bHasUnrealActorClass = IUsdPrim::GetUnrealActorClass( *Prim ).size() > 0;

		FActorSpawnData SpawnData;

		if (bHasUnrealActorClass)
		{
			SpawnData.ActorClassName = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealActorClass( *Prim ) );

			UE_LOG(LogUSDImport, Log, TEXT("Adding %s Actor with custom actor class to spawn"), *PrimName.ToString());
		}
		else if(bHasUnrealAssetPath)
		{
			SpawnData.AssetPath = UsdToUnreal::ConvertString( IUsdPrim::GetUnrealAssetPath( *Prim ) );
			FAssetData AssetData = AssetRegistry->GetAssetByObjectPath(FName(*SpawnData.AssetPath));
			if ( !AssetData.IsValid() || AssetData.AssetClass == UStaticMesh::StaticClass()->GetFName() )
			{
				// If the object is a static mesh allow it to be imported.  Import settings may override this though
				// Find the asset associated with this object that should be imported
				FindMeshAssetsToImport(ImportContext, *Prim, GroupParent, SpawnData.AssetsToImport);
			}

			UE_LOG(LogUSDImport, Log, TEXT("Adding %s Actor with custom asset path to spawn"), *PrimName.ToString());
		}
		else 
		{
			FindMeshAssetsToImport(ImportContext, *Prim, GroupParent, SpawnData.AssetsToImport);

			UE_LOG(LogUSDImport, Log, TEXT("Adding %s Actor with %d meshes"), *PrimName.ToString(), SpawnData.AssetsToImport.Num());
		}

		SpawnData.WorldTransform = UsdToUnreal::ConvertMatrix( UsdUtils::GetUsdStageAxis( *ImportContext.Stage ), IUsdPrim::GetLocalTransform( *Prim ) );
		SpawnData.ActorPrim = Prim;
		SpawnData.ActorName = PrimName;
		SpawnData.AttachParentPrim = GroupParent;

		if (ImportOptions->ExistingActorPolicy == EExistingActorPolicy::Replace && ImportContext.ExistingActors.Contains(SpawnData.ActorName))
		{
			ImportContext.ActorsToDestroy.Add(SpawnData.ActorName);
		}

		OutSpawnDatas.Add(SpawnData);

	
	}
	else if ( IUsdPrim::IsKindChildOf( *Prim, USDKindTypes::Group ))
	{
		// Blank actor for group prims
		FActorSpawnData SpawnData;

		SpawnData.ActorPrim = Prim;
		SpawnData.ActorName = PrimName;

		SpawnData.WorldTransform = UsdToUnreal::ConvertMatrix( UsdUtils::GetUsdStageAxis( *ImportContext.Stage ), IUsdPrim::GetLocalTransform( *Prim ) );
		SpawnData.AttachParentPrim = GroupParent;

		// New parent of all children is this prim
		GroupParent = Prim;
		OutSpawnDatas.Add(SpawnData);

		if (ImportOptions->ExistingActorPolicy == EExistingActorPolicy::Replace && ImportContext.ExistingActors.Contains(SpawnData.ActorName))
		{
			ImportContext.ActorsToDestroy.Add(SpawnData.ActorName);
		}

		UE_LOG(LogUSDImport, Log, TEXT("Adding %s Group Actor to spawn"), *PrimName.ToString());
	}

	for (pxr::UsdPrim Child : Prim.Get().GetFilteredChildren( pxr::UsdTraverseInstanceProxies() ))
	{
		FindActorsToSpawn_Recursive(ImportContext, Child, GroupParent, OutSpawnDatas);
	}
}
#endif // #if USE_USD_SDK
