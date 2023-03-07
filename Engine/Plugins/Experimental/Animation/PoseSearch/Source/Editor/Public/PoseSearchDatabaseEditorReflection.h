// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearchDatabaseEditorReflection.generated.h"


namespace UE::PoseSearch
{
	class FDatabaseAssetTreeNode;
	class SDatabaseAssetTree;
}


UCLASS()
class UPoseSearchDatabaseReflectionBase : public UObject
{
	GENERATED_BODY()

public:
	void SetSourceLink(
		const TWeakPtr<UE::PoseSearch::FDatabaseAssetTreeNode>& InWeakAssetTreeNode,
		const TSharedPtr<UE::PoseSearch::SDatabaseAssetTree>& InAssetTreeWidget);

protected:
	TWeakPtr<UE::PoseSearch::FDatabaseAssetTreeNode> WeakAssetTreeNode;
	TSharedPtr<UE::PoseSearch::SDatabaseAssetTree> AssetTreeWidget;
};

USTRUCT()
struct FPoseSearchDatabaseSequenceEx : public FPoseSearchDatabaseSequence
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 10))
	bool bLooping = false;

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 11))
	bool bHasRootMotion = false;
};

UCLASS()
class UPoseSearchDatabaseSequenceReflection : public UPoseSearchDatabaseReflectionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Selected Sequence")
	FPoseSearchDatabaseSequenceEx Sequence;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

USTRUCT()
struct FPoseSearchDatabaseBlendSpaceEx : public FPoseSearchDatabaseBlendSpace
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 10))
	bool bLooping = false;

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 11))
	bool bHasRootMotion = false;
};

UCLASS()
class UPoseSearchDatabaseBlendSpaceReflection : public UPoseSearchDatabaseReflectionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Selected Blend Space")
	FPoseSearchDatabaseBlendSpaceEx BlendSpace;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

USTRUCT()
struct FPoseSearchDatabaseAnimCompositeEx : public FPoseSearchDatabaseAnimComposite
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 10))
	bool bLooping = false;

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 11))
	bool bHasRootMotion = false;
};

UCLASS()
class UPoseSearchDatabaseAnimCompositeReflection : public UPoseSearchDatabaseReflectionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Selected Anim Composite")
	FPoseSearchDatabaseAnimCompositeEx AnimComposite;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

USTRUCT()
struct FPoseSearchDatabaseAnimMontageEx : public FPoseSearchDatabaseAnimMontage
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 10))
	bool bLooping = false;

	UPROPERTY(VisibleAnywhere, Category="Sequence", meta = (DisplayPriority = 11))
	bool bHasRootMotion = false;
};

UCLASS()
class UPoseSearchDatabaseAnimMontageReflection : public UPoseSearchDatabaseReflectionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Selected Anim Montage")
	FPoseSearchDatabaseAnimMontageEx AnimMontage;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

USTRUCT()
struct FPoseSearchDatabaseMemoryStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Stats")
	FText EstimatedDatabaseSize;

	UPROPERTY(VisibleAnywhere, Category = "Stats")
	FText ValuesSize;

	UPROPERTY(VisibleAnywhere, Category = "Stats")
	FText PCAValuesSize;

	UPROPERTY(VisibleAnywhere, Category = "Stats")
	FText KDTreeSize;

	UPROPERTY(VisibleAnywhere, Category = "Stats")
	FText PoseMetadataSize;
	
	UPROPERTY(VisibleAnywhere, Category = "Stats")
	FText AssetsSize;
	
	void Initialize(const UPoseSearchDatabase* PoseSearchDatabase);
};

UCLASS()
class UPoseSearchDatabaseStatistics : public UObject
{
	GENERATED_BODY()

public:
	
	// General Information

	UPROPERTY(VisibleAnywhere, Category = "General Information")
	uint32 AnimationSequences;

	UPROPERTY(VisibleAnywhere, Category = "General Information")
	uint32 TotalAnimationPosesInFrames;

	UPROPERTY(VisibleAnywhere, Category = "General Information")
	FText TotalAnimationPosesInTime;

	UPROPERTY(VisibleAnywhere, Category = "General Information")
	uint32 SearchableFrames;

	UPROPERTY(VisibleAnywhere, Category = "General Information")
	FText SearchableTime;

	// Kinematic Information

	UPROPERTY(VisibleAnywhere, Category = "Kinematic Information")
	FText AverageSpeed;

	UPROPERTY(VisibleAnywhere, Category = "Kinematic Information")
	FText MaxSpeed;

	UPROPERTY(VisibleAnywhere, Category = "Kinematic Information")
	FText AverageAcceleration;

	UPROPERTY(VisibleAnywhere, Category = "Kinematic Information")
	FText MaxAcceleration;

	// Principal Component Analysis Information

	UPROPERTY(VisibleAnywhere, Category = "Principal Component Analysis Information", meta = (Units = "Percent"))
	float ExplainedVariance;

	// Memory information
	
	UPROPERTY(VisibleAnywhere, Category = "Memory Information")
	FText EstimatedDatabaseSize;

	UPROPERTY(VisibleAnywhere, Category = "Memory Information")
	FText ValuesSize;

	UPROPERTY(VisibleAnywhere, Category = "Memory Information")
	FText PCAValuesSize;

	UPROPERTY(VisibleAnywhere, Category = "Memory Information")
	FText KDTreeSize;

	UPROPERTY(VisibleAnywhere, Category = "Memory Information")
	FText PoseMetadataSize;
	
	UPROPERTY(VisibleAnywhere, Category = "Memory Information")
	FText AssetsSize;

	/** Initialize statistics given a database */
	void Initialize(const UPoseSearchDatabase* PoseSearchDatabase);
};