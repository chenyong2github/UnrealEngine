// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MeshTrackerTypes.generated.h"

/** Type of mesh to query from the underlying system. */
UENUM(BlueprintType)
enum class EMagicLeapMeshType : uint8
{
	/** Meshing should be done as triangles. */
	Triangles,
	/** Return mesh vertices as a point cloud. */
	PointCloud
};

/** Vertex color mode. */
UENUM(BlueprintType)
enum class EMagicLeapMeshVertexColorMode : uint8
{
	/** Vertex Color is not set. */
	None		UMETA(DisplayName = "No Vertex Color"),
	/** Vertex confidence is interpolated between two specified colors. */
	Confidence	UMETA(DisplayName = "Vertex Confidence"),
	/** Each block is given a color from a list. */
	Block		UMETA(DisplayName = "Blocks Colored"),
	/** Each LOD is given a color from a list. */
	LOD			UMETA(DisplayName = "LODs Colored")
};

/** Discrete level of detail required. */
UENUM(BlueprintType)
enum class EMagicLeapMeshLOD : uint8
{
	/** Minimum LOD. */
	Minimum,
	/** Medium LOD. */
	Medium,
	/** Maximum LOD. */
	Maximum,
};

/** State of a block mesh. */
UENUM(BlueprintType)
enum class EMagicLeapMeshState : uint8
{
	/** Mesh has been created */
	New,
	/** Mesh has been updated. */
	Updated,
	/** Mesh has been deleted. */
	Deleted,
	/** Mesh is unchanged. */
	Unchanged
};

/** Representation of a mesh block. */
USTRUCT(BlueprintType)
struct MAGICLEAP_API FMagicLeapMeshBlockInfo
{
	GENERATED_BODY()

public:
	/** The coordinate frame UID to represent the block. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	FGuid BlockID;

	/** The center of the mesh block bounding box. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	FVector BlockPosition = FVector(0.0f);

	/** The orientation of the mesh block bounding box.*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	FRotator BlockOrientation = FRotator(0.0f);

	/** The size of the mesh block bounding box (in Unreal Units). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	FVector BlockDimensions = FVector(0.0f);

	/** The timestamp when block was updated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	FTimespan Timestamp = FTimespan::Zero();

	/** The state of the mesh block. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	EMagicLeapMeshState BlockState = EMagicLeapMeshState::New;
};

/** Response structure for the mesh block info. */
USTRUCT(BlueprintType)
struct MAGICLEAP_API FMagicLeapTrackingMeshInfo
{
	GENERATED_BODY()

public:
	/** The response timestamp to a earlier request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	FTimespan Timestamp = FTimespan::Zero();

	/** The meshinfo returned by the system. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Meshing|MagicLeap")
	TArray<FMagicLeapMeshBlockInfo> BlockData;
};

/** Request structure to get the actual mesh for a block. */
USTRUCT(BlueprintType)
struct MAGICLEAP_API FMagicLeapMeshBlockRequest
{
	GENERATED_BODY()

public:
	/** The UID to represent the block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	FGuid BlockID;

	/** The LOD level to request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	EMagicLeapMeshLOD LevelOfDetail = EMagicLeapMeshLOD::Minimum;
};
