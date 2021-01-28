// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "EngineDefines.h"
#include "PhysicsEngine/ConstraintTypes.h"
#include "PhysicsEngine/ConstraintDrives.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "ConstraintInstance.generated.h"

class FMaterialRenderProxy;
class FMeshElementCollector;
class FPrimitiveDrawInterface;
class UMaterialInterface;
struct FBodyInstance;

class FMaterialRenderProxy;
class FPrimitiveDrawInterface;
class FMaterialRenderProxy;

/** Container for properties of a physics constraint that can be easily swapped at runtime. This is useful for switching different setups when going from ragdoll to standup for example */
USTRUCT()
struct ENGINE_API FConstraintProfileProperties
{
	GENERATED_USTRUCT_BODY()

	/** [PhysX only] Linear tolerance value in world units. If the distance error exceeds this tolerence limit, the body will be projected. */
	UPROPERTY(EditAnywhere, Category = Projection, meta = (editcondition = "bEnableProjection", ClampMin = "0.0"))
	float ProjectionLinearTolerance;

	/** [PhysX only] Angular tolerance value in world units. If the distance error exceeds this tolerence limit, the body will be projected. */
	UPROPERTY(EditAnywhere, Category = Projection, meta = (editcondition = "bEnableProjection", ClampMin = "0.0"))
	float ProjectionAngularTolerance;

	/** [Chaos Only] How much linear projection to apply [0-1]. Projection fixes any post-solve position error in the constraint. */
	UPROPERTY(EditAnywhere, Category = Projection, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProjectionLinearAlpha;

	/** [Chaos Only] How much angular projection to apply [0-1]. Projection fixes any post-solve angle error in the constraint. */
	UPROPERTY(EditAnywhere, Category = Projection, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProjectionAngularAlpha;

	/** Force needed to break the distance constraint. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Linear, meta = (editcondition = "bLinearBreakable", ClampMin = "0.0"))
	float LinearBreakThreshold;

	/** Percent threshold from target position needed to reset the spring rest length.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Linear, meta = (editcondition = "bLinearPlasticity", ClampMin = "0.0"))
	float LinearPlasticityThreshold;

	/** Torque needed to break the joint. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Angular, meta = (editcondition = "bAngularBreakable", ClampMin = "0.0"))
	float AngularBreakThreshold;

	/** Degree threshold from target angle needed to reset the target angle.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Angular, meta = (editcondition = "bAngularPlasticity", ClampMin = "0.0"))
	float AngularPlasticityThreshold;

	UPROPERTY(EditAnywhere, Category = Linear)
	FLinearConstraint LinearLimit;

	UPROPERTY(EditAnywhere, Category = Angular)
	FConeConstraint ConeLimit;

	UPROPERTY(EditAnywhere, Category = Angular)
	FTwistConstraint TwistLimit;

	UPROPERTY(EditAnywhere, Category = Linear)
	FLinearDriveConstraint LinearDrive;

	UPROPERTY(EditAnywhere, Category = Angular)
	FAngularDriveConstraint AngularDrive;

	// Disable collision between bodies joined by this constraint.
	UPROPERTY(EditAnywhere, Category = Constraint)
	uint8 bDisableCollision : 1;

	// When set, the parent body in a constraint will not be affected by the motion of the child
	UPROPERTY(EditAnywhere, Category = Constraint)
	uint8 bParentDominates : 1;

	/**
	* [PhysX] If distance error between bodies exceeds 0.1 units, or rotation error exceeds 10 degrees, body will be projected to fix this.
	* For example a chain spinning too fast will have its elements appear detached due to velocity, this will project all bodies so they still appear attached to each other.
	*
	* [Chaos] Chaos applies a post-solve position and angular fixup where the parent body in the constraint is treated as having infinite mass and the child body is 
	* translated and rotated to resolve any remaining errors. This can be used to make constraint chains significantly stiffer at lower iteration counts. Increasing
	* iterations would have the same effect, but be much more expensive. Projection only works well if the chain is not interacting with other objects (e.g.,
	* through collisions) because the projection of the bodies in the chain will cause other constraints to be violated. Likewise, if a body is influenced by multiple
	* constraints, then enabling projection on more than one constraint may lead to unexpected results - the "last" constraint would win but the order in which constraints
	* are solved cannot be directly controlled.
	*
	* Note: projection will not be applied to constraints with soft limits.
	*/
	UPROPERTY(EditAnywhere, Category = Projection)
	uint8 bEnableProjection : 1;

	/**
	 * [Chaos Only] Apply projection to constraints with soft limits. This can be used to stiffen up soft joints at low iteration counts, but the projection will
	 * override a lot of the spring-damper behaviour of the soft limits. E.g., if you have soft projection enabled and ProjectionAngularAlpha = 1.0,
	 * the joint will act as if it is a hard limit.
	 */
	UPROPERTY(EditAnywhere, Category = Projection)
	uint8 bEnableSoftProjection : 1;

	/** Whether it is possible to break the joint with angular force. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Angular)
	uint8 bAngularBreakable : 1;

	/** Whether it is possible to reset target rotations from the angular displacement. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Angular)
	uint8 bAngularPlasticity : 1;

	/** Whether it is possible to break the joint with linear force. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Linear)
	uint8 bLinearBreakable : 1;

	/** Whether it is possible to reset spr4ing rest length from the linear deformation. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Linear)
	uint8 bLinearPlasticity : 1;

	FConstraintProfileProperties();

	/** Updates physx joint properties from unreal properties (limits, drives, flags, etc...) */
	void Update_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, float AverageMass, float UseScale) const;

	/** Updates joint breakable properties (threshold, etc...)*/
	void UpdateBreakable_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef) const;

	/** Updates joint breakable properties (threshold, etc...)*/
	void UpdatePlasticity_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef) const;

	/** Updates joint flag based on profile properties */
	void UpdateConstraintFlags_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef) const;

#if WITH_EDITOR
	void SyncChangedConstraintProperties(struct FPropertyChangedChainEvent& PropertyChangedEvent);
#endif
};

USTRUCT()
struct ENGINE_API FConstraintInstanceBase
{
	GENERATED_USTRUCT_BODY()

	/** Constructor **/
	FConstraintInstanceBase();
	void Reset();


	/** Indicates position of this constraint within the array in SkeletalMeshComponent. */
	int32 ConstraintIndex;

	// Internal physics constraint representation
	FPhysicsConstraintHandle ConstraintHandle;

	// Scene thats using the constraint
	FPhysScene* PhysScene;

	FPhysScene* GetPhysicsScene() { return PhysScene; }
	const FPhysScene* GetPhysicsScene() const { return PhysScene; }
};

/** Container for a physics representation of an object. */
USTRUCT()
struct ENGINE_API FConstraintInstance : public FConstraintInstanceBase
{
	GENERATED_USTRUCT_BODY()

	/** Name of bone that this joint is associated with. */
	UPROPERTY(VisibleAnywhere, Category=Constraint)
	FName JointName;

	///////////////////////////// CONSTRAINT GEOMETRY
	
	/** 
	 *	Name of first bone (body) that this constraint is connecting. 
	 *	This will be the 'child' bone in a PhysicsAsset.
	 */
	UPROPERTY(EditAnywhere, Category=Constraint)
	FName ConstraintBone1;

	/** 
	 *	Name of second bone (body) that this constraint is connecting. 
	 *	This will be the 'parent' bone in a PhysicsAset.
	 */
	UPROPERTY(EditAnywhere, Category=Constraint)
	FName ConstraintBone2;

	///////////////////////////// Body1 ref frame
	
	/** Location of constraint in Body1 reference frame (usually the "child" body for skeletal meshes). */
	UPROPERTY(EditAnywhere, Category=Constraint)
	FVector Pos1;

	/** Primary (twist) axis in Body1 reference frame. */
	UPROPERTY()
	FVector PriAxis1;

	/** Seconday axis in Body1 reference frame. Orthogonal to PriAxis1. */
	UPROPERTY()
	FVector SecAxis1;

	///////////////////////////// Body2 ref frame
	
	/** Location of constraint in Body2 reference frame (usually the "parent" body for skeletal meshes). */
	UPROPERTY(EditAnywhere, Category=Constraint)
	FVector Pos2;

	/** Primary (twist) axis in Body2 reference frame. */
	UPROPERTY()
	FVector PriAxis2;

	/** Seconday axis in Body2 reference frame. Orthogonal to PriAxis2. */
	UPROPERTY()
	FVector SecAxis2;

	/** Specifies the angular offset between the two frames of reference. By default limit goes from (-Angle, +Angle)
	* This allows you to bias the limit for swing1 swing2 and twist. */
	UPROPERTY(EditAnywhere, Category = Angular)
	FRotator AngularRotationOffset;

	/** If true, linear limits scale using the absolute min of the 3d scale of the owning component */
	UPROPERTY(EditAnywhere, Category = Linear)
	uint32 bScaleLinearLimits : 1;

	float AverageMass;

	//Constraint Data (properties easily swapped at runtime based on different constraint profiles)
	UPROPERTY(EditAnywhere, Category = Constraint)
	FConstraintProfileProperties ProfileInstance;

public:
	/** Copies behavior properties from the given profile. Automatically updates the physx representation if it's been created */
	void CopyProfilePropertiesFrom(const FConstraintProfileProperties& FromProperties);

	/** Get underlying physics engine constraint */
	const FPhysicsConstraintHandle& GetPhysicsConstraintRef() const;

	FChaosUserData UserData;

private:
	/** The component scale passed in during initialization*/
	float LastKnownScale;

public:

	/** Constructor **/
	FConstraintInstance();

	/** Gets the linear limit size */
	float GetLinearLimit() const
	{
		return ProfileInstance.LinearLimit.Limit;
	}

	/** Sets the Linear XYZ Motion Type and the limit distance (Note distance is the same for all 3 axes) */
	void SetLinearLimits(ELinearConstraintMotion XConstraintType, ELinearConstraintMotion YConstraintType, ELinearConstraintMotion ZConstraintType, float InLinearLimitSize)
	{
		ProfileInstance.LinearLimit.XMotion = XConstraintType;
		ProfileInstance.LinearLimit.YMotion = YConstraintType;
		ProfileInstance.LinearLimit.ZMotion = ZConstraintType;
		ProfileInstance.LinearLimit.Limit = InLinearLimitSize;
		UpdateLinearLimit();
	}

	/** Gets the motion type for the linear X-axis limit. */
	ELinearConstraintMotion GetLinearXMotion() const
	{
		return ProfileInstance.LinearLimit.XMotion;
	}

	/** Sets the Linear XMotion type */
	void SetLinearXMotion(ELinearConstraintMotion XConstraintType)
	{
		const FLinearConstraint& prevLimits = ProfileInstance.LinearLimit;
		SetLinearLimits(XConstraintType, prevLimits.YMotion, prevLimits.ZMotion, prevLimits.Limit);
	}

	/** Sets the LinearX Motion Type and the limit distance (Note distance is the same for all 3 axes) */
	void SetLinearXLimit(ELinearConstraintMotion XConstraintType, float InLinearLimitSize)
	{
		const FLinearConstraint& prevLimits = ProfileInstance.LinearLimit;
		SetLinearLimits(XConstraintType, prevLimits.YMotion, prevLimits.ZMotion, InLinearLimitSize);
	}

	/** Gets the motion type for the linear Y-axis limit. */
	ELinearConstraintMotion GetLinearYMotion() const
	{
		return ProfileInstance.LinearLimit.YMotion;
	}

	/** Sets the Linear YMotion type */
	void SetLinearYMotion(ELinearConstraintMotion YConstraintType)
	{
		const FLinearConstraint& prevLimits = ProfileInstance.LinearLimit;
		SetLinearLimits(prevLimits.XMotion, YConstraintType, prevLimits.ZMotion, prevLimits.Limit);
	}

	/** Sets the LinearY Motion Type and the limit distance (Note distance is the same for all 3 axes) */
	void SetLinearYLimit(ELinearConstraintMotion YConstraintType, float InLinearLimitSize)
	{
		const FLinearConstraint& prevLimits = ProfileInstance.LinearLimit;
		SetLinearLimits(prevLimits.XMotion, YConstraintType, prevLimits.ZMotion, InLinearLimitSize);
	}

	/** Gets the motion type for the linear Z-axis limit. */
	ELinearConstraintMotion GetLinearZMotion() const
	{
		return ProfileInstance.LinearLimit.ZMotion;
	}

	/** Sets the Linear ZMotion type */
	void SetLinearZMotion(ELinearConstraintMotion ZConstraintType)
	{
		const FLinearConstraint& prevLimits = ProfileInstance.LinearLimit;
		SetLinearLimits(prevLimits.XMotion, prevLimits.YMotion, ZConstraintType, prevLimits.Limit);
	}

	/** Sets the LinearZ Motion Type and the limit distance (Note distance is the same for all 3 axes) */
	void SetLinearZLimit(ELinearConstraintMotion ZConstraintType, float InLinearLimitSize)
	{
		const FLinearConstraint& prevLimits = ProfileInstance.LinearLimit;
		SetLinearLimits(prevLimits.XMotion, prevLimits.YMotion, ZConstraintType, InLinearLimitSize);
	}

	/** Gets the motion type for the swing1 of the cone constraint */
	EAngularConstraintMotion GetAngularSwing1Motion() const
	{
		return ProfileInstance.ConeLimit.Swing1Motion;
	}

	/** Sets the cone limit's swing1 motion type */
	void SetAngularSwing1Motion(EAngularConstraintMotion MotionType)
	{
		ProfileInstance.ConeLimit.Swing1Motion = MotionType;
		UpdateAngularLimit();
	}

	// The current swing1 of the constraint
	float GetCurrentSwing1() const;

	/** Gets the cone limit swing1 angle in degrees */
	float GetAngularSwing1Limit() const
	{
		return ProfileInstance.ConeLimit.Swing1LimitDegrees;
	}

	/** Sets the Angular Swing1 Motion Type
	*	@param MotionType	New Motion Type
	*/
	void SetAngularSwing1Limit(EAngularConstraintMotion MotionType, float InSwing1LimitAngle)
	{
		ProfileInstance.ConeLimit.Swing1Motion = MotionType;
		ProfileInstance.ConeLimit.Swing1LimitDegrees = InSwing1LimitAngle;
		UpdateAngularLimit();
	}
	
	/** Gets the motion type for the swing2 of the cone constraint */
	EAngularConstraintMotion GetAngularSwing2Motion() const
	{
		return ProfileInstance.ConeLimit.Swing2Motion;
	}

	/** Sets the cone limit's swing2 motion type */
	void SetAngularSwing2Motion(EAngularConstraintMotion MotionType)
	{
		ProfileInstance.ConeLimit.Swing2Motion = MotionType;
		UpdateAngularLimit();
	}

	// The current swing2 of the constraint
	float GetCurrentSwing2() const;

	/** Gets the cone limit swing2 angle in degrees */
	float GetAngularSwing2Limit() const
	{
		return ProfileInstance.ConeLimit.Swing2LimitDegrees;
	}

	/** Sets the Angular Swing2 Motion Type
	*	@param MotionType	New Motion Type
	*/
	void SetAngularSwing2Limit(EAngularConstraintMotion MotionType, float InSwing2LimitAngle)
	{
		ProfileInstance.ConeLimit.Swing2Motion = MotionType;
		ProfileInstance.ConeLimit.Swing2LimitDegrees = InSwing2LimitAngle;
		UpdateAngularLimit();
	}

	/** Gets the motion type for the twist of the cone constraint */
	EAngularConstraintMotion GetAngularTwistMotion() const
	{
		return ProfileInstance.TwistLimit.TwistMotion;
	}

	/** Sets the twist limit's motion type */
	void SetAngularTwistMotion(EAngularConstraintMotion MotionType)
	{
		ProfileInstance.TwistLimit.TwistMotion = MotionType;
		UpdateAngularLimit();
	}

	// The current twist of the constraint
	float GetCurrentTwist() const;

	/** Gets the twist limit angle in degrees */
	float GetAngularTwistLimit() const
	{
		return ProfileInstance.TwistLimit.TwistLimitDegrees;
	}

	/** Sets the Angular Twist Motion Type
	*	@param MotionType	New Motion Type
	*/
	void SetAngularTwistLimit(EAngularConstraintMotion MotionType, float InTwistLimitAngle)
	{
		ProfileInstance.TwistLimit.TwistMotion = MotionType;
		ProfileInstance.TwistLimit.TwistLimitDegrees = InTwistLimitAngle;
		UpdateAngularLimit();
	}

	/** Whether the linear limits are soft (only if at least one axis if Limited) */
	bool GetIsSoftLinearLimit() const
	{
		return ProfileInstance.LinearLimit.bSoftConstraint;
	}

	/** Linear stiffness if the constraint is set to use soft linear limits */
	float GetSoftLinearLimitStiffness() const
	{
		return ProfileInstance.LinearLimit.Stiffness;
	}

	/** Linear damping if the constraint is set to use soft linear limits */
	float GetSoftLinearLimitDamping() const
	{
		return ProfileInstance.LinearLimit.Damping;
	}

	/** Whether the twist limits are soft (only available if twist is Limited) */
	bool GetIsSoftTwistLimit() const
	{
		return ProfileInstance.TwistLimit.bSoftConstraint;
	}

	/** Twist stiffness if the constraint is set to use soft limits */
	float GetSoftTwistLimitStiffness() const
	{
		return ProfileInstance.TwistLimit.Stiffness;
	}

	/** Twist damping if the constraint is set to use soft limits */
	float GetSoftTwistLimitDamping() const
	{
		return ProfileInstance.TwistLimit.Damping;
	}

	/** Whether the swing limits are soft (only available if swing1 and/or swing2 is Limited) */
	bool GetIsSoftSwingLimit() const
	{
		return ProfileInstance.ConeLimit.bSoftConstraint;
	}

	/** Swing stiffness if the constraint is set to use soft limits */
	float GetSoftSwingLimitStiffness() const
	{
		return ProfileInstance.ConeLimit.Stiffness;
	}

	/** Swing damping if the constraint is set to use soft limits */
	float GetSoftSwingLimitDamping() const
	{
		return ProfileInstance.ConeLimit.Damping;
	}

	/** Sets the Linear Breakable properties
	*	@param bInLinearBreakable		Whether it is possible to break the joint with linear force
	*	@param InLinearBreakThreshold	Force needed to break the joint
	*/
	void SetLinearBreakable(bool bInLinearBreakable, float InLinearBreakThreshold)
	{
		ProfileInstance.bLinearBreakable = bInLinearBreakable;
		ProfileInstance.LinearBreakThreshold = InLinearBreakThreshold;
		UpdateBreakable();
	}

	/** Gets Whether it is possible to break the joint with linear force */
	bool IsLinearBreakable() const
	{
		return ProfileInstance.bLinearBreakable;
	}

	/** Gets linear force needed to break the joint */
	float GetLinearBreakThreshold() const
	{
		return ProfileInstance.LinearBreakThreshold;
	}

	/** Sets the Linear Plasticity properties
	*	@param bInLinearPlasticity 	Whether it is possible to reset the target angles
	*	@param InLinearPlasticityThreshold	Delta from target needed to reset the target joint
	*/
	void SetLinearPlasticity(bool bInLinearPlasticity, float InLinearPlasticityThreshold)
	{
		ProfileInstance.bLinearPlasticity = bInLinearPlasticity;
		ProfileInstance.LinearPlasticityThreshold = InLinearPlasticityThreshold;
		UpdatePlasticity();
	}

	/** Sets the Angular Breakable properties
	*	@param bInAngularBreakable		Whether it is possible to break the joint with angular force
	*	@param InAngularBreakThreshold	Torque needed to break the joint
	*/
	void SetAngularBreakable(bool bInAngularBreakable, float InAngularBreakThreshold)
	{
		ProfileInstance.bAngularBreakable = bInAngularBreakable;
		ProfileInstance.AngularBreakThreshold = InAngularBreakThreshold;
		UpdateBreakable();
	}

	/** Gets Whether it is possible to break the joint with angular force */
	bool IsAngularBreakable() const
	{
		return ProfileInstance.bAngularBreakable;
	}

	/** Gets Torque needed to break the joint */
	float GetAngularBreakThreshold() const
	{
		return ProfileInstance.AngularBreakThreshold;
	}

	/** Sets the Angular Plasticity properties
	*	@param bInAngularPlasticity 	Whether it is possible to reset the target angles
	*	@param InAngularPlasticityThreshold	Delta from target needed to reset the target joint
	*/
	void SetAngularPlasticity(bool bInAngularPlasticity, float InAngularPlasticityThreshold)
	{
		ProfileInstance.bAngularPlasticity = bInAngularPlasticity;
		ProfileInstance.AngularPlasticityThreshold = InAngularPlasticityThreshold;
		UpdatePlasticity();
	}

	/** Gets Whether it is possible to reset the target angles */
	bool HasAngularPlasticity() const
	{
		return ProfileInstance.bAngularPlasticity;
	}

	/** Gets Delta from target needed to reset the target joint */
	float GetAngularPlasticityThreshold() const
	{
		return ProfileInstance.AngularPlasticityThreshold;
	}

	// @todo document
	void CopyConstraintGeometryFrom(const FConstraintInstance* FromInstance);

	// @todo document
	void CopyConstraintParamsFrom(const FConstraintInstance* FromInstance);

	// Retrieve the constraint force most recently applied to maintain this constraint. Returns 0 forces if the constraint is not initialized or broken.
	void GetConstraintForce(FVector& OutLinearForce, FVector& OutAngularForce);

	// Retrieve the status of constraint being broken.
	bool IsBroken();

	/** Set which linear position drives are enabled */
	void SetLinearPositionDrive(bool bEnableXDrive, bool bEnableYDrive, bool bEnableZDrive);

	/** Get which linear position drives is enabled on XAxis */
	bool IsLinearPositionDriveXEnabled() const
	{
		return ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive;
	}

	/** Get which linear position drives is enabled on YAxis */
	bool IsLinearPositionDriveYEnabled() const
	{
		return ProfileInstance.LinearDrive.YDrive.bEnablePositionDrive;
	}

	/** Get which linear position drives is enabled on ZAxis */
	bool IsLinearPositionDriveZEnabled() const
	{
		return ProfileInstance.LinearDrive.ZDrive.bEnablePositionDrive;
	}

	/** Whether the linear position drive is enabled */
	bool IsLinearPositionDriveEnabled() const
	{
		return ProfileInstance.LinearDrive.IsPositionDriveEnabled();
	}

	/** Set the linear drive's target position position */
	void SetLinearPositionTarget(const FVector& InPosTarget);

	/** Get the linear drive's target position position */
	const FVector& GetLinearPositionTarget()
	{
		return ProfileInstance.LinearDrive.PositionTarget;
	}

	/** Set which linear velocity drives are enabled */
	void SetLinearVelocityDrive(bool bEnableXDrive, bool bEnableYDrive, bool bEnableZDrive);

	/** Get which linear position drives is enabled on XAxis */
	bool IsLinearVelocityDriveXEnabled() const
	{
		return ProfileInstance.LinearDrive.XDrive.bEnableVelocityDrive;
	}

	/** Get which linear position drives is enabled on YAxis */
	bool IsLinearVelocityDriveYEnabled() const
	{
		return ProfileInstance.LinearDrive.YDrive.bEnableVelocityDrive;
	}

	/** Get which linear position drives is enabled on ZAxis */
	bool IsLinearVelocityDriveZEnabled() const
	{
		return ProfileInstance.LinearDrive.ZDrive.bEnableVelocityDrive;
	}

	/** Whether the linear velocity drive is enabled */
	bool IsLinearVelocityDriveEnabled() const
	{
		return ProfileInstance.LinearDrive.IsVelocityDriveEnabled();
	}

	/** Set the linear drive's target velocity */
	void SetLinearVelocityTarget(const FVector& InVelTarget);

	/** Get the linear drive's target velocity */
	const FVector& GetLinearVelocityTarget()
	{
		return ProfileInstance.LinearDrive.VelocityTarget;
	}

	/** Set the linear drive's strength parameters */
	void SetLinearDriveParams(float InPositionStrength, float InVelocityStrength, float InForceLimit);

	/** Get the linear drive's strength parameters */
	void GetLinearDriveParams(float& OutPositionStrength, float& OutVelocityStrength, float& OutForceLimit);

	UE_DEPRECATED(4.15, "Please call SetOrientationDriveTwistAndSwing. Note the order of bools is reversed (Make sure to pass Twist and then Swing)")
	void SetAngularPositionDrive(bool bInEnableSwingDrive, bool bInEnableTwistDrive)
	{
		SetOrientationDriveTwistAndSwing(bInEnableTwistDrive, bInEnableSwingDrive);
	}

	/** Set which twist and swing orientation drives are enabled. Only applicable when Twist And Swing drive mode is used */
	void SetOrientationDriveTwistAndSwing(bool bInEnableTwistDrive, bool bInEnableSwingDrive);

	/** Get which twist and swing orientation drives are enabled. Only applicable when Twist And Swing drive mode is used */
	void GetOrientationDriveTwistAndSwing(bool& bOutEnableTwistDrive, bool& bOutEnableSwingDrive);

	/** Set whether the SLERP angular position drive is enabled. Only applicable when SLERP drive mode is used */
	void SetOrientationDriveSLERP(bool bInEnableSLERP);

	/** Get whether the SLERP angular position drive is enabled. Only applicable when SLERP drive mode is used */
	bool GetOrientationDriveSLERP()
	{
		return ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive;
	}

	/** Whether the angular orientation drive is enabled */
	bool IsAngularOrientationDriveEnabled() const
	{
		return ProfileInstance.AngularDrive.IsOrientationDriveEnabled();
	}
	
	/** Set the angular drive's orientation target*/
	void SetAngularOrientationTarget(const FQuat& InPosTarget);

	/** Get the angular drive's orientation target*/
	const FRotator& GetAngularOrientationTarget() const
	{
		return ProfileInstance.AngularDrive.OrientationTarget;
	}

	UE_DEPRECATED(4.15, "Please call SetAngularVelocityDriveTwistAndSwing. Note the order of bools is reversed (Make sure to pass Twist and then Swing)")
	void SetAngularVelocityDrive(bool bInEnableSwingDrive, bool bInEnableTwistDrive)
	{
		SetAngularVelocityDriveTwistAndSwing(bInEnableTwistDrive, bInEnableSwingDrive);
	}

	/** Set which twist and swing angular velocity drives are enabled. Only applicable when Twist And Swing drive mode is used */
	void SetAngularVelocityDriveTwistAndSwing(bool bInEnableTwistDrive, bool bInEnableSwingDrive);

	/** Get which twist and swing angular velocity drives are enabled. Only applicable when Twist And Swing drive mode is used */
	void GetAngularVelocityDriveTwistAndSwing(bool& bOutEnableTwistDrive, bool& bOutEnableSwingDrive);

	/** Set whether the SLERP angular velocity drive is enabled. Only applicable when SLERP drive mode is used */
	void SetAngularVelocityDriveSLERP(bool bInEnableSLERP);

	/** Get whether the SLERP angular velocity drive is enabled. Only applicable when SLERP drive mode is used */
	bool GetAngularVelocityDriveSLERP()
	{
		return ProfileInstance.AngularDrive.SlerpDrive.bEnableVelocityDrive;
	}

	/** Whether the angular velocity drive is enabled */
	bool IsAngularVelocityDriveEnabled() const
	{
		return ProfileInstance.AngularDrive.IsVelocityDriveEnabled();
	}
	
	/** Set the angular drive's angular velocity target*/
	void SetAngularVelocityTarget(const FVector& InVelTarget);

	/** Get the angular drive's angular velocity target*/
	const FVector& GetAngularVelocityTarget() const
	{
		return ProfileInstance.AngularDrive.AngularVelocityTarget;
	}

	/** Set the angular drive's strength parameters*/
	void SetAngularDriveParams(float InSpring, float InDamping, float InForceLimit);

	/** Get the angular drive's strength parameters*/
	void GetAngularDriveParams(float& OutSpring, float& OutDamping, float& OutForceLimit) const;

	/** Set the angular drive mode */
	void SetAngularDriveMode(EAngularDriveMode::Type DriveMode);

	/** Set the angular drive mode */
	EAngularDriveMode::Type GetAngularDriveMode()
	{
		return ProfileInstance.AngularDrive.AngularDriveMode;
	}

	/** Refreshes the physics engine joint's linear limits. Only applicable if the joint has been created already.*/
	void UpdateLinearLimit();

	/** Refreshes the physics engine joint's angular limits. Only applicable if the joint has been created already.*/
	void UpdateAngularLimit();

	/** Scale Angular Limit Constraints (as defined in RB_ConstraintSetup). This only affects the physics engine and does not update the unreal side so you can do things like a LERP of the scale values. */
	void SetAngularDOFLimitScale(float InSwing1LimitScale, float InSwing2LimitScale, float InTwistLimitScale);

	/** Allows you to dynamically change the size of the linear limit 'sphere'. */
	void SetLinearLimitSize(float NewLimitSize);

	/** Create physics engine constraint. */
	void InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float Scale, UObject* DebugOwner, FOnConstraintBroken InConstraintBrokenDelegate = FOnConstraintBroken());

	/** Create physics engine constraint using physx actors. */
	void InitConstraint_AssumesLocked(const FPhysicsActorHandle& ActorRef1, const FPhysicsActorHandle& ActorRef2, float InScale, FOnConstraintBroken InConstraintBrokenDelegate = FOnConstraintBroken());

	/** Set teh constraint broken delegate. */
	void SetConstraintBrokenDelegate(FOnConstraintBroken InConstraintBrokenDelegate);

	/** Terminate physics engine constraint */
	void TermConstraint();

	/** Whether the physics engine constraint has been terminated */
	bool IsTerminated() const;

	/** See if this constraint is valid. */
	bool IsValidConstraintInstance() const;

	// Get component ref frame
	FTransform GetRefFrame(EConstraintFrame::Type Frame) const;

	// Pass in reference frame in. If the constraint is currently active, this will set its active local pose. Otherwise the change will take affect in InitConstraint. 
	void SetRefFrame(EConstraintFrame::Type Frame, const FTransform& RefFrame);

	/** Get the position of this constraint in world space. */
	FVector GetConstraintLocation();

	// Pass in reference position in (maintains reference orientation). If the constraint is currently active, this will set its active local pose. Otherwise the change will take affect in InitConstraint.
	void SetRefPosition(EConstraintFrame::Type Frame, const FVector& RefPosition);

	// Pass in reference orientation in (maintains reference position). If the constraint is currently active, this will set its active local pose. Otherwise the change will take affect in InitConstraint.
	void SetRefOrientation(EConstraintFrame::Type Frame, const FVector& PriAxis, const FVector& SecAxis);

	/** Whether collision is currently disabled */
	bool IsCollisionDisabled() const
	{
		return ProfileInstance.bDisableCollision;
	}

	/** Set whether jointed actors can collide with each other */
	void SetDisableCollision(bool InDisableCollision);

	// @todo document
	void DrawConstraint(int32 ViewIndex, class FMeshElementCollector& Collector,
		float Scale, float LimitDrawScale, bool bDrawLimits, bool bDrawSelected,
		const FTransform& Con1Frame, const FTransform& Con2Frame, bool bDrawAsPoint) const
	{
		DrawConstraintImp(FPDIOrCollector(ViewIndex, Collector), Scale, LimitDrawScale, bDrawLimits, bDrawSelected, Con1Frame, Con2Frame, bDrawAsPoint);
	}

	void DrawConstraint(FPrimitiveDrawInterface* PDI,
		float Scale, float LimitDrawScale, bool bDrawLimits, bool bDrawSelected,
		const FTransform& Con1Frame, const FTransform& Con2Frame, bool bDrawAsPoint) const
	{
		DrawConstraintImp(FPDIOrCollector(PDI), Scale, LimitDrawScale, bDrawLimits, bDrawSelected, Con1Frame, Con2Frame, bDrawAsPoint);
	}

	void GetUsedMaterials(TArray<UMaterialInterface*>& Materials);

	bool Serialize(FArchive& Ar);
#if WITH_EDITORONLY_DATA
	void PostSerialize(const FArchive& Ar);
#endif

	/** Whether projection is enabled for this constraint */
	bool IsProjectionEnabled() const
	{
		return ProfileInstance.bEnableProjection;
	}

	/** Turn on linear and angular projection */
	void EnableProjection();

	/** Turn off linear and angular projection */
	void DisableProjection();

	/** Set projection parameters */
	void SetProjectionParams(bool bEnableProjection, float ProjectionLinearAlphaOrTolerance, float ProjectionAngularAlphaOrTolerance);

	/** Get projection parameters 
	* Chaos returns alphas, PhysX returns tolerances
	*/
	void GetProjectionAlphasOrTolerances(float& ProjectionLinearAlphaOrTolerance, float& ProjectionAngularAlphaOrTolerance) const;

	/** Whether parent domination is enabled (meaning the parent body cannot be be affected at all by a child) */
	bool IsParentDominatesEnabled() const
	{
		return ProfileInstance.bParentDominates;
	}

	/** Enable/Disable parent dominates (meaning the parent body cannot be be affected at all by a child) */
	void EnableParentDominates();
	void DisableParentDominates();


	float GetLastKnownScale() const { return LastKnownScale; }

	//Hacks to easily get zeroed memory for special case when we don't use GC
	static void Free(FConstraintInstance * Ptr);
	static FConstraintInstance * Alloc();

private:

	bool CreateJoint_AssumesLocked(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2);
	void UpdateAverageMass_AssumesLocked(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2);

	struct FPDIOrCollector
	{
		FPrimitiveDrawInterface* PDI;
		FMeshElementCollector* Collector;
		int32 ViewIndex;

		FPDIOrCollector(FPrimitiveDrawInterface* InPDI)
			: PDI(InPDI)
			, Collector(nullptr)
			, ViewIndex(INDEX_NONE)
		{
		}

		FPDIOrCollector(int32 InViewIndex, FMeshElementCollector& InCollector)
			: PDI(nullptr)
			, Collector(&InCollector)
			, ViewIndex(InViewIndex)
		{
		}

		bool HasCollector() const
		{
			return Collector != nullptr;
		}

		FPrimitiveDrawInterface* GetPDI() const;

		void DrawCylinder(const FVector& Start, const FVector& End, float Thickness, FMaterialRenderProxy* MaterialProxy, ESceneDepthPriorityGroup DepthPriority) const;
	};

	void DrawConstraintImp(const FPDIOrCollector& PDIOrCollector,
		float Scale, float LimitDrawScale, bool bDrawLimits, bool bDrawSelected,
		const FTransform& Con1Frame, const FTransform& Con2Frame, bool bDrawAsPoint) const;

	void UpdateBreakable();
	void UpdatePlasticity();
	void UpdateDriveTarget();

	FOnConstraintBroken OnConstraintBrokenDelegate;

	friend struct FConstraintBrokenDelegateData;

public:

	///////////////////////////// DEPRECATED
	// Most of these properties have moved inside the ProfileInstance member (FConstraintProfileProperties struct)
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	uint32 bDisableCollision_DEPRECATED : 1;
	UPROPERTY()
	uint32 bEnableProjection_DEPRECATED : 1;
	UPROPERTY()
	float ProjectionLinearTolerance_DEPRECATED;
	UPROPERTY()
	float ProjectionAngularTolerance_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<enum ELinearConstraintMotion> LinearXMotion_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<enum ELinearConstraintMotion> LinearYMotion_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<enum ELinearConstraintMotion> LinearZMotion_DEPRECATED;
	UPROPERTY()
	float LinearLimitSize_DEPRECATED;
	UPROPERTY()
	uint32 bLinearLimitSoft_DEPRECATED : 1;
	UPROPERTY()
	float LinearLimitStiffness_DEPRECATED;
	UPROPERTY()
	float LinearLimitDamping_DEPRECATED;
	UPROPERTY()
	uint32 bLinearBreakable_DEPRECATED : 1;
	UPROPERTY()
	float LinearBreakThreshold_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<enum EAngularConstraintMotion> AngularSwing1Motion_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<enum EAngularConstraintMotion> AngularTwistMotion_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<enum EAngularConstraintMotion> AngularSwing2Motion_DEPRECATED;
	UPROPERTY()
	uint32 bSwingLimitSoft_DEPRECATED : 1;
	UPROPERTY()
	uint32 bTwistLimitSoft_DEPRECATED : 1;
	UPROPERTY()
	float Swing1LimitAngle_DEPRECATED;
	UPROPERTY()
	float TwistLimitAngle_DEPRECATED;
	UPROPERTY()
	float Swing2LimitAngle_DEPRECATED;
	UPROPERTY()
	float SwingLimitStiffness_DEPRECATED;
	UPROPERTY()
	float SwingLimitDamping_DEPRECATED;
	UPROPERTY()
	float TwistLimitStiffness_DEPRECATED;
	UPROPERTY()
	float TwistLimitDamping_DEPRECATED;
	UPROPERTY()
	uint32 bAngularBreakable_DEPRECATED : 1;
	UPROPERTY()
	float AngularBreakThreshold_DEPRECATED;
private:
	UPROPERTY()
	uint32 bLinearXPositionDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bLinearXVelocityDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bLinearYPositionDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bLinearYVelocityDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bLinearZPositionDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bLinearZVelocityDrive_DEPRECATED : 1;
public:
	UPROPERTY()
	uint32 bLinearPositionDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bLinearVelocityDrive_DEPRECATED : 1;
	UPROPERTY()
	FVector LinearPositionTarget_DEPRECATED;
	UPROPERTY()
	FVector LinearVelocityTarget_DEPRECATED;
	UPROPERTY()
	float LinearDriveSpring_DEPRECATED;
	UPROPERTY()
	float LinearDriveDamping_DEPRECATED;
	UPROPERTY()
	float LinearDriveForceLimit_DEPRECATED;
	UPROPERTY()
	uint32 bSwingPositionDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bSwingVelocityDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bTwistPositionDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bTwistVelocityDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bAngularSlerpDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bAngularOrientationDrive_DEPRECATED : 1;
private:
	UPROPERTY()
	uint32 bEnableSwingDrive_DEPRECATED : 1;
	UPROPERTY()
	uint32 bEnableTwistDrive_DEPRECATED : 1;
public:
	UPROPERTY()
	uint32 bAngularVelocityDrive_DEPRECATED : 1;
	UPROPERTY()
	FQuat AngularPositionTarget_DEPRECATED;
	UPROPERTY()
	TEnumAsByte<EAngularDriveMode::Type> AngularDriveMode_DEPRECATED;
	UPROPERTY()
	FRotator AngularOrientationTarget_DEPRECATED;
	UPROPERTY()
	FVector AngularVelocityTarget_DEPRECATED;    // Revolutions per second
	UPROPERTY()
	float AngularDriveSpring_DEPRECATED;
	UPROPERTY()
	float AngularDriveDamping_DEPRECATED;
	UPROPERTY()
	float AngularDriveForceLimit_DEPRECATED;
#endif //EDITOR_ONLY_DATA
};

template<>
struct TStructOpsTypeTraits<FConstraintInstance> : public TStructOpsTypeTraitsBase2<FConstraintInstance>
{
	enum 
	{
		WithSerializer = true,
#if WITH_EDITORONLY_DATA
		WithPostSerialize = true
#endif
	};
};


// Wrapping type around instance pointer to be returned per value in Blueprints
USTRUCT(BlueprintType)
struct ENGINE_API FConstraintInstanceAccessor
{
	GENERATED_USTRUCT_BODY()

public:
	FConstraintInstanceAccessor()
		: Owner(nullptr)
		, Index(0)
	{}

	FConstraintInstanceAccessor(const TWeakObjectPtr<UObject>& Owner, uint32 Index = 0)
		: Owner(Owner)
		, Index(Index)
	{}

	FConstraintInstance* Get() const;

private:
	UPROPERTY()
	TWeakObjectPtr<UObject> Owner;

	UPROPERTY()
	uint32 Index;
	
};