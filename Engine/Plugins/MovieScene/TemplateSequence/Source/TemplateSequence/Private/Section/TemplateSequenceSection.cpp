// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/TemplateSequenceSection.h"
#include "TemplateSequence.h"
#include "Systems/TemplateSequenceSystem.h"

UTemplateSequenceSection::UTemplateSequenceSection()
{
	SetBlendType(EMovieSceneBlendType::Absolute);
}

void UTemplateSequenceSection::OnDilated(float DilationFactor, FFrameNumber Origin)
{
	// TODO-lchabant: shouldn't this be in the base class?
	Parameters.TimeScale /= DilationFactor;
}

void UTemplateSequenceSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	FTemplateSequenceComponentData ComponentData;

	if (UTemplateSequence* TemplateSubSequence = Cast<UTemplateSequence>(GetSequence()))
	{
		const FMovieSceneObjectBindingID InnerObjectBindingID(
				TemplateSubSequence->GetRootObjectBindingID(), GetSequenceID(), EMovieSceneObjectBindingSpace::Local);

		const FSequenceInstance& SequenceInstance = EntityLinker->GetInstanceRegistry()->GetInstance(Params.Sequence.InstanceHandle);
		const FMovieSceneObjectBindingID AbsoluteInnerObjectBindingID(
				InnerObjectBindingID.ResolveLocalToRoot(SequenceInstance.GetSequenceID(), *SequenceInstance.GetPlayer()));

		ComponentData.InnerOperand = FMovieSceneEvaluationOperand(
				AbsoluteInnerObjectBindingID.GetSequenceID(), AbsoluteInnerObjectBindingID.GetGuid());
	}

	FGuid ObjectBindingID = Params.GetObjectBindingID();

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.AddConditional(FBuiltInComponentTypes::Get()->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
		.Add(FTemplateSequenceComponentTypes::Get()->TemplateSequence, ComponentData)
	);

	BuildDefaultSubSectionComponents(EntityLinker, Params, OutImportedEntity);
}
