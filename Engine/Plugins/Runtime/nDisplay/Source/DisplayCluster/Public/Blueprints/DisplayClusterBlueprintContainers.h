// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterBlueprintContainers.generated.h"

USTRUCT()
struct DISPLAYCLUSTER_API FDisplayClusterViewportContext
{
	GENERATED_BODY()

public:
	// Viewport Name
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FString ViewportID;

	// Location on a backbuffer.
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FIntPoint RectLocation;

	// Size on a backbuffer.
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FIntPoint RectSize;

	// Camera view location
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FVector  ViewLocation;

	// Camera view rotation
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FRotator ViewRotation;

	// Camera projection Matrix
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FMatrix ProjectionMatrix;

	// Rendering status for this viewport (Overlay and other configuration rules can disable rendering for this viewport.)
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	bool bIsRendering;
};

USTRUCT()
struct DISPLAYCLUSTER_API FDisplayClusterViewportStereoContext
{
	GENERATED_BODY()

public:
	// Viewport Name
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FString ViewportID;

	// Location on a backbuffer.
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FIntPoint RectLocation;

	// Size on a backbuffer.
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	FIntPoint RectSize;

	// Camera view location
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	TArray<FVector> ViewLocation;

	// Camera view rotation
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	TArray<FRotator> ViewRotation;

	// Camera projection Matrix
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	TArray<FMatrix> ProjectionMatrix;

	// Rendering status for this viewport (Overlay and other configuration rules can disable rendering for this viewport.)
	UPROPERTY(EditAnywhere, Category = "Display Cluster")
	bool bIsRendering;
};
