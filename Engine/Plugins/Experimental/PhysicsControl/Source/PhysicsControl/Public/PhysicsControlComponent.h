// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsControlData.h"
#include "PhysicsControlLimbData.h"

#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "EngineDefines.h"
#include "Templates/PimplPtr.h"

#include "PhysicsControlComponent.generated.h"

class FPrimitiveDrawInterface;
struct FPhysicsControlComponentImpl;
struct FPhysicsControlRecord;
struct FPhysicsBodyModifier;
struct FConstraintInstance;

/**
 * Specifies the type of control that is created when making controls from a skeleton or a set of limbs. 
 * Note that if controls are made individually then other options are available - i.e. in a character, 
 * any body part can be controlled relative to any other part, or indeed any other object.
 */
UENUM(BlueprintType)
enum class EPhysicsControlType : uint8
{
	/** Control is done in world space, so each object/part is driven independently */
	WorldSpace,
	/** Control is done in the space of the parent of each object */
	ParentSpace,
};

/**
 * This is the main Physics Control Component class which manages Controls and Body Modifiers associated 
 * with one or more static or skeletal meshes. You can add this as a component to an actor containing a 
 * mesh and then use it to create, configure and destroy Controls/Body Modifiers:
 * 
 * Controls are used to control one physics body relative to another (or the world). These controls are done
 * through physical spring/damper drives.
 * 
 * Body Modifiers are used to update the most important physical properties of physics bodies such as whether 
 * they are simulated vs kinematic, or whether they experience gravity.
 * 
 * Note that Controls and Body Modifiers are given names (which are predictable). These names can then be stored 
 * (perhaps in arrays) to make it easy to quickly change multiple Controls/Body Modifiers.
 */
UCLASS(meta = (BlueprintSpawnableComponent), ClassGroup = Physics, Experimental)
class PHYSICSCONTROL_API UPhysicsControlComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Makes a new control for mesh components
	 * 
	 * @param ControlData   Describes the initial strength etc of the new control
	 * 
	 * @param ControlTarget Describes the initial target for the new control
	 * 
	 * @param ControlSettings General settings for the control
	 * 
	 * @param bEnabled      If true then the control will be enabled immediately. If false you will need to call
	 *                      SetControlEnabled(true) in order to activate it.
	 *
	 * @return The name of the new control
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	FName MakeControl(
		UMeshComponent*         ParentMeshComponent,
		const FName             ParentBoneName,
		UMeshComponent*         ChildMeshComponent,
		const FName             ChildBoneName,
		FPhysicsControlData     ControlData,
		FPhysicsControlTarget   ControlTarget, 
		FPhysicsControlSettings ControlSettings,
		bool                    bEnabled = true);

	/**
	 * Makes a new control for mesh components
	 * 
	 * @param ControlData   Describes the initial strength etc of the new control
	 * 
	 * @param ControlTarget Describes the initial target for the new control
	 * 
	 * @param ControlSettings General settings for the control
	 * 
	 * @param bEnabled      If true then the control will be enabled immediately. If false you will need to call
	 *                      SetControlEnabled(true) in order to enable it.
	 * 
	 * @return True if a new control was created, false if a control of the specified name already exists
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool MakeNamedControl(
		const FName             Name,
		UMeshComponent*         ParentMeshComponent,
		const FName             ParentBoneName,
		UMeshComponent*         ChildMeshComponent,
		const FName             ChildBoneName,
		FPhysicsControlData     ControlData, 
		FPhysicsControlTarget   ControlTarget, 
		FPhysicsControlSettings ControlSettings, 
		bool                    bEnabled = true);

	/**
	 * Makes a collection of controls controlling a skeletal mesh
	 * 
	 * @param SkeletalMeshComponent The skeletal mesh which will have controls
	 * 
	 * @param BoneName The name of the bone below which controls should be created. Each bone will be the child in a control
	 * 
	 * @param bIncludeSelf Whether or not to include BoneName when creating controls
	 * 
	 * @param ControlType What type of control to create. This determines what the parent will be for each control
	 *
	 * @param ControlData   Describes the initial strength etc of the new control
	 * 
	 * @param ControlSettings General settings for the control
	 *
	 * @param bEnabled      If true then the control will be enabled immediately. If false you will need to call
	 *                      SetControlEnabled(true) in order to enable it.
	 *
	 * @return An array of the controls that have been created
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	TArray<FName> MakeControlsFromSkeletalMeshBelow(
		USkeletalMeshComponent* SkeletalMeshComponent,
		FName                   BoneName,
		bool                    bIncludeSelf,
		EPhysicsControlType     ControlType,
		FPhysicsControlData     ControlData,
		FPhysicsControlSettings ControlSettings,
		bool                    bEnabled = true);

	/**
	 * Makes a collection of controls controlling a skeletal mesh
	 *
	 * @param SkeletalMeshComponent The skeletal mesh which will have controls
	 *
	 * @param BoneNames The names of bones for which controls should be created. Each bone will be the child in a control
	 *
	 * @param ControlType What type of control to create. This determines what the parent will be for each control
	 *
	 * @param ControlData   Describes the initial strength etc of the new control
	 *
	 * @param ControlSettings General settings for the control
	 *
	 * @param bEnabled      If true then the control will be enabled immediately. If false you will need to call
	 *                      SetControlEnabled(true) in order to enable it.
	 *
	 * @return An array of the controls that have been created
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	TArray<FName> MakeControlsFromSkeletalMesh(
		USkeletalMeshComponent* SkeletalMeshComponent,
		const TArray<FName>&    BoneNames,
		EPhysicsControlType     ControlType,
		FPhysicsControlData     ControlData,
		FPhysicsControlSettings ControlSettings,
		bool                    bEnabled = true);

	/**
	 * Calculates which bones belong to which limb in a skeletal mesh
	 * 
	 * @param SkeletalMeshComponent The skeletal mesh which will be analyzed
	 * 
	 * @param LimbSetupData This needs to be filled in with the list of limbs to "discover". Note that the 
	 *                      limbs should be listed starting at the "leaf" (i.e. outer) parts of the skeleton 
	 *                      first, typically finishing with the spine. In addition, the spine limb is typically 
	 *                      specified using the first spine bone, but flagging it to include its parent 
	 *                      (normally the pelvis). 
	 * 
	 * @return A map of limb names to bones
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	TMap<FName, FPhysicsControlLimbBones> GetLimbBonesFromSkeletalMesh(
		USkeletalMeshComponent* SkeletalMeshComponent,
		const TArray<FPhysicsControlLimbSetupData>& LimbSetupData) const;

	/**
	 * Makes a collection of controls controlling a skeletal mesh, grouped together in limbs
	 *
	 * @param AllControls A single container for all the controls that have been created
	 *
	 * @param LimbBones A map relating the limbs and the bones that they contain. Typically create this 
	 *                  using GetLimbBonesFromSkeletalMesh
	 *
	 * @param ControlType What type of control to create. This determines what the parent will be for each control
	 *
	 * @param ControlData Describes the initial strength etc of the new control
	 *
	 * @param ControlSettings General settings for the control
	 *
	 * @param bEnabled If true then the control will be enabled immediately. If false you will need to call
	 *                 SetControlEnabled(true) in order to enable it.
	 *
	 * @return A map containing the controls for each limb
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	TMap<FName, FPhysicsControlNameArray> MakeControlsFromLimbBones(
		FPhysicsControlNameArray&                    AllControls,
		const TMap<FName, FPhysicsControlLimbBones>& LimbBones,
		EPhysicsControlType                          ControlType,
		FPhysicsControlData                          ControlData,
		FPhysicsControlSettings                      ControlSettings,
		bool                                         bEnabled = true);

	/**
	 * Destroys a control
	 *
	 * @param Name The name of the control to destroy. If blank, then this will destroy the first
	 *             control, whatever its name.
	 *
	 * @return     Returns true if the control was found and destroyed, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool DestroyControl(const FName Name);

	/**
	 * Modifies an existing control data - i.e. the strengths etc of the control driving towards the target
	 *
	 * @param Name The name of the control to modify. If blank, then this will access the first 
	 *             control, whatever its name.
	 * 
	 * @param ControlData The new control data
	 * 
	 * @param bEnableControl Enables/disables the control
	 * 
	 * @return Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlData(const FName Name, FPhysicsControlData ControlData, bool bEnableControl = true);

	/**
	 * Modifies an existing control data using the multipliers
	 *
	 * @param Name The name of the control to modify. If blank, then this will access the first
	 *             control, whatever its name.
	 *
	 * @param ControlMultipliers The new control multipliers
	 *
	 * @param bEnableControl Enables/disables the control
	 *
	 * @return Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlMultipliers(const FName Name, FPhysicsControlMultipliers ControlMultipliers, bool bEnableControl = true);

	/**
	 * Modifies an existing control's linear data - i.e. the strengths etc of the control driving towards the target
	 *
	 * @param Name The name of the control to modify. If blank, then this will access the first
	 *             control, whatever its name.
	 *
	 * @param Strength The strength used to drive linear motion
	 *
	 * @param DampingRatio The amount of damping associated with the linear strength. 1 Results in critically damped motion
	 *
	 * @param ExtraDamping The amount of additional linear damping
	 *
	 * @param MaxForce The maximum force used to drive the linear motion. Zero indicates no limit.
	 *
	 * @param bEnableControl Enables/disables the control
	 * 
	 * @return Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlLinearData(
		const FName Name,
		float Strength = 1.0f, 
		float DampingRatio = 1.0f, 
		float ExtraDamping = 0.0f, 
		float MaxForce = 0.0f, 
		bool  bEnableControl = true);

	/**
	 * Modifies an existing control's angular data - i.e. the strengths etc of the control driving towards the target
	 *
	 * @param Name The name of the control to modify. If blank, then this will access the first
	 *             control, whatever its name.
	 *
	 * @param Strength The strength used to drive angular motion
	 *
	 * @param DampingRatio The amount of damping associated with the angular strength. 1 Results in critically damped motion
	 *
	 * @param ExtraDamping The amount of additional angular damping
	 *
	 * @param MaxTorque The maximum torque used to drive the angular motion. Zero indicates no limit.
	 * 
	 * @param bEnableControl Enables/disables the control
	 *
	 * @return Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlAngularData(
		const FName Name,
		float Strength = 1.0f, 
		float DampingRatio = 1.0f, 
		float ExtraDamping = 0.0f, 
		float MaxTorque = 0.0f, 
		bool  bEnableControl = true);

	/**
	 * Sets the point at which controls will "push" the child object.
	 * 
	 * @param Name The name of the control to modify. If blank, then this will access the first
	 *             control, whatever its name.
	 * 
	 * @param Position The position of the control point on the child mesh object (only relevant if that 
	 *                 object is in use and is being simulated)
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlPoint(const FName Name, const FVector Position);

	/**
	 * Resets the control point to the center of mass of the mesh
	 *
	 * @param Name    The name of the control to modify. If blank, then this will access the first
	 *                control, whatever its name.
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool ResetControlPoint(const FName Name);

	/**
	 * Modifies an existing control target - i.e. what it is driving towards, relative to the parent object
	 *
	 * @param Name The name of the control to modify. If blank, then this will access the first 
	 *             control, whatever its name.
	 * 
	 * @param ControlTarget The new target for the control
	 * 
	 * @param bEnableControl Enables/disables the control
	 * 
	 * @return Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlTarget(const FName Name, FPhysicsControlTarget ControlTarget, bool bEnableControl = true);

	/**
	 * Modifies an existing control target - i.e. what it is driving towards, relative to the parent object
	 *
	 * @param Name The name of the control to modify. If blank, then this will access the first
	 *             control, whatever its name.
	 *
	 * @param Transform The new transform target for the control
	 * 
	 * @param VelocityDeltaTime If non-zero, the target velocity will be calculated using the current target 
	 *                          position. If zero, the target velocity will be set to zero.    
	 *
	 * @param bEnableControl Enables/disables the control
	 * 
	 * @param bApplyControlPointToTarget If true, then the target position/orientation is treated as
	 *                                   a "virtual" object, where the system attempts to move the object
	 *                                   to match the pose of this "virtual" object that has been placed at
	 *                                   the target transform. Use this when you want to specify the target
	 *                                   transform for the object as a whole. If false, then the target transform
	 *                                   is used as is, and the system drives the control point towards this
	 *                                   transform.
	 *
	 * @return                  Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlTargetTransform(
		const FName      Name,
		const FTransform Transform, 
		float            VelocityDeltaTime, 
		bool             bEnableControl = true,
		bool             bApplyControlPointToTarget = true);

	/**
	 * Modifies an existing control target - i.e. what it is driving towards, relative to the parent object
	 *
	 * @param Name              The name of the control to modify. If blank, then this will access the first
	 *                          control, whatever its name.
	 *
	 * @param Position          The new position target for the control
	 * 
	 * @param VelocityDeltaTime If non-zero, the target velocity will be calculated using the current target 
	 *                          position. If zero, the target velocity will be set to zero.    
	 *
	 * @param bEnableControl    Enables/disables the control
	 * 
	 * @param bApplyControlPointToTarget If true, then the target position/orientation is treated as
	 *                                   a "virtual" object, where the system attempts to move the object
	 *                                   to match the pose of this "virtual" object that has been placed at
	 *                                   the target transform. Use this when you want to specify the target
	 *                                   transform for the object as a whole. If false, then the target transform
	 *                                   is used as is, and the system drives the control point towards this
	 *                                   transform.
	 *
	 * @return                  Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlTargetPosition(
		const FName Name,
		const FVector  Position, 
		float          VelocityDeltaTime, 
		bool           bEnableControl = true,
		bool           bApplyControlPointToTarget = true);

	/**
	 * Modifies an existing control target - i.e. what it is driving towards, relative to the parent object
	 *
	 * @param Name                      The name of the control to modify. If blank, then this will access the first
	 *                                  control, whatever its name.
	 *							        
	 * @param Orientation               The new orientation target for the control
	 *
	 * @param AngularVelocityDeltaTime  If non-zero, the target angular velocity will be calculated using the current 
	 *                                  target position. If zero, the target velocity will be set to zero.
	 * 
	 * @param bEnableControl Enables/disables the control
	 *
	 * @param bApplyControlPointToTarget If true, then the target position/orientation is treated as
	 *                                   a "virtual" object, where the system attempts to move the object
	 *                                   to match the pose of this "virtual" object that has been placed at
	 *                                   the target transform. Use this when you want to specify the target
	 *                                   transform for the object as a whole. If false, then the target transform
	 *                                   is used as is, and the system drives the control point towards this
	 *                                   transform.
	 * 
	 * @return                          Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlTargetOrientation(
		const FName Name,
		const FRotator Orientation, 
		float          AngularVelocityDeltaTime, 
		bool           bEnableControl = true,
		bool           bApplyControlPointToTarget = true);

	/**
	 * Calculates and sets an existing control target. This takes the "virtual" position/orientation of the parent 
	 * and child and calculates the relative control. Note that this will set bApplyControlPointToTarget to true.
	 * 
	 * @param Name              The name of the control to modify. If blank, then this will access the first
	 *                          control, whatever its name.
	 *
	 * @param ParentPosition    The virtual/target parent position
	 * 
	 * @param ParentOrientation The virtual/target parent orientation
	 *
	 * @param ChildPosition     The virtual/target child position
	 *
	 * @param ChildOrientation  The virtual/target child orientation
	 *
	 * @param VelocityDeltaTime If non-zero, the target velocity will be calculated using the current target
	 *                          position. If zero, the target velocity will be set to zero.    
	 *
	 * @param bEnableControl Enables/disables the control
	 * 
	 * @return                  Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlTargetPoses(
		const FName Name,
		const FVector ParentPosition, const FRotator ParentOrientation,
		const FVector ChildPosition, const FRotator ChildOrientation,
		float VelocityDeltaTime, bool bEnableControl = true);


	/**
	 * Sets whether or not the control should use skeletal animation for the targets
	 *
	 * @param Name              The name of the control to modify. If blank, then this will access the first
	 *                          control, whatever its name.
	 * 
	 * @param bUseSkeletalAnimation If true then the targets will be a combination of the skeletal animation (if
	 *                              there is any) and the control target that has been set
	 * 
	 * @param SkeletalAnimationVelocityMultiplier If skeletal animation is being used, then this determines the amount of 
	 *                              velocity extracted from the animation that is used as targets for the controls
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlUseSkeletalAnimation(
		const FName Name,
		bool        bUseSkeletalAnimation = true,
		float       SkeletalAnimationVelocityMultiplier = 1.0f);

	/**
	 * Activates or deactivates a control
	 *
	 * @param Name     The name of the control to modify. If blank, then this will access the first
	 *                 control, whatever its name.
	 *
	 * @param bEnable  The control to enable/disable
	 *
	 * @return         Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlEnabled(const FName Name, bool bEnable);

	/**
	 * @param Name The name of the control to modify. If blank, then this will access the first 
	 *             control, whatever its name.
	 * 
	 * @param bAutoDisable If set then the control will automatically deactivate after each tick.
	 * 
	 * @return     Returns true if the control was found and modified, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetControlAutoDisable(const FName Name, bool bAutoDisable);

	/**
	 * @param Name     The name of the control to access. If blank, then this will access the first 
	 *                 control, whatever its name.
	 *
	 * @param Control  The control that will be filled in, if found
	 *
	 * @return         Returns true if the control was found, false if not
	 */
	//UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool GetControl(const FName Name, FPhysicsControl& Control) const;

	/**
	 * @param Name     The name of the control to access. If blank, then this will access the first
	 *                 control, whatever its name.
	 *
	 * @param Control  The control data that will be filled in if found
	 *
	 * @return         Returns true if the control was found, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool GetControlData(const FName Name, FPhysicsControlData& ControlData) const;

	/**
	 * @param Name     The name of the control to access. If blank, then this will access the first
	 *                 control, whatever its name.
	 *
	 * @param Control  The control multipliers that will be filled in if found
	 *
	 * @return         Returns true if the control was found, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool GetControlMultipliers(const FName Name, FPhysicsControlMultipliers& ControlMultipliers) const;

	/**
	 * @param Name     The name of the control to access. If blank, then this will access the first
	 *                 control, whatever its name.
	 *
	 * @param Control  The control target, if found
	 *
	 * @return         Returns true if the control was found, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool GetControlTarget(const FName Name, FPhysicsControlTarget& ControlTarget) const;

	/**
	 * @param Name        The name of the control to access. If blank, then this will access the first
	 *                    control, whatever its name.
	 *
	 * @return            Returns true if the control is marked to automatically disable after each tick
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool GetControlAutoDisable(const FName Name) const;

	/**
	 * @param Name        The name of the control to access. If blank, then this will access the first
	 *                    control, whatever its name.
	 *
	 * @return            Returns true if the control is enabled
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool GetControlEnabled(const FName Name) const;

	/**
	 * Makes a new body modifier for mesh components
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	FName MakeBodyModifier(
		UMeshComponent*       MeshComponent,
		const FName           BoneName,
		EPhysicsMovementType  MovementType = EPhysicsMovementType::Simulated, 
		float                 GravityMultiplier = 1.0f);

	/**
	 * Makes a new body modifier for mesh components
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool MakeNamedBodyModifier(
		const FName           Name,
		UMeshComponent*       MeshComponent,
		const FName           BoneName,
		EPhysicsMovementType  MovementType = EPhysicsMovementType::Simulated,
		float                 GravityMultiplier = 1.0f);

	/**
	 * Makes new body modifiers for skeletal mesh components
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	TArray<FName> MakeBodyModifiersFromSkeletalMeshBelow(
		USkeletalMeshComponent* SkeletalMeshComponent,
		FName                   BoneName,
		bool                    bIncludeSelf,
		EPhysicsMovementType    MovementType = EPhysicsMovementType::Simulated,
		float                   GravityMultiplier = 1.0f);


	/**
	 * Makes a collection of controls controlling a skeletal mesh, grouped together in limbs
	 *
	 * @param AllControls A single container for all the controls that have been created
	 *
	 * @param LimbBones A map relating the limbs and the bones that they contain. Typically create this 
	 *                  using GetLimbBonesFromSkeletalMesh
	 *
	 * @param ControlType What type of control to create. This determines what the parent will be for each control
	 *
	 * @param ControlData   Describes the initial strength etc of the new control
	 *
	 * @param ControlSettings General settings for the control
	 *
	 * @param bEnabled      If true then the control will be enabled immediately. If false you will need to call
	 *                      SetControlEnabled(true) in order to enable it.
	 *
	 * @return A map containing the controls for each limb
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	TMap<FName, FPhysicsControlNameArray> MakeBodyModifiersFromLimbBones(
		FPhysicsControlNameArray&                    AllBodyModifiers,
		const TMap<FName, FPhysicsControlLimbBones>& LimbBones,
		EPhysicsMovementType                         MovementType = EPhysicsMovementType::Simulated,
		float                                        GravityMultiplier = 1.0f);

	/**
	 * Destroys a BodyModifier
	 *
	 * @param Name        The name of the body modifier to destroy. If blank, then this will destroy the first
	 *                    body modifier, whatever its name.
	 *
	 * @return            Returns true if the body modifier was found and destroyed, false if not
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool DestroyBodyModifier(const FName Name);

	/**
	 * @param Name        The name of the body modifier to access. If blank, then this will access the first
	 *                    modifier, whatever its name.
	 *
	 * @param bSimulate   Whether to enable/disable simulation on the body
	 * 
	 * @param GravityMultiplier The amount of gravity to apply when simulating
	 * 
	 * @return            Returns true if the body modifier was found and modified
	 */
	UFUNCTION(BlueprintCallable, Category = PhysicsControl)
	bool SetBodyModifier(
		const FName          Name,
		EPhysicsMovementType MovementType = EPhysicsMovementType::Simulated, 
		float                GravityMultiplier = 1.0f);

public:

	/**
	 * If the component moves by more than this distance then it is treated as a teleport,
	 * which prevents velocities being used for a frame. Zero or negative disables.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	float TeleportDistanceThreshold;

	/**
	 * If the component rotates by more than this angle (in degrees) then it is treated as a teleport,
	 * which prevents velocities being used for a frame. Zero or negative disables.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	float TeleportRotationThreshold;

	/** Visualize the controls when this actor/component is selected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debug)
	bool bShowDebugVisualization;

	/** Size of the gizmos etc used during visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debug)
	float VisualizationSizeScale;

	/**
	 * The time used when "predicting" the target position/orientation. Zero will disable the visualization
	 * of this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debug)
	float VelocityPredictionTime;

	/**
	 * Upper limit on the number of controls or modifiers that will be created using the same name (which
	 * will get a numerical postfix). When this limit is reached a warning will be issued  and the control 
	 * or modifier won't be created. This is to avoid problems if controls or modifiers are being created 
	 * dynamically, and can generally be a "moderately large" number, depending on how many controls or 
	 * modifiers you expect to create.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int32 MaxNumControlsOrModifiersPerName;

protected:

	// Begin UActorComponent Interface
	virtual void InitializeComponent() override;
	virtual void BeginDestroy() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// End UActorComponent Interface

public:
#if WITH_EDITOR
	// Used by the component visualizer
	void DebugDraw(FPrimitiveDrawInterface* PDI) const;
	void DebugDrawControl(FPrimitiveDrawInterface* PDI, const FPhysicsControlRecord& Record, FName ControlName) const;
#endif

protected:

	TPimplPtr<FPhysicsControlComponentImpl> Implementation;
};
