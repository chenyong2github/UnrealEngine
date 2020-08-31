// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorRegistry.h"
#include "AssetRegistryModule.h"
#include "Engine/Level.h"

#if WITH_EDITOR
void FActorRegistry::GetLevelActors(const FName& LevelPath, TArray<FAssetData>& OutAssets)
{
	if (LevelPath.IsNone())
	{
		return;
	}

	FString LevelPathStr = LevelPath.ToString();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	
	// Do a synchronous scan of the level external actors path.
	AssetRegistry.ScanPathsSynchronous({ULevel::GetExternalActorsPath(LevelPath.ToString())});

	static const FName NAME_LevelPackage(TEXT("LevelPackage"));
	FARFilter Filter;
	Filter.TagsAndValues.Add(NAME_LevelPackage, LevelPathStr);
	Filter.bIncludeOnlyOnDiskAssets = true;
	AssetRegistry.GetAssets(Filter, OutAssets);
}

void FActorRegistry::GetLevelActors(const ULevel* Level, TArray<FAssetData>& OutAssets)
{
	GetLevelActors(Level->GetOutermost()->FileName, OutAssets);
}

void FActorRegistry::SaveActorMetaData(FName Name, bool Value, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, Value ? TEXT("1") : TEXT("0"), FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, int32 Value, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, *FString::Printf(TEXT("%d"), Value), FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, int64 Value, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, *FString::Printf(TEXT("%lld"), Value), FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, const FGuid& Value, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, Value.ToString(EGuidFormats::Base36Encoded), FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, const FVector& Value, TArray<FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, Value.ToCompactString(), FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, const FTransform& Value, TArray<FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, Value.ToString(), FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, const FString& Value, TArray<FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, Value, FAssetRegistryTag::TT_Hidden));
}

void FActorRegistry::SaveActorMetaData(FName Name, const FName& Value, TArray<FAssetRegistryTag>& OutTags)
{
	OutTags.Add(FAssetRegistryTag(Name, Value.ToString(), FAssetRegistryTag::TT_Hidden));
}

bool FActorRegistry::ReadActorMetaData(FName Name, bool& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	if (AssetData.GetTagValue(Name, ValueStr))
	{
		if (ValueStr == TEXT("0"))
		{
			OutValue = false;
			return true;
		}
		else if (ValueStr == TEXT("1"))
		{
			OutValue = true;
			return true;
		}
	}

	return false;
}

bool FActorRegistry::ReadActorMetaData(FName Name, int32& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	if (AssetData.GetTagValue(Name, ValueStr))
	{
		OutValue = FCString::Atoi(*ValueStr);
		return true;
	}

	return false;
}

bool FActorRegistry::ReadActorMetaData(FName Name, int64& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	if (AssetData.GetTagValue(Name, ValueStr))
	{
		OutValue = FCString::Atoi64(*ValueStr);
		return true;
	}

	return false;
}

bool FActorRegistry::ReadActorMetaData(FName Name, FGuid& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	return AssetData.GetTagValue(Name, ValueStr) && FGuid::Parse(ValueStr, OutValue);
}

bool FActorRegistry::ReadActorMetaData(FName Name, FVector& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	if (AssetData.GetTagValue(Name, ValueStr))
	{
		OutValue.InitFromString(ValueStr);
		return true;
	}
	return false;
}

bool FActorRegistry::ReadActorMetaData(FName Name, FTransform& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	if (AssetData.GetTagValue(Name, ValueStr))
	{
		OutValue.InitFromString(ValueStr);
		return true;
	}
	return false;
}

bool FActorRegistry::ReadActorMetaData(FName Name, FString& OutValue, const FAssetData& AssetData)
{
	return AssetData.GetTagValue(Name, OutValue);
}

bool FActorRegistry::ReadActorMetaData(FName Name, FName& OutValue, const FAssetData& AssetData)
{
	FString ValueStr;
	if (AssetData.GetTagValue(Name, ValueStr))
	{
		OutValue = *ValueStr;
		return true;
	}
	return false;
}
#endif