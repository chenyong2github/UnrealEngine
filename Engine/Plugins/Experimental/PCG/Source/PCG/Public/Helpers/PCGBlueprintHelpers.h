// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGPoint.h"

#include "PCGBlueprintHelpers.generated.h"

class UPCGSettings;

UCLASS()
class PCG_API UPCGBlueprintHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PCG|Helpers")
	static int ComputeSeedFromPosition(const FVector& InPosition);

	UFUNCTION(BlueprintCallable, Category = "PCG|Helpers")
	static void SetSeedFromPosition(UPARAM(ref) FPCGPoint& InPoint);

	/** Creates a random stream from a point's seed and settings's seed (optional) */
	UFUNCTION(BlueprintCallable, Category = "PCG|Helpers", meta = (ScriptMethod))
	static FRandomStream GetRandomStream(const FPCGPoint& InPoint, const UPCGSettings* OptionalSettings = nullptr);
};