// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneMaterialParameterSystem.h"

#include "Systems/BoolChannelEvaluatorSystem.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "Systems/MovieSceneHierarchicalBiasSystem.h"
#include "Systems/MovieSceneMaterialSystem.h"

#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace UE::MovieScene
{

/** Apply scalar material parameters */
struct FApplyScalarParameters
{
	static void ForEachEntity(UObject* BoundMaterial, FName ParameterName, float InScalarValue)
	{
		// WARNING: BoundMaterial may be nullptr here
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BoundMaterial))
		{
			MID->SetScalarParameterValue(ParameterName, InScalarValue);
		}
		else if (UMaterialParameterCollectionInstance* MPCI = Cast<UMaterialParameterCollectionInstance>(BoundMaterial))
		{
			MPCI->SetScalarParameterValue(ParameterName, InScalarValue);
		}
	}
};

/** Apply vector material parameters */
struct FApplyVectorParameters
{
	static void ForEachAllocation(FEntityAllocationIteratorItem Item,
		TRead<UObject*> BoundMaterials,
		TReadOneOrMoreOf<FName, FName> VectorOrColorParameterNames,
		TReadOneOrMoreOf<float, float, float, float> VectorChannels)
	{
		const int32 Num = Item.GetAllocation()->Num();
		// Use either the vector parameter name, or the color parameter name
		const FName* ParameterNames = VectorOrColorParameterNames.Get<0>() ? VectorOrColorParameterNames.Get<0>() : VectorOrColorParameterNames.Get<1>();
		const float* RESTRICT R = VectorChannels.Get<0>();
		const float* RESTRICT G = VectorChannels.Get<1>();
		const float* RESTRICT B = VectorChannels.Get<2>();
		const float* RESTRICT A = VectorChannels.Get<3>();
		
		for (int32 Index = 0; Index < Num; ++Index)
		{
			UObject* BoundMaterial = BoundMaterials[Index];
			if (!BoundMaterial)
			{
				continue;
			}

			FLinearColor Color(
				R ? R[Index] : 0.f,
				G ? G[Index] : 0.f,
				B ? B[Index] : 0.f,
				A ? A[Index] : 1.f
			);
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BoundMaterial))
			{
				MID->SetVectorParameterValue(ParameterNames[Index], Color);
			}
			else if (UMaterialParameterCollectionInstance* MPCI = Cast<UMaterialParameterCollectionInstance>(BoundMaterial))
			{
				MPCI->SetVectorParameterValue(ParameterNames[Index], Color);
			}
		}
	}
};

/** Handler that manages creation of blend outputs where there are multiple contributors for the same material parameter */
struct FOverlappingMaterialParameterHandler
{
	UMovieSceneEntitySystemLinker* Linker;
	UMovieSceneMaterialParameterSystem* System;
	FComponentMask DefaultComponentMask;

	FOverlappingMaterialParameterHandler(UMovieSceneMaterialParameterSystem* InSystem)
		: Linker(InSystem->GetLinker())
		, System(InSystem)
	{}

	void InitializeOutput(UObject* BoundMaterial, FName ParameterName, TArrayView<const FMovieSceneEntityID> Inputs, FAnimatedMaterialParameterInfo* Output, FEntityOutputAggregate Aggregate)
	{
		UpdateOutput(BoundMaterial, ParameterName, Inputs, Output, Aggregate);
	}

	void UpdateOutput(UObject* BoundMaterial, FName ParameterName, TArrayView<const FMovieSceneEntityID> Inputs, FAnimatedMaterialParameterInfo* Output, FEntityOutputAggregate Aggregate)
	{
		if (BoundMaterial == nullptr)
		{
			return;
		}

		using namespace UE::MovieScene;

		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
		FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

		const int32 NumContributors = Inputs.Num();
		if (NumContributors > 1)
		{
			if (!Output->OutputEntityID)
			{
				if (!System->FloatBlenderSystem)
				{
					System->FloatBlenderSystem = Linker->LinkSystem<UMovieScenePiecewiseFloatBlenderSystem>();
					Linker->SystemGraph.AddReference(System, System->FloatBlenderSystem);
				}

				// Initialize the blend channel ID
				Output->BlendChannelID = System->FloatBlenderSystem->AllocateBlendChannel();

				// Needs blending
				Output->OutputEntityID = FEntityBuilder()
				.Add(TracksComponents->BoundMaterial, BoundMaterial)
				.Add(BuiltInComponents->BlendChannelOutput, Output->BlendChannelID)
				.AddTag(BuiltInComponents->Tags.NeedsLink)
				.AddMutualComponents()
				.CreateEntity(&Linker->EntityManager, DefaultComponentMask);

				Linker->EntityManager.CopyComponents(Inputs[0], Output->OutputEntityID, Linker->EntityManager.GetComponents()->GetCopyAndMigrationMask());
			}

			for (FMovieSceneEntityID Input : Inputs)
			{
				if (!Linker->EntityManager.HasComponent(Input, BuiltInComponents->BlendChannelInput))
				{
					Linker->EntityManager.AddComponent(Input, BuiltInComponents->BlendChannelInput, Output->BlendChannelID);
				}
			}
		}
		else
		{
			// Previously blended, but is no more - remove the tag from the remaining input if necessary and delete the output entity
			if (Output->OutputEntityID)
			{
				Linker->EntityManager.AddComponent(Output->OutputEntityID, BuiltInComponents->Tags.NeedsUnlink);
				Output->OutputEntityID = FMovieSceneEntityID();

				if (ensure(System->FloatBlenderSystem))
				{
					System->FloatBlenderSystem->ReleaseBlendChannel(Output->BlendChannelID);
				}
			}

			for (FMovieSceneEntityID Input : Inputs)
			{
				Linker->EntityManager.RemoveComponent(Input, BuiltInComponents->BlendChannelInput);
			}
		}

		Output->NumContributors = NumContributors;
	}

	void DestroyOutput(UObject* BoundMaterial, FName ParameterName, FAnimatedMaterialParameterInfo* Output, FEntityOutputAggregate Aggregate)
	{
		if (Output->OutputEntityID)
		{
			FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

			Linker->EntityManager.AddComponent(Output->OutputEntityID, BuiltInComponents->Tags.NeedsUnlink);
			Output->OutputEntityID = FMovieSceneEntityID();
		}
	}

};

} // namespace UE::MovieScene

UMovieSceneMaterialParameterSystem::UMovieSceneMaterialParameterSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

	RelevantComponent = TracksComponents->BoundMaterial;
	Phase = ESystemPhase::Instantiation | ESystemPhase::Evaluation;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), TracksComponents->BoundMaterial);

		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());

		DefineImplicitPrerequisite(UMovieScenePiecewiseFloatBlenderSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneHierarchicalBiasSystem::StaticClass());
	}
}

void UMovieSceneMaterialParameterSystem::OnLink()
{
	using namespace UE::MovieScene;

	// Always reset the float blender system on link to ensure that recycled systems are correctly initialized.
	FloatBlenderSystem = nullptr;

	FOverlappingMaterialParameterHandler Handler(this);

	ScalarParameterTracker.Destroy(Handler);
	VectorParameterTracker.Destroy(Handler);

	ScalarParameterTracker.Initialize(this);
	VectorParameterTracker.Initialize(this);
}

void UMovieSceneMaterialParameterSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	ESystemPhase CurrentPhase = Linker->GetActiveRunner()->GetCurrentPhase();
	if (CurrentPhase == ESystemPhase::Instantiation)
	{
		OnInstantiation();
	}
	else if (CurrentPhase == ESystemPhase::Evaluation)
	{
		OnEvaluation(InPrerequisites, Subsequents);
	}
}


void UMovieSceneMaterialParameterSystem::OnInstantiation()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	const bool bHasScalars = Linker->EntityManager.ContainsComponent(TracksComponents->ScalarParameterName);
	const bool bHasColors = Linker->EntityManager.ContainsComponent(TracksComponents->ColorParameterName);
	const bool bHasVectors = Linker->EntityManager.ContainsComponent(TracksComponents->VectorParameterName);

	if (bHasScalars)
	{
		ScalarParameterTracker.Update(Linker, TracksComponents->BoundMaterial, TracksComponents->ScalarParameterName, FEntityComponentFilter());

		FOverlappingMaterialParameterHandler Handler(this);
		Handler.DefaultComponentMask.Set(BuiltInComponents->FloatResult[0]);
		ScalarParameterTracker.ProcessInvalidatedOutputs(Linker, Handler);
	}

	if (bHasColors)
	{
		VectorParameterTracker.Update(Linker, TracksComponents->BoundMaterial, TracksComponents->ColorParameterName, FEntityComponentFilter());
	}
	if (bHasVectors)
	{
		VectorParameterTracker.Update(Linker, TracksComponents->BoundMaterial, TracksComponents->VectorParameterName, FEntityComponentFilter());
	}
	if (bHasColors || bHasVectors)
	{
		FOverlappingMaterialParameterHandler Handler(this);
		Handler.DefaultComponentMask.SetAll({ BuiltInComponents->FloatResult[0], BuiltInComponents->FloatResult[1], BuiltInComponents->FloatResult[2], BuiltInComponents->FloatResult[3] });
		VectorParameterTracker.ProcessInvalidatedOutputs(Linker, Handler);
	}
}

void UMovieSceneMaterialParameterSystem::OnEvaluation(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

	if (Linker->EntityManager.ContainsComponent(TracksComponents->ScalarParameterName))
	{
		FEntityTaskBuilder()
		.Read(TracksComponents->BoundMaterial)
		.Read(TracksComponents->ScalarParameterName)
		.Read(BuiltInComponents->FloatResult[0])
		.FilterNone({ BuiltInComponents->BlendChannelInput })
		.SetDesiredThread(Linker->EntityManager.GetDispatchThread())
		.Dispatch_PerEntity<FApplyScalarParameters>(&Linker->EntityManager, InPrerequisites, &Subsequents);
	}

	// Vectors and colors use the same API
	if (Linker->EntityManager.ContainsComponent(TracksComponents->VectorParameterName) || Linker->EntityManager.ContainsComponent(TracksComponents->ColorParameterName))
	{
		FEntityTaskBuilder()
		.Read(TracksComponents->BoundMaterial)
		.ReadOneOrMoreOf(TracksComponents->VectorParameterName, TracksComponents->ColorParameterName)
		.ReadOneOrMoreOf(BuiltInComponents->FloatResult[0], BuiltInComponents->FloatResult[1], BuiltInComponents->FloatResult[2], BuiltInComponents->FloatResult[3])
		.FilterNone({ BuiltInComponents->BlendChannelInput })
		.SetDesiredThread(Linker->EntityManager.GetDispatchThread())
		.Dispatch_PerAllocation<FApplyVectorParameters>(&Linker->EntityManager, InPrerequisites, &Subsequents);
	}
}