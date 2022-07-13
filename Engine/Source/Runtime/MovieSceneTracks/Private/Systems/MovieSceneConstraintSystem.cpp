// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneConstraintSystem.h"
#include "ConstraintsManager.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "EntitySystem/MovieSceneBoundObjectInstantiator.h"
#include "EntitySystem/MovieSceneBoundSceneComponentInstantiator.h"

#include "PreAnimatedState/MovieScenePreAnimatedComponentTransformStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

#include "EntitySystem/BuiltInComponentTypes.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Systems/MovieSceneComponentTransformSystem.h"

class UTickableConstraint;

namespace UE::MovieScene
{

struct FPreAnimatedConstraint
{
	TWeakObjectPtr<UTickableConstraint> WeakConstraint;
	bool bPreviouslyEnabled;
};

struct FPreAnimatedConstraintTraits : FBoundObjectPreAnimatedStateTraits
{
	using KeyType = TTuple<FObjectKey, FName>;
	using StorageType = FPreAnimatedConstraint;

	static FPreAnimatedConstraint CachePreAnimatedValue(UObject* InBoundObject, const FName& ConstraintName)
	{
		USceneComponent* SceneComponent = CastChecked<USceneComponent>(InBoundObject);
		FConstraintsManagerController& Controller = FConstraintsManagerController::Get(SceneComponent->GetWorld());

		// @todo: I don't know how to store pre-animated values - do we need to enable/disable constraints or add/remove them?
		//        Maybe we just need to also cache some other state other than Constraint->Active
		UTickableConstraint* Constraint = Controller.GetConstraint(ConstraintName);
		return FPreAnimatedConstraint{ Constraint, Constraint ? Constraint->Active : false };
	}

	static void RestorePreAnimatedValue(const TTuple<FObjectKey, FName>& InKey, const FPreAnimatedConstraint& OldValue, const FRestoreStateParams& Params)
	{
		if (UTickableConstraint* Constraint = OldValue.WeakConstraint.Get())
		{
			Constraint->Active = OldValue.bPreviouslyEnabled;
		}
	}
};

struct FPreAnimatedConstraintStorage
	: public TPreAnimatedStateStorage<FPreAnimatedConstraintTraits>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedConstraintStorage> StorageID;
};

TAutoRegisterPreAnimatedStorageID<FPreAnimatedConstraintStorage> FPreAnimatedConstraintStorage::StorageID;

} // namespace UE::MovieScene

UMovieSceneConstraintSystem::UMovieSceneConstraintSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

	//RelevantComponent = TracksComponents->ConstraintName;
	RelevantComponent = TracksComponents->ConstraintChannel;

	// Run constraints during instantiation or evaluation
	Phase = ESystemPhase::Instantiation | ESystemPhase::Evaluation;

	if (HasAnyFlags(RF_ClassDefaultObject) ) 
	{
		// Constraints must be evaluated before their transforms are evaluated.
		// This is only really necessary if they are in the same phase (which they are not), but I've
		// defined the prerequisite for safety if its phase changes in future
		DefineImplicitPrerequisite(GetClass(), UMovieSceneComponentTransformSystem::StaticClass());
		DefineComponentConsumer(GetClass(), UE::MovieScene::FBuiltInComponentTypes::Get()->EvalTime);
	}
}

void UMovieSceneConstraintSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FMovieSceneEntitySystemRunner* ActiveRunner = Linker->GetActiveRunner();
	ESystemPhase CurrentPhase = ActiveRunner->GetCurrentPhase();


	if (CurrentPhase == ESystemPhase::Instantiation)
	{
		// Save pre-animated state
		//TSharedPtr<FPreAnimatedConstraintStorage> PreAnimatedStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedConstraintStorage>();

		//andrew, had to remove this, last parameter not the correct variadic type
		//PreAnimatedStorage->BeginTrackingAndCachePreAnimatedValues(Linker, BuiltInComponents->BoundObject, TracksComponents->ConstraintName);	
	}
	else if (CurrentPhase == ESystemPhase::Evaluation)
	{
		FConstraintsManagerController& Controller = FConstraintsManagerController::Get(GetWorld());
		FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		struct FEvaluateConstraintChannels
		{
			FEvaluateConstraintChannels(FConstraintsManagerController* InController) :Controller(InController) { check(Controller); };
			void ForEachEntity(UObject* BoundObject, const FConstraintComponentData& ConstraintChannel, FFrameTime FrameTime)
			{
				UTickableConstraint* Constraint = Controller->GetConstraint(ConstraintChannel.ConstraintName);
				if (Constraint)
				{
					bool Result;
					if (ConstraintChannel.Channel->Evaluate(FrameTime, Result))
					{
						Constraint->Active = Result;
					}
				}
			}
			FConstraintsManagerController* Controller;
		};

		// Set up new constraints
		FEntityTaskBuilder()
			.SetDesiredThread(Linker->EntityManager.GetGatherThread())
			.Read(BuiltInComponents->BoundObject)
			.Read(TracksComponents->ConstraintChannel)
			.Read(BuiltInComponents->EvalTime)
			.Dispatch_PerEntity<FEvaluateConstraintChannels>(&Linker->EntityManager, InPrerequisites, &Subsequents, &Controller);
	}
}


