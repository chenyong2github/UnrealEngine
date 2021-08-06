// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "Engine/StaticMesh.h"

#include "DisplayClusterConfigurationTypes_OutputRemap.generated.h"

/* Source types of the output remapping */
UENUM()
enum class EDisplayClusterConfigurationFramePostProcess_OutputRemapSource : uint8
{
	/** Use static mesh. */
	StaticMesh     UMETA(DisplayName = "Mesh"),

	/** Load mesh from external file. */
	ExternalFile   UMETA(DisplayName = "External file"),
};

/* Screen space remapping of the final backbuffer output. Applied at the whole window */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationFramePostProcess_OutputRemap
{
	GENERATED_BODY()

public:
	// Enable window output remapping
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay OutputRemap")
	bool bEnable = false;

	// Remap source type
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay OutputRemap")
	EDisplayClusterConfigurationFramePostProcess_OutputRemapSource DataSource = EDisplayClusterConfigurationFramePostProcess_OutputRemapSource::StaticMesh;

	// Reference to the mesh asset to remap
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NDisplay OutputRemap")
	class UStaticMesh* StaticMesh;

	// path to the obj file to remap the output
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay OutputRemap")
	FString ExternalFile;
};
