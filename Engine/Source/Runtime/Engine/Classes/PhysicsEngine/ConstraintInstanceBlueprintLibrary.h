// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"
#include "PhysicsEngine/ConstraintInstance.h"

#include "ConstraintInstanceBlueprintLibrary.generated.h"

UCLASS()
class ENGINE_API UConstraintInstanceBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//---------------------------------------------------------------------------------------------------
	//
	// CONSTRAINT BEHAVIOR 
	//
	//---------------------------------------------------------------------------------------------------

	/** Sets whether bodies attched to the constraint can collide or not
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bDisableCollision	true to disable collision between constrained bodies
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetDisableCollision(
			UPARAM(ref) FConstraintInstanceAccessor& Accessor,
			bool bDisableCollision
		);

	/** Gets whether bodies attched to the constraint can collide or not
	*	@param ConstraintInstance	Instance of the constraint to change
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static bool GetDisableCollsion(FConstraintInstanceAccessor& Accessor);

	/** Sets projection parameters of the constraint
	*	@param ConstraintInstance		Instance of the constraint to change
	*	@param bEnableProjection		true to enable projection
	*	@param ProjectionLinearAlpha	how much linear projection to apply in [0,1] range
	*	@param ProjectionAngularAlpha	how much angular projection to apply in [0,1] range
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetProjectionParams(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool bEnableProjection,
		float ProjectionLinearAlpha,
		float ProjectionAngularAlpha
	);

	/** Gets projection parameters of the constraint
	*	@param ConstraintInstance		Instance of the constraint to change
	*	@param bEnableProjection		true to enable projection
	*	@param ProjectionLinearAlpha	how much linear projection to apply in [0,1] range
	*	@param ProjectionAngularAlpha	how much angular projection to apply in [0,1] range
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetProjectionParams(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bEnableProjection,
		float& ProjectionLinearAlpha,
		float& ProjectionAngularAlpha
	);

	/** Sets whether the parent body is not affected by it's child motion 
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bParentDominates		true to avoid the parent being affected by its child motion
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetParentDominates(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool bParentDominates
	);

	/** Gets whether the parent body is not affected by it's child motion
	*	@param ConstraintInstance	Instance of the constraint to change
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static bool GetParentDominates(FConstraintInstanceAccessor& Accessor);

	//---------------------------------------------------------------------------------------------------
	//
	// LINEAR LIMITS
	//
	//---------------------------------------------------------------------------------------------------

	/** Sets Constraint Linear Motion Ranges
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param XMotion	Type of motion along the X axis
	*	@param YMotion	Type of motion along the Y axis
	*	@param ZMotion	Type of motion along the Z axis
	*	@param Limit	linear limit to apply to all axis
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearLimits(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		TEnumAsByte<ELinearConstraintMotion> XMotion,
		TEnumAsByte<ELinearConstraintMotion> YMotion,
		TEnumAsByte<ELinearConstraintMotion> ZMotion,
		float Limit
	);

	/** Gets Constraint Linear Motion Ranges
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param XMotion	Type of motion along the X axis
	*	@param YMotion	Type of motion along the Y axis
	*	@param ZMotion	Type of motion along the Z axis
	*	@param Limit	linear limit applied to all axis
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearLimits(
		FConstraintInstanceAccessor& Accessor,
		TEnumAsByte<ELinearConstraintMotion>& XMotion,
		TEnumAsByte<ELinearConstraintMotion>& YMotion,
		TEnumAsByte<ELinearConstraintMotion>& ZMotion,
		float& Limit
	);

	/** Sets the Linear Breakable properties
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bLinearBreakable		Whether it is possible to break the joint with linear force
	*	@param LinearBreakThreshold	Force needed to break the joint
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearBreakable(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		bool bLinearBreakable, 
		float LinearBreakThreshold
	);

	/** Gets Constraint Linear Breakable properties
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param bLinearBreakable		Whether it is possible to break the joint with linear force
	*	@param LinearBreakThreshold	Force needed to break the joint
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearBreakable(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bLinearBreakable,
		float& LinearBreakThreshold
	);

	//---------------------------------------------------------------------------------------------------
	//
	// ANGULAR LIMITS 
	//
	//---------------------------------------------------------------------------------------------------

	/** Sets COnstraint Angular Motion Ranges
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param Swing1MotionType		Type of swing motion ( first axis )
	*	@param Swing1LimitAngle		Size of limit in degrees in [0, 180] range
	*   @param Swing2MotionType		Type of swing motion ( second axis )
	*	@param Swing2LimitAngle		Size of limit in degrees in [0, 180] range
	*   @param TwistMotionType		Type of twist motion
	*	@param TwistLimitAngle		Size of limit in degrees in [0, 180] range	
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularLimits(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		TEnumAsByte<EAngularConstraintMotion> Swing1MotionType,
		float Swing1LimitAngle, 
		TEnumAsByte<EAngularConstraintMotion> Swing2MotionType,
		float Swing2LimitAngle,
		TEnumAsByte<EAngularConstraintMotion> TwistMotionType,
		float TwistLimitAngle
	);

	/** Gets Constraint Angular Motion Ranges
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param Swing1MotionType		Type of swing motion ( first axis )
	*	@param Swing1LimitAngle		Size of limit in degrees in [0, 180] range
	*   @param Swing2MotionType		Type of swing motion ( second axis )
	*	@param Swing2LimitAngle		Size of limit in degrees in [0, 180] range
	*   @param TwistMotionType		Type of twist motion
	*	@param TwistLimitAngle		Size of limit in degrees in [0, 180] range
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularLimits(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		TEnumAsByte<EAngularConstraintMotion>& Swing1MotionType,
		float& Swing1LimitAngle,
		TEnumAsByte<EAngularConstraintMotion>& Swing2MotionType,
		float& Swing2LimitAngle,
		TEnumAsByte<EAngularConstraintMotion>& TwistMotionType,
		float& TwistLimitAngle
	);

	/** Sets Constraint Angular Breakable properties
	*	@param ConstraintInstance		Instance of the constraint to change
	*	@param bAngularBreakable		Whether it is possible to break the joint with angular force
	*	@param AngularBreakThreshold	Torque needed to break the joint
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularBreakable(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool bAngularBreakable, 
		float AngularBreakThreshold
	);

	/** Gets Constraint Angular Breakable properties
	*	@param ConstraintInstance		Instance of the constraint to query
	*	@param bAngularBreakable		Whether it is possible to break the joint with angular force
	*	@param AngularBreakThreshold	Torque needed to break the joint
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularBreakable(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bAngularBreakable,
		float& AngularBreakThreshold
	);

	/** Sets Constraint Angular Plasticity properties
	*	@param ConstraintInstance			Instance of the constraint to change
	*	@param bAngularPlasticity			Whether it is possible to reset the target angle from the angular displacement
	*	@param AngularPlasticityThreshold	Degrees needed to reset the rest state of the joint
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularPlasticity(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool bAngularPlasticity, 
		float AngularPlasticityThreshold
	);

	/** Sets Constraint Angular Plasticity properties
	*	@param ConstraintInstance			Instance of the constraint to query
	*	@param bAngularPlasticity			Whether it is possible to reset the target angle from the angular displacement
	*	@param AngularPlasticityThreshold	Degrees needed to reset the rest state of the joint
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularPlasticity(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bAngularPlasticity,
		float& AngularPlasticityThreshold
	);

	//---------------------------------------------------------------------------------------------------
	//
	// LINEAR MOTOR
	//
	//---------------------------------------------------------------------------------------------------

	/** Enables/Disables linear position drive
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bEnableDriveX		Indicates whether the drive for the X-Axis should be enabled
	*	@param bEnableDriveY		Indicates whether the drive for the Y-Axis should be enabled
	*	@param bEnableDriveZ		Indicates whether the drive for the Z-Axis should be enabled
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearPositionDrive(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		bool bEnableDriveX, 
		bool bEnableDriveY, 
		bool bEnableDriveZ
	);

	/** Gets whether linear position drive is enabled or not
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param bOutEnableDriveX		Indicates whether the drive for the X-Axis is enabled
	*	@param bOutEnableDriveY		Indicates whether the drive for the Y-Axis is enabled
	*	@param bOutEnableDriveZ		Indicates whether the drive for the Z-Axis is enabled
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearPositionDrive(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bOutEnableDriveX,
		bool& bOutEnableDriveY,
		bool& bOutEnableDriveZ
	);

	/** Enables/Disables linear velocity drive
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bEnableDriveX		Indicates whether the drive for the X-Axis should be enabled
	*	@param bEnableDriveY		Indicates whether the drive for the Y-Axis should be enabled
	*	@param bEnableDriveZ		Indicates whether the drive for the Z-Axis should be enabled
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearVelocityDrive(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool bEnableDriveX, 
		bool bEnableDriveY, 
		bool bEnableDriveZ
	);

	/** Gets whether linear velocity drive is enabled or not
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param bOutEnableDriveX		Indicates whether the drive for the X-Axis is enabled
	*	@param bOutEnableDriveY		Indicates whether the drive for the Y-Axis is enabled
	*	@param bOutEnableDriveZ		Indicates whether the drive for the Z-Axis is enabled
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearVelocityDrive(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bOutEnableDriveX,
		bool& bOutEnableDriveY,
		bool& bOutEnableDriveZ
	);

	/** Sets the target position for the linear drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param InPosTarget			Target position
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearPositionTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		const FVector& InPosTarget
	);

	/** Gets the target position for the linear drive.
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param OutPosTarget			Target position
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearPositionTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		FVector& OutPosTarget
	);

	/** Sets the target velocity for the linear drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param InVelTarget			Target velocity
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearVelocityTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		const FVector& InVelTarget
	);

	/** Gets the target velocity for the linear drive.
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param OutVelTarget			Target velocity
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearVelocityTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		FVector& OutVelTarget
	);

	/** Sets the drive params for the linear drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param PositionStrength		Positional strength for the drive (stiffness)
	*	@param VelocityStrength		Velocity strength of the drive (damping)
	*	@param InForceLimit			Max force applied by the drive
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetLinearDriveParams(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		float PositionStrength, 
		float VelocityStrength, 
		float InForceLimit
	);

	/** Gets the drive params for the linear drive.
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param OutPositionStrength	Positional strength for the drive (stiffness)
	*	@param OutVelocityStrength	Velocity strength of the drive (damping)
	*	@param OutForceLimit		Max force applied by the drive
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetLinearDriveParams(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		float& OutPositionStrength,
		float& OutVelocityStrength,
		float& OutForceLimit
	);

	//---------------------------------------------------------------------------------------------------
	//
	// ANGULAR MOTOR
	//
	//---------------------------------------------------------------------------------------------------

	/** Enables/Disables angular orientation drive. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bEnableSwingDrive	Indicates whether the drive for the swing axis should be enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param bEnableTwistDrive	Indicates whether the drive for the twist axis should be enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetOrientationDriveTwistAndSwing(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		bool bEnableTwistDrive, 
		bool bEnableSwingDrive
	);

	/** Gets whether angular orientation drive are enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param bOutEnableTwistDrive	Indicates whether the drive for the twist axis is enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param bOutEnableSwingDrive	Indicates whether the drive for the swing axis is enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetOrientationDriveTwistAndSwing(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bOutEnableTwistDrive,
		bool& bOutEnableSwingDrive
	);

	/** Enables/Disables the angular orientation slerp drive. Only relevant if the AngularDriveMode is set to SLERP
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bEnableSLERP			Indicates whether the SLERP drive should be enabled. Only relevant if the AngularDriveMode is set to SLERP
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetOrientationDriveSLERP(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		bool bEnableSLERP
	);

	/** Gets whether the angular orientation slerp drive is enabled or not. Only relevant if the AngularDriveMode is set to SLERP
	*	@param ConstraintInstance	Instance of the constraint to query
	*	@param bOutEnableSLERP		Indicates whether the SLERP drive should be enabled. Only relevant if the AngularDriveMode is set to SLERP
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetOrientationDriveSLERP(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bOutEnableSLERP
	);

	/** Enables/Disables angular velocity twist and swing drive. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bEnableTwistDrive	Indicates whether the drive for the twist axis should be enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param bEnableSwingDrive	Indicates whether the drive for the swing axis should be enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularVelocityDriveTwistAndSwing(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor, 
		bool bEnableTwistDrive, 
		bool bEnableSwingDrive
	);

	/** Gets whether angular velocity twist and swing drive is enabled or not. Only relevant if the AngularDriveMode is set to Twist and Swing
	*	@param ConstraintInstance	Instance of the constraint to query	
	*	@param bOutEnableTwistDrive	Indicates whether the drive for the twist axis should be enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*   @param bOutEnableSwingDrive	Indicates whether the drive for the swing axis should be enabled. Only relevant if the AngularDriveMode is set to Twist and Swing
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularVelocityDriveTwistAndSwing(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bOutEnableTwistDrive,
		bool& bOutEnableSwingDrive
	);

	/** Enables/Disables the angular velocity slerp drive. Only relevant if the AngularDriveMode is set to SLERP
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bEnableSLERP			Indicates whether the SLERP drive should be enabled. Only relevant if the AngularDriveMode is set to SLERP
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularVelocityDriveSLERP(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool bEnableSLERP
	);

	/** Gets whether the angular velocity slerp drive is enabled or not. Only relevant if the AngularDriveMode is set to SLERP
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param bOutEnableSLERP		Indicates whether the SLERP drive is enabled. Only relevant if the AngularDriveMode is set to SLERP
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularVelocityDriveSLERP(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		bool& bOutEnableSLERP
	);

	/** Switches the angular drive mode between SLERP and Twist And Swing
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param DriveMode	The angular drive mode to use. SLERP uses shortest spherical path, but will not work if any angular constraints are locked. Twist and Swing decomposes the path into the different angular degrees of freedom but may experience gimbal lock
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularDriveMode(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		EAngularDriveMode::Type DriveMode
	);

	/** Gets the angular drive mode ( SLERP or Twist And Swing)
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param OutDriveMode	The angular drive mode to use. SLERP uses shortest spherical path, but will not work if any angular constraints are locked. Twist and Swing decomposes the path into the different angular degrees of freedom but may experience gimbal lock
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularDriveMode(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		TEnumAsByte<EAngularDriveMode::Type>& OutDriveMode
	);

	/** Sets the target orientation for the angular drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param InPosTarget			Target orientation
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularOrientationTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		const FRotator& InPosTarget
	);

	/** Gets the target orientation for the angular drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param OutPosTarget			Target orientation
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularOrientationTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		FRotator& OutPosTarget
	);

	/** Sets the target velocity for the angular drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param InVelTarget			Target velocity
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularVelocityTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		const FVector& InVelTarget
	);

	/** Gets the target velocity for the angular drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param OutVelTarget			Target velocity
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularVelocityTarget(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		FVector& OutVelTarget
	);

	/** Sets the drive params for the angular drive.
	*	@param ConstraintInstance	Instance of the constraint to change
	*	@param PositionStrength		Positional strength for the drive (stiffness)
	*	@param VelocityStrength 	Velocity strength of the drive (damping)
	*	@param InForceLimit			Max force applied by the drive
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void SetAngularDriveParams(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		float PositionStrength, 
		float VelocityStrength, 
		float InForceLimit
	);

	/** Gets the drive params for the angular drive.
*	@param ConstraintInstance	Instance of the constraint to change
*	@param OutPositionStrength	Positional strength for the drive (stiffness)
*	@param OutVelocityStrength 	Velocity strength of the drive (damping)
*	@param OutForceLimit		Max force applied by the drive
*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Constraints")
	static void GetAngularDriveParams(
		UPARAM(ref) FConstraintInstanceAccessor& Accessor,
		float OutPositionStrength,
		float OutVelocityStrength,
		float OutForceLimit
	);
};