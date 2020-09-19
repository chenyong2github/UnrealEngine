// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackInstances/MovieSceneCameraCutTrackInstance.h"
#include "ContentStreaming.h"
#include "Evaluation/IMovieSceneMotionVectorSimulation.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "GameFramework/Actor.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "MovieSceneCommonHelpers.h"

DECLARE_CYCLE_STAT(TEXT("Camera Cut Track Token Execute"), MovieSceneEval_CameraCutTrack_TokenExecute, STATGROUP_MovieSceneEval);

namespace UE
{
namespace MovieScene
{

	/** Information about a camera cut's easing (in or out) */
	struct FBlendedCameraCutEasingInfo
	{
		float BlendTime = -1.f;
		TOptional<EMovieSceneBuiltInEasing> BlendType;

		FBlendedCameraCutEasingInfo() {}
		FBlendedCameraCutEasingInfo(const TRange<FFrameNumber> EasingRange, const TScriptInterface<IMovieSceneEasingFunction>& EasingFunction, const FFrameRate FrameRate)
		{
			// Get the blend time in seconds.
			int32 EaseInTime = UE::MovieScene::DiscreteSize(EasingRange);
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

	/** Camera cut info struct. */
	struct FBlendedCameraCut
	{
		FInstanceHandle InstanceHandle;

		FMovieSceneObjectBindingID CameraBindingID;
		FMovieSceneSequenceID OperandSequenceID;

		FBlendedCameraCutEasingInfo EaseIn;
		FBlendedCameraCutEasingInfo EaseOut;
		bool bIsFinalCut = false;

		FMovieSceneObjectBindingID PreviousCameraBindingID;
		FMovieSceneSequenceID PreviousOperandSequenceID;

		float PreviewBlendFactor = -1.f;

		FBlendedCameraCut()
		{}
		FBlendedCameraCut(FInstanceHandle InInstanceHandle, FMovieSceneObjectBindingID InCameraBindingID, FMovieSceneSequenceID InOperandSequenceID) 
			: InstanceHandle(InInstanceHandle)
			, CameraBindingID(InCameraBindingID)
			, OperandSequenceID(InOperandSequenceID)
		{}
	};

	/** Pre-roll camera cut info struct. */
	struct FPreRollCameraCut
	{
		FInstanceHandle InstanceHandle;
		FMovieSceneObjectBindingID CameraBindingID;
		FTransform CutTransform;
		bool bHasCutTransform;
	};

	/** A movie scene pre-animated token that stores a pre-animated camera cut */
	struct FCameraCutPreAnimatedToken : IMovieScenePreAnimatedGlobalToken
	{
		static FMovieSceneAnimTypeID GetAnimTypeID()
		{
			return TMovieSceneAnimTypeID<FCameraCutPreAnimatedToken>();
		}

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

	struct FCameraCutAnimator
	{
		static UObject* FindBoundObject(FMovieSceneObjectBindingID BindingID, FMovieSceneSequenceID SequenceID, IMovieScenePlayer& Player)
		{
			FMovieSceneObjectBindingID ResolvedID = BindingID.ResolveLocalToRoot(SequenceID, Player);

			FMovieSceneEvaluationOperand Operand(ResolvedID.GetSequenceID(), BindingID.GetGuid());
			TArrayView<TWeakObjectPtr<>> Objects = Player.FindBoundObjects(Operand);
			if (Objects.Num() > 0)
			{
				return Objects[0].Get();
			}
			return nullptr;
		}

		static void AnimatePreRoll(const UE::MovieScene::FPreRollCameraCut& Params, const FMovieSceneContext& Context, const FMovieSceneSequenceID& SequenceID, IMovieScenePlayer& Player)
		{
			if (Params.bHasCutTransform)
			{
				FVector Location = Params.CutTransform.GetLocation();
				IStreamingManager::Get().AddViewSlaveLocation(Location);
			}
			else
			{
				UObject* CameraObject = FindBoundObject(Params.CameraBindingID, SequenceID, Player);

				if (AActor* Actor = Cast<AActor>(CameraObject))
				{
					FVector Location = Actor->GetActorLocation();
					IStreamingManager::Get().AddViewSlaveLocation(Location);
				}
			}
		}

		static bool AnimateBlendedCameraCut(const UE::MovieScene::FBlendedCameraCut& Params, UMovieSceneCameraCutTrackInstance::FCameraCutCache& CameraCutCache, const FMovieSceneContext& Context, IMovieScenePlayer& Player)
		{
			using namespace UE::MovieScene;

			UObject* CameraActor = FindBoundObject(Params.CameraBindingID, Params.OperandSequenceID, Player);

			EMovieSceneCameraCutParams CameraCutParams;
			CameraCutParams.bJumpCut = Context.HasJumped();
			CameraCutParams.BlendTime = Params.EaseIn.BlendTime;
			CameraCutParams.BlendType = Params.EaseIn.BlendType;

#if WITH_EDITOR
			UObject* PreviousCameraActor = FindBoundObject(Params.PreviousCameraBindingID, Params.PreviousOperandSequenceID, Player);
			CameraCutParams.PreviousCameraObject = PreviousCameraActor;
			CameraCutParams.PreviewBlendFactor = Params.PreviewBlendFactor;
#endif

			static const FMovieSceneAnimTypeID CameraAnimTypeID = FMovieSceneAnimTypeID::Unique();

			if (CameraCutCache.LastLockedCamera.Get() != CameraActor)
			{
				Player.SavePreAnimatedState(CameraAnimTypeID, FCameraCutPreAnimatedTokenProducer());

				CameraCutParams.UnlockIfCameraObject = CameraCutCache.LastLockedCamera.Get();
				Player.UpdateCameraCut(CameraActor, CameraCutParams);
				CameraCutCache.LastLockedCamera = CameraActor;
				// TODO-ludovic
				//IMovieSceneMotionVectorSimulation::EnableThisFrame(PersistentData);
				return true;
			}
			else if (CameraActor || CameraCutParams.BlendTime > 0.f)
			{
				Player.SavePreAnimatedState(CameraAnimTypeID, FCameraCutPreAnimatedTokenProducer());
	
				Player.UpdateCameraCut(CameraActor, CameraCutParams);
				return true;
			}

			return false;
		}
	};

}  // namespace MovieScene
}  // namespace UE


void UMovieSceneCameraCutTrackInstance::OnAnimate()
{
	using namespace UE::MovieScene;

	// Gather active camera cuts, and triage pre-rolls from actual cuts.
	TArray<FPreRollCameraCut> CameraCutPreRolls;
	TArray<FBlendedCameraCut> CameraCutParams;
	const FInstanceRegistry* InstanceRegistry = GetLinker()->GetInstanceRegistry();

	for (const FCameraCutInputInfo& InputInfo : SortedInputInfos)
	{
		const FMovieSceneTrackInstanceInput& Input = InputInfo.Input;
		const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(Input.InstanceHandle);
		const FMovieSceneContext& Context = SequenceInstance.GetContext();
		IMovieScenePlayer* Player = SequenceInstance.GetPlayer();

		const UMovieSceneCameraCutSection* Section = Cast<const UMovieSceneCameraCutSection>(Input.Section);
		const FMovieSceneObjectBindingID CameraBindingID = Section->GetCameraBindingID();

		FTransform CutTransform = Section->InitialCameraCutTransform;
		const bool bHasCutTransform = Section->bHasInitialCameraCutTransform;

		if (Context.IsPreRoll())
		{
			FPreRollCameraCut PreRollCameraCut { Input.InstanceHandle, CameraBindingID, CutTransform, bHasCutTransform };
			CameraCutPreRolls.Add(PreRollCameraCut);
		}
		else
		{
			const UMovieSceneCameraCutTrack* Track = Section->GetTypedOuter<UMovieSceneCameraCutTrack>();
			const TArray<UMovieSceneSection*>& AllSections = Track->GetAllSections();
			const bool bIsFinalSection = (AllSections.Num() > 0 && AllSections.Last() == Section);

			FBlendedCameraCut Params(Input.InstanceHandle, CameraBindingID, SequenceInstance.GetSequenceID());
			Params.bIsFinalCut = bIsFinalSection;

			// Get ease-in/out info.
			const TRange<FFrameNumber> EaseInRange = Section->GetEaseInRange();
			if (!EaseInRange.IsEmpty())
			{
				Params.EaseIn = FBlendedCameraCutEasingInfo(EaseInRange, Section->Easing.EaseIn, Context.GetFrameRate());
			}
			const TRange<FFrameNumber> EaseOutRange = Section->GetEaseOutRange();
			if (!EaseOutRange.IsEmpty())
			{
				Params.EaseOut = FBlendedCameraCutEasingInfo(EaseOutRange, Section->Easing.EaseOut, Context.GetFrameRate());
			}

			// Get preview blending.
			const float Weight = Section->EvaluateEasing(Context.GetTime());
			Params.PreviewBlendFactor = Weight;

			if (bIsFinalSection && Params.EaseOut.BlendTime > 0.f)
			{
				const TRange<FFrameNumber> SourceSectionRange = Section->GetTrueRange();
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

			CameraCutParams.Add(Params);
		}
	}

	// For now we only support one pre-roll.
	if (CameraCutPreRolls.Num() > 0)
	{
		FPreRollCameraCut& CameraCutPreRoll = CameraCutPreRolls.Last();
		const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(CameraCutPreRoll.InstanceHandle);
		const FMovieSceneContext& Context = SequenceInstance.GetContext();
		IMovieScenePlayer* Player = SequenceInstance.GetPlayer();
		const FMovieSceneSequenceID SequenceID = SequenceInstance.GetSequenceID();
		FCameraCutAnimator::AnimatePreRoll(CameraCutPreRoll, Context, SequenceID, *Player);
	}

	// For now we only support 2 active camera cuts at most (with blending between them).
	FBlendedCameraCut FinalCameraCut;

	if (CameraCutParams.Num() >= 2)
	{
		FBlendedCameraCut PrevCameraCut = CameraCutParams[1];
		FBlendedCameraCut NextCameraCut = CameraCutParams[0];
		
		// Blending 2 camera cuts: just keep track of what the previous shot is supposed to be.
		FinalCameraCut = NextCameraCut;
		FinalCameraCut.PreviousCameraBindingID = PrevCameraCut.CameraBindingID;
		FinalCameraCut.PreviousOperandSequenceID = PrevCameraCut.OperandSequenceID;
	}
	else if (CameraCutParams.Num() == 1)
	{
		// Only one camera cut active.
		FinalCameraCut = CameraCutParams[0];
	}

	if (CameraCutParams.Num() > 0)
	{
		const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(FinalCameraCut.InstanceHandle);
		const FMovieSceneContext& Context = SequenceInstance.GetContext();
		IMovieScenePlayer* Player = SequenceInstance.GetPlayer();
		if (FCameraCutAnimator::AnimateBlendedCameraCut(FinalCameraCut, CameraCutCache, Context, *Player))
		{
			// Track whether this ever evaluated to take control. If so, we'll want to remove control OnDestroyed
			PlayerUseCounts.FindChecked(Player).bValid = true;
		}
	}
}

void UMovieSceneCameraCutTrackInstance::OnInputAdded(const FMovieSceneTrackInstanceInput& InInput)
{
	using namespace UE::MovieScene;

	const FInstanceRegistry* InstanceRegistry = GetLinker()->GetInstanceRegistry();
	const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(InInput.InstanceHandle);
	IMovieScenePlayer* Player = SequenceInstance.GetPlayer();

	int32& UseCount = PlayerUseCounts.FindOrAdd(Player, FCameraCutUseData()).UseCount;
	++UseCount;
}

void UMovieSceneCameraCutTrackInstance::OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput)
{
	using namespace UE::MovieScene;

	const FInstanceRegistry* InstanceRegistry = GetLinker()->GetInstanceRegistry();
	const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(InInput.InstanceHandle);
	IMovieScenePlayer* Player = SequenceInstance.GetPlayer();

	int32& UseCount = PlayerUseCounts.FindChecked(Player).UseCount;
	--UseCount;
	if (UseCount == 0)
	{
		PlayerUseCounts.Remove(Player);
	}
}

void UMovieSceneCameraCutTrackInstance::OnEndUpdateInputs()
{
	using namespace UE::MovieScene;

	const FInstanceRegistry* InstanceRegistry = GetLinker()->GetInstanceRegistry();

	// Rebuild our sorted input infos.
	SortedInputInfos.Reset();
	for (const FMovieSceneTrackInstanceInput& Input : GetInputs())
	{
		FCameraCutInputInfo InputInfo;
		InputInfo.Input = Input;

		const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(Input.InstanceHandle);
		IMovieScenePlayer* Player = SequenceInstance.GetPlayer();

		if (UObject* PlaybackContext = Player->GetPlaybackContext())
		{
			if (UWorld* World = PlaybackContext->GetWorld())
			{
				const float WorldTime = World->GetTimeSeconds();
				InputInfo.GlobalStartTime = WorldTime;	
			}
		}

		SortedInputInfos.Add(InputInfo);
	}

	// Sort all active camera cuts by hierarchical bias first, and when they started in absolute game time second.
	// Later (higher starting time) cuts are sorted first, so we can prioritize the latest camera cut that started.
	Algo::Sort(SortedInputInfos,
			[InstanceRegistry](const FCameraCutInputInfo& A, const FCameraCutInputInfo& B) -> bool
			{
				const FSequenceInstance& SeqA = InstanceRegistry->GetInstance(A.Input.InstanceHandle);
				const FSequenceInstance& SeqB = InstanceRegistry->GetInstance(B.Input.InstanceHandle);
				const int32 HierarchicalBiasA = SeqA.GetContext().GetHierarchicalBias();
				const int32 HierarchicalBiasB = SeqB.GetContext().GetHierarchicalBias();
				if (HierarchicalBiasA > HierarchicalBiasB)
				{
					return true;
				}
				else if (HierarchicalBiasA < HierarchicalBiasB)
				{
					return false;
				}
				else
				{
					return A.GlobalStartTime > B.GlobalStartTime;
				}
			});
}

void UMovieSceneCameraCutTrackInstance::OnDestroyed()
{
	// All sequencer players actually point to the same player controller and view target in a given world,
	// so we only need to restore the pre-animated state on one sequencer player, like, say, the first one
	// we still have in use. And we only do that when we have no more inputs active (if we still have some
	// inputs active, regardless of what sequencer player they belong to, they still have control of the
	// player controller's view target, so we don't want to mess that up).
	//
	// TODO-ludovic: when we have proper splitscreen support, this should be changed heavily.
	//
	for (const TPair<IMovieScenePlayer*, FCameraCutUseData>& PlayerUseCount : PlayerUseCounts)
	{
		// Restore only if we ever took control
		if (PlayerUseCount.Value.bValid)
		{
			EMovieSceneCameraCutParams Params;
			PlayerUseCount.Key->UpdateCameraCut(nullptr, Params);
			break;  // Only do it on the first one.
		}
	}

	PlayerUseCounts.Reset();
}
