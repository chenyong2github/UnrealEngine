// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangePipelineBase.h"
#include "Math/Color.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "Nodes/InterchangeBaseNode.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"

#include "InterchangeGenericAssetsPipelineSharedSettings.generated.h"

class USkeleton;

/** Force mesh type, if user want to import all meshes as one type*/
UENUM(BlueprintType)
enum class EInterchangeForceMeshType : uint8
{
	/** Will import from the source type, no conversion */
	IFMT_None UMETA(DisplayName = "None"),
	/** Will import any mesh to static mesh. */
	IFMT_StaticMesh UMETA(DisplayName = "Static Mesh"),
	/** Will import any mesh to skeletal mesh. */
	IFMT_SkeletalMesh UMETA(DisplayName = "Skeletal Mesh"),

	IFMT_MAX
};

UENUM(BlueprintType)
enum class EInterchangeVertexColorImportOption : uint8
{
	/** Import the mesh using the vertex colors from the translated source. */
	IVCIO_Replace UMETA(DisplayName = "Replace"),
	/** Ignore vertex colors from the translated source. In case of a re-import keep the existing mesh vertex colors. */
	IVCIO_Ignore UMETA(DisplayName = "Ignore"),
	/** Override all vertex colors with the specified color. */
	IVCIO_Override UMETA(DisplayName = "Override"),

	IVCIO_MAX
};


UCLASS(BlueprintType, hidedropdown, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericCommonMeshesProperties : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	//////	COMMON_MESHES_CATEGORY Properties //////

	/** Allow to convert mesh to a particular type */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
 	EInterchangeForceMeshType ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_None;

	/** If enable, meshes LODs will be imported. Note that it required the advanced bBakeMesh property to be enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes", meta = (editcondition = "bBakeMeshes"))
	bool bImportLods = true;

	/** If enable, meshes will be baked with the scene instance hierarchy transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
	bool bBakeMeshes = true;

	/** Specify how vertex colors should be imported */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
	EInterchangeVertexColorImportOption VertexColorImportOption = EInterchangeVertexColorImportOption::IVCIO_Replace;

	/** Specify override color in the case that VertexColorImportOption is set to Override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
	FColor VertexOverrideColor;
};

UCLASS(BlueprintType, hidedropdown, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	/** Enable this option to only import animation, a valid skeleton must be set to import only the animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Skeletal Meshes and Animations")
	bool bImportOnlyAnimations = false;

	/** Skeleton to use for imported asset. When importing a skeletal mesh, leaving this as "None" will create a new skeleton. When importing an animation this MUST be specified to import the asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Skeletal Meshes and Animations")
	TObjectPtr<USkeleton> Skeleton;

	/** If checked, meshes nested in bone hierarchies will be imported instead of being converted to bones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Skeletal Meshes and Animations")
	bool bImportMeshesInBoneHierarchy = true;

	/** Enable this option to use frame 0 as reference pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Skeletal Meshes and Animations")
	bool bUseT0AsRefPose = false;

	virtual bool IsSettingsAreValid(TOptional<FText>& OutInvalidReason) const override
	{
		if (bImportOnlyAnimations && Skeleton.IsNull())
		{
			OutInvalidReason = NSLOCTEXT("UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties", "SkeletonMustBeSpecified", "When importing only animations, a valid skeleton must be set.");
			return false;
		}
		return Super::IsSettingsAreValid(OutInvalidReason);
	}
};
