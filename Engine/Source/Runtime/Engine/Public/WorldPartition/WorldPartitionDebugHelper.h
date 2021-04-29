// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "ProfilingDebugging/ProfilingHelpers.h"

class FAutoConsoleCommand;
class UCanvas;
class UFont;

class FWorldPartitionDebugHelper
{
public:
	static bool IsDebugDataLayerShown(FName DataLayerName);
	static bool AreDebugDataLayersShown(const TArray<FName>& DataLayerNames);
	static bool IsDebugStreamingStatusShown(EStreamingStatus StreamingStatus);
	static bool IsDebugRuntimeHashGridShown(FName Name);
	static bool IsDebugCellNameShow(const FString& Name);
	static void DrawText(UCanvas* Canvas, const FString& Text, const UFont* Font, const FColor& Color, FVector2D& Pos, float* MaxTextWidth = nullptr);
	static void DrawLegendItem(UCanvas* Canvas, const FString& Text, const UFont* Font, const FColor& Color, FVector2D& Pos, float* MaxItemWidth = nullptr);

private:
	static FAutoConsoleCommand DebugFilterByStreamingStatusCommand;
	static TSet<EStreamingStatus> DebugStreamingStatusFilter;

	static FAutoConsoleCommand DebugFilterByDataLayerCommand;
	static TSet<FName> DebugDataLayerFilter;

	static FAutoConsoleCommand DebugFilterByRuntimeHashGridNameCommand;
	static TSet<FName> DebugRuntimeHashFilter;

	static FAutoConsoleCommand DebugFilterByCellNameCommand;
	static TArray<FString> DebugCellNameFilter;
};