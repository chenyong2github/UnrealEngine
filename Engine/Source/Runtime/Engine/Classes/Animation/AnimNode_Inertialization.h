// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimCurveTypes.h"
#include "AnimNode_Inertialization.generated.h"


// Inertialization: High-Performance Animation Transitions in 'Gears of War'
// David Bollo
// Game Developer Conference 2018
//
// https://www.gdcvault.com/play/1025165/Inertialization
// https://www.gdcvault.com/play/1025331/Inertialization


UENUM()
enum class EInertializationState : uint8
{
	Inactive,		// Inertialization inactive
	Pending,		// Inertialization request pending... prepare to capture the pose difference and then switch to active
	Active			// Inertialization active... apply the previously captured pose difference
};


UENUM()
enum class EInertializationBoneState : uint8
{
	Invalid,		// Invalid bone (ie: bone was present in the skeleton but was not present in the pose when it was captured)
	Valid,			// Valid bone
	Excluded		// Valid bone that is to be excluded from the inertialization request
};


UENUM()
enum class EInertializationSpace : uint8
{
	Default,		// Inertialize in local space (default)
	WorldSpace,		// Inertialize translation and rotation in world space (to conceal discontinuities in actor transform such snapping to a new attach parent)
	WorldRotation	// Inertialize rotation only in world space (to conceal discontinuities in actor orientation)
};

struct ENGINE_API FInertializationCurve
{
	FBlendedHeapCurve BlendedCurve;
	TArray<uint16> CurveUIDToArrayIndexLUT;

	FInertializationCurve() = default;

	FInertializationCurve(const FInertializationCurve& Other)
	{
		*this = Other;
	}

	FInertializationCurve(FInertializationCurve&& Other)
	{
		*this = MoveTemp(Other);
	}

	FInertializationCurve& operator=(const FInertializationCurve& Other)
	{
		BlendedCurve.CopyFrom(Other.BlendedCurve);
		BlendedCurve.UIDToArrayIndexLUT = &CurveUIDToArrayIndexLUT;
		CurveUIDToArrayIndexLUT = Other.CurveUIDToArrayIndexLUT;
		return *this;
	}

	FInertializationCurve& operator=(FInertializationCurve&& Other)
	{
		BlendedCurve.MoveFrom(Other.BlendedCurve);
		BlendedCurve.UIDToArrayIndexLUT = &CurveUIDToArrayIndexLUT;
		CurveUIDToArrayIndexLUT = MoveTemp(Other.CurveUIDToArrayIndexLUT);
		return *this;
	}

	template <typename OtherAllocator>
	void InitFrom(const FBaseBlendedCurve<OtherAllocator>& Other)
	{
		CurveUIDToArrayIndexLUT.Reset();

		BlendedCurve.CopyFrom(Other);
		BlendedCurve.UIDToArrayIndexLUT = &CurveUIDToArrayIndexLUT;

		if (Other.UIDToArrayIndexLUT)
		{
			CurveUIDToArrayIndexLUT = *Other.UIDToArrayIndexLUT;
		}
	}
};

USTRUCT()
struct FInertializationPose
{
	GENERATED_BODY()

	FTransform ComponentTransform;

	// Bone transforms indexed by skeleton bone index.  Transforms are in local space except for direct descendants of
	// the root which are in component space (ie: they have been multiplied by the root).  Invalid bones (ie: bones
	// that are present in the skeleton but were not present in the pose when it was captured) are all zero
	//
	TArray<FTransform> BoneTransforms;

	// Bone states indexed by skeleton bone index
	//
	TArray<EInertializationBoneState> BoneStates;

	// Snapshot of active curves
	// 
	FInertializationCurve Curves;

	FName AttachParentName;
	float DeltaTime;

	FInertializationPose()
		: ComponentTransform(FTransform::Identity)
		, AttachParentName(NAME_None)
		, DeltaTime(0.0f)
	{
	}

	FInertializationPose(const FInertializationPose&) = default;
	FInertializationPose(FInertializationPose&&) = default;
	FInertializationPose& operator=(const FInertializationPose&) = default;
	FInertializationPose& operator=(FInertializationPose&&) = default;

	void InitFrom(const FCompactPose& Pose, const FBlendedCurve& InCurves, const FTransform& InComponentTransform, const FName& InAttachParentName, float InDeltaTime);
};

template <>
struct TUseBitwiseSwap<FInertializationPose>
{
	enum { Value = false };
};


USTRUCT()
struct FInertializationBoneDiff
{
	GENERATED_BODY()

	FVector TranslationDirection;
	FVector RotationAxis;
	FVector ScaleAxis;

	float TranslationMagnitude;
	float TranslationSpeed;

	float RotationAngle;
	float RotationSpeed;

	float ScaleMagnitude;
	float ScaleSpeed;

	FInertializationBoneDiff()
		: TranslationDirection(FVector::ZeroVector)
		, RotationAxis(FVector::ZeroVector)
		, ScaleAxis(FVector::ZeroVector)
		, TranslationMagnitude(0.0f)
		, TranslationSpeed(0.0f)
		, RotationAngle(0.0f)
		, RotationSpeed(0.0f)
		, ScaleMagnitude(0.0f)
		, ScaleSpeed(0.0f)
	{
	}

	void Clear()
	{
		TranslationDirection = FVector::ZeroVector;
		RotationAxis = FVector::ZeroVector;
		ScaleAxis = FVector::ZeroVector;
		TranslationMagnitude = 0.0f;
		TranslationSpeed = 0.0f;
		RotationAngle = 0.0f;
		RotationSpeed = 0.0f;
		ScaleMagnitude = 0.0f;
		ScaleSpeed = 0.0f;
	}
};

USTRUCT()
struct FInertializationCurveDiff
{
	GENERATED_BODY();

	float Delta;
	float Derivative;

	FInertializationCurveDiff()
		: Delta(0.0f)
		, Derivative(0.0f)
	{}

	void Clear()
	{
		Delta = 0.0f;
		Derivative = 0.0f;
	}
};


USTRUCT()
struct FInertializationPoseDiff
{
	GENERATED_BODY()

	FInertializationPoseDiff()
		: InertializationSpace(EInertializationSpace::Default)
	{
	}

	void Reset()
	{
		BoneDiffs.Empty();
		CurveDiffs.Empty();
		InertializationSpace = EInertializationSpace::Default;
	}

	// Initialize the pose difference from the current pose and the two previous snapshots
	//
	// Pose					the current frame's pose
	// ComponentTransform	the current frame's component to world transform
	// AttachParentName		the current frame's attach parent name (for checking if the attachment has changed)
	// Prev1				the previous frame's pose
	// Prev2				the pose from two frames before
	//
	void InitFrom(const FCompactPose& Pose, const FBlendedCurve& Curves, const FTransform& ComponentTransform, const FName& AttachParentName, const FInertializationPose& Prev1, const FInertializationPose& Prev2);

	// Apply this difference to a pose, decaying over time as InertializationElapsedTime approaches InertializationDuration
	//
	void ApplyTo(FCompactPose& Pose, FBlendedCurve& Curves, float InertializationElapsedTime, float InertializationDuration) const;

	// Get the inertialization space for this pose diff (for debug display)
	//
	EInertializationSpace GetInertializationSpace() const
	{
		return InertializationSpace;
	}

private:

	static float CalcInertialFloat(float x0, float v0, float t, float t1);

	// Bone differences indexed by skeleton bone index
	TArray<FInertializationBoneDiff> BoneDiffs;

	// Curve differences indexed by CurveID
	TArray<FInertializationCurveDiff> CurveDiffs;

	// Inertialization space (local vs world for situations where we wish to correct a world-space discontinuity such as an abrupt orientation change)
	EInertializationSpace InertializationSpace;
};


USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_Inertialization : public FAnimNode_Base
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Source;

public: // FAnimNode_Inertialization

	FAnimNode_Inertialization();
	
	// Request to activate inertialization for a duration.
	// If multiple requests are made on the same inertialization node, the minimum requested time will be used.
	//
	virtual void RequestInertialization(float Duration);

	virtual float GetRequestedDuration() const { return RequestedDuration; }

	// Log an error when a node wants to inertialize but no inertialization ancestor node exists
	//
	static void LogRequestError(const FAnimationUpdateContext& Context, const FPoseLinkBase& RequesterPoseLink);

public: // FAnimNode_Base

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	virtual bool NeedsDynamicReset() const override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;

	virtual bool WantsSkippedUpdates() const override;
	virtual void OnUpdatesSkipped(TArrayView<const FAnimationUpdateContext *> SkippedUpdateContexts) override;


protected:

	// Consume Inertialization Request
	//
	// Returns any pending inertialization request and removes it from future processing.  Returns zero if there is no pending request.
	// This function is virtual so that a derived class could optionally hook into other external sources of inertialization requests
	// (for example from the owning actor for requests triggered from game code).
	//
	virtual float ConsumeInertializationRequest(FPoseContext& Context);

	// Start Inertialization
	//
	// Computes the inertialization pose difference from the current pose and the two previous poses (to capture velocity).  This function
	// is virtual so that a derived class could optionally regularize the pose snapshots to align better with the current frame's pose
	// before computing the inertial difference (for example to correct for instantaneous changes in the root relative to its children).
	//
	virtual void StartInertialization(FPoseContext& Context, FInertializationPose& PreviousPose1, FInertializationPose& PreviousPose2, float Duration, /*OUT*/ FInertializationPoseDiff& OutPoseDiff);

	// Apply Inertialization
	//
	// Applies the inertialization pose difference to the current pose (feathering down to zero as ElapsedTime approaches Duration).  This
	// function is virtual so that a derived class could optionally adjust the pose based on any regularization done in StartInertialization.
	//
	virtual void ApplyInertialization(FPoseContext& Context, const FInertializationPoseDiff& PoseDiff, float ElapsedTime, float Duration);


private:

	// Snapshots of the actor pose from past frames
	TArray<FInertializationPose> PoseSnapshots;

	// Elapsed delta time between calls to evaluate
	float DeltaTime;

	// Pending inertialization request
	float RequestedDuration;

	// Teleport type
	ETeleportType TeleportType;

	// Inertialization state
	EInertializationState InertializationState;
	float InertializationElapsedTime;
	float InertializationDuration;
	float InertializationDeficit;

	// Inertialization pose differences
	FInertializationPoseDiff InertializationPoseDiff;

	// Reset inertialization timing and state
	void Deactivate();
};
