// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeSkeletonFactory.h"

#include "Animation/Skeleton.h"
#include "InterchangeImportCommon.h"
#include "InterchangeSkeletonNode.h"
#include "InterchangeSourceData.h"
#include "LogInterchangeImportPlugin.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"



UClass* UInterchangeSkeletonFactory::GetFactoryClass() const
{
	return USkeleton::StaticClass();
}

UObject* UInterchangeSkeletonFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments) const
{
	UObject* Skeleton = nullptr;

#if WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletonNode* SkeletonNode = Cast<UInterchangeSkeletonNode>(Arguments.AssetNode);
	if (SkeletonNode == nullptr)
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		Skeleton = NewObject<UObject>(Arguments.Parent, USkeleton::StaticClass(), *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(USkeleton::StaticClass()))
	{
		//This is a reimport, we are just re-updating the source data
		Skeleton = ExistingAsset;
	}

	if (!Skeleton)
	{
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create Skeleton asset %s"), *Arguments.AssetName);
		return nullptr;
	}
	Skeleton->PreEditChange(nullptr);
#endif //WITH_EDITORONLY_DATA
	return Skeleton;
}

UObject* UInterchangeSkeletonFactory::CreateAsset(const UInterchangeSkeletonFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Cannot import Skeleton asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletonNode* SkeletonNode = Cast<UInterchangeSkeletonNode>(Arguments.AssetNode);
	if (SkeletonNode == nullptr)
	{
		return nullptr;
	}

	const UClass* SkeletonClass = SkeletonNode->GetAssetClass();
	check(SkeletonClass && SkeletonClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* SkeletonObject = nullptr;
	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		SkeletonObject = NewObject<UObject>(Arguments.Parent, SkeletonClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(SkeletonClass))
	{
		//This is a reimport, we are just re-updating the source data
		SkeletonObject = ExistingAsset;
	}

	if (!SkeletonObject)
	{
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create Skeleton asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	if (SkeletonObject)
	{
		//Currently material re-import will not touch the material at all
		//TODO design a re-import process for the material (expressions and input connections)
		if(!Arguments.ReimportObject)
		{
			USkeleton* Skeleton = Cast<USkeleton>(SkeletonObject);
			if (!ensure(Skeleton))
			{
				UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create Skeleton asset %s"), *Arguments.AssetName);
				return nullptr;
			}
		}
		
		//Getting the file Hash will cache it into the source data
		Arguments.SourceData->GetFileContentHash();

		//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all material in parallel
	}
	else
	{
		//The material is not a UMaterialInterface
		SkeletonObject->RemoveFromRoot();
		SkeletonObject->MarkPendingKill();
	}
	return SkeletonObject;
#endif
}
