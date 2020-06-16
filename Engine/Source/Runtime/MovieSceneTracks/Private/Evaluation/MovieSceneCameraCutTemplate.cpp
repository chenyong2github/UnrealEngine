// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneCameraCutTemplate.h"
#include "ContentStreaming.h"
#include "Evaluation/IMovieSceneMotionVectorSimulation.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "GameFramework/Actor.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"

DECLARE_CYCLE_STAT(TEXT("Camera Cut Track Token Execute"), MovieSceneEval_CameraCutTrack_TokenExecute, STATGROUP_MovieSceneEval);

struct FCameraCutTrackData : IPersistentEvaluationData
{
	TWeakObjectPtr<> LastLockedCamera;
};

struct FBlendedCameraCutEasingInfo
{
	float BlendTime = -1.f;
	TOptional<EMovieSceneBuiltInEasing> BlendType;

	FBlendedCameraCutEasingInfo() {}
	FBlendedCameraCutEasingInfo(const TRange<FFrameNumber> EasingRange, const TScriptInterface<IMovieSceneEasingFunction>& EasingFunction, const FFrameRate FrameRate)
	{
		// Get the blend time in seconds.
		int32 EaseInTime = MovieScene::DiscreteSize(EasingRange);
		BlendTime = FrameRate.AsSeconds(FFrameTime(EaseInTime));

		// If it's a built-in easing function, get the curve type. We'll try to convert it to what the
		// player controller knows later, in the movie scene player.
		const UObject* EaseInScript = EasingFunction.GetObject();
		if (const UMovieSceneBuiltInEasingFunction* BuiltInEaseIn = Cast<UMovieSceneBuiltInEasingFunction>(EaseInScript))
		{
			BlendType = BuiltInEaseIn->Type;
		}
	}
};

/** A movie scene execution token that sets up the streaming system with the camera cut location */
struct FCameraCutPreRollExecutionToken : IMovieSceneExecutionToken
{
	FMovieSceneObjectBindingID CameraBindingID;
	FTransform CutTransform;
	bool bHasCutTransform;

	FCameraCutPreRollExecutionToken(const FMovieSceneObjectBindingID& InCameraBindingID, const FTransform& InCutTransform, bool bInHasCutTransform)
		: CameraBindingID(InCameraBindingID)
		, CutTransform(InCutTransform)
		, bHasCutTransform(bInHasCutTransform)
	{}

	static FMovieSceneAnimTypeID GetAnimTypeID()
	{
		return TMovieSceneAnimTypeID<FCameraCutPreRollExecutionToken>();
	}
	
	/** Execute this token, operating on all objects referenced by 'Operand' */
	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		FVector Location;

		if (bHasCutTransform)
		{
			Location = CutTransform.GetLocation();
		}
		else
		{
			FMovieSceneSequenceID SequenceID = Operand.SequenceID;
			if (CameraBindingID.GetSequenceID().IsValid())
			{
				if (const FMovieSceneSubSequenceData* SubData = Player.GetEvaluationTemplate().GetHierarchy().FindSubData(SequenceID))
				{
					// Ensure that this ID is resolvable from the root, based on the current local sequence ID
					FMovieSceneObjectBindingID RootBindingID = CameraBindingID.ResolveLocalToRoot(SequenceID, Player.GetEvaluationTemplate().GetHierarchy());
					SequenceID = RootBindingID.GetSequenceID();
				}
			}

			// If the transform is set, otherwise use the bound actor's transform
			FMovieSceneEvaluationOperand CameraOperand(SequenceID, CameraBindingID.GetGuid());
		
			TArrayView<TWeakObjectPtr<>> Objects = Player.FindBoundObjects(CameraOperand);
			if (!Objects.Num())
			{
				return;
			}

			// Only ever deal with one camera
			UObject* CameraObject = Objects[0].Get();
			
			AActor* Actor = Cast<AActor>(CameraObject);
			if (Actor)
			{
				Location = Actor->GetActorLocation();
			}
		}

		IStreamingManager::Get().AddViewSlaveLocation( Location );
	}
};

/** A movie scene pre-animated token that stores a pre-animated camera cut */
struct FCameraCutPreAnimatedToken : IMovieScenePreAnimatedGlobalToken
{
	virtual void RestoreState(IMovieScenePlayer& Player) override
	{
		EMovieSceneCameraCutParams Params;
		Player.UpdateCameraCut(nullptr, Params);
	}
};

/** The producer class for the pre-animated token above */
struct FCameraCutPreAnimatedTokenProducer : IMovieScenePreAnimatedGlobalTokenProducer
{
	virtual IMovieScenePreAnimatedGlobalTokenPtr CacheExistingState() const override
	{
		return FCameraCutPreAnimatedToken();
	}
};

namespace MovieScene
{
	/** Camera cut info struct. */
	struct FBlendedCameraCut
	{
		FMovieSceneObjectBindingID CameraBindingID;
		FMovieSceneSequenceID OperandSequenceID;

		FBlendedCameraCutEasingInfo EaseIn;
		FBlendedCameraCutEasingInfo EaseOut;
		bool bIsFinalCut = false;

		FMovieSceneObjectBindingID PreviousCameraBindingID;
		FMovieSceneSequenceID PreviousOperandSequenceID;

		float PreviewBlendFactor = -1.f;

		FBlendedCameraCut() {}
		FBlendedCameraCut(FMovieSceneObjectBindingID InCameraBindingID, FMovieSceneSequenceID InOperandSequenceID) 
			: CameraBindingID(InCameraBindingID)
			, OperandSequenceID(InOperandSequenceID)
		{}

		FBlendedCameraCut& Resolve(TMovieSceneInitialValueStore<FBlendedCameraCut>& InitialValueStore)
		{
			return *this;
		}
	};

	/** Blending actuator for camera cuts. */
	struct FCameraCutBlendingActuator : public TMovieSceneBlendingActuator<FBlendedCameraCut>
	{
		FCameraCutBlendingActuator() : TMovieSceneBlendingActuator<FBlendedCameraCut>(GetActuatorTypeID()) {}

		static FMovieSceneBlendingActuatorID GetActuatorTypeID()
		{
			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FCameraCutBlendingActuator, 0>();
			return FMovieSceneBlendingActuatorID(TypeID);
		}

		static FMovieSceneAnimTypeID GetCameraCutTypeID()
		{
			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FCameraCutBlendingActuator, 2>();
			return TypeID;
		}

		static UObject* FindBoundObject(FMovieSceneObjectBindingID BindingID, FMovieSceneSequenceID SequenceID, IMovieScenePlayer& Player)
		{
			FMovieSceneSequenceID ResolvedSequenceID = SequenceID;
			if (BindingID.GetSequenceID().IsValid())
			{
				FMovieSceneObjectBindingID RootBindingID = BindingID.ResolveLocalToRoot(SequenceID, Player.GetEvaluationTemplate().GetHierarchy());
				ResolvedSequenceID = RootBindingID.GetSequenceID();
			}

			FMovieSceneEvaluationOperand Operand(ResolvedSequenceID, BindingID.GetGuid());
			TArrayView<TWeakObjectPtr<>> Objects = Player.FindBoundObjects(Operand);
			if (Objects.Num() > 0)
			{
				return Objects[0].Get();
			}
			return nullptr;
		}

		virtual void Actuate(UObject* InObject, const FBlendedCameraCut& InFinalValue, const TBlendableTokenStack<FBlendedCameraCut>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
		{
			UObject* CameraActor = FindBoundObject(InFinalValue.CameraBindingID, InFinalValue.OperandSequenceID, Player);

			FCameraCutTrackData& CameraCutCache = PersistentData.GetOrAddTrackData<FCameraCutTrackData>();

			EMovieSceneCameraCutParams CameraCutParams;
			CameraCutParams.bJumpCut = Context.HasJumped();
			CameraCutParams.BlendTime = InFinalValue.EaseIn.BlendTime;
			CameraCutParams.BlendType = InFinalValue.EaseIn.BlendType;

#if WITH_EDITOR
			UObject* PreviousCameraActor = FindBoundObject(InFinalValue.PreviousCameraBindingID, InFinalValue.PreviousOperandSequenceID, Player);
			CameraCutParams.PreviousCameraObject = PreviousCameraActor;
			CameraCutParams.PreviewBlendFactor = InFinalValue.PreviewBlendFactor;
#endif

			if (CameraCutCache.LastLockedCamera.Get() != CameraActor)
			{
				OriginalStack.SavePreAnimatedState(Player, GetCameraCutTypeID(), FCameraCutPreAnimatedTokenProducer());

				CameraCutParams.UnlockIfCameraObject = CameraCutCache.LastLockedCamera.Get();
				Player.UpdateCameraCut(CameraActor, CameraCutParams);
				CameraCutCache.LastLockedCamera = CameraActor;
				IMovieSceneMotionVectorSimulation::EnableThisFrame(PersistentData);
			}
			else if (CameraActor || CameraCutParams.BlendTime > 0.f)
			{
				OriginalStack.SavePreAnimatedState(Player, GetCameraCutTypeID(), FCameraCutPreAnimatedTokenProducer());
	
				Player.UpdateCameraCut(CameraActor, CameraCutParams);
			}
		}

		virtual void Actuate(FMovieSceneInterrogationData& InterrogationData, const FBlendedCameraCut& InValue, const TBlendableTokenStack<FBlendedCameraCut>& OriginalStack, const FMovieSceneContext& Context) const override
		{
			check(false);
		}

		virtual FBlendedCameraCut RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const override
		{
			// Not applicable: camera cut tracks are master tracks that don't have any object to retrieve anything from.
			check(false);
			return FBlendedCameraCut();
		}
	};

	void BlendValue(FBlendedCameraCut& OutBlend, const FBlendedCameraCut& InValue, float Weight, EMovieSceneBlendType BlendType, TMovieSceneInitialValueStore<FBlendedCameraCut>& InitialValueStore)
	{
		// Blending camera cuts... we just need to keep track of what's the next/previous shot so we can pass
		// that information to the player controller.
		if (!OutBlend.CameraBindingID.IsValid())
		{
			OutBlend = InValue;
		}
		else
		{
			FMovieSceneObjectBindingID PreviousCameraBindingID = OutBlend.CameraBindingID;
			FMovieSceneSequenceID PreviousOperandSequenceID = OutBlend.OperandSequenceID;
			OutBlend = InValue;
			OutBlend.PreviousCameraBindingID = PreviousCameraBindingID;
			OutBlend.PreviousOperandSequenceID = PreviousOperandSequenceID;
		}
	}
}

template<> FMovieSceneAnimTypeID GetBlendingDataType<MovieScene::FBlendedCameraCut>()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneCameraCutSectionTemplate::FMovieSceneCameraCutSectionTemplate(const UMovieSceneCameraCutSection& Section, TOptional<FTransform> InCutTransform)
	: CameraBindingID(Section.GetCameraBindingID())
	, CutTransform(InCutTransform.Get(FTransform()))
	, bHasCutTransform(InCutTransform.IsSet())
	, bIsFinalSection(false)
{
	if (UMovieSceneCameraCutTrack* Track = Section.GetTypedOuter<UMovieSceneCameraCutTrack>())
	{
		const TArray<UMovieSceneSection*>& AllSections = Track->GetAllSections();
		bIsFinalSection = (AllSections.Num() > 0 && AllSections.Last() == &Section);
	}
}

void FMovieSceneCameraCutSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	if (Context.IsPreRoll())
	{
		ExecutionTokens.Add(FCameraCutPreRollExecutionToken(CameraBindingID, CutTransform, bHasCutTransform));
	}
	else
	{
		// For now we only look at how long the camera blend is supposed to be, and we pass that on to
		// the player controller via the execution token. Later we'll need to actually drive the blend
		// ourselves so that the curve itself is actually matching.
		MovieScene::FBlendedCameraCut Params(CameraBindingID, Operand.SequenceID);
		Params.bIsFinalCut = bIsFinalSection;
		if (SourceSectionPtr.IsValid())
		{
			const TRange<FFrameNumber> EaseInRange = SourceSectionPtr->GetEaseInRange();
			if (!EaseInRange.IsEmpty())
			{
				Params.EaseIn = FBlendedCameraCutEasingInfo(EaseInRange, SourceSectionPtr->Easing.EaseIn, Context.GetFrameRate());
			}
			const TRange<FFrameNumber> EaseOutRange = SourceSectionPtr->GetEaseOutRange();
			if (!EaseOutRange.IsEmpty())
			{
				Params.EaseOut = FBlendedCameraCutEasingInfo(EaseOutRange, SourceSectionPtr->Easing.EaseOut, Context.GetFrameRate());
			}
		}

		FMovieSceneBlendingActuatorID ActuatorTypeID = MovieScene::FCameraCutBlendingActuator::GetActuatorTypeID();
		FMovieSceneBlendingAccumulator& Accumulator = ExecutionTokens.GetBlendingAccumulator();
		if (!Accumulator.FindActuator<MovieScene::FBlendedCameraCut>(ActuatorTypeID))
		{
			Accumulator.DefineActuator(ActuatorTypeID, MakeShared<MovieScene::FCameraCutBlendingActuator>());
		}

		float Weight = EvaluateEasing(Context.GetTime());
		Params.PreviewBlendFactor = Weight;

		if (bIsFinalSection && Params.EaseOut.BlendTime > 0.f)
		{
			const UMovieSceneSection* SourceSection = GetSourceSection();
			const TRange<FFrameNumber> SourceSectionRange = SourceSection->GetTrueRange();
			const FFrameTime OutBlendTime = Context.GetFrameRate().AsFrameTime(Params.EaseOut.BlendTime);
			if (Context.GetTime() >= SourceSectionRange.GetUpperBoundValue() - OutBlendTime)
			{
				Params.EaseIn = Params.EaseOut;
				Params.EaseOut = FBlendedCameraCutEasingInfo();
				Params.PreviousCameraBindingID = Params.CameraBindingID;
				Params.PreviousOperandSequenceID = Params.OperandSequenceID;
				Params.CameraBindingID = FMovieSceneObjectBindingID();
				Params.OperandSequenceID = FMovieSceneSequenceID();
			}
		}

		FMovieSceneEvaluationScope EvalScope;
		if (ExecutionTokens.GetCurrentScope().CompletionMode == EMovieSceneCompletionMode::RestoreState)
		{
			EvalScope = FMovieSceneEvaluationScope(PersistentData.GetTrackKey(), EMovieSceneCompletionMode::RestoreState);
		}
		
		ExecutionTokens.GetBlendingAccumulator().BlendToken(
				Operand, ActuatorTypeID, EvalScope, Context,
				TBlendableToken<MovieScene::FBlendedCameraCut>(
					Params, EvalScope, Context, 
					SourceSectionPtr->GetBlendType().Get(), Weight));
	}
}
