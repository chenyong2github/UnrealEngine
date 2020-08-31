// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AssetData.h"

#if WITH_EDITOR
/**
 * Actors metadata accessors, built on top of the asset registry
 */ 
class ENGINE_API FActorRegistry: public UObject
{
public:
	/**
	 * Gets all actors belonging to a level
	 *
	 * @param The level to gather actors from
	 * @param OutAssets the list of actors in this level
	 */
	static void GetLevelActors(const FName& LevelPath, TArray<FAssetData>& OutAssets);
	static void GetLevelActors(const ULevel* Level, TArray<FAssetData>& OutAssets);		

	/**
	 * Saves actor metadata
	 *
	 * @param Name the name of the metadata value
	 * @param Value the value to save
	 * @param OutTags the destination asset registry tags array
	 */
	static void SaveActorMetaData(FName Name, bool Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, int32 Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, int64 Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, const FGuid& Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, const FVector& Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, const FTransform& Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, const FString& Value, TArray<FAssetRegistryTag>& OutTags);
	static void SaveActorMetaData(FName Name, const FName& Value, TArray<FAssetRegistryTag>& OutTags);

	/**
	 * Parse an actor metadata
	 *
	 * @param Name the name of the metadata value
	 * @param OutValue the value to parse
	 * @param AssetData source asset registry data
	 */
	static bool ReadActorMetaData(FName Name, bool& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, int32& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, int64& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, FGuid& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, FVector& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, FTransform& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, FString& OutValue, const FAssetData& AssetData);
	static bool ReadActorMetaData(FName Name, FName& OutValue, const FAssetData& AssetData);
};
#endif