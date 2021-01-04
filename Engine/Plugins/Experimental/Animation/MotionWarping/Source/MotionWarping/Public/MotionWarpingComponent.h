// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RootMotionModifier.h"
#include "MotionWarpingComponent.generated.h"

class ACharacter;
class UAnimSequenceBase;
class UCharacterMovementComponent;
class UMotionWarpingComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogMotionWarping, Log, All);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
struct FMotionWarpingCVars
{
	static TAutoConsoleVariable<int32> CVarMotionWarpingDisable;
	static TAutoConsoleVariable<int32> CVarMotionWarpingDebug;
	static TAutoConsoleVariable<float> CVarMotionWarpingDrawDebugDuration;
};
#endif

UCLASS()
class UMotionWarpingUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Extracts data from a MotionWarpingSyncPoint */
	UFUNCTION(BlueprintPure, Category = "Motion Warping", meta = (NativeBreakFunc))
	static void BreakMotionWarpingSyncPoint(const FMotionWarpingSyncPoint& SyncPoint, FVector& Location, FRotator& Rotation);

	/** Create a MotionWarpingSyncPoint struct */
	UFUNCTION(BlueprintPure, Category = "Motion Warping", meta = (NativeMakeFunc))
	static FMotionWarpingSyncPoint MakeMotionWarpingSyncPoint(FVector Location, FRotator Rotation);

	/** Extract bone pose in local space for all bones in BoneContainer. If Animation is a Montage the pose is extracted from the first track */
	static void ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose);

	/** Extract bone pose in component space for all bones in BoneContainer. If Animation is a Montage the pose is extracted from the first track */
	static void ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose);

	/** Extract Root Motion transform from a contiguous position range */
	static FTransform ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime);
};

UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class UMotionWarpingComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	/** Whether to look inside animations within montage when looking for warping windows */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bSearchForWindowsInAnimsWithinMontages;

	UMotionWarpingComponent(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeComponent() override;

	/** Gets the character this component belongs to */
	FORCEINLINE ACharacter* GetCharacterOwner() const { return CharacterOwner.Get(); }

	/** Returns the list of root motion modifiers */
	FORCEINLINE const TArray<TUniquePtr<FRootMotionModifier>>& GetRootMotionModifiers() const { return RootMotionModifiers; }

	/** Find the SyncPoint associated with a specified name */
	FORCEINLINE const FMotionWarpingSyncPoint* FindSyncPoint(const FName& SyncPointName) const { return SyncPoints.Find(SyncPointName); }

	/** Adds or update the sync point associated with a specified name */
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	void AddOrUpdateSyncPoint(FName Name, const FMotionWarpingSyncPoint& SyncPoint);

	/** Check if we contain a RootMotionModifier that matches the supplied config data */
	bool ContainsMatchingModifier(const URootMotionModifierConfig* Config, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

protected:

	/** Character this component belongs to */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACharacter> CharacterOwner;

	/** List of root motion modifiers */
	TArray<TUniquePtr<FRootMotionModifier>> RootMotionModifiers;

	UPROPERTY(Transient)
	TMap<FName, FMotionWarpingSyncPoint> SyncPoints;

	void Update();

	FTransform ProcessRootMotionPreConvertToWorld(const FTransform& InRootMotion, class UCharacterMovementComponent* CharacterMovementComponent, float DeltaSeconds);
	
	FTransform ProcessRootMotionPostConvertToWorld(const FTransform& InRootMotion, class UCharacterMovementComponent* CharacterMovementComponent, float DeltaSeconds);
};