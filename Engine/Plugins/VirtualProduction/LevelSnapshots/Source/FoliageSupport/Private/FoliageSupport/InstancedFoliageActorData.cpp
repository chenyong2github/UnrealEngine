// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/InstancedFoliageActorData.h"

#include "FoliageRestorationInfo.h"
#include "InstancedFoliageActor.h"
#include "LevelSnapshotsLog.h"
#include "PropertySelectionMap.h"

namespace
{
	void SaveAsset(UFoliageType* FoliageType, FFoliageInfo& FoliageInfo, TMap<TSoftObjectPtr<UFoliageType>, FFoliageInfoData>& FoliageAssets, const FCustomVersionContainer& VersionInfo)
	{
		FoliageAssets.Add(FoliageType).Save(FoliageInfo, VersionInfo);
	}
	
	void SaveSubobject(UFoliageType* FoliageType, FFoliageInfo& FoliageInfo,TArray<FSubobjectFoliageInfoData>& SubobjectData, const FCustomVersionContainer& VersionInfo)
	{
		FSubobjectFoliageInfoData Data;
		Data.Save(FoliageType, FoliageInfo, VersionInfo);
		SubobjectData.Emplace(Data);
	}
}


void FInstancedFoliageActorData::Save(const FCustomVersionContainer& VersionInfo, AInstancedFoliageActor* FoliageActor)
{
	FoliageActor->ForEachFoliageInfo([this, &VersionInfo, FoliageActor](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
	{
		FFoliageImpl* Impl = FoliageInfo.Implementation.Get();
		if (!FoliageType || !Impl)
		{
			return true;
		}
		
		const bool bIsSubobject = FoliageType->IsIn(FoliageActor);
		if (bIsSubobject)
		{
			SaveSubobject(FoliageType, FoliageInfo, SubobjectData, VersionInfo);
		}
		else
		{
			SaveAsset(FoliageType, FoliageInfo, FoliageAssets, VersionInfo);
		}
		
		return true;
	});
}

namespace
{
	FFoliageInfo* FindOrAddFoliageInfo(UFoliageType* FoliageType, AInstancedFoliageActor* FoliageActor)
	{
		FFoliageInfo* FoliageInfo = FoliageActor->FindInfo(FoliageType);
		if (!FoliageInfo)
		{
			FoliageInfo = &FoliageActor->AddFoliageInfo(FoliageType).Get();
		}

		UE_CLOG(FoliageInfo == nullptr, LogLevelSnapshots, Warning, TEXT("Failed to create foliage info for foliage type %s"), *FoliageType->GetPathName());
		return FoliageInfo; 
	}
	
	void ApplyAssets(const FCustomVersionContainer& VersionInfo, AInstancedFoliageActor* FoliageActor, const FFoliageRestorationInfo& RestorationInfo, const TMap<TSoftObjectPtr<UFoliageType>, FFoliageInfoData>& FoliageAssets)
	{
		for (auto AssetIt = FoliageAssets.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			UFoliageType* FoliageType = AssetIt->Key.LoadSynchronous();
			if (!FoliageType)
			{
				UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to update foliage information for foliage type: %s"), *AssetIt->Key.ToString());
				continue;
			}

			const FFoliageInfoData& FoliageInfoData = AssetIt->Value;
			if (RestorationInfo.ShouldSkipFoliageType(FoliageInfoData))
			{
				continue;
			}

			if (RestorationInfo.ShouldRemoveFoliageType(FoliageInfoData))
			{
				FoliageActor->RemoveFoliageType(&FoliageType, 1);
			}
			else if (RestorationInfo.ShouldSerializeFoliageType(FoliageInfoData))
			{
				FFoliageInfo* FoliageInfo = FindOrAddFoliageInfo(FoliageType, FoliageActor);
				if (FoliageInfo)
				{
					FoliageInfoData.ApplyTo(*FoliageInfo, VersionInfo);
				}
			}
		}
	}

	void ApplySubobjects(const FCustomVersionContainer& VersionInfo, AInstancedFoliageActor* FoliageActor, const FFoliageRestorationInfo& RestorationInfo, const TArray<FSubobjectFoliageInfoData>& SubobjectData)
	{
		for (const FSubobjectFoliageInfoData& InstanceData : SubobjectData)
		{
			UFoliageType* FoliageType = InstanceData.FindOrRecreateSubobject(FoliageActor);
			if (!FoliageType)
			{
				UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to recreate foliage type. Skipping..."));
				continue;
			}

			if (RestorationInfo.ShouldSkipFoliageType(InstanceData))
			{
				continue;
			}

			if (RestorationInfo.ShouldRemoveFoliageType(InstanceData))
			{
				FoliageActor->RemoveFoliageType(&FoliageType, 1);
			}
			else if (RestorationInfo.ShouldSerializeFoliageType(InstanceData))
			{
				FFoliageInfo* FoliageInfo = FindOrAddFoliageInfo(FoliageType, FoliageActor);
				if (FoliageInfo)
				{
					InstanceData.ApplyTo(FoliageType, VersionInfo);
					InstanceData.ApplyTo(*FoliageInfo, VersionInfo);
				}
			}
		}
	}
}

void FInstancedFoliageActorData::ApplyTo(const FCustomVersionContainer& VersionInfo, AInstancedFoliageActor* FoliageActor, const FPropertySelectionMap& SelectedProperties) const
{
	const FFoliageRestorationInfo RestorationInfo = FFoliageRestorationInfo::From(FoliageActor, SelectedProperties);
	ApplyAssets(VersionInfo, FoliageActor, RestorationInfo, FoliageAssets);
	ApplySubobjects(VersionInfo, FoliageActor, RestorationInfo, SubobjectData);
}

FArchive& operator<<(FArchive& Ar, FInstancedFoliageActorData& FoliagInfoData)
{
	Ar << FoliagInfoData.FoliageAssets;
	Ar << FoliagInfoData.SubobjectData;
	return Ar;
}
