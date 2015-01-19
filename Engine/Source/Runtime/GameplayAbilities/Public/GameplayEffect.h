// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagAssetInterface.h"
#include "AbilitySystemLog.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffectAggregator.h"
#include "GameplayEffectCalculation.h"
#include "GameplayEffect.generated.h"

struct FActiveGameplayEffect;

class UGameplayEffect;
class UGameplayEffectTemplate;
class UAbilitySystemComponent;
class UGameplayModMagnitudeCalculation;
class UGameplayEffectExecutionCalculation;

DECLARE_DELEGATE_RetVal_OneParam(bool, FActiveGameplayEffectQueryCustomMatch, const FActiveGameplayEffect);

/** Enumeration outlining the possible gameplay effect magnitude calculation policies */
UENUM()
enum class EGameplayEffectMagnitudeCalculation : uint8
{
	/** Use a simple, scalable float for the calculation */
	ScalableFloat,
	/** Perform a calculation based upon an attribute */
	AttributeBased,
	/** Perform a custom calculation, capable of capturing and acting on multiple attributes, in either BP or native */
	CustomCalculationClass,	
	/** This magnitude will be set explicity by that code/blueprint that creates the spec. */
	SetByCaller,
};

/** Enumeration outlining the possible attribute based float calculation policies */
UENUM()
enum class EAttributeBasedFloatCalculationType : uint8
{
	AttributeMagnitude,			// Use the final evaluated magnitude of the attribute
	AttributeBaseValue,			// Use the base value of the attribute
	AttributeBonusMagnitude		// Use the "bonus" evaluated magnitude of the attribute: Equivalent to (FinalMag - BaseValue)
};

/** 
 * Struct representing a float whose magnitude is dictated by a backing attribute and a calculation policy, follows basic form of:
 * (Coefficient * (PreMultiplyAdditiveValue + [Eval'd Attribute Value According to Policy])) + PostMultiplyAdditiveValue
 */
USTRUCT()
struct FAttributeBasedFloat
{
	GENERATED_USTRUCT_BODY()

public:

	/** Constructor */
	FAttributeBasedFloat()
		: Coefficient(1.f)
		, PreMultiplyAdditiveValue(0.f)
		, PostMultiplyAdditiveValue(0.f)
		, BackingAttribute()
		, AttributeCalculationType(EAttributeBasedFloatCalculationType::AttributeMagnitude)
	{}

	/**
	 * Calculate and return the magnitude of the float given the specified gameplay effect spec.
	 * 
	 * @note:	This function assumes (and asserts on) the existence of the required captured attribute within the spec.
	 *			It is the responsibility of the caller to verify that the spec is properly setup before calling this function.
	 *			
	 *	@param InRelevantSpec	Gameplay effect spec providing the backing attribute capture
	 *	
	 *	@return Evaluated magnitude based upon the spec & calculation policy
	 */
	float CalculateMagnitude(const FGameplayEffectSpec& InRelevantSpec) const;

	/** Coefficient to the attribute calculation */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FScalableFloat Coefficient;

	/** Additive value to the attribute calculation, added in before the coefficient applies */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FScalableFloat PreMultiplyAdditiveValue;

	/** Additive value to the attribute calculation, added in after the coefficient applies */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FScalableFloat PostMultiplyAdditiveValue;

	/** Attribute backing the calculation */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FGameplayEffectAttributeCaptureDefinition BackingAttribute;

	/** Calculation policy in regards to the attribute */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	EAttributeBasedFloatCalculationType AttributeCalculationType;

	/** Filter to use on source tags; If specified, only modifiers applied with all of these tags will factor into the calculation */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FGameplayTagContainer SourceTagFilter;

	/** Filter to use on target tags; If specified, only modifiers applied with all of these tags will factor into the calculation */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FGameplayTagContainer TargetTagFilter;
};

/** Structure to encapsulte magnitude that are calculated via custom calculation */
USTRUCT()
struct FCustomCalculationBasedFloat
{
	GENERATED_USTRUCT_BODY()

	FCustomCalculationBasedFloat()
		: CalculationClassMagnitude(nullptr)
		, Coefficient(1.f)
		, PreMultiplyAdditiveValue(0.f)
		, PostMultiplyAdditiveValue(0.f)
	{}

public:

	/**
	 * Calculate and return the magnitude of the float given the specified gameplay effect spec.
	 * 
	 * @note:	This function assumes (and asserts on) the existence of the required captured attribute within the spec.
	 *			It is the responsibility of the caller to verify that the spec is properly setup before calling this function.
	 *			
	 *	@param InRelevantSpec	Gameplay effect spec providing the backing attribute capture
	 *	
	 *	@return Evaluated magnitude based upon the spec & calculation policy
	 */
	float CalculateMagnitude(const FGameplayEffectSpec& InRelevantSpec) const;

	UPROPERTY(EditDefaultsOnly, Category=CustomCalculation, DisplayName="Calculation Class")
	TSubclassOf<UGameplayModMagnitudeCalculation> CalculationClassMagnitude;

	/** Coefficient to the custom calculation */
	UPROPERTY(EditDefaultsOnly, Category=CustomCalculation)
	FScalableFloat Coefficient;

	/** Additive value to the attribute calculation, added in before the coefficient applies */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FScalableFloat PreMultiplyAdditiveValue;

	/** Additive value to the attribute calculation, added in after the coefficient applies */
	UPROPERTY(EditDefaultsOnly, Category=AttributeFloat)
	FScalableFloat PostMultiplyAdditiveValue;
};

/** Struct for holding SetBytCaller data */
USTRUCT()
struct FSetByCallerFloat
{
	GENERATED_USTRUCT_BODY()

	FSetByCallerFloat()
	: DataName(NAME_None)
	{}

	/** The Name the caller (code or blueprint) will use to set this magnitude by. */
	UPROPERTY(EditDefaultsOnly, Category=SetByCaller)
	FName	DataName;
};

/** Struct representing the magnitude of a gameplay effect modifier, potentially calculated in numerous different ways */
USTRUCT()
struct FGameplayEffectModifierMagnitude
{
	GENERATED_USTRUCT_BODY()

public:

	/** Default Constructor */
	FGameplayEffectModifierMagnitude()
		: MagnitudeCalculationType(EGameplayEffectMagnitudeCalculation::ScalableFloat)
	{
	}

	/** Constructors for setting value in code (for automation tests) */
	FGameplayEffectModifierMagnitude(const FScalableFloat& Value)
		: MagnitudeCalculationType(EGameplayEffectMagnitudeCalculation::ScalableFloat)
		, ScalableFloatMagnitude(Value)
	{
	}
	FGameplayEffectModifierMagnitude(const FAttributeBasedFloat& Value)
		: MagnitudeCalculationType(EGameplayEffectMagnitudeCalculation::AttributeBased)
		, AttributeBasedMagnitude(Value)
	{
	}
	FGameplayEffectModifierMagnitude(const FCustomCalculationBasedFloat& Value)
		: MagnitudeCalculationType(EGameplayEffectMagnitudeCalculation::CustomCalculationClass)
		, CustomMagnitude(Value)
	{
	}
	FGameplayEffectModifierMagnitude(const FSetByCallerFloat& Value)
		: MagnitudeCalculationType(EGameplayEffectMagnitudeCalculation::SetByCaller)
		, SetByCallerMagnitude(Value)
	{
	}
 
	/**
	 * Determines if the magnitude can be properly calculated with the specified gameplay effect spec (could fail if relying on an attribute not present, etc.)
	 * 
	 * @param InRelevantSpec	Gameplay effect spec to check for magnitude calculation
	 * 
	 * @return Whether or not the magnitude can be properly calculated
	 */
	bool CanCalculateMagnitude(const FGameplayEffectSpec& InRelevantSpec) const;

	/**
	 * Attempts to calculate the magnitude given the provided spec. May fail if necessary information (such as captured attributes) is missing from
	 * the spec.
	 * 
	 * @param InRelevantSpec			Gameplay effect spec to use to calculate the magnitude with
	 * @param OutCalculatedMagnitude	[OUT] Calculated value of the magnitude, will be set to 0.f in the event of failure
	 * 
	 * @return True if the calculation was successful, false if it was not
	 */
	bool AttemptCalculateMagnitude(const FGameplayEffectSpec& InRelevantSpec, OUT float& OutCalculatedMagnitude) const;

	/** Attempts to recalculate the magnitude given a changed aggregator. This will only recalculate if we are a modifier that is linked (non snapshot) to the given aggregator. */
	bool AttemptRecalculateMagnitudeFromDependentChange(const FGameplayEffectSpec& InRelevantSpec, OUT float& OutCalculatedMagnitude, const FAggregator* ChangedAggregator) const;

	/**
	 * Gather all of the attribute capture definitions necessary to compute the magnitude and place them into the provided array
	 * 
	 * @param OutCaptureDefs	[OUT] Array populated with necessary attribute capture definitions
	 */
	void GetAttributeCaptureDefinitions(OUT TArray<FGameplayEffectAttributeCaptureDefinition>& OutCaptureDefs) const;

	EGameplayEffectMagnitudeCalculation GetMagnitudeCalculationType() const { return MagnitudeCalculationType; }

#if WITH_EDITOR
	GAMEPLAYABILITIES_API FText GetValueForEditorDisplay() const;
#endif

protected:

	/** Type of calculation to perform to derive the magnitude */
	UPROPERTY(EditDefaultsOnly, Category=Magnitude)
	EGameplayEffectMagnitudeCalculation MagnitudeCalculationType;

	/** Magnitude value represented by a scalable float */
	UPROPERTY(EditDefaultsOnly, Category=Magnitude)
	FScalableFloat ScalableFloatMagnitude;

	/** Magnitude value represented by an attribute-based float
	(Coefficient * (PreMultiplyAdditiveValue + [Eval'd Attribute Value According to Policy])) + PostMultiplyAdditiveValue */
	UPROPERTY(EditDefaultsOnly, Category=Magnitude)
	FAttributeBasedFloat AttributeBasedMagnitude;

	/** Magnitude value represented by a custom calculation class */
	UPROPERTY(EditDefaultsOnly, Category=Magnitude)
	FCustomCalculationBasedFloat CustomMagnitude;

	/** Magnitude value represented by a SetByCaller magnitude */
	UPROPERTY(EditDefaultsOnly, Category=Magnitude)
	FSetByCallerFloat SetByCallerMagnitude;

	// @hack: @todo: This is temporary to aid in post-load fix-up w/o exposing members publicly
	friend class UGameplayEffect;
	friend class FGameplayEffectModifierMagnitudeDetails;
};

/** 
 * Struct representing modifier info used exclusively for "scoped" executions that happen instantaneously. These are
 * folded into a calculation only for the extent of the calculation and never permanently added to an aggregator.
 */
USTRUCT()
struct FGameplayEffectExecutionScopedModifierInfo
{
	GENERATED_USTRUCT_BODY()

	// Constructors
	FGameplayEffectExecutionScopedModifierInfo()
		: ModifierOp(EGameplayModOp::Additive)
	{}

	FGameplayEffectExecutionScopedModifierInfo(const FGameplayEffectAttributeCaptureDefinition& InCaptureDef)
		: CapturedAttribute(InCaptureDef)
		, ModifierOp(EGameplayModOp::Additive)
	{
	}

	/** Backing attribute that the scoped modifier is for */
	UPROPERTY(VisibleDefaultsOnly, Category=Execution)
	FGameplayEffectAttributeCaptureDefinition CapturedAttribute;

	/** Modifier operation to perform */
	UPROPERTY(EditDefaultsOnly, Category=Execution)
	TEnumAsByte<EGameplayModOp::Type> ModifierOp;

	/** Magnitude of the scoped modifier */
	UPROPERTY(EditDefaultsOnly, Category=Execution)
	FGameplayEffectModifierMagnitude ModifierMagnitude;

	/** Source tag requirements for the modifier to apply */
	UPROPERTY(EditDefaultsOnly, Category=Execution)
	FGameplayTagRequirements SourceTags;

	/** Target tag requirements for the modifier to apply */
	UPROPERTY(EditDefaultsOnly, Category=Execution)
	FGameplayTagRequirements TargetTags;
};

/** 
 * Struct representing the definition of a custom execution for a gameplay effect.
 * Custom executions run special logic from an outside class each time the gameplay effect executes.
 */
USTRUCT()
struct FGameplayEffectExecutionDefinition
{
	GENERATED_USTRUCT_BODY()

	/**
	 * Gathers and populates the specified array with the capture definitions that the execution would like in order
	 * to perform its custom calculation. Up to the individual execution calculation to handle if some of them are missing
	 * or not.
	 * 
	 * @param OutCaptureDefs	[OUT] Capture definitions requested by the execution
	 */
	void GetAttributeCaptureDefinitions(OUT TArray<FGameplayEffectAttributeCaptureDefinition>& OutCaptureDefs) const;

	/** Custom execution calculation class to run when the gameplay effect executes */
	UPROPERTY(EditDefaultsOnly, Category=Execution)
	TSubclassOf<UGameplayEffectExecutionCalculation> CalculationClass;
	
	/** Modifiers that are applied "in place" during the execution calculation */
	UPROPERTY(EditDefaultsOnly, Category = Execution)
	TArray<FGameplayEffectExecutionScopedModifierInfo> CalculationModifiers;

	/** Other Gameplay Effects that will be applied to the target of this execution if the execution is successful */
	UPROPERTY(EditDefaultsOnly, Category = Execution, meta = (DisplayName = "Conditional Gameplay Effects"))
	TArray<TSubclassOf<UGameplayEffect>> ConditionalGameplayEffectClasses;
};

/**
 * FGameplayModifierInfo
 *	Tells us "Who/What we" modify
 *	Does not tell us how exactly
 *
 */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayModifierInfo
{
	GENERATED_USTRUCT_BODY()

	FGameplayModifierInfo()	
	: ModifierOp(EGameplayModOp::Additive)
	{

	}

	/** The Attribute we modify or the GE we modify modifies. */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier, meta=(FilterMetaTag="HideFromModifiers"))
	FGameplayAttribute Attribute;

	/** The numeric operation of this modifier: Override, Add, Multiply, etc  */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	TEnumAsByte<EGameplayModOp::Type> ModifierOp;

	// @todo: Remove this after content resave
	/** Now "deprecated," though being handled in a custom manner to avoid engine version bump. */
	UPROPERTY()
	FScalableFloat Magnitude;

	/** Magnitude of the modifier */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	FGameplayEffectModifierMagnitude ModifierMagnitude;

	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	FGameplayTagRequirements	SourceTags;

	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	FGameplayTagRequirements	TargetTags;


	FString ToSimpleString() const
	{
		return FString::Printf(TEXT("%s BaseVaue: %s"), *EGameplayModOpToString(ModifierOp), *Magnitude.ToSimpleString());
	}
};

/**
 * FGameplayEffectCue
 *	This is a cosmetic cue that can be tied to a UGameplayEffect. 
 *  This is essentially a GameplayTag + a Min/Max level range that is used to map the level of a GameplayEffect to a normalized value used by the GameplayCue system.
 */
USTRUCT()
struct FGameplayEffectCue
{
	GENERATED_USTRUCT_BODY()

	FGameplayEffectCue()
		: MinLevel(0.f)
		, MaxLevel(0.f)
	{
	}

	FGameplayEffectCue(const FGameplayTag& InTag, float InMinLevel, float InMaxLevel)
		: MinLevel(InMinLevel)
		, MaxLevel(InMaxLevel)
	{
		GameplayCueTags.AddTag(InTag);
	}

	/** The attribute to use as the source for cue magnitude. If none use level */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue)
	FGameplayAttribute MagnitudeAttribute;

	/** The minimum level that this Cue supports */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue)
	float	MinLevel;

	/** The maximum level that this Cue supports */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue)
	float	MaxLevel;

	/** Tags passed to the gameplay cue handler when this cue is activated */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue, meta = (Categories="GameplayCue"))
	FGameplayTagContainer GameplayCueTags;

	float NormalizeLevel(float InLevel)
	{
		float Range = MaxLevel - MinLevel;
		if (Range <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}

		return FMath::Clamp((InLevel - MinLevel) / Range, 0.f, 1.0f);
	}
};

USTRUCT()
struct GAMEPLAYABILITIES_API FInheritedTagContainer
{
	GENERATED_USTRUCT_BODY()

	/** Tags that I inherited and tags that I added minus tags that I removed*/
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = Application)
	FGameplayTagContainer CombinedTags;

	/** Tags that I have in addition to my parent's tags */
	UPROPERTY(EditDefaultsOnly, Transient, BlueprintReadOnly, Category = Application)
	FGameplayTagContainer Added;

	/** Tags that should be removed if my parent had them */
	UPROPERTY(EditDefaultsOnly, Transient, BlueprintReadOnly, Category = Application)
	FGameplayTagContainer Removed;

	void UpdateInheritedTagProperties(const FInheritedTagContainer* Parent);

	void PostInitProperties();

	void AddTag(const FGameplayTag& TagToAdd);
	void RemoveTag(FGameplayTag TagToRemove);
};

/** Gameplay effect duration policies */
UENUM()
enum class EGameplayEffectDurationType : uint8
{
	/** This effect applies instantly */
	Instant,
	/** This effect lasts forever */
	Infinite,
	/** The duration of this effect will be specified by a magnitude */
	HasDuration
};

/** Enumeration of policies for dealing with duration of a gameplay effect while stacking */
UENUM()
enum class EGameplayEffectStackingDurationPolicy : uint8
{
	/** The duration of the effect will be refreshed from any successful stack application */
	RefreshOnSuccessfulApplication,

	/** The duration of the effect will never be refreshed */
	NeverRefresh,
};

/** Enumeration of policies for dealing with the period of a gameplay effect while stacking */
UENUM()
enum class EGameplayEffectStackingPeriodPolicy : uint8
{
	/** Any progress toward the next tick of a periodic effect is discarded upon any successful stack application */
	ResetOnSuccessfulApplication,

	/** The progress toward the next tick of a periodic effect will never be reset, regardless of stack applications */
	NeverReset,
};

/**
 * UGameplayEffect
 *	The GameplayEffect definition. This is the data asset defined in the editor that drives everything.
 *  This is only blueprintable to allow for templating gameplay effects. Gameplay effects should NOT contain blueprint graphs.
 */
UCLASS(Blueprintable)
class GAMEPLAYABILITIES_API UGameplayEffect : public UObject, public IGameplayTagAssetInterface
{

public:
	GENERATED_UCLASS_BODY()

	/** Infinite duration */
	static const float INFINITE_DURATION;

	/** No duration; Time specifying instant application of an effect */
	static const float INSTANT_APPLICATION;

	/** Constant specifying that the combat effect has no period and doesn't check for over time application */
	static const float NO_PERIOD;

	/** No Level/Level not set */
	static const float INVALID_LEVEL;

#if WITH_EDITORONLY_DATA
	/** Template to derive starting values and editing customization from */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Template)
	UGameplayEffectTemplate*	Template;

	/** When false, show a limited set of properties for editing, based on the template we are derived from */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Template)
	bool ShowAllProperties;
#endif

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif

	/** Policy for the duration of this effect */
	UPROPERTY(EditDefaultsOnly, Category=GameplayEffect)
	EGameplayEffectDurationType DurationPolicy;

	/** Duration in seconds. 0.0 for instantaneous effects; -1.0 for infinite duration. */
	UPROPERTY(EditDefaultsOnly, Category=GameplayEffect)
	FGameplayEffectModifierMagnitude DurationMagnitude;

	/** Deprecated. Use DurationMagnitude instead. */
	UPROPERTY()
	FScalableFloat	Duration;

	/** Period in seconds. 0.0 for non-periodic effects */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Period)
	FScalableFloat	Period;
	
	/** If true, the effect executes on application and then at every period interval. If false, no execution occurs until the first period elapses. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Period)
	bool bExecutePeriodicEffectOnApplication;

	/** Array of modifiers that will affect the target of this effect */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=GameplayEffect)
	TArray<FGameplayModifierInfo> Modifiers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = GameplayEffect)
	TArray<FGameplayEffectExecutionDefinition>	Executions;

	/** Probability that this gameplay effect will be applied to the target actor (0.0 for never, 1.0 for always) */
	UPROPERTY(EditDefaultsOnly, Category=Application, meta=(GameplayAttribute="True"))
	FScalableFloat	ChanceToApplyToTarget;

	/** other gameplay effects that will be applied to the target of this effect if this effect applies */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = GameplayEffect,meta = (DisplayName = "Linked Gameplay Effects"))
	TArray<TSubclassOf<UGameplayEffect>> TargetEffectClasses;

	/** Deprecated. Use TargetEffectClasses instead */
	UPROPERTY(VisibleDefaultsOnly, Category = Deprecated)
	TArray<UGameplayEffect*> TargetEffects;

	/** Effects to apply when a stacking effect "overflows" its stack count through another attempted application. Added whether the overflow application succeeds or not. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Overflow)
	TArray<TSubclassOf<UGameplayEffect>> OverflowEffects;

	/** If true, stacking attempts made while at the stack count will fail, resulting in the duration and context not being refreshed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Overflow)
	bool bDenyOverflowApplication;

	/** If true, the entire stack of the effect will be cleared once it overflows */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Overflow, meta=(EditCondition="bDenyOverflowApplication"))
	bool bClearStackOnOverflow;

	/** Effects to apply when this effect is made to expire prematurely (like via a forced removal, clear tags, etc.); Only works for effects with a duration */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Expiration)
	TArray<TSubclassOf<UGameplayEffect>> PrematureExpirationEffectClasses;

	/** Effects to apply when this effect expires naturally via its duration; Only works for effects with a duration */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Expiration)
	TArray<TSubclassOf<UGameplayEffect>> RoutineExpirationEffectClasses;

	void GetTargetEffects(TArray<const UGameplayEffect*>& OutEffects) const;

	// ------------------------------------------------
	// Gameplay tag interface
	// ------------------------------------------------

	/** Overridden to return requirements tags */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	void UpdateInheritedTagProperties();
	void ValidateGameplayEffect();

	virtual void PostLoad() override;

	// ----------------------------------------------

	/** Cues to trigger non-simulated reactions in response to this GameplayEffect such as sounds, particle effects, etc */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Display)
	TArray<FGameplayEffectCue>	GameplayCues;

	/** Data for the UI representation of this effect. This should include things like text, icons, etc. Not available in server-only builds. */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = Display)
	class UGameplayEffectUIData* UIData;

	// ----------------------------------------------------------------------
	//	Tag Containers
	// ----------------------------------------------------------------------
	
	/** The GameplayEffect's Tags: tags the the GE *has* and DOES NOT give to the actor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Tags, meta = (DisplayName = "GameplayEffectAssetTag"))
	FInheritedTagContainer InheritableGameplayEffectTags;

	/** The GameplayEffect's Tags: tags the the GE *has* and DOES NOT give to the actor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Deprecated, meta=(DisplayName="GameplayEffectAssetTag"))
	FGameplayTagContainer GameplayEffectTags;
	
	/** "These tags are applied to the actor I am applied to" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Tags, meta=(DisplayName="GrantedTags"))
	FInheritedTagContainer InheritableOwnedTagsContainer;

	/** "These tags are applied to the actor I am applied to" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Deprecated)
	FGameplayTagContainer OwnedTagsContainer;
	
	/** Once Applied, these tags requirements are used to determined if the GameplayEffect is "on" or "off". A GameplayEffect can be off and do nothing, but still applied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Tags)
	FGameplayTagRequirements OngoingTagRequirements;

	/** Tag requirements for this GameplayEffect to be applied to a target. This is pass/fail at the time of application. If fail, this GE fails to apply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Tags)
	FGameplayTagRequirements ApplicationTagRequirements;

	/** GameplayEffects that *have* tags in this container will be cleared upon effect application. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Tags)
	FInheritedTagContainer RemoveGameplayEffectsWithTags;

	/** Deprecated. Use RemoveGameplayEffectsWithTags instead */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Deprecated)
	FGameplayTagContainer ClearTagsContainer;

	/** Grants the owner immunity from these source tags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Tags, meta = (DisplayName = "GrantedApplicationImmunityTags"))
	FGameplayTagRequirements GrantedApplicationImmunityTags;

	// ----------------------------------------------------------------------
	//	Stacking
	// ----------------------------------------------------------------------
	
	/** How this GameplayEffect stacks with other instances of this same GameplayEffect */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	EGameplayEffectStackingType	StackingType;

	/** Stack limit for StackingType */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	int32 StackLimitCount;

	/** Policy for how the effect duration should be refreshed while stacking */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	EGameplayEffectStackingDurationPolicy StackDurationRefreshPolicy;

	/** Policy for how the effect period should be reset (or not) while stacking */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	EGameplayEffectStackingPeriodPolicy StackPeriodResetPolicy;
};

/** Holds evaluated magnitude from a GameplayEffect modifier */
USTRUCT()
struct FModifierSpec
{
	GENERATED_USTRUCT_BODY()

	FModifierSpec() : EvaluatedMagnitude(0.f) { }

	float GetEvaluatedMagnitude() const { return EvaluatedMagnitude; }

private:

	// @todo: Probably need to make the modifier info private so people don't go gunking around in the magnitude
	/** In the event that the modifier spec requires custom magnitude calculations, this is the authoritative, last evaluated value of the magnitude */
	UPROPERTY()
	float EvaluatedMagnitude;

	/** These structures are the only ones that should internally be able to update the EvaluatedMagnitude. Any gamecode that gets its hands on FModifierSpec should never be setting EvaluatedMagnitude manually */
	friend struct FGameplayEffectSpec;
	friend struct FActiveGameplayEffectsContainer;
};

/** Saves list of modified attributes, to use for gameplay cues or later processing */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayEffectModifiedAttribute
{
	GENERATED_USTRUCT_BODY()

	/** The attribute that has been modified */
	UPROPERTY()
	FGameplayAttribute Attribute;

	/** Total magnitude applied to that attribute */
	UPROPERTY()
	float TotalMagnitude;

	FGameplayEffectModifiedAttribute() : TotalMagnitude(0.0f) {}
};

/** Struct used to hold the result of a gameplay attribute capture; Initially seeded by definition data, but then populated by ability system component when appropriate */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayEffectAttributeCaptureSpec
{
	// Allow these as friends so they can seed the aggregator, which we don't otherwise want exposed
	friend struct FActiveGameplayEffectsContainer;
	friend class UAbilitySystemComponent;

	GENERATED_USTRUCT_BODY()

	// Constructors
	FGameplayEffectAttributeCaptureSpec();
	FGameplayEffectAttributeCaptureSpec(const FGameplayEffectAttributeCaptureDefinition& InDefinition);

	/**
	 * Returns whether the spec actually has a valid capture yet or not
	 * 
	 * @return True if the spec has a valid attribute capture, false if it does not
	 */
	bool HasValidCapture() const;

	/**
	 * Attempts to calculate the magnitude of the captured attribute given the specified parameters. Can fail if the spec doesn't have
	 * a valid capture yet.
	 * 
	 * @param InEvalParams	Parameters to evaluate the attribute under
	 * @param OutMagnitude	[OUT] Computed magnitude
	 * 
	 * @return True if the magnitude was successfully calculated, false if it was not
	 */
	bool AttemptCalculateAttributeMagnitude(const FAggregatorEvaluateParameters& InEvalParams, OUT float& OutMagnitude) const;

	/**
	 * Attempts to calculate the magnitude of the captured attribute given the specified parameters, including a starting base value. 
	 * Can fail if the spec doesn't have a valid capture yet.
	 * 
	 * @param InEvalParams	Parameters to evaluate the attribute under
	 * @param InBaseValue	Base value to evaluate the attribute under
	 * @param OutMagnitude	[OUT] Computed magnitude
	 * 
	 * @return True if the magnitude was successfully calculated, false if it was not
	 */
	bool AttemptCalculateAttributeMagnitudeWithBase(const FAggregatorEvaluateParameters& InEvalParams, float InBaseValue, OUT float& OutMagnitude) const;

	/**
	 * Attempts to calculate the base value of the captured attribute given the specified parameters. Can fail if the spec doesn't have
	 * a valid capture yet.
	 * 
	 * @param OutBaseValue	[OUT] Computed base value
	 * 
	 * @return True if the base value was successfully calculated, false if it was not
	 */
	bool AttemptCalculateAttributeBaseValue(OUT float& OutBaseValue) const;

	/**
	 * Attempts to calculate the "bonus" magnitude (final - base value) of the captured attribute given the specified parameters. Can fail if the spec doesn't have
	 * a valid capture yet.
	 * 
	 * @param InEvalParams		Parameters to evaluate the attribute under
	 * @param OutBonusMagnitude	[OUT] Computed bonus magnitude
	 * 
	 * @return True if the bonus magnitude was successfully calculated, false if it was not
	 */
	bool AttemptCalculateAttributeBonusMagnitude(const FAggregatorEvaluateParameters& InEvalParams, OUT float& OutBonusMagnitude) const;
	
	/**
	 * Attempts to populate the specified aggregator with a snapshot of the backing captured aggregator. Can fail if the spec doesn't have
	 * a valid capture yet.
	 *
	 * @param OutAggregatorSnapshot	[OUT] Snapshotted aggregator, if possible
	 *
	 * @return True if the aggregator was successfully snapshotted, false if it was not
	 */
	bool AttemptGetAttributeAggregatorSnapshot(OUT FAggregator& OutAggregatorSnapshot) const;

	/**
	 * Attempts to populate the specified aggregator with all of the mods of the backing captured aggregator. Can fail if the spec doesn't have
	 * a valid capture yet.
	 *
	 * @param OutAggregatorToAddTo	[OUT] Aggregator with mods appended, if possible
	 *
	 * @return True if the aggregator had mods successfully added to it, false if it did not
	 */
	bool AttemptAddAggregatorModsToAggregator(OUT FAggregator& OutAggregatorToAddTo) const;
	
	/** Simple accessor to backing capture definition */
	const FGameplayEffectAttributeCaptureDefinition& GetBackingDefinition() const;

	/** Register this handle with linked aggregators */
	void RegisterLinkedAggregatorCallback(FActiveGameplayEffectHandle Handle) const;

	/** Unregister this handle with linked aggregators */
	void UnregisterLinkedAggregatorCallback(FActiveGameplayEffectHandle Handle) const;
	
	/** Return true if this capture should be recalculated if the given aggregator has changed */
	bool ShouldRefreshLinkedAggregator(const FAggregator* ChangedAggregator) const;
		
private:

	/** Copy of the definition the spec should adhere to for capturing */
	UPROPERTY()
	FGameplayEffectAttributeCaptureDefinition BackingDefinition;

	/** Ref to the aggregator for the captured attribute */
	FAggregatorRef AttributeAggregator;
};

/** Struct used to handle a collection of captured source and target attributes */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayEffectAttributeCaptureSpecContainer
{
	GENERATED_USTRUCT_BODY()

public:

	/**
	 * Add a definition to be captured by the owner of the container. Will not add the definition if its exact
	 * match already exists within the container.
	 * 
	 * @param InCaptureDefinition	Definition to capture with
	 */
	void AddCaptureDefinition(const FGameplayEffectAttributeCaptureDefinition& InCaptureDefinition);

	/**
	 * Capture source or target attributes from the specified component. Should be called by the container's owner.
	 * 
	 * @param InAbilitySystemComponent	Component to capture attributes from
	 * @param InCaptureSource			Whether to capture attributes as source or target
	 */
	void CaptureAttributes(class UAbilitySystemComponent* InAbilitySystemComponent, EGameplayEffectAttributeCaptureSource InCaptureSource);

	/**
	 * Find a capture spec within the container matching the specified capture definition, if possible.
	 * 
	 * @param InDefinition				Capture definition to use as the search basis
	 * @param bOnlyIncludeValidCapture	If true, even if a spec is found, it won't be returned if it doesn't also have a valid capture already
	 * 
	 * @return The found attribute spec matching the specified search params, if any
	 */
	const FGameplayEffectAttributeCaptureSpec* FindCaptureSpecByDefinition(const FGameplayEffectAttributeCaptureDefinition& InDefinition, bool bOnlyIncludeValidCapture) const;

	/**
	 * Determines if the container has specs with valid captures for all of the specified definitions.
	 * 
	 * @param InCaptureDefsToCheck	Capture definitions to check for
	 * 
	 * @return True if the container has valid capture attributes for all of the specified definitions, false if it does not
	 */
	bool HasValidCapturedAttributes(const TArray<FGameplayEffectAttributeCaptureDefinition>& InCaptureDefsToCheck) const;

	/** Returns whether the container has at least one spec w/o snapshotted attributes */
	bool HasNonSnapshottedAttributes() const;

	/** Registers any linked aggregators to notify this active handle if they are dirtied */
	void RegisterLinkedAggregatorCallbacks(FActiveGameplayEffectHandle Handle) const;

	/** Unregisters any linked aggregators from notifying this active handle if they are dirtied */
	void UnregisterLinkedAggregatorCallbacks(FActiveGameplayEffectHandle Handle) const;

private:

	/** Captured attributes from the source of a gameplay effect */
	UPROPERTY()
	TArray<FGameplayEffectAttributeCaptureSpec> SourceAttributes;

	/** Captured attributes from the target of a gameplay effect */
	UPROPERTY()
	TArray<FGameplayEffectAttributeCaptureSpec> TargetAttributes;

	/** If true, has at least one capture spec that did not request a snapshot */
	UPROPERTY()
	bool bHasNonSnapshottedAttributes;
};

/**
 * GameplayEffect Specification. Tells us:
 *	-What UGameplayEffect (const data)
 *	-What Level
 *  -Who instigated
 *  
 * FGameplayEffectSpec is modifiable. We start with initial conditions and modifications be applied to it. In this sense, it is stateful/mutable but it
 * is still distinct from an FActiveGameplayEffect which in an applied instance of an FGameplayEffectSpec.
 */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayEffectSpec
{
	GENERATED_USTRUCT_BODY()

	FGameplayEffectSpec()
		: Def(nullptr)
		, Duration(UGameplayEffect::INSTANT_APPLICATION)
		, Period(UGameplayEffect::NO_PERIOD)
		, ChanceToApplyToTarget(1.f)
		, StackCount(1)
		, bCompletedSourceAttributeCapture(false)
		, bCompletedTargetAttributeCapture(false)
		, bDurationLocked(false)
		, Level(UGameplayEffect::INVALID_LEVEL)
	{

	}

	FGameplayEffectSpec(const UGameplayEffect* InDef, const FGameplayEffectContextHandle& InEffectContext, float Level = UGameplayEffect::INVALID_LEVEL);
	
	UPROPERTY()
	const UGameplayEffect* Def;
	
	UPROPERTY()
	TArray<FGameplayEffectModifiedAttribute> ModifiedAttributes;
	
	/** Attributes captured by the spec that are relevant to custom calculations, potentially in owned modifiers, etc.; NOT replicated to clients */
	UPROPERTY(NotReplicated)
	FGameplayEffectAttributeCaptureSpecContainer CapturedRelevantAttributes;

	/**
	 * Determines if the spec has capture specs with valid captures for all of the specified definitions.
	 * 
	 * @param InCaptureDefsToCheck	Capture definitions to check for
	 * 
	 * @return True if the container has valid capture attributes for all of the specified definitions, false if it does not
	 */
	bool HasValidCapturedAttributes(const TArray<FGameplayEffectAttributeCaptureDefinition>& InCaptureDefsToCheck) const;

	/** Looks for an existing modified attribute struct, may return NULL */
	const FGameplayEffectModifiedAttribute* GetModifiedAttribute(const FGameplayAttribute& Attribute) const;
	FGameplayEffectModifiedAttribute* GetModifiedAttribute(const FGameplayAttribute& Attribute);

	/** Adds a new modified attribute struct, will always add so check to see if it exists first */
	FGameplayEffectModifiedAttribute* AddModifiedAttribute(const FGameplayAttribute& Attribute);

	/** Deletes any modified attributes that aren't needed. Call before replication */
	void PruneModifiedAttributes();

	/**
	 * Helper function to attempt to calculate the duration of the spec from its GE definition
	 * 
	 * @param OutDefDuration	Computed duration of the spec from its GE definition; Not the actual duration of the spec
	 * 
	 * @return True if the calculation was successful, false if it was not
	 */
	bool AttemptCalculateDurationFromDef(OUT float& OutDefDuration) const;

	/** Sets duration. This should only be called as the GameplayEffect is being created and applied; Ignores calls after attribute capture */
	void SetDuration(float NewDuration, bool bLockDuration);

	float GetDuration() const;
	float GetPeriod() const;
	float GetChanceToApplyToTarget() const;

	/** other effects that need to be applied to the target if this effect is successful */
	TArray< FGameplayEffectSpecHandle > TargetEffectSpecs;

	/** Set the context info: who and where this spec came from. */
	void SetContext(FGameplayEffectContextHandle NewEffectContext);

	FGameplayEffectContextHandle GetContext() const
	{
		return EffectContext;
	}

	void GetAllGrantedTags(OUT FGameplayTagContainer& Container) const;

	/** Sets the magnitude of a SetByCaller modifier */
	void SetSetByCallerMagnitude(FName DataName, float Magnitude);

	/** Returns the magnitude of a SetByCaller modifier. Will return 0.f and Warn if the magnitude has not been set. */
	float GetSetByCallerMagnitude(FName DataName) const;

	// The duration in seconds of this effect
	// instantaneous effects should have a duration of UGameplayEffect::INSTANT_APPLICATION
	// effects that last forever should have a duration of UGameplayEffect::INFINITE_DURATION
	UPROPERTY()
	float Duration;

	// The period in seconds of this effect.
	// Nonperiodic effects should have a period of UGameplayEffect::NO_PERIOD
	UPROPERTY()
	float Period;

	// The chance, in a 0.0-1.0 range, that this GameplayEffect will be applied to the target Attribute or GameplayEffect.
	UPROPERTY()
	float ChanceToApplyToTarget;

	// Captured Source Tags on GameplayEffectSpec creation.	
	UPROPERTY(NotReplicated)
	FTagContainerAggregator	CapturedSourceTags;

	// Tags from the target, captured during execute	
	UPROPERTY(NotReplicated)
	FTagContainerAggregator	CapturedTargetTags;

	/** Tags that are granted and that did not come from the UGameplayEffect def. These are replicated. */
	UPROPERTY()
	FGameplayTagContainer DynamicGrantedTags;
	
	UPROPERTY()
	TArray<FModifierSpec> Modifiers;

	UPROPERTY()
	int32 StackCount;

	/** Whether the spec has had its source attribute capture completed or not yet */
	UPROPERTY(NotReplicated)
	uint32 bCompletedSourceAttributeCapture : 1;

	/** Whether the spec has had its target attribute capture completed or not yet */
	UPROPERTY(NotReplicated)
	uint32 bCompletedTargetAttributeCapture : 1;

	/** Whether the duration of the spec is locked or not; If it is, attempts to set it will fail */
	UPROPERTY(NotReplicated)
	uint32 bDurationLocked : 1;
	
	/**
	 * Get the computed magnitude of the modifier on the spec with the specified index
	 * 
	 * @param ModifierIndx			Modifier to get
	 * @param bFactorInStackCount	If true, the calculation will include the stack count
	 * 
	 * @return Computed magnitude
	 */
	float GetModifierMagnitude(int32 ModifierIdx, bool bFactorInStackCount) const;

	void CalculateModifierMagnitudes();

	void SetLevel(float InLevel);

	float GetLevel() const;

	void PrintAll() const;

	FString ToSimpleString() const
	{
		return FString::Printf(TEXT("%s"), *Def->GetName());
	}

	const FGameplayEffectContextHandle& GetEffectContext() const
	{
		return EffectContext;
	}

	void CaptureAttributeDataFromTarget(UAbilitySystemComponent* TargetAbilitySystemComponent);

private:

	/** Map of set by caller magnitudes */
	TMap<FName, float>	SetByCallerMagnitudes;

	void CaptureDataFromSource();

	/** Helper function to initialize all of the capture definitions required by the spec */
	void SetupAttributeCaptureDefinitions();
	
	UPROPERTY()
	FGameplayEffectContextHandle EffectContext; // This tells us how we got here (who / what applied us)
	
	UPROPERTY()
	float Level;	
};

/**
 * Active GameplayEffect instance
 *	-What GameplayEffect Spec
 *	-Start time
 *  -When to execute next
 *  -Replication callbacks
 *
 */
USTRUCT()
struct FActiveGameplayEffect : public FFastArraySerializerItem
{
	GENERATED_USTRUCT_BODY()

	FActiveGameplayEffect()
		: StartGameStateTime(0)
		, CachedStartGameStateTime(0)
		, StartWorldTime(0.f)
		, bIsInhibited(true)
		, IsPendingRemove(false)
	{
	}

	FActiveGameplayEffect(FActiveGameplayEffectHandle InHandle, const FGameplayEffectSpec &InSpec, float CurrentWorldTime, int32 InStartGameStateTime, FPredictionKey InPredictionKey)
		: Handle(InHandle)
		, Spec(InSpec)
		, PredictionKey(InPredictionKey)
		, StartGameStateTime(InStartGameStateTime)
		, CachedStartGameStateTime(InStartGameStateTime)
		, StartWorldTime(CurrentWorldTime)
		, bIsInhibited(true)
		, IsPendingRemove(false)
	{
	}

	FActiveGameplayEffectHandle Handle;

	UPROPERTY()
	FGameplayEffectSpec Spec;

	UPROPERTY()
	FPredictionKey	PredictionKey;

	/** Game time this started */
	UPROPERTY()
	int32 StartGameStateTime;

	/** Used for handling duration modifications being replicated */
	UPROPERTY(NotReplicated)
	int32 CachedStartGameStateTime;

	UPROPERTY(NotReplicated)
	float StartWorldTime;

	// Not sure if this should replicate or not. If replicated, we may have trouble where IsInhibited doesn't appear to change when we do tag checks (because it was previously inhibited, but replication made it inhibited).
	UPROPERTY()
	bool bIsInhibited;

	bool IsPendingRemove;

	FOnActiveGameplayEffectRemoved	OnRemovedDelegate;

	FTimerHandle PeriodHandle;

	FTimerHandle DurationHandle;

	float GetTimeRemaining(float WorldTime)
	{
		float Duration = GetDuration();		
		return (Duration == UGameplayEffect::INFINITE_DURATION ? -1.f : Duration - (WorldTime - StartWorldTime));
	}
	
	float GetDuration() const
	{
		return Spec.GetDuration();
	}

	float GetPeriod() const
	{
		return Spec.GetPeriod();
	}

	void CheckOngoingTagRequirements(const FGameplayTagContainer& OwnerTags, struct FActiveGameplayEffectsContainer& OwningContainer);

	void PrintAll() const;

	void PreReplicatedRemove(const struct FActiveGameplayEffectsContainer &InArray);
	void PostReplicatedAdd(const struct FActiveGameplayEffectsContainer &InArray);
	void PostReplicatedChange(const struct FActiveGameplayEffectsContainer &InArray);

	bool operator==(const FActiveGameplayEffect& Other)
	{
		return Handle == Other.Handle;
	}
};

/**
 *	Generic querying data structure for active GameplayEffects. Lets us ask things like:
 *		Give me duration/magnitude of active gameplay effects with these tags
 *		Give me handles to all activate gameplay effects modifying this attribute.
 *		
 *	Any requirements specified in the query are required: must meet "all" not "one".
 */
USTRUCT()
struct FActiveGameplayEffectQuery
{
	GENERATED_USTRUCT_BODY()

	FActiveGameplayEffectQuery()
		: OwningTagContainer(nullptr)
		, EffectTagContainer(nullptr)
		, OwningTagContainer_Rejection(nullptr)
		, EffectTagContainer_Rejection(nullptr)
		, EffectSource(nullptr)
	{
	}

	FActiveGameplayEffectQuery(const FGameplayTagContainer* InOwningTagContainer)
		: OwningTagContainer(InOwningTagContainer)
		, EffectTagContainer(nullptr)
		, OwningTagContainer_Rejection(nullptr)
		, EffectTagContainer_Rejection(nullptr)
		, EffectSource(nullptr)
	{
	}

	/** Bind this to override the default query-matching code. */
	FActiveGameplayEffectQueryCustomMatch CustomMatch;

	/** Returns true if Effect matches the criteria of this query, which will be overridden by CustomMatch if it is bound. Returns false otherwise. */
	bool Matches(const FActiveGameplayEffect& Effect) const;

	/** used to match with InheritableOwnedTagsContainer */
	const FGameplayTagContainer* OwningTagContainer;

	/** used to match with InheritableGameplayEffectTags */
	const FGameplayTagContainer* EffectTagContainer;

	/** used to reject matches with InheritableOwnedTagsContainer */
	const FGameplayTagContainer* OwningTagContainer_Rejection;

	/** used to reject matches with InheritableGameplayEffectTags */
	const FGameplayTagContainer* EffectTagContainer_Rejection;

	// Matches on GameplayEffects which modify given attribute
	FGameplayAttribute ModifyingAttribute;

	// Matches on GameplayEffects which come from this source
	const UObject* EffectSource;

	// Handles to ignore as matches, even if other criteria is met
	TArray<FActiveGameplayEffectHandle> IgnoreHandles;
};


/**
 * Active GameplayEffects Container
 *	-Bucket of ActiveGameplayEffects
 *	-Needed for FFastArraySerialization
 *  
 * This should only be used by UAbilitySystemComponent. All of this could just live in UAbilitySystemComponent except that we need a distinct USTRUCT to implement FFastArraySerializer.
 *
 */
USTRUCT()
struct FActiveGameplayEffectsContainer : public FFastArraySerializer
{
	GENERATED_USTRUCT_BODY();

	friend struct FActiveGameplayEffect;
	friend class UAbilitySystemComponent;
	friend struct FScopedActiveGameplayEffectLock;
	friend class AAbilitySystemDebugHUD;

	FActiveGameplayEffectsContainer();

	UAbilitySystemComponent* Owner;

#if ENABLE_VISUAL_LOG
	struct DebugExecutedGameplayEffectData
	{
		FString GameplayEffectName;
		FString ActivationState;
		FGameplayAttribute Attribute;
		float Magnitude;
	};

	// Stores a record of gameplay effects that have executed and their results. Useful for debugging.
	TArray<DebugExecutedGameplayEffectData> DebugExecutedGameplayEffects;

	GAMEPLAYABILITIES_API void GrabDebugSnapshot(FVisualLogEntry* Snapshot) const;
#endif // ENABLE_VISUAL_LOG

	void RegisterWithOwner(UAbilitySystemComponent* Owner);	
	
	FActiveGameplayEffect* ApplyGameplayEffectSpec(const FGameplayEffectSpec& Spec, FPredictionKey InPredictionKey);

	FActiveGameplayEffect* GetActiveGameplayEffect(const FActiveGameplayEffectHandle Handle);
		
	void ExecuteActiveEffectsFrom(FGameplayEffectSpec &Spec, FPredictionKey PredictionKey = FPredictionKey() );
	
	void ExecutePeriodicGameplayEffect(FActiveGameplayEffectHandle Handle);	// This should not be outward facing to the skill system API, should only be called by the owning AbilitySystemComponent

	bool RemoveActiveGameplayEffect(FActiveGameplayEffectHandle Handle, int32 StacksToRemove);
	
	float GetGameplayEffectDuration(FActiveGameplayEffectHandle Handle) const;

	float GetGameplayEffectMagnitude(FActiveGameplayEffectHandle Handle, FGameplayAttribute Attribute) const;

	void SetAttributeBaseValue(FGameplayAttribute Attribute, float NewBaseValue);

	/** Actually applies given mod to the attribute */
	void ApplyModToAttribute(const FGameplayAttribute &Attribute, TEnumAsByte<EGameplayModOp::Type> ModifierOp, float ModifierMagnitude, const FGameplayEffectModCallbackData* ModData=nullptr);

	/**
	 * Get the source tags from the gameplay spec represented by the specified handle, if possible
	 * 
	 * @param Handle	Handle of the gameplay effect to retrieve source tags from
	 * 
	 * @return Source tags from the gameplay spec represented by the handle, if possible
	 */
	const FGameplayTagContainer* GetGameplayEffectSourceTagsFromHandle(FActiveGameplayEffectHandle Handle) const;

	/**
	 * Get the target tags from the gameplay spec represented by the specified handle, if possible
	 * 
	 * @param Handle	Handle of the gameplay effect to retrieve target tags from
	 * 
	 * @return Target tags from the gameplay spec represented by the handle, if possible
	 */
	const FGameplayTagContainer* GetGameplayEffectTargetTagsFromHandle(FActiveGameplayEffectHandle Handle) const;

	/**
	 * Populate the specified capture spec with the data necessary to capture an attribute from the container
	 * 
	 * @param OutCaptureSpec	[OUT] Capture spec to populate with captured data
	 */
	void CaptureAttributeForGameplayEffect(OUT FGameplayEffectAttributeCaptureSpec& OutCaptureSpec);

	void PrintAllGameplayEffects() const;

	int32 GetNumGameplayEffects() const
	{
		return GameplayEffects.Num();
	}

	void CheckDuration(FActiveGameplayEffectHandle Handle);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms)
	{
		return FastArrayDeltaSerialize<FActiveGameplayEffect>(GameplayEffects, DeltaParms, *this);
	}

	void PreDestroy();	

	// ------------------------------------------------

	bool CanApplyAttributeModifiers(const UGameplayEffect *GameplayEffect, float Level, const FGameplayEffectContextHandle& EffectContext);
	
	TArray<float> GetActiveEffectsTimeRemaining(const FActiveGameplayEffectQuery Query) const;

	TArray<float> GetActiveEffectsDuration(const FActiveGameplayEffectQuery Query) const;

	void RemoveActiveEffects(const FActiveGameplayEffectQuery Query, int32 StacksToRemove);

	int32 GetGameStateTime() const;

	float GetWorldTime() const;

	bool HasReceivedEffectWithPredictedKey(FPredictionKey PredictionKey) const;

	bool HasPredictedEffectWithPredictedKey(FPredictionKey PredictionKey) const;
		
	void SetBaseAttributeValueFromReplication(FGameplayAttribute Attribute, float BaseBalue);

	// -------------------------------------------------------------------------------------------

	FOnGameplayAttributeChange& RegisterGameplayAttributeEvent(FGameplayAttribute Attribute);

	void OnOwnerTagChange(FGameplayTag TagChange, int32 NewCount);

	bool HasApplicationImmunityToSpec(const FGameplayEffectSpec& SpecToApply) const;

	void IncrementLock();
	void DecrementLock();

private:

	UPROPERTY()
	TArray<FActiveGameplayEffect>	GameplayEffects;

	void InternalUpdateNumericalAttribute(FGameplayAttribute Attribute, float NewValue, const FGameplayEffectModCallbackData* ModData);
	
	/**
	 * Helper function to execute a mod on owned attributes
	 * 
	 * @param Spec			Gameplay effect spec executing the mod
	 * @param ModEvalData	Evaluated data for the mod
	 * 
	 * @return True if the mod successfully executed, false if it did not
	 */
	bool InternalExecuteMod(FGameplayEffectSpec& Spec, FGameplayModifierEvaluatedData& ModEvalData);

	bool IsNetAuthority() const;

	/** Called internally to actually remove a GameplayEffect or to reduce its StackCount. Returns true if we resized our internal GameplayEffect array. */
	bool InternalRemoveActiveGameplayEffect(int32 Idx, int32 StacksToRemove, bool bPrematureRemoval);
	
	/** Called both in server side creation and replication creation/deletion */
	void InternalOnActiveGameplayEffectAdded(FActiveGameplayEffect& Effect);
	void InternalOnActiveGameplayEffectRemoved(const FActiveGameplayEffect& Effect);

	void RemoveActiveGameplayEffectGrantedTagsAndModifiers(const FActiveGameplayEffect& Effect);
	void AddActiveGameplayEffectGrantedTagsAndModifiers(FActiveGameplayEffect& Effect);

	/** Updates tag dependency map when a GameplayEffect is removed */
	void RemoveActiveEffectTagDependency(const FGameplayTagContainer& Tags, FActiveGameplayEffectHandle Handle);

	/** Internal helper function to apply expiration effects from a removed/expired gameplay effect spec */
	void InternalApplyExpirationEffects(const FGameplayEffectSpec& ExpiringSpec, bool bPrematureRemoval);

	// -------------------------------------------------------------------------------------------

	TMap<FGameplayAttribute, FAggregatorRef>		AttributeAggregatorMap;

	TMap<FGameplayAttribute, FOnGameplayAttributeChange> AttributeChangeDelegates;

	TMap<FGameplayTag, TSet<FActiveGameplayEffectHandle> >	ActiveEffectTagDependencies;

	/** A map to manage stacking while we are the source */
	TMap<TWeakObjectPtr<UGameplayEffect>, TArray<FActiveGameplayEffectHandle> >	SourceStackingMap;

	FGameplayTagCountContainer ApplicationImmunityGameplayTagCountContainer;

	FAggregatorRef& FindOrCreateAttributeAggregator(FGameplayAttribute Attribute);

	void OnAttributeAggregatorDirty(FAggregator* Aggregator, FGameplayAttribute Attribute);

	void OnMagnitudeDependencyChange(FActiveGameplayEffectHandle Handle, const FAggregator* ChangedAgg);

	void OnStackCountChange(FActiveGameplayEffect& ActiveEffect);

	void UpdateAllAggregatorModMagnitudes(FActiveGameplayEffect& ActiveEffect);

	void UpdateAggregatorModMagnitudes(const TSet<FGameplayAttribute>& AttributesToUpdate, FActiveGameplayEffect& ActiveEffect);

	/** Helper function to find the active GE that the specified spec can stack with, if any */
	FActiveGameplayEffect* FindStackableActiveGameplayEffect(const FGameplayEffectSpec& Spec);

	/** Helper function to compute the modified duration of a specified spec & base duration value */
	float ComputeModifiedDurationOfAppliedSpec(const FGameplayEffectSpec& Spec, float BaseValue) const;
	
	/** Helper function to handle the case of same-effect stacking overflow; Returns true if the overflow application should apply, false if it should not */
	bool HandleActiveGameplayEffectStackOverflow(const FActiveGameplayEffect& ActiveStackableGE, const FGameplayEffectSpec& OldSpec, const FGameplayEffectSpec& OverflowingSpec);

	/** After application has gone through, give stacking rules a chance to do something as the source of the gameplay effect (E.g., remove an old version) */
	void ApplyStackingLogicPostApplyAsSource(UAbilitySystemComponent* Target, const FGameplayEffectSpec& SpecApplied, FActiveGameplayEffectHandle ActiveHandle);

	mutable int32 ScopedLockCount;
	int32 PendingRemoves;
};

template<>
struct TStructOpsTypeTraits< FActiveGameplayEffectsContainer > : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};


struct GAMEPLAYABILITIES_API FScopedActiveGameplayEffectLock
{
	FScopedActiveGameplayEffectLock(FActiveGameplayEffectsContainer& InContainer);
	~FScopedActiveGameplayEffectLock();

private:
	FActiveGameplayEffectsContainer& Container;
};


#define GAMEPLAYEFFECT_SCOPE_LOCK()	FScopedActiveGameplayEffectLock ActiveScopeLock(*this);
