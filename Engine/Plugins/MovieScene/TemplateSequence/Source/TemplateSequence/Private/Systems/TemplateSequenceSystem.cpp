// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/TemplateSequenceSystem.h"
#include "IMovieScenePlaybackClient.h"
#include "IMovieScenePlayer.h"
#include "TemplateSequence.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "Sections/TemplateSequenceSection.h"
#include "EntitySystem/MovieSceneSpawnablesSystem.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"

namespace UE
{
namespace MovieScene
{

static TUniquePtr<FTemplateSequenceComponentTypes> GTemplateSequenceComponentTypes;

FTemplateSequenceComponentTypes* FTemplateSequenceComponentTypes::Get()
{
	if (!GTemplateSequenceComponentTypes.IsValid())
	{
		GTemplateSequenceComponentTypes.Reset(new FTemplateSequenceComponentTypes);
	}
	return GTemplateSequenceComponentTypes.Get();
}

FTemplateSequenceComponentTypes::FTemplateSequenceComponentTypes()
{
	using namespace UE::MovieScene;

	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	ComponentRegistry->NewComponentType(&TemplateSequence, TEXT("Template Sequence"));

	ComponentRegistry->Factories.DuplicateChildComponent(TemplateSequence);
}

} // namespace MovieScene
} // namespace UE

UTemplateSequenceSystem::UTemplateSequenceSystem(const FObjectInitializer& ObjInit)
	: UMovieSceneEntitySystem(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FTemplateSequenceComponentTypes* TemplateSequenceComponents = FTemplateSequenceComponentTypes::Get();

	Phase = UE::MovieScene::ESystemPhase::Spawn;
	RelevantComponent = TemplateSequenceComponents->TemplateSequence;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(GetClass(), UMovieSceneSpawnablesSystem::StaticClass());
	}

	// We only need to run if there are template sequence sections starting or stopping.
	ApplicableFilter.Filter.All({ TemplateSequenceComponents->TemplateSequence });
	ApplicableFilter.Filter.Any({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->Tags.NeedsUnlink });
}

void UTemplateSequenceSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	// Only run if we must
	if (!ApplicableFilter.Matches(Linker->EntityManager))
	{
		return;
	}

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FTemplateSequenceComponentTypes* TemplateSequenceComponents = FTemplateSequenceComponentTypes::Get();

	FInstanceRegistry* InstanceRegistry  = Linker->GetInstanceRegistry();

	auto SetupTeardownBindingOverrides = [BuiltInComponents, InstanceRegistry](
			FEntityAllocationIteratorItem AllocationItem,
			TRead<FInstanceHandle> InstanceHandles,
			TRead<FGuid> ObjectBindingIDs,
			TRead<FTemplateSequenceComponentData> TemplateSequenceDatas)
	{
		const FComponentMask& Mask = AllocationItem.GetAllocationType();
		const bool bHasNeedsLink   = Mask.Contains(BuiltInComponents->Tags.NeedsLink);
		const bool bHasNeedsUnlink = Mask.Contains(BuiltInComponents->Tags.NeedsUnlink);

		const int32 Num = AllocationItem.GetAllocation()->Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(InstanceHandles[Index]);
			const FGuid& ObjectBindingID = ObjectBindingIDs[Index];
			const FTemplateSequenceComponentData& TemplateSequenceData = TemplateSequenceDatas[Index];

			IMovieScenePlayer* Player = SequenceInstance.GetPlayer();
			if (ensure(Player))
			{
				if (bHasNeedsLink)
				{
					const FMovieSceneSequenceID SequenceID = SequenceInstance.GetSequenceID();
					const FMovieSceneEvaluationOperand OuterOperand(SequenceID, ObjectBindingID);
					Player->BindingOverrides.Add(TemplateSequenceData.InnerOperand, OuterOperand);
				}
				else if (bHasNeedsUnlink)
				{
					Player->BindingOverrides.Remove(TemplateSequenceData.InnerOperand);
				}
			}
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(BuiltInComponents->GenericObjectBinding)
		.Read(TemplateSequenceComponents->TemplateSequence)
		.FilterAny({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->Tags.NeedsUnlink })
		.Iterate_PerAllocation(&Linker->EntityManager, SetupTeardownBindingOverrides);
}
