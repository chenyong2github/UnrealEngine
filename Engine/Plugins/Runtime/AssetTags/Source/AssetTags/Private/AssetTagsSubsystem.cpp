// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTagsSubsystem.h"

#include "IAssetRegistry.h"
#include "AssetRegistryState.h"
#include "AssetRegistryModule.h"

#if WITH_EDITOR
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#endif	// WITH_EDITOR

DEFINE_LOG_CATEGORY_STATIC(LogAssetTags, Log, All);

#if WITH_EDITOR

namespace AssetTagsSubsystemUtil
{

void LogLastCollectionManagerError(ICollectionManager& CollectionManager)
{
	UE_LOG(LogAssetTags, Warning, TEXT("%s"), *CollectionManager.GetLastError().ToString());
}

void LogLastCollectionManagerError()
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();
	LogLastCollectionManagerError(CollectionManager);
}

FCollectionNameType FindCollectionByName(ICollectionManager& CollectionManager, const FName Name)
{
	TArray<FCollectionNameType> CollectionNamesAndTypes;
	CollectionManager.GetCollections(Name, CollectionNamesAndTypes);
	
	if (CollectionNamesAndTypes.Num() == 0)
	{
		UE_LOG(LogAssetTags, Warning, TEXT("No collection found called '%s'"), *Name.ToString());
		return FCollectionNameType(NAME_None, ECollectionShareType::CST_All);
	}
	else if (CollectionNamesAndTypes.Num() > 1)
	{
		UE_LOG(LogAssetTags, Warning, TEXT("%d collections found called '%s'; ambiguous result"), CollectionNamesAndTypes.Num(), *Name.ToString());
		return FCollectionNameType(NAME_None, ECollectionShareType::CST_All);
	}
	
	return CollectionNamesAndTypes[0];
}

FCollectionNameType FindCollectionByName(const FName Name)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();
	return FindCollectionByName(CollectionManager, Name);
}

}	// namespace AssetTagsSubsystemUtil

bool UAssetTagsSubsystem::CreateCollection(const FName Name, const ECollectionScriptingShareType ShareType)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	if (!CollectionManager.IsValidCollectionName(Name.ToString(), ECollectionShareType::CST_All))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	static_assert((int32)ECollectionShareType::CST_Local == (int32)ECollectionScriptingShareType::Local + 1, "ECollectionShareType::CST_Local is expected to be ECollectionScriptingShareType::Local + 1");
	static_assert((int32)ECollectionShareType::CST_Private == (int32)ECollectionScriptingShareType::Private + 1, "ECollectionShareType::CST_Private is expected to be ECollectionScriptingShareType::Private + 1");
	static_assert((int32)ECollectionShareType::CST_Shared == (int32)ECollectionScriptingShareType::Shared + 1, "ECollectionShareType::CST_Shared is expected to be ECollectionScriptingShareType::Shared + 1");

	if (!CollectionManager.CreateCollection(Name, (ECollectionShareType::Type)((int32)ShareType + 1), ECollectionStorageMode::Static))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::DestroyCollection(const FName Name)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.DestroyCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::RenameCollection(const FName Name, const FName NewName)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.IsValidCollectionName(NewName.ToString(), ECollectionShareType::CST_All))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	if (!CollectionManager.RenameCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, NewName, ResolvedNameAndType.Type))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::ReparentCollection(const FName Name, const FName NewParentName)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	const FCollectionNameType ResolvedParentNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, NewParentName);
	if (ResolvedParentNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.ReparentCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, ResolvedParentNameAndType.Name, ResolvedParentNameAndType.Type))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::EmptyCollection(const FName Name)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.EmptyCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::AddAssetToCollection(const FName Name, const FName AssetPathName)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.AddToCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, AssetPathName))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::AddAssetDataToCollection(const FName Name, const FAssetData& AssetData)
{
	return AddAssetToCollection(Name, AssetData.ObjectPath);
}

bool UAssetTagsSubsystem::AddAssetPtrToCollection(const FName Name, const UObject* AssetPtr)
{
	return AddAssetToCollection(Name, AssetPtr ? FName(*AssetPtr->GetPathName()) : NAME_None);
}

bool UAssetTagsSubsystem::AddAssetsToCollection(const FName Name, const TArray<FName>& AssetPathNames)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.AddToCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, AssetPathNames))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::AddAssetDatasToCollection(const FName Name, const TArray<FAssetData>& AssetDatas)
{
	TArray<FName> AssetPathNames;
	AssetPathNames.Reserve(AssetDatas.Num());
	for (const FAssetData& AssetData : AssetDatas)
	{
		AssetPathNames.Add(AssetData.ObjectPath);
	}
	return AddAssetsToCollection(Name, AssetPathNames);
}

bool UAssetTagsSubsystem::AddAssetPtrsToCollection(const FName Name, const TArray<UObject*>& AssetPtrs)
{
	TArray<FName> AssetPathNames;
	AssetPathNames.Reserve(AssetPtrs.Num());
	for (const UObject* AssetPtr : AssetPtrs)
	{
		AssetPathNames.Add(AssetPtr ? FName(*AssetPtr->GetPathName()) : NAME_None);
	}
	return AddAssetsToCollection(Name, AssetPathNames);
}

bool UAssetTagsSubsystem::RemoveAssetFromCollection(const FName Name, const FName AssetPathName)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.RemoveFromCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, AssetPathName))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::RemoveAssetDataFromCollection(const FName Name, const FAssetData& AssetData)
{
	return RemoveAssetFromCollection(Name, AssetData.ObjectPath);
}

bool UAssetTagsSubsystem::RemoveAssetPtrFromCollection(const FName Name, const UObject* AssetPtr)
{
	return RemoveAssetFromCollection(Name, AssetPtr ? FName(*AssetPtr->GetPathName()) : NAME_None);
}

bool UAssetTagsSubsystem::RemoveAssetsFromCollection(const FName Name, const TArray<FName>& AssetPathNames)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
	if (ResolvedNameAndType.Name.IsNone())
	{
		return false;
	}

	if (!CollectionManager.RemoveFromCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, AssetPathNames))
	{
		AssetTagsSubsystemUtil::LogLastCollectionManagerError(CollectionManager);
		return false;
	}

	return true;
}

bool UAssetTagsSubsystem::RemoveAssetDatasFromCollection(const FName Name, const TArray<FAssetData>& AssetDatas)
{
	TArray<FName> AssetPathNames;
	AssetPathNames.Reserve(AssetDatas.Num());
	for (const FAssetData& AssetData : AssetDatas)
	{
		AssetPathNames.Add(AssetData.ObjectPath);
	}
	return RemoveAssetsFromCollection(Name, AssetPathNames);
}

bool UAssetTagsSubsystem::RemoveAssetPtrsFromCollection(const FName Name, const TArray<UObject*>& AssetPtrs)
{
	TArray<FName> AssetPathNames;
	AssetPathNames.Reserve(AssetPtrs.Num());
	for (const UObject* AssetPtr : AssetPtrs)
	{
		AssetPathNames.Add(AssetPtr ? FName(*AssetPtr->GetPathName()) : NAME_None);
	}
	return RemoveAssetsFromCollection(Name, AssetPathNames);
}

#endif	// WITH_EDITOR

bool UAssetTagsSubsystem::CollectionExists(const FName Name)
{
	bool bExists = false;

#if WITH_EDITOR
	{
		ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();
		bExists = CollectionManager.CollectionExists(Name, ECollectionShareType::CST_All);
	}
#else	// WITH_EDITOR
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();

		if (const FAssetRegistryState* StatePtr = AssetRegistry.GetAssetRegistryState())
		{
			const FString TagNameStr = FString::Printf(TEXT("%s%s"), FAssetData::GetCollectionTagPrefix(), *Name.ToString());
			bExists = StatePtr->GetTagToAssetDatasMap().Contains(*TagNameStr);
		}
	}
#endif	// WITH_EDITOR

	return bExists;
}

TArray<FName> UAssetTagsSubsystem::GetCollections()
{
	TArray<FName> CollectionNames;

#if WITH_EDITOR
	{
		ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

		TArray<FCollectionNameType> CollectionNamesAndTypes;
		CollectionManager.GetCollections(CollectionNamesAndTypes);

		CollectionNames.Reserve(CollectionNamesAndTypes.Num());
		for (const FCollectionNameType& CollectionNameAndType : CollectionNamesAndTypes)
		{
			CollectionNames.AddUnique(CollectionNameAndType.Name);
		}
	}
#else	// WITH_EDITOR
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();

		if (const FAssetRegistryState* StatePtr = AssetRegistry.GetAssetRegistryState())
		{
			const TCHAR* CollectionTagPrefix = FAssetData::GetCollectionTagPrefix();
			const int32 CollectionTagPrefixLen = FCString::Strlen(CollectionTagPrefix);

			for (const auto& TagAndArrayPair : StatePtr->GetTagToAssetDatasMap())
			{
				const FString TagNameStr = TagAndArrayPair.Key.ToString();
				if (TagNameStr.StartsWith(CollectionTagPrefix))
				{
					const FString TrimmedTagNameStr = TagNameStr.Mid(CollectionTagPrefixLen);
					CollectionNames.Add(*TrimmedTagNameStr);
				}
			}
		}
	}
#endif	// WITH_EDITOR

	CollectionNames.Sort(FNameLexicalLess());
	return CollectionNames;
}

TArray<FAssetData> UAssetTagsSubsystem::GetAssetsInCollection(const FName Name)
{
	TArray<FAssetData> Assets;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
#if WITH_EDITOR
	{
		ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

		const FCollectionNameType ResolvedNameAndType = AssetTagsSubsystemUtil::FindCollectionByName(CollectionManager, Name);
		if (!ResolvedNameAndType.Name.IsNone())
		{
			TArray<FName> AssetPathNames;
			CollectionManager.GetAssetsInCollection(ResolvedNameAndType.Name, ResolvedNameAndType.Type, AssetPathNames);

			Assets.Reserve(AssetPathNames.Num());
			for (const FName& AssetPathName : AssetPathNames)
			{
				FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPathName);
				if (AssetData.IsValid())
				{
					Assets.Add(MoveTemp(AssetData));
				}
			}
		}
	}
#else	// WITH_EDITOR
	{
		const FString TagNameStr = FString::Printf(TEXT("%s%s"), FAssetData::GetCollectionTagPrefix(), *Name.ToString());

		FARFilter Filter;
		Filter.bIncludeOnlyOnDiskAssets = true; // Collection tags are added at cook-time, so we *must* search the on-disk version of the tags (from the asset registry)
		Filter.TagsAndValues.Add(*TagNameStr);

		AssetRegistry.GetAssets(Filter, Assets);
	}
#endif	// WITH_EDITOR

	Assets.Sort();
	return Assets;
}

TArray<FName> UAssetTagsSubsystem::GetCollectionsContainingAsset(const FName AssetPathName)
{
	TArray<FName> CollectionNames;

#if WITH_EDITOR
	{
		ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

		TArray<FCollectionNameType> CollectionNamesAndTypes;
		CollectionManager.GetCollectionsContainingObject(AssetPathName, CollectionNamesAndTypes);

		CollectionNames.Reserve(CollectionNamesAndTypes.Num());
		for (const FCollectionNameType& CollectionNameAndType : CollectionNamesAndTypes)
		{
			CollectionNames.AddUnique(CollectionNameAndType.Name);
		}
	}
#else	// WITH_EDITOR
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();

		const bool bIncludeOnlyOnDiskAssets = true; // Collection tags are added at cook-time, so we *must* search the on-disk version of the tags (from the asset registry)
		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPathName, bIncludeOnlyOnDiskAssets);
		if (AssetData.IsValid())
		{
			const TCHAR* CollectionTagPrefix = FAssetData::GetCollectionTagPrefix();
			const int32 CollectionTagPrefixLen = FCString::Strlen(CollectionTagPrefix);

			for (const auto& TagAndValuePair : AssetData.TagsAndValues)
			{
				const FString TagNameStr = TagAndValuePair.Key.ToString();
				if (TagNameStr.StartsWith(CollectionTagPrefix))
				{
					const FString TrimmedTagNameStr = TagNameStr.Mid(CollectionTagPrefixLen);
					CollectionNames.Add(*TrimmedTagNameStr);
				}
			}
		}
	}
#endif	// WITH_EDITOR

	CollectionNames.Sort(FNameLexicalLess());
	return CollectionNames;
}

TArray<FName> UAssetTagsSubsystem::GetCollectionsContainingAssetData(const FAssetData& AssetData)
{
	// Note: Use the path name version as the common implementation as:
	//  1) The path name is always required for the collection manager implementation
	//  2) The FAssetData for the asset registry implementation *must* come from the asset registry (as the tags are added at cook-time, and missing if FAssetData is generated from a UObject* at runtime)
	return GetCollectionsContainingAsset(AssetData.ObjectPath);
}

TArray<FName> UAssetTagsSubsystem::GetCollectionsContainingAssetPtr(const UObject* AssetPtr)
{
	// Note: Use the path name version as the common implementation as:
	//  1) The path name is always required for the collection manager implementation
	//  2) The FAssetData for the asset registry implementation *must* come from the asset registry (as the tags are added at cook-time, and missing if FAssetData is generated from a UObject* at runtime)
	return GetCollectionsContainingAsset(AssetPtr ? FName(*AssetPtr->GetPathName()) : NAME_None);
}
