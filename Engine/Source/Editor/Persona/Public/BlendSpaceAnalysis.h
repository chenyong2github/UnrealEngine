// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/BoneSocketReference.h"
#include "Features/IModularFeature.h"
#include "Animation/BlendSpace.h"

#include "BlendSpaceAnalysis.generated.h"

class UAnimSequence;
class UBlendSpace;
class UAnalysisProperties;

/**
* Users wishing to add their own analysis functions and structures should inherit from this, implement the virtual
* functions, and register an instance with IModularFeatures. It may help to look at the implementation of 
* FCoreBlendSpaceAnalysisFeature when doing this.
*/
class IBlendSpaceAnalysisFeature : public IModularFeature
{
public:
	static FName GetModuleFeatureName() { return "BlendSpaceAnalysis"; }

	// This should process the animation according to the analysis properties, or return false if that is not possible.
	virtual bool CalculateSampleValue(float&                     Result,
									  const UBlendSpace&         BlendSpace,
									  const UAnalysisProperties* AnalysisProperties,
									  const UAnimSequence&       Animation,
									  const float                RateScale) const = 0;

	// This should return an instance derived from UAnalysisProperties that is suitable for the Function. The caller
	// will pass in a suitable owning object, outer, that the implementation should assign as owner of the newly created
	// object. 
	virtual UAnalysisProperties* MakeAnalysisProperties(UObject* Outer, const FString& FunctionName) const = 0;

	// This should return the names of the functions handled
	virtual TArray<FString> GetAnalysisFunctions() const = 0;
};

UENUM()
enum class EAnalysisSpace : uint8
{
	World    UMETA(DisplayName = "World"),
	Fixed    UMETA(DisplayName = "Fixed"),
	Changing UMETA(DisplayName = "Changing"),
	Moving   UMETA(DisplayName = "Moving"),
};

UENUM()
enum class EAnalysisLinearAxis : uint8
{
	X  UMETA(DisplayName = "X"),
	Y  UMETA(DisplayName = "Y"),
	Z  UMETA(DisplayName = "Z"),
};

UENUM()
enum class EAnalysisEulerAxis : uint8
{
	Roll   UMETA(DisplayName = "Roll"),
	Pitch  UMETA(DisplayName = "Pitch"),
	Yaw    UMETA(DisplayName = "Yaw"),
};

/**
 * This will be used to preserve values as far as possible when switching between analysis functions, so it contains all
 * the parameters used by the engine functions. User defined can inherit from this and add their own - then the
 * user-defined MakeCache function should replace any base class cache that is passed in with their own.
*/
class FCachedAnalysisProperties
{
public:
	EAnalysisLinearAxis  LinearFunctionAxis = EAnalysisLinearAxis::X;
	EAnalysisEulerAxis   EulerFunctionAxis = EAnalysisEulerAxis::Pitch;
	EAnalysisSpace       Space = EAnalysisSpace::World;
	FBoneSocketTarget    SpaceBoneSocket;
	EAnalysisLinearAxis  CharacterFacingAxis = EAnalysisLinearAxis::Y;
	EAnalysisLinearAxis  CharacterUpAxis = EAnalysisLinearAxis::Z;
	float                StartTimeFraction = 0.0f;
	float                EndTimeFraction = 1.0f;
	FBoneSocketTarget    BoneSocket1;
	FBoneSocketTarget    BoneSocket2;
	EAnalysisLinearAxis  BoneFacingAxis = EAnalysisLinearAxis::X;
	EAnalysisLinearAxis  BoneRightAxis = EAnalysisLinearAxis::Y;
	bool                 bLockAfterAnalysis = false;
};

UCLASS()
class ULinearAnalysisProperties : public UAnalysisProperties
{
	GENERATED_BODY()
public:
	void InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache) override;
	void MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const override;

	/** Axis for the analysis function */
	UPROPERTY(EditAnywhere, DisplayName = "Axis", Category = AnalysisProperties)
	EAnalysisLinearAxis FunctionAxis = EAnalysisLinearAxis::X;

	/**
	* The space in which to perform the analysis. Fixed will use the analysis bone/socket at the first frame
	* of the analysis time range. Changing will use the analysis bone/socket at the relevant frame during the
	* analysis, but calculate velocities assuming that frame isn't moving. Moving will do the same but velocities
	* as well as positions/rotations will be relative to this moving frame.
	*/
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisSpace Space = EAnalysisSpace::World;

	/** Bone or socket that defines the analysis space (when it isn't World) */
	UPROPERTY(EditAnywhere, DisplayName = "Analysis Space Bone/Socket", Category = AnalysisProperties, meta = (EditCondition = "Space != EAnalysisSpace::World"))
	FBoneSocketTarget SpaceBoneSocket;

	/** The bone or socket used for analysis */
	UPROPERTY(EditAnywhere, DisplayName = "Bone/Socket", Category = AnalysisProperties)
	FBoneSocketTarget BoneSocket;

	/** Fraction through each animation at which analysis starts */
	UPROPERTY(EditAnywhere, DisplayName = "Start time fraction", Category = AnalysisProperties, meta = (ClampMin = "0", ClampMax = "1"))
	float StartTimeFraction = 0.0f;

	/** Fraction through each animation at which analysis ends */
	UPROPERTY(EditAnywhere, DisplayName = "End time fraction", Category = AnalysisProperties, meta = (ClampMin = "0", ClampMax = "1"))
	float EndTimeFraction = 1.0f;
};

UCLASS()
class UEulerAnalysisProperties : public UAnalysisProperties
{
	GENERATED_BODY()
public:
	void InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache) override;
	void MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const override;

	/** Axis for the analysis function */
	UPROPERTY(EditAnywhere, DisplayName = "Axis", Category = AnalysisProperties)
	EAnalysisEulerAxis FunctionAxis = EAnalysisEulerAxis::Pitch;

	/**
	* The space in which to perform the analysis. Fixed will use the analysis bone/socket at the first frame
	* of the analysis time range. Changing will use the analysis bone/socket at the relevant frame during the
	* analysis, but calculate velocities assuming that frame isn't moving. Moving will do the same but velocities
	* as well as positions/rotations will be relative to this moving frame.
	*/
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisSpace Space = EAnalysisSpace::World;

	/** Bone or socket that defines the analysis space (when it isn't World) */
	UPROPERTY(EditAnywhere, DisplayName = "Analysis Space Bone/Socket", Category = AnalysisProperties, meta = (EditCondition = "Space != 0"))
	FBoneSocketTarget SpaceBoneSocket;

	/** World or bone/socket axis that specifies the character's facing direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis CharacterFacingAxis = EAnalysisLinearAxis::Y;

	/** World or bone/socket axis that specifies the character's up direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis CharacterUpAxis = EAnalysisLinearAxis::Z;

	/** Fraction through each animation at which analysis starts */
	UPROPERTY(EditAnywhere, DisplayName = "Start time fraction", Category = AnalysisProperties, meta = (ClampMin = "0", ClampMax = "1"))
	float StartTimeFraction = 0.0f;
	/** Fraction through each animation at which analysis ends */

	UPROPERTY(EditAnywhere, DisplayName = "End time fraction", Category = AnalysisProperties, meta = (ClampMin = "0", ClampMax = "1"))
	float EndTimeFraction = 1.0f;

	/** The bone or socket used for analysis */
	UPROPERTY(EditAnywhere, DisplayName = "Bone/Socket", Category = AnalysisProperties)
	FBoneSocketTarget BoneSocket;

	/** Used for some analysis functions - specifies the bone/socket axis that points in the facing/forwards direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis BoneFacingAxis = EAnalysisLinearAxis::X;

	/** Used for some analysis functions - specifies the bone/socket axis that points to the "right" */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis BoneRightAxis = EAnalysisLinearAxis::Y;
};

UCLASS()
class UMovementAnalysisProperties : public UAnalysisProperties
{
	GENERATED_BODY()
public:
	void InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache) override;
	void MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const override;

	/**
	* The space in which to perform the analysis. Fixed will use the analysis bone/socket at the first frame
	* of the analysis time range. Changing will use the analysis bone/socket at the relevant frame during the
	* analysis, but calculate velocities assuming that frame isn't moving. Moving will do the same but velocities
	* as well as positions/rotations will be relative to this moving frame.
	*/
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisSpace Space = EAnalysisSpace::World;

	/** Bone or socket that defines the analysis space (when it isn't World) */
	UPROPERTY(EditAnywhere, DisplayName = "Analysis Space Bone/Socket", Category = AnalysisProperties, meta = (EditCondition = "Space != EAnalysisSpace::World"))
	FBoneSocketTarget SpaceBoneSocket;

	/** World or bone/socket axis that specifies the character's facing direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis CharacterFacingAxis = EAnalysisLinearAxis::Y;

	/** World or bone/socket axis that specifies the character's up direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis CharacterUpAxis = EAnalysisLinearAxis::Z;

	/** Fraction through each animation at which analysis starts */
	UPROPERTY(EditAnywhere, DisplayName = "Start time fraction", Category = AnalysisProperties, meta = (ClampMin = "0", ClampMax = "1"))
	float StartTimeFraction = 0.0f;

	/** Fraction through each animation at which analysis ends */
	UPROPERTY(EditAnywhere, DisplayName = "End time fraction", Category = AnalysisProperties, meta = (ClampMin = "0", ClampMax = "1"))
	float EndTimeFraction = 1.0f;

	/** The bone or socket used for analysis */
	UPROPERTY(EditAnywhere, DisplayName = "Bone/Socket", Category = AnalysisProperties)
	FBoneSocketTarget BoneSocket;
};

UCLASS()
class ULocomotionAnalysisProperties : public UAnalysisProperties
{
	GENERATED_BODY()
public:
	void InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache) override;
	void MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const override;

	/** World or bone/socket axis that specifies the character's facing direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis CharacterFacingAxis = EAnalysisLinearAxis::Y;

	/** World or bone/socket axis that specifies the character's up direction */
	UPROPERTY(EditAnywhere, Category = AnalysisProperties)
	EAnalysisLinearAxis CharacterUpAxis = EAnalysisLinearAxis::Z;

	/** The primary bone or socket used for analysis */
	UPROPERTY(EditAnywhere, DisplayName = "Bone/Socket 1", Category = AnalysisProperties)
	FBoneSocketTarget PrimaryBoneSocket;

	/** The secondary bone or socket used for analysis */
	UPROPERTY(EditAnywhere, DisplayName = "Bone/Socket 2", Category = AnalysisProperties)
	FBoneSocketTarget SecondaryBoneSocket;
};

