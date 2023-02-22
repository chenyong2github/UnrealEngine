// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box.h"

class AActor;
class APCGWorldActor;
class ALandscape;
class ALandscapeProxy;
class UPCGComponent;
class UWorld;

namespace PCGHelpers
{
	/** Tag that will be added on every component generated through the PCG system */
	const FName DefaultPCGTag = TEXT("PCG Generated Component");
	const FName DefaultPCGDebugTag = TEXT("PCG Generated Debug Component");
	const FName DefaultPCGActorTag = TEXT("PCG Generated Actor");
	const FName MarkedForCleanupPCGTag = TEXT("PCG Marked For Cleanup");

	int ComputeSeed(int A);
	int ComputeSeed(int A, int B);
	int ComputeSeed(int A, int B, int C);

	bool IsInsideBounds(const FBox& InBox, const FVector& InPosition);
	bool IsInsideBoundsXY(const FBox& InBox, const FVector& InPosition);

	FBox OverlapBounds(const FBox& InBoxA, const FBox& InBoxB);

	/** Returns the bounds of InActor, intersected with the component if InActor is a partition actor */
	FBox GetGridBounds(const AActor* InActor, const UPCGComponent* InComponent);

	FBox GetActorBounds(const AActor* InActor, bool bIgnorePCGCreatedComponents = true);
	FBox GetActorLocalBounds(const AActor* InActor, bool bIgnorePCGCreatedComponents = true);
	FBox GetLandscapeBounds(const ALandscapeProxy* InLandscape);

	ALandscape* GetLandscape(UWorld* InWorld, const FBox& InActorBounds);
	TArray<TWeakObjectPtr<ALandscapeProxy>> GetLandscapeProxies(UWorld* InWorld, const FBox& InActorBounds);
	TArray<TWeakObjectPtr<ALandscapeProxy>> GetAllLandscapeProxies(UWorld* InWorld);

	bool IsRuntimeOrPIE();

	APCGWorldActor* GetPCGWorldActor(UWorld* InWorld);

	TArray<FString> GetStringArrayFromCommaSeparatedString(const FString& InCommaSeparatedString);

#if WITH_EDITOR
	void GatherDependencies(UObject* Object, TSet<TObjectPtr<UObject>>& OutDependencies, int32 MaxDepth = -1);
	void GatherDependencies(FProperty* Property, const void* InContainer, TSet<TObjectPtr<UObject>>& OutDependencies, int32 MaxDepth);
#endif
};
