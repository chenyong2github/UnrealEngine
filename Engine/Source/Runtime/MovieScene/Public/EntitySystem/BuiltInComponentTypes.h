// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityManager.h"
#include "UObject/GCObjectScopeGuard.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Evaluation/Blending/MovieSceneBlendType.h"
#include "Evaluation/IMovieSceneEvaluationHook.h"
#include "Templates/SubclassOf.h"

#include "EntitySystem/MovieScenePropertyRegistry.h"
#include "EntitySystem/MovieSceneInitialValueCache.h"

#include "BuiltInComponentTypes.generated.h"

enum class EMovieSceneBlendType : uint8;
struct FMovieSceneFloatChannel;
struct FMovieScenePropertyBinding;

class UMovieSceneSection;
class UMovieSceneTrackInstance;
class UMovieSceneBlenderSystem;
class FTrackInstancePropertyBindings;


/**
 * Easing component data.
 */
USTRUCT()
struct FEasingComponentData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMovieSceneSection> Section = nullptr;
};


/**
 * A component that defines a type for a track instance
 */
USTRUCT()
struct FMovieSceneTrackInstanceComponent
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMovieSceneSection> Owner = nullptr;

	UPROPERTY()
	TSubclassOf<UMovieSceneTrackInstance> TrackInstanceClass;
};


/**
 * A component that defines a hook for direct evaluation
 */
USTRUCT()
struct FMovieSceneEvaluationHookComponent
{
	GENERATED_BODY()

	UPROPERTY()
	TScriptInterface<IMovieSceneEvaluationHook> Interface;

	FGuid ObjectBindingID;
};



USTRUCT()
struct FTrackInstanceInputComponent
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMovieSceneSection> Section = nullptr;

	UPROPERTY()
	int32 OutputIndex = INDEX_NONE;
};


namespace UE
{
namespace MovieScene
{


/**
 * The component data for evaluating a float channel
 */
struct FSourceFloatChannel
{
	FSourceFloatChannel()
		: Source(nullptr)
	{}

	FSourceFloatChannel(const FMovieSceneFloatChannel* InSource)
		: Source(InSource)
	{}

	const FMovieSceneFloatChannel* Source;
};


struct FSourceFloatChannelFlags
{
	bool bNeedsEvaluate = true;
};

struct FEvaluationHookFlags
{
	bool bHasBegun = false;
};

/**
 * Pre-defined built in component types
 */
struct MOVIESCENE_API FBuiltInComponentTypes
{
	~FBuiltInComponentTypes();

public:

	FPropertyRegistry PropertyRegistry;

public:

	TComponentTypeID<FMovieSceneEntityID> ParentEntity;

	TComponentTypeID<UObject*>            BoundObject;

	TComponentTypeID<FInstanceHandle>     InstanceHandle;

	TComponentTypeID<FFrameTime>          EvalTime;

public:

	TComponentTypeID<uint16>              BlendChannelInput;

	TComponentTypeID<int16>               HierarchicalBias;

	TComponentTypeID<uint16>              BlendChannelOutput;

	TComponentTypeID<FInitialValueIndex>  InitialValueIndex;
public:

	// An FMovieScenePropertyBinding structure
	TComponentTypeID<FMovieScenePropertyBinding> PropertyBinding;

	// An FGuid relating to a direct object binding in a sequence
	TComponentTypeID<FGuid> GenericObjectBinding;

	// An FGuid that is always resolved as a USceneComponent either directly or through the AActor that the GUID relates to
	TComponentTypeID<FGuid> SceneComponentBinding;

	// An FGuid relating to a spawnable binding in a sequence
	TComponentTypeID<FGuid> SpawnableBinding;

public:

	// A boolean repesenting the output of a bool property track or channel
	TComponentTypeID<bool> BoolResult;

	// An FMovieSceneFloatChannel considered to be at index N within the source structure (ie 0 = Location.X, Vector.X, Color.R; 1 = Location.Y, Vector.Y, Color.G)
	TComponentTypeID<FSourceFloatChannel> FloatChannel[9];
	TComponentTypeID<FSourceFloatChannelFlags> FloatChannelFlags[9];

	// An FMovieSceneFloatChannel that represents an arbitrary weight
	TComponentTypeID<FSourceFloatChannel> WeightChannel;
	TComponentTypeID<FSourceFloatChannelFlags> WeightChannelFlags;

	// A float representing the output of the channel considered to be at index N within the source structure (ie 0 = Location.X, Vector.X, Color.R; 1 = Location.Y, Vector.Y, Color.G)
	TComponentTypeID<float> FloatResult[9];

	// A float representing the base value for the float channel at index N, for the purposes of "additive from base" blending.
	TComponentTypeID<float> BaseFloat[9];

	// The time at which to evaluate a base value, such as BaseFloat[].
	TComponentTypeID<FFrameTime> BaseValueEvalTime;

	// A float representing the evaluated output of a weight channel
	TComponentTypeID<float> WeightResult;

public:

	// An FEasingComponentData for computing easing curves
	TComponentTypeID<FEasingComponentData> Easing;

	// An index associated to hierarchical easing for the owning sub-sequence
	TComponentTypeID<uint16> HierarchicalEasingChannel;

	// The sub-sequence ID that should receive ease in/out as a whole
	TComponentTypeID<FMovieSceneSequenceID> HierarchicalEasingProvider;

	// A float representing the evaluated easing weight
	TComponentTypeID<float> WeightAndEasingResult;

	/** A blender type that should be used for blending this entity */
	TComponentTypeID<TSubclassOf<UMovieSceneBlenderSystem>> BlenderType;

	// An FMovieSceneTrackInstanceComponent that defines the track instance to use
	TComponentTypeID<FMovieSceneTrackInstanceComponent> TrackInstance;

	// An FTrackInstanceInputComponent that defines an input for a track instance
	TComponentTypeID<FTrackInstanceInputComponent> TrackInstanceInput;

	// An FMovieSceneEvaluationHookComponent that defines a stateless hook interface that doesn't need any overlap handling (track instances should be preferred there)
	TComponentTypeID<FMovieSceneEvaluationHookComponent> EvaluationHook;

	TComponentTypeID<FEvaluationHookFlags> EvaluationHookFlags;

public:

	// 
	TComponentTypeID<FCustomPropertyIndex> CustomPropertyIndex;

	// A property offset from a UObject* that points to the memory for a given property - care should be taken to ensure that this is only ever accessed in conjunction with a property tag
	TComponentTypeID<uint16> FastPropertyOffset;

	// A property binding that supports setters and notifications
	TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>> SlowProperty;

	struct
	{
		// A tag specifying that an entity wants to restore state on completioon
		FComponentTypeID RestoreState;

		FComponentTypeID AbsoluteBlend;
		FComponentTypeID RelativeBlend;
		FComponentTypeID AdditiveBlend;
		FComponentTypeID AdditiveFromBaseBlend;

		FComponentTypeID NeedsLink;
		FComponentTypeID NeedsUnlink;

		FComponentTypeID MigratedFromFastPath;
		FComponentTypeID CachePreAnimatedValue;

		FComponentTypeID ImportedEntity;
		FComponentTypeID Master;

		FComponentTypeID FixedTime;

		FComponentTypeID SectionPreRoll;
		FComponentTypeID PreRoll;

		FComponentTypeID Finished;

		FComponentTypeID Ignored;

	} Tags;

	struct
	{
		TComponentTypeID<FInterrogationKey> InputKey;
		TComponentTypeID<FInterrogationKey> OutputKey;
	} Interrogation;

	struct
	{
		FComponentTypeID CreatesEntities;
	} SymbolicTags;

	FComponentMask FinishedMask;

	static void Destroy();

	static FBuiltInComponentTypes* Get();

	FORCEINLINE static bool IsBoundObjectGarbage(UObject* InObject)
	{
		return InObject == nullptr || InObject->IsPendingKillOrUnreachable();
	}

private:
	FBuiltInComponentTypes();
};


} // namespace MovieScene
} // namespace UE
