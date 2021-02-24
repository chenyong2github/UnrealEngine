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
class UAnimNotifyState_MotionWarping;

DECLARE_LOG_CATEGORY_EXTERN(LogMotionWarping, Log, All);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
struct FMotionWarpingCVars
{
	static TAutoConsoleVariable<int32> CVarMotionWarpingDisable;
	static TAutoConsoleVariable<int32> CVarMotionWarpingDebug;
	static TAutoConsoleVariable<float> CVarMotionWarpingDrawDebugDuration;
};
#endif

USTRUCT(BlueprintType)
struct FMotionWarpingWindowData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TObjectPtr<UAnimNotifyState_MotionWarping> AnimNotify;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float StartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float EndTime = 0.f;
};

UCLASS()
class MOTIONWARPING_API UMotionWarpingUtilities : public UBlueprintFunctionLibrary
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
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static FTransform ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime);

	/** @return All the MotionWarping windows within the supplied animation */
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static void GetMotionWarpingWindowsFromAnimation(const UAnimSequenceBase* Animation, TArray<FMotionWarpingWindowData>& OutWindows);

	/** @return All the MotionWarping windows within the supplied animation for a given Sync Point */
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static void GetMotionWarpingWindowsForSyncPointFromAnimation(const UAnimSequenceBase* Animation, FName SyncPointName, TArray<FMotionWarpingWindowData>& OutWindows);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMotionWarpingPreUpdate, class UMotionWarpingComponent*, MotionWarpingComp);

UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class MOTIONWARPING_API UMotionWarpingComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	/** Whether to look inside animations within montage when looking for warping windows */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bSearchForWindowsInAnimsWithinMontages;

	/** Event called before Root Motion Modifiers are updated */
	UPROPERTY(BlueprintAssignable, Category = "Motion Warping")
	FMotionWarpingPreUpdate OnPreUpdate;

	UMotionWarpingComponent(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeComponent() override;

	/** Gets the character this component belongs to */
	FORCEINLINE ACharacter* GetCharacterOwner() const { return CharacterOwner.Get(); }

	/** Returns the list of root motion modifiers */
	FORCEINLINE const TArray<TSharedPtr<FRootMotionModifier>>& GetRootMotionModifiers() const { return RootMotionModifiers; }

	/** Find the SyncPoint associated with a specified name */
	FORCEINLINE const FMotionWarpingSyncPoint* FindSyncPoint(const FName& SyncPointName) const { return SyncPoints.Find(SyncPointName); }

	/** Adds or update the sync point associated with a specified name */
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	void AddOrUpdateSyncPoint(FName Name, const FMotionWarpingSyncPoint& SyncPoint);

	/** Removes sync point associated with the specified key  */
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	int32 RemoveSyncPoint(FName Name);

	/** Check if we contain a RootMotionModifier for the supplied animation and time range */
	bool ContainsModifier(const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

	/** Add a new modifier */
	void AddRootMotionModifier(TSharedPtr<FRootMotionModifier> Modifier);

	/** Mark all the modifiers as Disable */
	void DisableAllRootMotionModifiers();

protected:

	/** Character this component belongs to */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACharacter> CharacterOwner;

	/** List of root motion modifiers */
	TArray<TSharedPtr<FRootMotionModifier>> RootMotionModifiers;

	UPROPERTY(Transient)
	TMap<FName, FMotionWarpingSyncPoint> SyncPoints;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	TOptional<FVector> OriginalRootMotionAccum;
	TOptional<FVector> WarpedRootMotionAccum;
#endif

	void Update();

	FTransform ProcessRootMotionPreConvertToWorld(const FTransform& InRootMotion, class UCharacterMovementComponent* CharacterMovementComponent, float DeltaSeconds);
	
	FTransform ProcessRootMotionPostConvertToWorld(const FTransform& InRootMotion, class UCharacterMovementComponent* CharacterMovementComponent, float DeltaSeconds);
};