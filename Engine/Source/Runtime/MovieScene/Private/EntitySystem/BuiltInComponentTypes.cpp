// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/BuiltInComponentTypes.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieScenePropertyBinding.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"

namespace UE
{
namespace MovieScene
{

static bool GMovieSceneBuiltInComponentTypesDestroyed = false;
static TUniquePtr<FBuiltInComponentTypes> GMovieSceneBuiltInComponentTypes;

FBuiltInComponentTypes::FBuiltInComponentTypes()
{
	using namespace UE::MovieScene;

	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	ComponentRegistry->NewComponentType(&ParentEntity,          TEXT("Parent Entity"));
	ComponentRegistry->NewComponentType(&InstanceHandle,        TEXT("Instance Handle"));
	ComponentRegistry->NewComponentType(&RootInstanceHandle,    TEXT("Root Instance Handle"));

	ComponentRegistry->NewComponentType(&EvalTime,              TEXT("Eval Time"));

	ComponentRegistry->NewComponentType(&BoundObject,           TEXT("Bound Object"));

	ComponentRegistry->NewComponentType(&PropertyBinding,         TEXT("Property Binding"), EComponentTypeFlags::CopyToOutput);
	ComponentRegistry->NewComponentType(&GenericObjectBinding,    TEXT("Generic Object Binding ID"));
	ComponentRegistry->NewComponentType(&SceneComponentBinding,   TEXT("USceneComponent Binding ID"));
	ComponentRegistry->NewComponentType(&SpawnableBinding,        TEXT("Spawnable Binding"));
	ComponentRegistry->NewComponentType(&TrackInstance,           TEXT("Track Instance"));
	ComponentRegistry->NewComponentType(&ByteChannel,             TEXT("Byte Channel"));
	ComponentRegistry->NewComponentType(&IntegerChannel,          TEXT("Integer Channel"));
	ComponentRegistry->NewComponentType(&FloatChannel[0],         TEXT("Float Channel 0"));
	ComponentRegistry->NewComponentType(&FloatChannel[1],         TEXT("Float Channel 1"));
	ComponentRegistry->NewComponentType(&FloatChannel[2],         TEXT("Float Channel 2"));
	ComponentRegistry->NewComponentType(&FloatChannel[3],         TEXT("Float Channel 3"));
	ComponentRegistry->NewComponentType(&FloatChannel[4],         TEXT("Float Channel 4"));
	ComponentRegistry->NewComponentType(&FloatChannel[5],         TEXT("Float Channel 5"));
	ComponentRegistry->NewComponentType(&FloatChannel[6],         TEXT("Float Channel 6"));
	ComponentRegistry->NewComponentType(&FloatChannel[7],         TEXT("Float Channel 7"));
	ComponentRegistry->NewComponentType(&FloatChannel[8],         TEXT("Float Channel 8"));
	ComponentRegistry->NewComponentType(&WeightChannel,           TEXT("Weight Channel"));

	ComponentRegistry->NewComponentType(&FloatChannelFlags[0],    TEXT("Float Channel 0 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[1],    TEXT("Float Channel 1 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[2],    TEXT("Float Channel 2 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[3],    TEXT("Float Channel 3 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[4],    TEXT("Float Channel 4 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[5],    TEXT("Float Channel 5 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[6],    TEXT("Float Channel 6 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[7],    TEXT("Float Channel 7 Flags"));
	ComponentRegistry->NewComponentType(&FloatChannelFlags[8],    TEXT("Float Channel 8 Flags"));
	ComponentRegistry->NewComponentType(&WeightChannelFlags,      TEXT("Weight Channel Flags"));

	ComponentRegistry->NewComponentType(&Easing,                  TEXT("Easing"));
	ComponentRegistry->NewComponentType(&HierarchicalEasingChannel, TEXT("Hierarchical Easing Channel"));
	ComponentRegistry->NewComponentType(&HierarchicalEasingProvider, TEXT("Hierarchical Easing Provider"));

	ComponentRegistry->NewComponentType(&BlenderType,           TEXT("Blender System Type"), EComponentTypeFlags::CopyToChildren);
	ComponentRegistry->NewComponentType(&BlendChannelInput,     TEXT("Blend Channel Input"));
	ComponentRegistry->NewComponentType(&HierarchicalBias,      TEXT("Hierarchical Bias"));
	ComponentRegistry->NewComponentType(&BlendChannelOutput,    TEXT("Blend Channel Output"));
	ComponentRegistry->NewComponentType(&InitialValueIndex,     TEXT("Initial Value Index"));

	ComponentRegistry->NewComponentType(&CustomPropertyIndex,   TEXT("Custom Property Index"));			// Not EComponentTypeFlags::Preserved because the system property manager will always ensure that the component is added to the correct entity
	ComponentRegistry->NewComponentType(&FastPropertyOffset,    TEXT("Fast Property Offset"));			// Not EComponentTypeFlags::Preserved because the system property manager will always ensure that the component is added to the correct entity
	ComponentRegistry->NewComponentType(&SlowProperty,          TEXT("Slow Property Binding"));			// Not EComponentTypeFlags::Preserved because the system property manager will always ensure that the component is added to the correct entity
	ComponentRegistry->NewComponentType(&BoolResult,            TEXT("Bool Result"));
	ComponentRegistry->NewComponentType(&ByteResult,            TEXT("Byte Result"));
	ComponentRegistry->NewComponentType(&IntegerResult,         TEXT("Integer Result"));
	ComponentRegistry->NewComponentType(&FloatResult[0],        TEXT("Float Result 0"));
	ComponentRegistry->NewComponentType(&FloatResult[1],        TEXT("Float Result 1"));
	ComponentRegistry->NewComponentType(&FloatResult[2],        TEXT("Float Result 2"));
	ComponentRegistry->NewComponentType(&FloatResult[3],        TEXT("Float Result 3"));
	ComponentRegistry->NewComponentType(&FloatResult[4],        TEXT("Float Result 4"));
	ComponentRegistry->NewComponentType(&FloatResult[5],        TEXT("Float Result 5"));
	ComponentRegistry->NewComponentType(&FloatResult[6],        TEXT("Float Result 6"));
	ComponentRegistry->NewComponentType(&FloatResult[7],        TEXT("Float Result 7"));
	ComponentRegistry->NewComponentType(&FloatResult[8],        TEXT("Float Result 8"));

	ComponentRegistry->NewComponentType(&BaseInteger,			TEXT("Base Integer"));
	ComponentRegistry->NewComponentType(&BaseFloat[0],          TEXT("Base Float 0"));
	ComponentRegistry->NewComponentType(&BaseFloat[1],          TEXT("Base Float 1"));
	ComponentRegistry->NewComponentType(&BaseFloat[2],          TEXT("Base Float 2"));
	ComponentRegistry->NewComponentType(&BaseFloat[3],          TEXT("Base Float 3"));
	ComponentRegistry->NewComponentType(&BaseFloat[4],          TEXT("Base Float 4"));
	ComponentRegistry->NewComponentType(&BaseFloat[5],          TEXT("Base Float 5"));
	ComponentRegistry->NewComponentType(&BaseFloat[6],          TEXT("Base Float 6"));
	ComponentRegistry->NewComponentType(&BaseFloat[7],          TEXT("Base Float 7"));
	ComponentRegistry->NewComponentType(&BaseFloat[8],          TEXT("Base Float 8"));

	ComponentRegistry->NewComponentType(&BaseValueEvalTime,     TEXT("Base Value Eval Time"));

	ComponentRegistry->NewComponentType(&WeightResult,          TEXT("Weight Result"));
	ComponentRegistry->NewComponentType(&WeightAndEasingResult, TEXT("Weight/Easing Result"));

	ComponentRegistry->NewComponentType(&TrackInstance,         TEXT("Track Instance"));
	ComponentRegistry->NewComponentType(&TrackInstanceInput,    TEXT("Track Instance Input"));

	ComponentRegistry->NewComponentType(&EvaluationHook,        TEXT("Evaluation Hook"));
	ComponentRegistry->NewComponentType(&EvaluationHookFlags,   TEXT("Evaluation Hook Flags"), EComponentTypeFlags::Preserved);

	ComponentRegistry->NewComponentType(&Interrogation.InputKey,  TEXT("Interrogation Input"));
	ComponentRegistry->NewComponentType(&Interrogation.OutputKey, TEXT("Interrogation Output"));

	Tags.RestoreState            = ComponentRegistry->NewTag(TEXT("Is Restore State Entity"));
	Tags.AbsoluteBlend           = ComponentRegistry->NewTag(TEXT("Is Absolute Blend"));
	Tags.RelativeBlend           = ComponentRegistry->NewTag(TEXT("Is Relative Blend"));
	Tags.AdditiveBlend           = ComponentRegistry->NewTag(TEXT("Is Additive Blend"));
	Tags.AdditiveFromBaseBlend   = ComponentRegistry->NewTag(TEXT("Is Additive From Base Blend"));

	Tags.NeedsLink               = ComponentRegistry->NewTag(TEXT("Needs Link"));
	Tags.NeedsUnlink             = ComponentRegistry->NewTag(TEXT("Needs Unlink"));
	Tags.MigratedFromFastPath    = ComponentRegistry->NewTag(TEXT("Migrated From Fast Path"));
	Tags.Master                  = ComponentRegistry->NewTag(TEXT("Master"));
	Tags.ImportedEntity          = ComponentRegistry->NewTag(TEXT("Imported Entity"));
	Tags.Finished                = ComponentRegistry->NewTag(TEXT("Finished Evaluating"));
	Tags.Ignored                 = ComponentRegistry->NewTag(TEXT("Ignored"));
	Tags.FixedTime               = ComponentRegistry->NewTag(TEXT("Fixed Time"));
	Tags.PreRoll                 = ComponentRegistry->NewTag(TEXT("Pre Roll"));
	Tags.SectionPreRoll          = ComponentRegistry->NewTag(TEXT("Section Pre Roll"));

	SymbolicTags.CreatesEntities = ComponentRegistry->NewTag(TEXT("~~ SYMBOLIC ~~ Creates Entities"));

	FinishedMask.SetAll({ Tags.NeedsUnlink, Tags.Finished });

	// New children always need link
	ComponentRegistry->Factories.DefineChildComponent(Tags.NeedsLink);

	// Always copy these tags over to children
	ComponentRegistry->Factories.DefineChildComponent(Tags.RestoreState,  Tags.RestoreState);
	ComponentRegistry->Factories.DefineChildComponent(Tags.AbsoluteBlend, Tags.AbsoluteBlend);
	ComponentRegistry->Factories.DefineChildComponent(Tags.RelativeBlend, Tags.RelativeBlend);
	ComponentRegistry->Factories.DefineChildComponent(Tags.AdditiveBlend, Tags.AdditiveBlend);
	ComponentRegistry->Factories.DefineChildComponent(Tags.AdditiveFromBaseBlend, Tags.AdditiveFromBaseBlend);
	ComponentRegistry->Factories.DefineChildComponent(Tags.FixedTime,     Tags.FixedTime);
	ComponentRegistry->Factories.DefineChildComponent(Tags.PreRoll,       Tags.PreRoll);
	ComponentRegistry->Factories.DefineChildComponent(Tags.SectionPreRoll,Tags.SectionPreRoll);

	ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(Tags.SectionPreRoll,Tags.PreRoll);

	ComponentRegistry->Factories.DuplicateChildComponent(EvalTime);
	ComponentRegistry->Factories.DuplicateChildComponent(BaseValueEvalTime);

	ComponentRegistry->Factories.DuplicateChildComponent(InstanceHandle);
	ComponentRegistry->Factories.DuplicateChildComponent(RootInstanceHandle);
	ComponentRegistry->Factories.DuplicateChildComponent(PropertyBinding);
	ComponentRegistry->Factories.DuplicateChildComponent(HierarchicalBias);

	// Children always need a Parent
	{
		struct FParentEntityInitializer : TChildEntityInitializer<FMovieSceneEntityID, FMovieSceneEntityID>
		{
			explicit FParentEntityInitializer(TComponentTypeID<FMovieSceneEntityID> ParentEntity)
				: TChildEntityInitializer(TComponentTypeID<FMovieSceneEntityID>(), ParentEntity)
			{}

			virtual void Run(const FEntityRange& ChildRange, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets) override
			{
				TArrayView<const FMovieSceneEntityID> ParentIDs       = ParentAllocation->GetEntityIDs();
				TComponentWriter<FMovieSceneEntityID> ChildComponents = GetChildComponents(ChildRange.Allocation);

				for (int32 Index = 0; Index < ChildRange.Num; ++Index)
				{
					const int32 ParentIndex = ParentAllocationOffsets[Index];
					const int32 ChildIndex  = ChildRange.ComponentStartOffset + Index;

					ChildComponents[ChildIndex] = ParentIDs[ParentIndex];
				}
			}
		};
		ComponentRegistry->Factories.DefineChildComponent(FParentEntityInitializer(ParentEntity));
	}
	
	// Bool channel relationships
	{
		ComponentRegistry->Factories.DuplicateChildComponent(BoolResult);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(BoolResult, EvalTime);
	}

	// Byte channel relationships
	{
		ComponentRegistry->Factories.DuplicateChildComponent(ByteChannel);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(ByteChannel, ByteResult);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(ByteChannel, EvalTime);
	}
	
	// Integer channel relationships
	{
		ComponentRegistry->Factories.DuplicateChildComponent(IntegerChannel);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(IntegerChannel, IntegerResult);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(IntegerChannel, EvalTime);

		ComponentRegistry->Factories.DefineComplexInclusiveComponents(
				FComplexInclusivityFilter::All({ IntegerChannel, BaseValueEvalTime, Tags.AdditiveFromBaseBlend }),
				BaseInteger);
	}

	// Float channel relationships
	{
		static_assert(
				UE_ARRAY_COUNT(FloatChannel) == UE_ARRAY_COUNT(BaseFloat), 
				"Base floats and float results should have the same size.");

		// Duplicate float channels
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FloatChannel); ++Index)
		{
			ComponentRegistry->Factories.DuplicateChildComponent(FloatChannel[Index]);
			ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(FloatChannel[Index], FloatResult[Index]);
			ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(FloatChannel[Index], EvalTime);
			ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(FloatChannel[Index], FloatChannelFlags[Index]);
		}

		// Create base float components for float channels that are meant to be additive from base.
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(BaseFloat); ++Index)
		{
			ComponentRegistry->Factories.DefineComplexInclusiveComponents(
					FComplexInclusivityFilter::All({ FloatChannel[Index], BaseValueEvalTime, Tags.AdditiveFromBaseBlend }),
					BaseFloat[Index]);
		}
	}

	// Easing component relationships
	{
		// Easing components should be duplicated to children
		ComponentRegistry->Factories.DuplicateChildComponent(Easing);
		ComponentRegistry->Factories.DuplicateChildComponent(HierarchicalEasingChannel);
		ComponentRegistry->Factories.DuplicateChildComponent(HierarchicalEasingProvider);

		// Easing needs a time to evaluate
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(Easing, EvalTime);
	}

	// Weight channel relationships
	{
		// Weight channel components should be duplicated to children
		ComponentRegistry->Factories.DuplicateChildComponent(WeightChannel);

		// Weight channel components need a time and result to evaluate
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(WeightChannel, EvalTime);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(WeightChannel, WeightResult);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(WeightResult, WeightChannelFlags);
	}

	// Weight and easing result component relationship
	{
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(Easing, WeightAndEasingResult);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(HierarchicalEasingChannel, WeightAndEasingResult);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(WeightResult, WeightAndEasingResult);
	}

	// Track instances always produce inputs
	{
		auto InitInput = [](const FMovieSceneTrackInstanceComponent& InInstance, FTrackInstanceInputComponent& OutInput)
		{
			OutInput.Section = InInstance.Owner;
		};
		ComponentRegistry->Factories.DefineChildComponent(TrackInstance, TrackInstanceInput, InitInput);
	}

	{
		ComponentRegistry->Factories.DefineChildComponent(EvaluationHook, EvaluationHook);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(EvaluationHook, EvalTime);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(EvaluationHook, EvaluationHookFlags);
	}
}

FBuiltInComponentTypes::~FBuiltInComponentTypes()
{
}

void FBuiltInComponentTypes::Destroy()
{
	GMovieSceneBuiltInComponentTypes.Reset();
	GMovieSceneBuiltInComponentTypesDestroyed = true;
}

FBuiltInComponentTypes* FBuiltInComponentTypes::Get()
{
	if (!GMovieSceneBuiltInComponentTypes.IsValid())
	{
		check(!GMovieSceneBuiltInComponentTypesDestroyed);
		GMovieSceneBuiltInComponentTypes.Reset(new FBuiltInComponentTypes);
	}
	return GMovieSceneBuiltInComponentTypes.Get();
}


} // namespace MovieScene
} // namespace UE
