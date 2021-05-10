// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "ProfilingDebugging/ProfilingHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "HAL/IConsoleManager.h"
#include "Algo/AnyOf.h"
#include "Algo/AllOf.h"

TSet<FName> FWorldPartitionDebugHelper::DebugRuntimeHashFilter;
FAutoConsoleCommand FWorldPartitionDebugHelper::DebugFilterByRuntimeHashGridNameCommand(
	TEXT("wp.Runtime.DebugFilterByRuntimeHashGridName"),
	TEXT("Filter debug diplay of world partition streaming by grid name. Args [grid names]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		DebugRuntimeHashFilter.Reset();
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (UWorldPartition* WorldPartition = World->GetWorldPartition())
				{
					for (const FString& Arg : InArgs)
					{
						if (WorldPartition->RuntimeHash && WorldPartition->RuntimeHash->ContainsRuntimeHash(Arg))
						{
							DebugRuntimeHashFilter.Add(FName(Arg));
						}
					}
				}
			}
		}
	})
);

bool FWorldPartitionDebugHelper::IsDebugRuntimeHashGridShown(FName Name)
{
	return !DebugRuntimeHashFilter.Num() || DebugRuntimeHashFilter.Contains(Name);
}

TSet<FName> FWorldPartitionDebugHelper::DebugDataLayerFilter;
FAutoConsoleCommand FWorldPartitionDebugHelper::DebugFilterByDataLayerCommand(
	TEXT("wp.Runtime.DebugFilterByDataLayer"),
	TEXT("Filter debug diplay of world partition streaming by data layer. Args [datalayer labels]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		DebugDataLayerFilter.Reset();

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				TArray<UDataLayer*> DataLayers = UDataLayerSubsystem::ConvertArgsToDataLayers(World, InArgs);
				for (UDataLayer* DataLayer : DataLayers)
				{
					DebugDataLayerFilter.Add(DataLayer->GetFName());
				}
			}
		}

		if (Algo::AnyOf(InArgs, [](const FString& Arg) { return FName(Arg) == NAME_None; }))
		{
			DebugDataLayerFilter.Add(NAME_None);
		}
	})
);

bool FWorldPartitionDebugHelper::IsDebugDataLayerShown(FName DataLayerName)
{
	return !DebugDataLayerFilter.Num() || DebugDataLayerFilter.Contains(DataLayerName);
}

bool FWorldPartitionDebugHelper::AreDebugDataLayersShown(const TArray<FName>& DataLayerNames)
{
	if (DebugDataLayerFilter.Num())
	{
		bool bFilter = !DataLayerNames.IsEmpty() || !DebugDataLayerFilter.Contains(NAME_None);
		for (const FName& DataLayerName : DataLayerNames)
		{
			if (DebugDataLayerFilter.Contains(DataLayerName))
			{
				bFilter = false;
				break;
			}
		}
		if (bFilter)
		{
			return false;
		}
	}
	return true;
}

TSet<EStreamingStatus> FWorldPartitionDebugHelper::DebugStreamingStatusFilter;
FAutoConsoleCommand FWorldPartitionDebugHelper::DebugFilterByStreamingStatusCommand(
	TEXT("wp.Runtime.DebugFilterByStreamingStatus"),
	TEXT("Filter debug diplay of world partition streaming by streaming status. Args [streaming status]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		DebugStreamingStatusFilter.Reset();
		for (const FString& Arg : InArgs)
		{
			int32 Result = 0;
			TTypeFromString<int32>::FromString(Result, *Arg);
			if (Result >= 0 && Result < (int32)LEVEL_StreamingStatusCount)
			{
				DebugStreamingStatusFilter.Add((EStreamingStatus)Result);
			}
		}
	})
);

bool FWorldPartitionDebugHelper::IsDebugStreamingStatusShown(EStreamingStatus StreamingStatus)
{
	return !DebugStreamingStatusFilter.Num() || DebugStreamingStatusFilter.Contains(StreamingStatus);
}

TArray<FString> FWorldPartitionDebugHelper::DebugCellNameFilter;
FAutoConsoleCommand FWorldPartitionDebugHelper::DebugFilterByCellNameCommand(
	TEXT("wp.Runtime.DebugFilterByCellName"),
	TEXT("Filter debug diplay of world partition streaming by full or partial cell name. Args [cell name]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		TSet<FString> Filter;
		Filter.Append(InArgs);
		DebugCellNameFilter = Filter.Array();
	})
);

bool FWorldPartitionDebugHelper::IsDebugCellNameShow(const FString& CellName)
{
	return !DebugCellNameFilter.Num() || Algo::AllOf(DebugCellNameFilter, [&CellName](const FString& NameFilter) { return CellName.Contains(NameFilter); });
}

void FWorldPartitionDebugHelper::DrawText(UCanvas* Canvas, const FString& Text, const UFont* Font, const FColor& Color, FVector2D& Pos, float* MaxTextWidth)
{
	float TextWidth, TextHeight;
	Canvas->StrLen(Font, Text, TextWidth, TextHeight);
	Canvas->SetDrawColor(Color);
	Canvas->DrawText(Font, Text, Pos.X, Pos.Y);
	Pos.Y += TextHeight + 1;
	if (MaxTextWidth)
	{
		*MaxTextWidth = FMath::Max(*MaxTextWidth, TextWidth);
	}
}

void FWorldPartitionDebugHelper::DrawLegendItem(UCanvas* Canvas, const FString& Text, const UFont* Font, const FColor& Color, FVector2D& Pos, float* MaxItemWidth)
{
	static const FVector2D ItemSize(12, 12);
	
	float MaxTextWidth = 0.f;
	
	FCanvasTileItem Item(Pos, GWhiteTexture, ItemSize, Color);
	Canvas->DrawItem(Item);

	FVector2D TextPos(Pos.X + ItemSize.X + 10, Pos.Y);
	float TextWidth = 0.f;
	FWorldPartitionDebugHelper::DrawText(Canvas, Text, Font, FColor::White, TextPos, &TextWidth);
	if (MaxItemWidth)
	{
		*MaxItemWidth = FMath::Max(*MaxItemWidth, TextWidth + ItemSize.X + 10);
	}

	Pos.Y = TextPos.Y;
}