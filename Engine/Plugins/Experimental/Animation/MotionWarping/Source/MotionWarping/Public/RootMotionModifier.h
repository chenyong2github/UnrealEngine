// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "RootMotionModifier.generated.h"

class UMotionWarpingComponent;
class UAnimNotifyState_MotionWarping;

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

/** Handle to identify a RootMotionModifier */
USTRUCT(BlueprintType)
struct MOTIONWARPING_API FRootMotionModifierHandle
{
	GENERATED_BODY()

	FRootMotionModifierHandle() : Handle(INDEX_NONE) {}

	void GenerateNewHandle();
	
	bool IsValid() const { return Handle != INDEX_NONE; }
	void Invalidate() { Handle = INDEX_NONE; }
	
	bool operator==(const FRootMotionModifierHandle& Other) const { return Handle == Other.Handle; }
	bool operator!=(const FRootMotionModifierHandle& Other) const { return Handle != Other.Handle; }
	
	friend uint32 GetTypeHash(const FRootMotionModifierHandle& InHandle) { return InHandle.Handle; }

	FString ToString() const { return IsValid() ? FString::FromInt(Handle) : TEXT("Invalid"); }

	static const FRootMotionModifierHandle InvalidHandle;

private:

	UPROPERTY()
	int32 Handle;
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnRootMotionModifierDelegate, UMotionWarpingComponent*, MotionWarpingComp, const FRootMotionModifierHandle&, Handle);

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
	ERootMotionModifierState State = ERootMotionModifierState::Waiting; //@TODO: Move to private

	/** Delegate called when this modifier is activated (starts affecting the root motion) */
	UPROPERTY()
	FOnRootMotionModifierDelegate OnActivateDelegate;

	/** Delegate called when this modifier updates while active (affecting the root motion) */
	UPROPERTY()
	FOnRootMotionModifierDelegate OnUpdateDelegate;

	/** Delegate called when this modifier is deactivated (stops affecting the root motion) */
	UPROPERTY()
	FOnRootMotionModifierDelegate OnDeactivateDelegate;

	FRootMotionModifier(){}
	virtual ~FRootMotionModifier(){}

	/** Initialize this modifier */
	void Initialize(UMotionWarpingComponent* OwnerComp);

	/** Returns the actual struct used for RTTI */
	virtual UScriptStruct* GetScriptStruct() const PURE_VIRTUAL(FRootMotionModifier::GetScriptStruct, return FRootMotionModifier::StaticStruct(););

	/** Called when the state of the modifier changes */
	virtual void OnStateChanged(ERootMotionModifierState LastState);

	/** Sets the state of the modifier */
	void SetState(ERootMotionModifierState NewState);

	/** Returns the state of the modifier */
	FORCEINLINE ERootMotionModifierState GetState() const { return State; }

	/** Returns the handle to identify this modifier */
	FORCEINLINE const FRootMotionModifierHandle& GetHandle() const { return Handle; }

	/** Returns a pointer to the component that owns this modifier */
	FORCEINLINE UMotionWarpingComponent* GetOwnerComponent() const { return OwnerComponent.Get(); }

	/** Returns a pointer to the character that owns the component that owns this modifier */
	class ACharacter* GetCharacterOwner() const;

	//DEPRECATED will be removed shortly, replaced with a signature that does not receive the comp via param.
	virtual void Update(UMotionWarpingComponent& OwnerComp);
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) { return FTransform::Identity;	}

	FORCEINLINE const UAnimSequenceBase* GetAnimation() const { return Animation.Get(); }

private:

	friend UMotionWarpingComponent;

	/** Pointer back to the Component that owns this modifier */
	UPROPERTY()
	TWeakObjectPtr<UMotionWarpingComponent> OwnerComponent = nullptr;

	/** Handle to identify this modifier */
	UPROPERTY()
	FRootMotionModifierHandle Handle;

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
	virtual FRootMotionModifierHandle AddRootMotionModifierNew(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const { return FRootMotionModifierHandle::InvalidHandle; }

	//DEPRECATED will be removed shortly
	virtual void AddRootMotionModifier(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const {}
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

/** Method used to extract the warp point from the animation */
UENUM(BlueprintType)
enum class EWarpPointAnimProvider : uint8
{
	/** No warp point is provided */
	None,

	/** Warp point defined by a 'hard-coded' transform  user can enter through the warping notify */
	Static,

	/** Warp point defined by a bone */
	Bone
};

USTRUCT()
struct MOTIONWARPING_API FRootMotionModifier_Warp : public FRootMotionModifier
{
	GENERATED_BODY()

public:

	/** Name used to find the warp point for this modifier */
	UPROPERTY()
	FName SyncPointName = NAME_None; //@TODO: Rename to WarpPointName

	/** Method used to extract the warp point from the animation */
	UPROPERTY()
	EWarpPointAnimProvider WarpPointAnimProvider = EWarpPointAnimProvider::None;

	UPROPERTY()
	FTransform WarpPointAnimTransform = FTransform::Identity;

	UPROPERTY()
	FName WarpPointAnimBoneName = NAME_None;

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

	//DEPRECATED: Will be removed shortly
	UPROPERTY()
	FMotionWarpingSyncPoint CachedSyncPoint;

	FRootMotionModifier_Warp(){}
	virtual ~FRootMotionModifier_Warp() {}

	//~ Begin FRootMotionModifier Interface
	virtual UScriptStruct* GetScriptStruct() const override { return FRootMotionModifier_Warp::StaticStruct(); }
	virtual void Update(UMotionWarpingComponent& OwnerComp) override;
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) override;
	//~ End FRootMotionModifier Interface

	/** Event called during update if the target transform changes while the warping is active */
	virtual void OnTargetTransformChanged() {}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void PrintLog(const FString& Name, const FTransform& OriginalRootMotion, const FTransform& WarpedRootMotion) const;

	//DEPRECATED will be removed shortly
	void PrintLog(UMotionWarpingComponent& OwnerComp, const FString& Name, const FTransform& OriginalRootMotion, const FTransform& WarpedRootMotion) const
	{
		PrintLog(Name, OriginalRootMotion, WarpedRootMotion);
	}

#endif

protected:

	FORCEINLINE FVector GetTargetLocation() const { return CachedTargetTransform.GetLocation(); }
	FORCEINLINE FRotator GetTargetRotator() const { return GetTargetRotation().Rotator(); }
	FQuat GetTargetRotation() const;
	
	FQuat WarpRotation(const FTransform& RootMotionDelta, const FTransform& RootMotionTotal, float DeltaSeconds);

	//@TODO: This should be Optional and private
	UPROPERTY()
	FTransform CachedTargetTransform = FTransform::Identity;

private:

	/** Cached of the offset from the warp point. Used to calculate the final target transform when a warp point is defined in the animation */
	TOptional<FTransform> CachedOffsetFromWarpPoint;
};

UCLASS(meta = (DisplayName = "Simple Warp"))
class MOTIONWARPING_API URootMotionModifierConfig_Warp : public URootMotionModifierConfig
{
	GENERATED_BODY()

public:

	/** Name used to find the warp point for this modifier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (DisplayName = "Warp Point Name"))
	FName SyncPointName = NAME_None;  //@TODO: Rename to WarpPointName

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	EWarpPointAnimProvider WarpPointAnimProvider = EWarpPointAnimProvider::None;

	//@TODO: Hide from the UI when Provider != Static
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "WarpPointAnimProvider == EWarpPointAnimProvider::Static"))
	FTransform WarpPointAnimTransform = FTransform::Identity;

	//@TODO: Hide from the UI when Provider != Bone
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "WarpPointAnimProvider == EWarpPointAnimProvider::Bone"))
	FName WarpPointAnimBoneName = NAME_None;

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

	virtual FRootMotionModifierHandle AddRootMotionModifierNew(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		return URootMotionModifierConfig_Warp::AddRootMotionModifierSimpleWarp(MotionWarpingComp, Animation, StartTime, EndTime, 
			SyncPointName, WarpPointAnimProvider, WarpPointAnimTransform, WarpPointAnimBoneName,
			bWarpTranslation, bIgnoreZAxis, bWarpRotation, RotationType, WarpRotationTimeMultiplier);
	}

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static FRootMotionModifierHandle AddRootMotionModifierSimpleWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime,
		FName InSyncPointName, EWarpPointAnimProvider InWarpPointAnimProvider, FTransform InWarpPointAnimTransform, FName InWarpPointAnimBoneName,
		bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier = 1.f);
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

	virtual FRootMotionModifierHandle AddRootMotionModifierNew(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		return URootMotionModifierConfig_Scale::AddRootMotionModifierScale(MotionWarpingComp, Animation, StartTime, EndTime, Scale);
	}

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static FRootMotionModifierHandle AddRootMotionModifierScale(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, FVector InScale);
};