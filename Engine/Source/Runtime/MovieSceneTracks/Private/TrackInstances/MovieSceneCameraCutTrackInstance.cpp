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
		float RootBlendTime = -1.f;
		TOptional<EMovieSceneBuiltInEasing> BlendType;

		FBlendedCameraCutEasingInfo() {}
		FBlendedCameraCutEasingInfo(float InRootBlendTime, const TScriptInterface<IMovieSceneEasingFunction>& EasingFunction)
		{
			RootBlendTime = InRootBlendTime;

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
		bool bLockPreviousCamera = false;
		bool bIsFinalBlendOut = false;

		FMovieSceneObjectBindingID PreviousCameraBindingID;
		FMovieSceneSequenceID PreviousOperandSequenceID;

		float PreviewBlendFactor = -1.f;
		bool bCanBlend = false;

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
			CameraCutParams.BlendTime = Params.EaseIn.RootBlendTime;
			CameraCutParams.BlendType = Params.EaseIn.BlendType;
			CameraCutParams.bLockPreviousCamera = Params.bLockPreviousCamera;

#if WITH_EDITOR
			UObject* PreviousCameraActor = FindBoundObject(Params.PreviousCameraBindingID, Params.PreviousOperandSequenceID, Player);
			CameraCutParams.PreviousCameraObject = PreviousCameraActor;
			CameraCutParams.PreviewBlendFactor = Params.PreviewBlendFactor;
			CameraCutParams.bCanBlend = Params.bCanBlend;
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

			const FMovieSceneTimeTransform SequenceToRootTransform = Context.GetSequenceToRootTransform();

			FBlendedCameraCut Params(Input.InstanceHandle, CameraBindingID, SequenceInstance.GetSequenceID());
			Params.bCanBlend = Track->bCanBlend;

			// Get ease-in/out info.
			if (Section->HasStartFrame() && Section->Easing.GetEaseInDuration() > 0)
			{
				const float RootEaseInTime = SequenceToRootTransform.TimeScale * Context.GetFrameRate().AsSeconds(FFrameNumber(Section->Easing.GetEaseInDuration()));
				Params.EaseIn = FBlendedCameraCutEasingInfo(RootEaseInTime, Section->Easing.EaseIn);
			}
			if (Section->HasEndFrame() && Section->Easing.GetEaseOutDuration() > 0)
			{
				const float RootEaseOutTime = SequenceToRootTransform.TimeScale * Context.GetFrameRate().AsSeconds(FFrameNumber(Section->Easing.GetEaseOutDuration()));
				Params.EaseOut = FBlendedCameraCutEasingInfo(RootEaseOutTime, Section->Easing.EaseOut);
			}

			// Remember locking option.
			Params.bLockPreviousCamera = Section->bLockPreviousCamera;

			// Get preview blending.
			const float Weight = Section->EvaluateEasing(Context.GetTime());
			Params.PreviewBlendFactor = Weight;

			// If this camera cut is blending away from the sequence (it's the final camera cut section), then
			// we reverse the blend: we make it blend into a null camera.
			if (bIsFinalSection && Params.EaseOut.RootBlendTime > 0.f)
			{
				const TRange<FFrameNumber> SourceSectionRange = Section->GetTrueRange();
				const FFrameNumber OutBlendTime = Section->Easing.GetEaseOutDuration();
				if (Context.GetTime() >= SourceSectionRange.GetUpperBoundValue() - OutBlendTime)
				{
					Params.bIsFinalBlendOut = true;
					Params.PreviewBlendFactor = 1.0f - Params.PreviewBlendFactor;
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
		const FBlendedCameraCut& PrevCameraCut = CameraCutParams[1];
		const FBlendedCameraCut& NextCameraCut = CameraCutParams[0];
		
		// Blending 2 camera cuts: keep track of what the previous shot is supposed to be,
		// if both cuts are next to each other.
		FinalCameraCut = NextCameraCut;
		FinalCameraCut.PreviousCameraBindingID = PrevCameraCut.CameraBindingID;
		FinalCameraCut.PreviousOperandSequenceID = PrevCameraCut.OperandSequenceID;

		if (NextCameraCut.bIsFinalBlendOut)
		{
			// bIsFinalBlendOut is only true if we are in the final blend out *right now*. But if we are
			// here, it also means we have at least 2 active camera cuts, which means we're blending out
			// from a sequence into a different sequence (most likely a parent sequence where a camera cut
			// is extending past the child sequence in which the 1st camera cut section is).
			check(PrevCameraCut.InstanceHandle != NextCameraCut.InstanceHandle);

			// NextCameraCut is the child cut.
			// PrevCameraCut is the parent cut.
			//
			// We are blending *out* from the child cut (the last cut of its sequence) to the parent cut,
			// so the variable names are misleading in this case.
			const FBlendedCameraCut& PrevChildCameraCut = CameraCutParams[0];
			const FBlendedCameraCut& NextParentCameraCut = CameraCutParams[1];
			// Let's now correctly use the proper next/previous information. However, because the previous
			// (child) cut has been "reversed" (it was expressed as blending into gameplay, with it as the
			// "previous" camera), we need to take that cut's "previous" info.
			FinalCameraCut = NextParentCameraCut;
			FinalCameraCut.PreviousCameraBindingID = PrevChildCameraCut.PreviousCameraBindingID;
			FinalCameraCut.PreviousOperandSequenceID = PrevChildCameraCut.PreviousOperandSequenceID;
			// We need to use the child blend out information (blend type, time, and preview factor), because
			// this is the blend we're using to go to the next/ (parent) cut.
			// Because the child cut was the final blend out, the easing info and the preview blend factor
			// have both already been "reversed" (ease-out has been transferred to ease-in, blend factor has
			// been subtracted from 1.f). So we grab those for the blend info.
			FinalCameraCut.EaseIn = PrevChildCameraCut.EaseIn;
			FinalCameraCut.PreviewBlendFactor = PrevChildCameraCut.PreviewBlendFactor;
			FinalCameraCut.bLockPreviousCamera = PrevChildCameraCut.bLockPreviousCamera;
		}
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
			// Track whether this ever evaluated to take control. If so, we'll want to remove control in OnDestroyed.
			FCameraCutUseData& PlayerUseCount = PlayerUseCounts.FindChecked(Player);
			PlayerUseCount.bValid = true;
			// Remember whether we had blending support the last time we took control of the viewport. This is also
			// for OnDestroyed.
			PlayerUseCount.bCanBlend = FinalCameraCut.bCanBlend;
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
#if WITH_EDITOR
			Params.bCanBlend = PlayerUseCount.Value.bCanBlend;
#endif
			PlayerUseCount.Key->UpdateCameraCut(nullptr, Params);
			break;  // Only do it on the first one.
		}
	}

	PlayerUseCounts.Reset();
}
