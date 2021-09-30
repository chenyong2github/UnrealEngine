// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"

#include "ZoneGraphTypes.h"
#include "MassMovementTypes.h"
#include "MassAIMovementTypes.generated.h"

MASSAIMOVEMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogMassNavigation, Warning, All);
MASSAIMOVEMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogMassDynamicObstacle, Warning, All);

//@ todo remove optimization hack once we find a better way to filter out signals from LOD/listener on signals, as for now we only need this signal for look at in high and med LOD
#define HACK_DISABLE_PATH_CHANGED_ON_LOWER_LOD 1

namespace UE::Mass::Signals
{
	const FName FollowPointPathStart = FName(TEXT("FollowPointPathStart"));
	const FName FollowPointPathDone = FName(TEXT("FollowPointPathDone"));
	const FName CurrentLaneChanged = FName(TEXT("CurrentLaneChanged"));
}

UENUM()
enum class EMassMovementAction : uint8
{
	Stand,		// Stop and stand.
	Move,		// Move or keep on moving.
	Animate,	// Animation has control over the transform
};

/** Actions for lane dynamic obstacles. */
UENUM()
enum class EMassLaneObstacleEventAction : uint8
{
	Add,	// Add lane obstacle
	Remove	// Remove lane obstacle
};

/** Reference to movement style in MassMovementSettings. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementStyleRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Movement")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (IgnoreForMemberInitializationTest, EditCondition = "false", EditConditionHides))
	FGuid ID;
};

/** Reference to movement config in MassMovementSettings. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementConfigRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Movement")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (IgnoreForMemberInitializationTest, EditCondition = "false", EditConditionHides))
	FGuid ID;
};


/** Describes movement style name. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Movement")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (IgnoreForMemberInitializationTest, EditCondition = "false", EditConditionHides))
	FGuid ID;
};

/**
 * Movement style consists of multiple speeds which are assigned to agents based on agents unique ID.
 * Same speed is assigned consistently for the same ID.
 */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementStyleSpeed
{
	GENERATED_BODY()

	/** Desired speed */
	UPROPERTY(EditAnywhere, Category = "Movement Style", meta = (UIMin = 0.0, ClampMin = 0.0))
	float Speed = 140.0f;

	/** How much default desired speed is varied randomly. */
	UPROPERTY(EditAnywhere, Category = "Movement Style", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float Variance = 0.1f;

	/** Probability to assign this speed. */
	UPROPERTY(EditAnywhere, Category = "Movement Style", meta = (UIMin = 1, ClampMin = 1, UIMax = 100, ClampMax = 100))
	int32 Probability = 1;

	/** Running sum of the probabilities so far, used to faster lookup. Update via FMassMovementConfig::Update(). */
	int32 ProbabilityThreshold = 0;
};

/** Movement style config. A movement style abstracts movement properties for specific style. Behaviors can refer to specific styles when handling speeds. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementStyleConfig
{
	GENERATED_BODY()

	/** Style of the movement */
	UPROPERTY(EditAnywhere, Category = "Movement Style")
	FMassMovementStyleRef Style;

	/** Array of desired speeds (cm/s) assigned to agents based on probability. */
	UPROPERTY(EditAnywhere, Category = "Movement Style")
	TArray<FMassMovementStyleSpeed> DesiredSpeeds;

	/** Sum of all probabilities in style speeds array. Update via FMassMovementConfig::Update() */
	int32 MaxProbability = 0;
};

/** Steering related movement configs. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementSteeringConfig
{
	GENERATED_BODY()

	/** Steering reaction time in seconds. */
	UPROPERTY(config, EditAnywhere, Category = "Steering", meta = (ClampMin = "0.05"))
	float ReactionTime = 0.3f;

	/** 200..600 Smaller steering maximum acceleration makes the steering more \"calm\" but less opportunistic, may not find solution, or gets stuck. */
	UPROPERTY(config, EditAnywhere, Category = "Steering", meta = (UIMin = 0.0, ClampMin = 0.0))
	float MaxAcceleration = 250.f;

	/** How much ahead to predict when steering. */
	UPROPERTY(EditAnywhere, Category="Steering", meta = (UIMin = 0.0, ClampMin = 0.0))
	float LookAheadDistance = 100.0f;
};
	
USTRUCT()
struct MASSAIMOVEMENT_API FMassMovementConfig
{
	GENERATED_BODY()

	/** Updates internal values for faster desired speed generation. */
	void Update();

	/** Generates desired speed based on style and unique id. The id is used deterministically assign a specific speed range. */
	float GenerateDesiredSpeed(const FMassMovementStyleRef& Style, const int32 UniqueId) const
	{
		float DesiredSpeed = DefaultDesiredSpeed;
		float DesiredSpeedVariance = DefaultDesiredSpeedVariance;
		
		const FMassMovementStyleConfig* StyleConfig = MovementStyles.FindByPredicate([&Style](const FMassMovementStyleConfig& Config)
			{
				return Config.Style.ID == Style.ID;
			});
		
		if (StyleConfig != nullptr && StyleConfig->MaxProbability > 0)
		{
			const int32 Prob = UniqueId % StyleConfig->MaxProbability;
			for (const FMassMovementStyleSpeed& Speed : StyleConfig->DesiredSpeeds)
			{
				if (Prob < Speed.ProbabilityThreshold)
				{
					DesiredSpeed = Speed.Speed;
					DesiredSpeedVariance = Speed.Variance;
					break;
				}
			}
		}
		
		return DesiredSpeed * FMath::RandRange(1.0f - DesiredSpeedVariance, 1.0f + DesiredSpeedVariance);
	}

	/** Name of the movement configuration. */
	UPROPERTY(EditAnywhere, Category = "Movement")
	FName Name;

	/** GUID representing this configuration. */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (IgnoreForMemberInitializationTest, EditCondition = "false", EditConditionHides))
	FGuid ID;

	/** Maximum speed (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0"))
	float MaximumSpeed = 200.f;

	/** Default desired speed (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0"))
	float DefaultDesiredSpeed = 140.f;

	/** How much default desired speed is varied randomly. */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float DefaultDesiredSpeedVariance = 0.1f;

	/** Steering config. */
	UPROPERTY(EditAnywhere, Category="Movement")
	FMassMovementSteeringConfig Steering;
	
	/** Filter describing which lanes can be used when spawned. */
	UPROPERTY(EditAnywhere, Category="Movement")
	FZoneGraphTagFilter LaneFilter;

	/** Query radius when trying to find nearest lane when spawned. */
	UPROPERTY(EditAnywhere, Category="Movement", meta = (UIMin = 0.0, ClampMin = 0.0))
	float QueryRadius = 500.0f;

	/** List of supported movement styles for this configuration. */
	UPROPERTY(EditAnywhere, Category = "Movement")
	TArray<FMassMovementStyleConfig> MovementStyles;
};

/** Handle to FMassMovementConfig in MassMovementSettings. */
struct MASSAIMOVEMENT_API FMassMovementConfigHandle
{
	FMassMovementConfigHandle() = default;
	explicit FMassMovementConfigHandle(const uint8 InIndex) : Index(InIndex) {}

	static constexpr uint8 InvalidValue = 0xff; 
	
	bool IsValid() const { return Index != InvalidValue; }
	bool operator==(const FMassMovementConfigHandle& Other) const { return Index == Other.Index; }
	bool operator!=(const FMassMovementConfigHandle& Other) const { return Index != Other.Index; }
	
	uint8 Index = InvalidValue;
};
