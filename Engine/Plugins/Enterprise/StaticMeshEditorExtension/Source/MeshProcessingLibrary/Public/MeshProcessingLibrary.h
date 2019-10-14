// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"

#include "MeshDescription.h"

#include "MeshProcessingLibrary.generated.h"

UENUM()
enum class EJacketingTarget : uint8
{
	/** Apply jacketing on the level, will hide/tag/destroy actors and static mesh components. */
	Level,
	/** Apply jacketing on the mesh, will remove triangles/vertices. */
	Mesh
};

UCLASS(BlueprintType, config = EditorPerProjectUserSettings, Transient)
class UJacketingOptions : public UObject
{
	GENERATED_BODY()

public:
	UJacketingOptions() : Accuracy(1.0F), MergeDistance(0.0F), Target(EJacketingTarget::Level)
	{ }

	UJacketingOptions(float InAccuracy, float InMergeDistance, EJacketingTarget InTarget)
		: Accuracy(InAccuracy)
		, MergeDistance(InMergeDistance)
		, Target(InTarget)
	{ }

	/** Accuracy of the distance field approximation, in UE units. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Jacketing", meta = (DisplayName = "Voxel Precision", Units = cm, ToolTip = "Set the minimum size of voxels"))
	float Accuracy;

	/** Merge distance used to fill gap, in UE units. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Jacketing", meta = (DisplayName = "Gap Max Diameter", Units = cm, ToolTip = "Maximum diameter of gaps to fill"))
	float MergeDistance;

	/** Target to apply the jacketing to. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Jacketing", meta = (DisplayName = "Action Level", ToolTip = "Action to be applied on actors (Level) or triangles (Mesh)"))
	EJacketingTarget Target;
};

UCLASS(BlueprintType, config = EditorPerProjectUserSettings, Transient)
class MESHPROCESSINGLIBRARY_API UMeshDefeaturingParameterObject : public UObject
{
	GENERATED_BODY()

public:
	UMeshDefeaturingParameterObject()
		: bFillThroughHoles(false)
		, ThroughHoleMaxDiameter(0.0f)
		, bFillBlindHoles(false)
		, FilledHoleMaxDiameter(0.0f)
		, FilledHoleMaxDepth(0.0f)
		, bRemoveProtrusions(false)
		, ProtrusionMaxDiameter(0.0f)
		, ProtrusionMaxHeight(0.0f)
		, MaxVolumeRatio(0.3f)
		, ChordTolerance(0.005f)
		, AngleTolerance(5.0f)
	{
	}

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = ThroughHoles, meta = (DisplayName = "Fill through holes", ToolTip = "Enable filling of through holes with diameter smaller than a given maximum"))
	bool bFillThroughHoles;

	/** Maximum diameter of removable emerging holes */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = ThroughHoles, meta = (EditCondition="bFillThroughHoles", Units = cm, DisplayName = "Filled holes max diameter", ToolTip = "Maximum diameter of through holes to fill"))
	float ThroughHoleMaxDiameter;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = BlindHoles, meta = (DisplayName = "Fill blind holes", ToolTip = "Enable filling of non emerging (blind) holes with diameter smaller than a given maximum"))
	bool bFillBlindHoles;

	/** Maximum diameter of removable blind holes */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = BlindHoles, meta = (EditCondition="bFillBlindHoles", Units = cm, DisplayName = "Filled hole max diameter", ToolTip = "Maximum diameter of blind holes to fill"))
	float FilledHoleMaxDiameter;

	/** Maximum depth of removable blind holes */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = BlindHoles, meta = (EditCondition="bFillBlindHoles", Units = cm, DisplayName = "Filled hole max depth", ToolTip = "Maximum depth of blind holes to fill"))
	float FilledHoleMaxDepth;

	/** Enable erasing of bumps. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = Protrusion, meta = (DisplayName = "Remove protrusions", ToolTip = "Remove bumps under a maximal height"))
	bool bRemoveProtrusions;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = Protrusion, meta = (EditCondition="bRemoveProtrusions", Units = cm, DisplayName = "Protrusion max diameter", ToolTip = "Maximum diameter of protrusions to remove"))
	float ProtrusionMaxDiameter;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = Protrusion, meta = (EditCondition="bRemoveProtrusions", Units = cm, DisplayName = "Protrusion max height", ToolTip = "Maximum height of protrusions to remove"))
	float ProtrusionMaxHeight;

	// Maximum percentage of volume the non-emerging holes / bumps in comparison with the volume of the whole mesh
	float MaxVolumeRatio;

	// Used to simplify mesh after de-featuring
	float ChordTolerance;

	// Used to simplify mesh after de-featuring
	float AngleTolerance;
};

UCLASS(config = EditorSettings, MinimalAPI)
class UMeshProcessingEnterpriseSettings : public UObject
{
	GENERATED_BODY()

public:
	UMeshProcessingEnterpriseSettings();

	UPROPERTY(config, EditAnywhere, Category = Settings, meta = (Units = MB))
	int32 OverrideUndoBufferSize;

};

UCLASS()
class MESHPROCESSINGLIBRARY_API UMeshProcessingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Remove holes, emerging and/or non-emerging, and bumps (features).
	 * @param	StaticMesh			Static mesh to remove features from.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	Parameters			Parameter values to use for the defeaturing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Processing | StaticMesh")
	static void DefeatureMesh(UStaticMesh* StaticMesh, int32 LODIndex, const UMeshDefeaturingParameterObject* Parameters);

	/**
	 * Detect partially or totally occluded objects in a list of actors. Truncate partially occluded meshes.
	 * @param	Actors				List of actors to process.
	 * @param	Options				Parameter values to use for the jacketing.
	 * @param	OccludedActorArray	Array of actors which are fully occluded. Only filled if target is EJacketingTarget::Level.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Processing | MeshActor", meta = (DisplayName = "Simplify Assembly"))
	static void ApplyJacketingOnMeshActors(const TArray<AActor*>& Actors, const UJacketingOptions* Options, TArray<AActor*>& OccludedActorArray)
	{
		ApplyJacketingOnMeshActors(Actors, Options, OccludedActorArray, true);
	}

public:
	static void DefeatureMesh(FMeshDescription& MeshDescription, const UMeshDefeaturingParameterObject& Parameters);
	static void ApplyJacketingOnMeshActors(const TArray<AActor*>& Actors, const UJacketingOptions* Options, TArray<AActor*>& OccludedActorArray, bool bSilent);
};