// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "RootMotionModifier.generated.h"

class UMotionWarpingComponent;

/** The possible states of a Root Motion Modifier */
UENUM(BlueprintType)
enum class ERootMotionModifierState : uint8
{
	/** The modifier is waiting for the animation to hit the warping window */
	Waiting,

	/** The modifier is active and currently affecting the final root motion */
	Active,

	/** The modifier has been marked for removal. Usually because the warping window is done */
	MarkedForRemoval,

	/** The modifier will remain in the list (as long as the window is active) but will not modify the root motion */
	Disabled
};

/** Base struct for Root Motion Modifiers */
USTRUCT()
struct MOTIONWARPING_API FRootMotionModifier
{
	GENERATED_BODY()

public:

	/** Source of the root motion we are warping */
	UPROPERTY()
	TWeakObjectPtr<const UAnimSequenceBase> Animation = nullptr;

	/** Start time of the warping window */
	UPROPERTY()
	float StartTime = 0.f;

	/** End time of the warping window */
	UPROPERTY()
	float EndTime = 0.f;

	/** Previous playback time of the animation */
	UPROPERTY()
	float PreviousPosition = 0.f;

	/** Current playback time of the animation */
	UPROPERTY()
	float CurrentPosition = 0.f;

	/** Current blend weight of the animation */
	UPROPERTY()
	float Weight = 0.f;

	/** Whether this modifier runs before the extracted root motion is converted to world space or after */
	UPROPERTY()
	bool bInLocalSpace = false;

	/** Current state */
	UPROPERTY()
	ERootMotionModifierState State = ERootMotionModifierState::Waiting;

	FRootMotionModifier(){}
	virtual ~FRootMotionModifier() {}

	/** Returns the actual struct used for RTTI */
	virtual UScriptStruct* GetScriptStruct() const PURE_VIRTUAL(FRootMotionModifier::GetScriptStruct, return FRootMotionModifier::StaticStruct(););

	/** Updates the state of the modifier. Runs before ProcessRootMotion */
	virtual void Update(UMotionWarpingComponent& OwnerComp);

	/** Performs the actual modification to the motion */
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) PURE_VIRTUAL(FRootMotionModifier::ProcessRootMotion, return FTransform::Identity;);
};

/** Blueprint wrapper around the config properties of a root motion modifier */
UCLASS(abstract, BlueprintType, EditInlineNew)
class MOTIONWARPING_API URootMotionModifierConfig : public UObject
{
	GENERATED_BODY()

public:
	
	URootMotionModifierConfig(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer) {}

	/** Adds a RootMotionModifier of the type this object represents */
	virtual void AddRootMotionModifier(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const PURE_VIRTUAL(URootMotionModifierConfig::AddRootMotionModifier,);
};

/** Represents a point of alignment in the world */
USTRUCT(BlueprintType, meta = (HasNativeMake = "MotionWarping.MotionWarpingUtilities.MakeMotionWarpingSyncPoint", HasNativeBreak = "MotionWarping.MotionWarpingUtilities.BreakMotionWarpingSyncPoint"))
struct MOTIONWARPING_API FMotionWarpingSyncPoint
{
	GENERATED_BODY()

	FMotionWarpingSyncPoint()
		: Location(FVector::ZeroVector), Rotation(FQuat::Identity) {}
	FMotionWarpingSyncPoint(const FVector& InLocation, const FQuat& InRotation)
		: Location(InLocation), Rotation(InRotation) {}
	FMotionWarpingSyncPoint(const FVector& InLocation, const FRotator& InRotation)
		: Location(InLocation), Rotation(InRotation.Quaternion()) {}
	FMotionWarpingSyncPoint(const FTransform& InTransform)
		: Location(InTransform.GetLocation()), Rotation(InTransform.GetRotation()) {}

	FORCEINLINE const FVector& GetLocation() const { return Location; }
	FORCEINLINE const FQuat& GetRotation() const { return Rotation; }
	FORCEINLINE FRotator Rotator() const { return Rotation.Rotator(); }

	FORCEINLINE bool operator==(const FMotionWarpingSyncPoint& Other) const
	{
		return Other.Location.Equals(Location) && Other.Rotation.Equals(Rotation);
	}

	FORCEINLINE bool operator!=(const FMotionWarpingSyncPoint& Other) const
	{
		return !Other.Location.Equals(Location) || !Other.Rotation.Equals(Rotation);
	}

protected:

	UPROPERTY()
	FVector Location;

	UPROPERTY()
	FQuat Rotation;
};

// FRootMotionModifier_Warp
///////////////////////////////////////////////////////////////

UENUM(BlueprintType)
enum class EMotionWarpRotationType : uint8
{
	/** Character rotates to match the rotation of the sync point */
	Default,

	/** Character rotates to face the sync point */
	Facing,
};

USTRUCT()
struct MOTIONWARPING_API FRootMotionModifier_Warp : public FRootMotionModifier
{
	GENERATED_BODY()

public:

	/** Name used to find the sync point for this modifier */
	UPROPERTY()
	FName SyncPointName = NAME_None;

	/** Whether to warp the translation component of the root motion */
	UPROPERTY()
	bool bWarpTranslation = true;

	/** Whether to ignore the Z component of the translation. Z motion will remain untouched */
	UPROPERTY()
	bool bIgnoreZAxis = true;

	/** Whether to warp the rotation component of the root motion */
	UPROPERTY()
	bool bWarpRotation = true;

	/** Whether rotation should be warp to match the rotation of the sync point or to face the sync point */
	UPROPERTY()
	EMotionWarpRotationType RotationType = EMotionWarpRotationType::Default;

	/** 
	 * Allow to modify how fast the rotation is warped. 
	 * e.g if the window duration is 2sec and this is 0.5, the target rotation will be reached in 1sec instead of 2sec
	 */
	UPROPERTY()
	float WarpRotationTimeMultiplier = 1.f;

	/** Sync Point used by this modifier as target for the warp. Cached during the Update */
	UPROPERTY()
	FMotionWarpingSyncPoint CachedSyncPoint;

	FRootMotionModifier_Warp(){}
	virtual ~FRootMotionModifier_Warp() {}

	//~ Begin FRootMotionModifier Interface
	virtual UScriptStruct* GetScriptStruct() const override { return FRootMotionModifier_Warp::StaticStruct(); }
	virtual void Update(UMotionWarpingComponent& OwnerComp) override;
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) override;
	//~ End FRootMotionModifier Interface

	/** Event called during update if the sync point changes while the warping is active */
	virtual void OnSyncPointChanged(UMotionWarpingComponent& OwnerComp) {}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void PrintLog(const UMotionWarpingComponent& OwnerComp, const FString& Name, const FTransform& OriginalRootMotion, const FTransform& WarpedRootMotion) const;
#endif

protected:

	FQuat GetTargetRotation(UMotionWarpingComponent& OwnerComp) const;
	FQuat WarpRotation(UMotionWarpingComponent& OwnerComp, const FTransform& RootMotionDelta, const FTransform& RootMotionTotal, float DeltaSeconds);
};

UCLASS(meta = (DisplayName = "Simple Warp"))
class MOTIONWARPING_API URootMotionModifierConfig_Warp : public URootMotionModifierConfig
{
	GENERATED_BODY()

public:

	/** Name used to find the sync point for this modifier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	FName SyncPointName = NAME_None;

	/** Whether to warp the translation component of the root motion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bWarpTranslation = true;

	/** Whether to ignore the Z component of the translation. Z motion will remain untouched */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpTranslation"))
	bool bIgnoreZAxis = true;

	/** Whether to warp the rotation component of the root motion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bWarpRotation = true;

	/** Whether rotation should be warp to match the rotation of the sync point or to face the sync point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpRotation"))
	EMotionWarpRotationType RotationType;

	/**
	 * Allow to modify how fast the rotation is warped.
	 * e.g if the window duration is 2sec and this is 0.5, the target rotation will be reached in 1sec instead of 2sec
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpRotation"))
	float WarpRotationTimeMultiplier = 1.f;

	URootMotionModifierConfig_Warp(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

	virtual void AddRootMotionModifier(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		URootMotionModifierConfig_Warp::AddRootMotionModifierSimpleWarp(MotionWarpingComp, Animation, StartTime, EndTime, SyncPointName, bWarpTranslation, bIgnoreZAxis, bWarpRotation, RotationType, WarpRotationTimeMultiplier);
	}

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static void AddRootMotionModifierSimpleWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, FName InSyncPointName, bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier = 1.f);
};

// FRootMotionModifier_Scale
///////////////////////////////////////////////////////////////

USTRUCT()
struct MOTIONWARPING_API FRootMotionModifier_Scale : public FRootMotionModifier
{
	GENERATED_BODY()

public:

	/** Vector used to scale each component of the translation */
	UPROPERTY()
	FVector Scale = FVector(1.f);

	FRootMotionModifier_Scale() { bInLocalSpace = true; }
	virtual ~FRootMotionModifier_Scale() {}

	virtual UScriptStruct* GetScriptStruct() const override { return FRootMotionModifier_Scale::StaticStruct(); }
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) override
	{
		FTransform FinalRootMotion = InRootMotion;
		FinalRootMotion.ScaleTranslation(Scale);
		return FinalRootMotion;
	}
};

UCLASS(meta = (DisplayName = "Scale"))
class MOTIONWARPING_API URootMotionModifierConfig_Scale : public URootMotionModifierConfig
{
	GENERATED_BODY()

public:

	/** Vector used to scale each component of the translation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	FVector Scale = FVector(1.f);

	URootMotionModifierConfig_Scale(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

	virtual void AddRootMotionModifier(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		URootMotionModifierConfig_Scale::AddRootMotionModifierScale(MotionWarpingComp, Animation, StartTime, EndTime, Scale);
	}

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static void AddRootMotionModifierScale(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, FVector InScale);
};