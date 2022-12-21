// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "Misc/HierarchicalLogArchive.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldPartitionRuntimeCell)

int32 UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch = 0;

UWorldPartitionRuntimeCell::UWorldPartitionRuntimeCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsAlwaysLoaded(false)
	, ContentBounds(ForceInit)
	, Priority(0)
	, CachedMinSourcePriority((uint8)EStreamingSourcePriority::Lowest)
	, CachedSourceInfoEpoch(INT_MIN)
#if !UE_BUILD_SHIPPING
	, DebugStreamingPriority(-1.f)
#endif	
{
	check(HasAnyFlags(RF_ClassDefaultObject) || GetOuter()->Implements<UWorldPartitionRuntimeCellOwner>());
}

#if WITH_EDITOR
bool UWorldPartitionRuntimeCell::NeedsActorToCellRemapping() const
{
	// When cooking, always loaded cells content is moved to persistent level (see PopulateGeneratorPackageForCook)
	return !(IsAlwaysLoaded() && IsRunningCookCommandlet());
}

void UWorldPartitionRuntimeCell::SetDataLayers(const TArray<const UDataLayerInstance*>& InDataLayerInstances)
{
	check(DataLayers.IsEmpty());
	DataLayers.Reserve(InDataLayerInstances.Num());
	for (const UDataLayerInstance* DataLayerInstance : InDataLayerInstances)
	{
		check(DataLayerInstance->IsRuntime());
		DataLayers.Add(DataLayerInstance->GetDataLayerFName());
	}
	DataLayers.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
	UpdateDebugName();
}

void UWorldPartitionRuntimeCell::SetDebugInfo(int64 InCoordX, int64 InCoordY, int64 InCoordZ, FName InGridName)
{
	DebugInfo.CoordX = InCoordX;
	DebugInfo.CoordY = InCoordY;
	DebugInfo.CoordZ = InCoordZ;
	DebugInfo.GridName = InGridName;
	UpdateDebugName();
}

void UWorldPartitionRuntimeCell::UpdateDebugName()
{
	TStringBuilder<512> Builder;
	Builder += DebugInfo.GridName.ToString();
	Builder += TEXT("_");
	Builder += FString::Printf(TEXT("L%d_X%d_Y%d"), DebugInfo.CoordZ, DebugInfo.CoordX, DebugInfo.CoordY);
	int32 DataLayerCount = DataLayers.Num();

	if (DataLayerCount > 0)
	{
		if (const UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(GetCellOwner()->GetOuterWorld()))
		{
			TArray<const UDataLayerInstance*> DataLayerObjects;
			Builder += TEXT(" DL[");
			for (int i = 0; i < DataLayerCount; ++i)
			{
				const UDataLayerInstance* DataLayer = DataLayerSubsystem->GetDataLayerInstance(DataLayers[i]);
				DataLayerObjects.Add(DataLayer);
				Builder += DataLayer->GetDataLayerShortName();
				Builder += TEXT(",");
			}
			Builder += FString::Printf(TEXT("ID:%X]"), FDataLayersID(DataLayerObjects).GetHash());
		}
	}

	if (ContentBundleID.IsValid())
	{
		Builder += FString::Printf(TEXT(" CB[%s]"), *UContentBundleDescriptor::GetContentBundleCompactString(ContentBundleID));
	}

	DebugInfo.Name = Builder.ToString();
}

void UWorldPartitionRuntimeCell::DumpStateLog(FHierarchicalLogArchive& Ar)
{
	Ar.Printf(TEXT("Actor Count: %d"), GetActorCount());
	Ar.Printf(TEXT("MinMaxZ: %s"), *GetMinMaxZ().ToString());
}

#endif

bool UWorldPartitionRuntimeCell::ShouldResetStreamingSourceInfo() const
{
	return CachedSourceInfoEpoch != StreamingSourceCacheEpoch;
}

void UWorldPartitionRuntimeCell::ResetStreamingSourceInfo() const
{
	check(ShouldResetStreamingSourceInfo());
	CachedSourcePriorityWeights.Reset();
	CachedMinSourcePriority = MAX_uint8;
	CachedSourceInfoEpoch = StreamingSourceCacheEpoch;
}

void UWorldPartitionRuntimeCell::AppendStreamingSourceInfo(const FWorldPartitionStreamingSource& Source, const FSphericalSector& SourceShape) const
{
	static_assert((uint8)EStreamingSourcePriority::Lowest == 255);
	CachedSourcePriorityWeights.Add(1.f - ((float)Source.Priority / 255.f));
	CachedMinSourcePriority = FMath::Min((uint8)Source.Priority, CachedMinSourcePriority);
}

void UWorldPartitionRuntimeCell::MergeStreamingSourceInfo() const
{}

int32 UWorldPartitionRuntimeCell::SortCompare(const UWorldPartitionRuntimeCell* Other, bool bCanUseSortingCache) const
{
	// Source priority (lower value is higher prio)
	const int32 Comparison = bCanUseSortingCache ? ((int32)CachedMinSourcePriority - (int32)Other->CachedMinSourcePriority) : 0;
	// Cell priority (lower value is higher prio)
	return (Comparison != 0) ? Comparison : (Priority - Other->Priority);
}

bool UWorldPartitionRuntimeCell::IsDebugShown() const
{
	return FWorldPartitionDebugHelper::IsDebugRuntimeHashGridShown(GetGridName()) &&
		   FWorldPartitionDebugHelper::IsDebugStreamingStatusShown(GetStreamingStatus()) &&
	       FWorldPartitionDebugHelper::AreDebugDataLayersShown(DataLayers) &&
		   FWorldPartitionDebugHelper::IsDebugCellNameShown(DebugInfo.Name) &&
		   (FWorldPartitionDebugHelper::CanDrawContentBundles() || !ContentBundleID.IsValid());
}

FLinearColor UWorldPartitionRuntimeCell::GetDebugStreamingPriorityColor() const
{
#if !UE_BUILD_SHIPPING
	if (DebugStreamingPriority >= 0.f && DebugStreamingPriority <= 1.f)
	{
		return FWorldPartitionDebugHelper::GetHeatMapColor(1.f - DebugStreamingPriority);
	}
#endif
	return FLinearColor::Transparent;
}

TArray<const UDataLayerInstance*> UWorldPartitionRuntimeCell::GetDataLayerInstances() const
{
	if (const UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>())
	{
		return DataLayerSubsystem->GetDataLayerInstances(GetDataLayers());
	}

	return TArray<const UDataLayerInstance*>();
}

bool UWorldPartitionRuntimeCell::ContainsDataLayer(const UDataLayerAsset* DataLayerAsset) const
{
	if (const UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>())
	{
		if (const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerAsset))
		{
			return ContainsDataLayer(DataLayerInstance);
		}
	}

	return false;
}

bool UWorldPartitionRuntimeCell::ContainsDataLayer(const UDataLayerInstance* DataLayerInstance) const
{
	return GetDataLayers().Contains(DataLayerInstance->GetDataLayerFName());
}

FName UWorldPartitionRuntimeCell::GetLevelPackageName() const
{ 
#if WITH_EDITOR
	return LevelPackageName;
#else
	return NAME_None;
#endif
}
