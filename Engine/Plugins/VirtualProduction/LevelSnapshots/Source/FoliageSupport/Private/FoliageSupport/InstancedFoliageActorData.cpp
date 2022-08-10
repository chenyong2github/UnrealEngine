// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/InstancedFoliageActorData.h"

#include "FoliageRestorationInfo.h"
#include "LevelSnapshotsLog.h"
#include "Selection/PropertySelectionMap.h"

#include "InstancedFoliageActor.h"

namespace UE::LevelSnapshots::Foliage::Private::Internal
{
	static void SaveAsset(UFoliageType* FoliageType, FFoliageInfo& FoliageInfo, TMap<TSoftObjectPtr<UFoliageType>, FFoliageInfoData>& FoliageAssets, const FCustomVersionContainer& VersionInfo)
	{
		FoliageAssets.Add(FoliageType).Save(FoliageInfo, VersionInfo);
	}
	
	static void SaveSubobject(UFoliageType* FoliageType, FFoliageInfo& FoliageInfo,TArray<FSubobjectFoliageInfoData>& SubobjectData, const FCustomVersionContainer& VersionInfo)
	{
		FSubobjectFoliageInfoData Data;
		Data.Save(FoliageType, FoliageInfo, VersionInfo);
		SubobjectData.Emplace(Data);
	}
}


FArchive& UE::LevelSnapshots::Foliage::Private::FInstancedFoliageActorData::SerializeInternal(FArchive& Ar)
{
	Ar << FoliageAssets;
	Ar << SubobjectData;
	return Ar;
}

void UE::LevelSnapshots::Foliage::Private::FInstancedFoliageActorData::Save(const FCustomVersionContainer& VersionInfo, AInstancedFoliageActor* FoliageActor)
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
			Internal::SaveSubobject(FoliageType, FoliageInfo, SubobjectData, VersionInfo);
		}
		else
		{
			Internal::SaveAsset(FoliageType, FoliageInfo, FoliageAssets, VersionInfo);
		}
		
		return true;
	});
}

namespace UE::LevelSnapshots::Foliage::Private::Internal
{
	static FFoliageInfo* FindOrAddFoliageInfo(UFoliageType* FoliageType, AInstancedFoliageActor* FoliageActor)
	{
		FFoliageInfo* FoliageInfo = FoliageActor->FindInfo(FoliageType);
		if (!FoliageInfo)
		{
			FoliageInfo = &FoliageActor->AddFoliageInfo(FoliageType).Get();
		}

		UE_CLOG(FoliageInfo == nullptr, LogLevelSnapshots, Warning, TEXT("Failed to create foliage info for foliage type %s"), *FoliageType->GetPathName());
		return FoliageInfo; 
	}

	static void HandleExistingFoliageUsingRequiredComponent(AInstancedFoliageActor* FoliageActor, FName RequiredComponentName, const TMap<FName, UFoliageType*>& PreexistingComponentToFoliageType, UFoliageType* AllowedFoliageType)
	{
		// Handles the following case:
		// 1. Add (static mesh) foliage type FoliageA. Let's suppose the component that is added is called Component01.
		// 2. Take snapshot
		// 3. Reload map without saving
		// 4. Add foliage type FoliageB. This creates a new component that is also called Component01.
		// 5. Restore snapshot
		// 
		// Without the below check, both FoliageA and FoliageB would use Component01 to add instances.
		// We remove the foliage type under the "interpretation" that Component01 is "restored" to reuse the previous foliage type.
		if (UFoliageType* const* FoliageType = PreexistingComponentToFoliageType.Find(RequiredComponentName); FoliageType && *FoliageType != AllowedFoliageType)
		{
			TUniqueObj<FFoliageInfo> FoliageInfo;
			FoliageActor->RemoveFoliageInfoAndCopyValue(*FoliageType, FoliageInfo);
		}
	}

	static void RemoveRecreatedComponent(AInstancedFoliageActor* FoliageActor, TOptional<FName> RecreatedComponentName)
	{
		if (ensure(RecreatedComponentName))
		{
			for (UHierarchicalInstancedStaticMeshComponent* Component : TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*>(FoliageActor))
			{
				if (Component->GetFName() == *RecreatedComponentName)
				{
					Component->DestroyComponent();
					return;
				}
			}
		}
	}
	
	static void ApplyAssets(
		const FCustomVersionContainer& VersionInfo,
		AInstancedFoliageActor* FoliageActor,
		const FFoliageRestorationInfo& RestorationInfo,
		const TMap<TSoftObjectPtr<UFoliageType>, FFoliageInfoData>& FoliageAssets,
		const TMap<FName, UFoliageType*> PreexistingComponentToFoliageType)
	{
		for (auto AssetIt = FoliageAssets.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			const FFoliageInfoData& FoliageInfoData = AssetIt->Value;
			UFoliageType* FoliageType = AssetIt->Key.LoadSynchronous();
			if (!FoliageType)
			{
				UE_LOG(LogLevelSnapshots, Warning, TEXT("Foliage type %s is missing. Component %s will not be restored."), *AssetIt->Key.ToString(), *FoliageInfoData.GetComponentName().Get(NAME_None).ToString());

				// Standard component restoration has already recreated the component. Remove it again.
				RemoveRecreatedComponent(FoliageActor, FoliageInfoData.GetComponentName());
				continue;
			}

			if (RestorationInfo.ShouldSkipFoliageType(FoliageInfoData))
			{
				continue;
			}

			if (RestorationInfo.ShouldSerializeFoliageType(FoliageInfoData))
			{
				FFoliageInfo* FoliageInfo = FindOrAddFoliageInfo(FoliageType, FoliageActor);
				if (FoliageInfo)
				{
					// If there were no instances, no component existed, hence no component name could be saved
					const bool bHadAtLeastOneInstanceWhenSaved = FoliageInfoData.GetComponentName().IsSet();
					if (bHadAtLeastOneInstanceWhenSaved)
					{
						HandleExistingFoliageUsingRequiredComponent(FoliageActor, *FoliageInfoData.GetComponentName(), PreexistingComponentToFoliageType, FoliageType);
					}
					FoliageInfoData.ApplyTo(*FoliageInfo, VersionInfo);
				}
			}
		}
	}

	static void ApplySubobjects(
		const FCustomVersionContainer& VersionInfo,
		AInstancedFoliageActor* FoliageActor,
		const FFoliageRestorationInfo& RestorationInfo,
		const TArray<FSubobjectFoliageInfoData>& SubobjectData, 
		const TMap<FName, UFoliageType*> PreexistingComponentToFoliageType)
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

			if (RestorationInfo.ShouldSerializeFoliageType(InstanceData))
			{
				FFoliageInfo* FoliageInfo = FindOrAddFoliageInfo(FoliageType, FoliageActor);
				if (FoliageInfo
					&& ensureAlwaysMsgf(InstanceData.GetComponentName(), TEXT("ComponentName is supposed to have been saved. Investigate. Maybe you used an unsupported foliage type (only EFoliageImplType::StaticMesh is supported)?")))
				{
					HandleExistingFoliageUsingRequiredComponent(FoliageActor, *InstanceData.GetComponentName(), PreexistingComponentToFoliageType, FoliageType);

					InstanceData.ApplyTo(FoliageType, VersionInfo);
					InstanceData.ApplyTo(*FoliageInfo, VersionInfo);
				}
			}
		}
	}

	static TMap<FName, UFoliageType*> BuildComponentToFoliageType(AInstancedFoliageActor* FoliageActor)
	{
		TMap<FName, UFoliageType*> Result;
		for (auto It = FoliageActor->GetFoliageInfos().CreateConstIterator(); It; ++It)
		{
			if (UHierarchicalInstancedStaticMeshComponent* Component = It->Value->GetComponent())
			{
				Result.Add(Component->GetFName(), It->Key);
			}
		}
		return Result;
	}
}

void UE::LevelSnapshots::Foliage::Private::FInstancedFoliageActorData::ApplyTo(const FCustomVersionContainer& VersionInfo, AInstancedFoliageActor* FoliageActor, const FPropertySelectionMap& SelectedProperties, bool bWasRecreated) const
{
	const FFoliageRestorationInfo RestorationInfo = FFoliageRestorationInfo::From(FoliageActor, SelectedProperties, bWasRecreated);
	const TMap<FName, UFoliageType*> PreexistingComponentToFoliageType = Internal::BuildComponentToFoliageType(FoliageActor);
	Internal::ApplyAssets(VersionInfo, FoliageActor, RestorationInfo, FoliageAssets, PreexistingComponentToFoliageType);
	Internal::ApplySubobjects(VersionInfo, FoliageActor, RestorationInfo, SubobjectData, PreexistingComponentToFoliageType);
}
