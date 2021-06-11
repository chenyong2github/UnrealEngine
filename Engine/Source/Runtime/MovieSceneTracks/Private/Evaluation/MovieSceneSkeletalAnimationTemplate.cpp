// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneSkeletalAnimationTemplate.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Runtime/AnimGraphRuntime/Public/AnimSequencerInstance.h"
#include "AnimCustomInstanceHelper.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "IMovieScenePlayer.h"
#include "UObject/ObjectKey.h"
#include "Rendering/MotionVectorSimulation.h"
#include "Systems/MovieSceneMotionVectorSimulationSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "UObject/StrongObjectPtr.h"
#include "SkeletalMeshRestoreState.h"
#include "AnimSequencerInstanceProxy.h"

bool ShouldUsePreviewPlayback(IMovieScenePlayer& Player, UObject& RuntimeObject)
{
	// we also use PreviewSetAnimPosition in PIE when not playing, as we can preview in PIE
	bool bIsNotInPIEOrNotPlaying = (RuntimeObject.GetWorld() && !RuntimeObject.GetWorld()->HasBegunPlay()) || Player.GetPlaybackStatus() != EMovieScenePlayerStatus::Playing;
	return GIsEditor && bIsNotInPIEOrNotPlaying;
}

bool CanPlayAnimation(USkeletalMeshComponent* SkeletalMeshComponent, UAnimSequenceBase* AnimAssetBase)
{
	return (SkeletalMeshComponent->SkeletalMesh && SkeletalMeshComponent->SkeletalMesh->GetSkeleton() && 
		(!AnimAssetBase || SkeletalMeshComponent->SkeletalMesh->GetSkeleton()->IsCompatible(AnimAssetBase->GetSkeleton())));
}

void ResetAnimSequencerInstance(UObject& InObject, const UE::MovieScene::FRestoreStateParams& Params)
{
	CastChecked<ISequencerAnimationSupport>(&InObject)->ResetNodes();
}

UAnimSequencerInstance* GetAnimSequencerInstance(USkeletalMeshComponent* SkeletalMeshComponent) 
{
	ISequencerAnimationSupport* SeqInterface = Cast<ISequencerAnimationSupport>(SkeletalMeshComponent->GetAnimInstance());
	if (SeqInterface)
	{
		return Cast<UAnimSequencerInstance>(SeqInterface->GetSourceAnimInstance());
	}

	return nullptr;
}

UAnimInstance* GetSourceAnimInstance(USkeletalMeshComponent* SkeletalMeshComponent) 
{
	UAnimInstance* SkelAnimInstance = SkeletalMeshComponent->GetAnimInstance();
	ISequencerAnimationSupport* SeqInterface = Cast<ISequencerAnimationSupport>(SkelAnimInstance);
	if (SeqInterface)
	{
		return SeqInterface->GetSourceAnimInstance();
	}

	return SkelAnimInstance;
}

struct FStopPlayingMontageTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	TWeakObjectPtr<UAnimInstance> TempInstance;
	int32 TempMontageInstanceId;

	FStopPlayingMontageTokenProducer(TWeakObjectPtr<UAnimInstance> InTempInstance, int32 InTempMontageInstanceId)
	: TempInstance(InTempInstance)
	, TempMontageInstanceId(InTempMontageInstanceId){}

	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const
	{
		struct FToken : IMovieScenePreAnimatedToken
		{
			TWeakObjectPtr<UAnimInstance> WeakInstance;
			int32 MontageInstanceId;

			FToken(TWeakObjectPtr<UAnimInstance> InWeakInstance, int32 InMontageInstanceId) 
			: WeakInstance(InWeakInstance)
			, MontageInstanceId(InMontageInstanceId) {}

			virtual void RestoreState(UObject& InObject, const UE::MovieScene::FRestoreStateParams& Params) override
			{
				UAnimInstance* AnimInstance = WeakInstance.Get();
				if (AnimInstance)
				{
					FAnimMontageInstance* MontageInstance = AnimInstance->GetMontageInstanceForID(MontageInstanceId);
					if (MontageInstance)
					{
						MontageInstance->Stop(FAlphaBlend(0.f), false);
					}
				}
			}
		};

		return FToken(TempInstance, TempMontageInstanceId);
	}
};


struct FPreAnimatedAnimationTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const
	{
		struct FToken : IMovieScenePreAnimatedToken
		{
			FToken(USkeletalMeshComponent* InComponent)
			{
				// Cache this object's current update flag and animation mode
				AnimationMode = InComponent->GetAnimationMode();
				CachedAnimInstance.Reset(InComponent->AnimScriptInstance);
				SkeletalMeshRestoreState.SaveState(InComponent);
			}

			virtual void RestoreState(UObject& ObjectToRestore, const UE::MovieScene::FRestoreStateParams& Params)
			{
				USkeletalMeshComponent* Component = CastChecked<USkeletalMeshComponent>(&ObjectToRestore);

				ISequencerAnimationSupport* SequencerInst = Cast<ISequencerAnimationSupport>(GetAnimSequencerInstance(Component));
				if (SequencerInst)
				{
					SequencerInst->ResetPose();
					SequencerInst->ResetNodes();
				}

				FAnimCustomInstanceHelper::UnbindFromSkeletalMeshComponent<UAnimSequencerInstance>(Component);

				// Restore LOD before reinitializing anim instance
				SkeletalMeshRestoreState.RestoreLOD(Component);

				if (Component->GetAnimationMode() != AnimationMode)
				{
					// this SetAnimationMode reinitializes even if the mode is same
					// if we're using same anim blueprint, we don't want to keep reinitializing it. 
					Component->SetAnimationMode(AnimationMode);
				}
				if (CachedAnimInstance.Get())
				{
					Component->AnimScriptInstance = CachedAnimInstance.Get();
					CachedAnimInstance.Reset();
					if (Component->AnimScriptInstance && Component->SkeletalMesh)
					{
						//the skeleton may have changed so need to recalc required bones as needed.
						Component->AnimScriptInstance->CurrentSkeleton = Component->SkeletalMesh->GetSkeleton();
						//Need at least RecalcRequiredbones and UpdateMorphTargetrs
						Component->InitializeAnimScriptInstance(true);
					}
				}

				// Restore pose after unbinding to force the restored pose
				Component->SetUpdateAnimationInEditor(true);
				Component->SetUpdateClothInEditor(true);
				Component->TickAnimation(0.f, false);

				Component->RefreshBoneTransforms();
				Component->RefreshSlaveComponents();
				Component->UpdateComponentToWorld();
				Component->FinalizeBoneTransform();
				Component->MarkRenderTransformDirty();
				Component->MarkRenderDynamicDataDirty();
						
				// Reset the mesh component update flag and animation mode to what they were before we animated the object
				SkeletalMeshRestoreState.RestoreState(Component);

				// if not game world, don't clean this up
				if (Component->GetWorld() != nullptr && Component->GetWorld()->IsGameWorld() == false)
				{
					Component->ClearMotionVector();
				}
			}

			EAnimationMode::Type AnimationMode;
			TStrongObjectPtr<UAnimInstance> CachedAnimInstance;

			FSkeletalMeshRestoreState SkeletalMeshRestoreState;

		};

		return FToken(CastChecked<USkeletalMeshComponent>(&Object));
	}
};


struct FMinimalAnimParameters
{
	FMinimalAnimParameters(UAnimSequenceBase* InAnimation, float InFromEvalTime, float InToEvalTime, float InBlendWeight, const FMovieSceneEvaluationScope& InScope, FName InSlotName,
		FObjectKey InSection, bool InSkipAnimationNotifiers, bool InForceCustomMode,
		const UMovieSceneSkeletalAnimationSection* InAnimSection, FFrameTime InSectionTime)
		: Animation(InAnimation)
		, FromEvalTime(InFromEvalTime)
		, ToEvalTime(InToEvalTime)
		, BlendWeight(InBlendWeight)
		, EvaluationScope(InScope)
		, SlotName(InSlotName)
		, Section(InSection)
		, bSkipAnimNotifiers(InSkipAnimationNotifiers)
		, bForceCustomMode(InForceCustomMode)
		, AnimSection(InAnimSection)
		, SectionTime(InSectionTime)

	{}

	UAnimSequenceBase* Animation;
	float FromEvalTime;
	float ToEvalTime;
	float BlendWeight;
	FMovieSceneEvaluationScope EvaluationScope;
	FName SlotName;
	FObjectKey Section;
	bool bSkipAnimNotifiers;
	bool bForceCustomMode;
	const UMovieSceneSkeletalAnimationSection* AnimSection;
	FFrameTime SectionTime;

};

struct FSimulatedAnimParameters
{
	FMinimalAnimParameters AnimParams;
};

/** Montage player per section data */
struct FMontagePlayerPerSectionData 
{
	TWeakObjectPtr<UAnimMontage> Montage;
	int32 MontageInstanceId;
};

namespace UE
{
namespace MovieScene
{
	struct FBlendedAnimation
	{
		TArray<FMinimalAnimParameters> SimulatedAnimations;
		TArray<FMinimalAnimParameters> AllAnimations;

		FBlendedAnimation& Resolve(TMovieSceneInitialValueStore<FBlendedAnimation>& InitialValueStore)
		{
			return *this;
		}
	};

	void BlendValue(FBlendedAnimation& OutBlend, const FMinimalAnimParameters& InValue, float Weight, EMovieSceneBlendType BlendType, TMovieSceneInitialValueStore<FBlendedAnimation>& InitialValueStore)
	{
		OutBlend.AllAnimations.Add(InValue);
	}
	void BlendValue(FBlendedAnimation& OutBlend, const FSimulatedAnimParameters& InValue, float Weight, EMovieSceneBlendType BlendType, TMovieSceneInitialValueStore<FBlendedAnimation>& InitialValueStore)
	{
		OutBlend.SimulatedAnimations.Add(InValue.AnimParams);
	}

	struct FComponentAnimationActuator : TMovieSceneBlendingActuator<FBlendedAnimation>
	{
		FComponentAnimationActuator() : TMovieSceneBlendingActuator<FBlendedAnimation>(GetActuatorTypeID()) {}

		static FMovieSceneBlendingActuatorID GetActuatorTypeID()
		{
			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FComponentAnimationActuator, 0>();
			return FMovieSceneBlendingActuatorID(TypeID);
		}

		static FMovieSceneAnimTypeID GetAnimControlTypeID()
		{
			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FComponentAnimationActuator, 2>();
			return TypeID;
		}

		virtual FBlendedAnimation RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const override
		{
			check(false);
			return FBlendedAnimation();
		}

		virtual void Actuate(UObject* InObject, const FBlendedAnimation& InFinalValue, const TBlendableTokenStack<FBlendedAnimation>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
		{
			ensureMsgf(InObject, TEXT("Attempting to evaluate an Animation track with a null object."));

			USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshComponentFromObject(InObject);
			if (!SkeletalMeshComponent)
			{
				return;
			}
			OriginalStack.SavePreAnimatedState(Player, *SkeletalMeshComponent, GetAnimControlTypeID(), FPreAnimatedAnimationTokenProducer());

			UAnimInstance* ExistingAnimInstance = GetSourceAnimInstance(SkeletalMeshComponent);
			bool bWasCreated = false;
			ISequencerAnimationSupport* SequencerInstance = FAnimCustomInstanceHelper::BindToSkeletalMeshComponent<UAnimSequencerInstance>(SkeletalMeshComponent,bWasCreated);
			if (SequencerInstance)
			{
				if (bWasCreated)
				{
					SequencerInstance->SavePose();
				}
				else
				{
					SequencerInstance->ConstructNodes();
				}
			}

			const bool bPreviewPlayback = ShouldUsePreviewPlayback(Player, *SkeletalMeshComponent);

			const EMovieScenePlayerStatus::Type PlayerStatus = Player.GetPlaybackStatus();

			// If the playback status is jumping, ie. one such occurrence is setting the time for thumbnail generation, disable anim notifies updates because it could fire audio.
			// If the playback status is scrubbing, we disable notifies for now because we can't properly fire them in all cases until we get evaluation range info.
			// We now layer this with the passed in notify toggle to force a disable in this case.
			const bool bFireNotifies = !bPreviewPlayback || (PlayerStatus != EMovieScenePlayerStatus::Jumping && PlayerStatus != EMovieScenePlayerStatus::Stopped && PlayerStatus != EMovieScenePlayerStatus::Scrubbing);

			// When jumping from one cut to another cut, the delta time should be 0 so that anim notifies before the current position are not evaluated. Note, anim notifies at the current time should still be evaluated.
			const double DeltaTime = ( Context.HasJumped() ? FFrameTime(0) : Context.GetRange().Size<FFrameTime>() ) / Context.GetFrameRate();

			const bool bResetDynamics = PlayerStatus == EMovieScenePlayerStatus::Stepping || 
										PlayerStatus == EMovieScenePlayerStatus::Jumping || 
										PlayerStatus == EMovieScenePlayerStatus::Scrubbing || 
										(DeltaTime == 0.0f && PlayerStatus != EMovieScenePlayerStatus::Stopped); 

			//Need to zero all weights first since we may be blending animation that are keeping state but are no longer active.
			
			if(SequencerInstance)
			{
				SequencerInstance->ResetNodes();
			}
			else if (ExistingAnimInstance)
			{
				for (const TPair<FObjectKey, FMontagePlayerPerSectionData >& Pair : MontageData)
				{
					int32 InstanceId = Pair.Value.MontageInstanceId;
					FAnimMontageInstance* MontageInstanceToUpdate = ExistingAnimInstance->GetMontageInstanceForID(InstanceId);
					if (MontageInstanceToUpdate)
					{
						MontageInstanceToUpdate->SetDesiredWeight(0.0f);
						MontageInstanceToUpdate->SetWeight(0.0f);
					}
				}
			}

			if (InFinalValue.SimulatedAnimations.Num() != 0)
			{
				UMovieSceneMotionVectorSimulationSystem* MotionVectorSim = Player.GetEvaluationTemplate().GetEntitySystemLinker()->FindSystem<UMovieSceneMotionVectorSimulationSystem>();
				if (MotionVectorSim && MotionVectorSim->IsSimulationEnabled())
				{
					ApplyAnimations(PersistentData, Player, SkeletalMeshComponent, InFinalValue.SimulatedAnimations, DeltaTime, bPreviewPlayback, bFireNotifies, bResetDynamics);
					SkeletalMeshComponent->TickAnimation(0.f, false);
					SkeletalMeshComponent->ForceMotionVector();

					SimulateMotionVectors(PersistentData, SkeletalMeshComponent, MotionVectorSim);
				}
			}

			ApplyAnimations(PersistentData, Player, SkeletalMeshComponent, InFinalValue.AllAnimations, DeltaTime, bPreviewPlayback, bFireNotifies, bResetDynamics);

			// If the skeletal component has already ticked this frame because tick prerequisites weren't set up yet or a new binding was created, forcibly tick this component to update.
			// This resolves first frame issues where the skeletal component ticks first, then the sequencer binding is resolved which sets up tick prerequisites
			// for the next frame.
			if (SkeletalMeshComponent->PoseTickedThisFrame() || (SequencerInstance && SequencerInstance->GetSourceAnimInstance() != ExistingAnimInstance))
			{
				SkeletalMeshComponent->TickAnimation(0.f, false);

				SkeletalMeshComponent->RefreshBoneTransforms();
				SkeletalMeshComponent->RefreshSlaveComponents();
				SkeletalMeshComponent->UpdateComponentToWorld();
				SkeletalMeshComponent->FinalizeBoneTransform();
				SkeletalMeshComponent->MarkRenderTransformDirty();
				SkeletalMeshComponent->MarkRenderDynamicDataDirty();
			}
		}

	private:

		static USkeletalMeshComponent* SkeletalMeshComponentFromObject(UObject* InObject)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InObject);
			if (SkeletalMeshComponent)
			{
				return SkeletalMeshComponent;
			}

			// then check to see if we are controlling an actor & if so use its first USkeletalMeshComponent 
			AActor* Actor = Cast<AActor>(InObject);

			if (!Actor)
			{
				if (UChildActorComponent* ChildActorComponent = Cast<UChildActorComponent>(InObject))
				{
					Actor = ChildActorComponent->GetChildActor();
				}
			}

			if (Actor)
			{
				return Actor->FindComponentByClass<USkeletalMeshComponent>();
			}
			return nullptr;
		}

		void SimulateMotionVectors(FPersistentEvaluationData& PersistentData, USkeletalMeshComponent* SkeletalMeshComponent, UMovieSceneMotionVectorSimulationSystem* MotionVectorSim)
		{
			for (USceneComponent* Child : SkeletalMeshComponent->GetAttachChildren())
			{
				FName SocketName = Child->GetAttachSocketName();
				if (SocketName != NAME_None)
				{
					FTransform SocketTransform = SkeletalMeshComponent->GetSocketTransform(SocketName, RTS_Component);
					MotionVectorSim->AddSimulatedTransform(SkeletalMeshComponent, SocketTransform, SocketName);
				}
			}
		}

		void ApplyAnimations(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, USkeletalMeshComponent* SkeletalMeshComponent,
			TArrayView<const FMinimalAnimParameters> Parameters, float DeltaTime, bool bPreviewPlayback, bool bFireNotifies, bool bResetDynamics)
		{
			const EMovieScenePlayerStatus::Type PlayerStatus = Player.GetPlaybackStatus();

			for (const FMinimalAnimParameters& AnimParams : Parameters)
			{
				FScopedPreAnimatedCaptureSource CaptureSource(&Player.PreAnimatedState, AnimParams.EvaluationScope.Key, AnimParams.EvaluationScope.CompletionMode == EMovieSceneCompletionMode::RestoreState);

				if (bPreviewPlayback)
				{
					PreviewSetAnimPosition(PersistentData, Player, SkeletalMeshComponent,
						AnimParams.SlotName, AnimParams.Section, AnimParams.Animation, AnimParams.FromEvalTime, AnimParams.ToEvalTime, AnimParams.BlendWeight,
						bFireNotifies && !AnimParams.bSkipAnimNotifiers, DeltaTime, PlayerStatus == EMovieScenePlayerStatus::Playing,
						bResetDynamics, AnimParams.bForceCustomMode, AnimParams.AnimSection, AnimParams.SectionTime
					);
				}
				else
				{
					SetAnimPosition(PersistentData, Player, SkeletalMeshComponent,
						AnimParams.SlotName, AnimParams.Section, AnimParams.Animation, AnimParams.FromEvalTime, AnimParams.ToEvalTime, AnimParams.BlendWeight,
						PlayerStatus == EMovieScenePlayerStatus::Playing, bFireNotifies && !AnimParams.bSkipAnimNotifiers,
						AnimParams.bForceCustomMode, AnimParams.AnimSection, AnimParams.SectionTime
					);
				}
			}
		}
		
		void SetAnimPosition(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, USkeletalMeshComponent* SkeletalMeshComponent, FName SlotName, FObjectKey Section, UAnimSequenceBase* InAnimSequence, float InFromPosition, float InToPosition, float Weight, 
			bool bPlaying, bool bFireNotifies, bool bForceCustomMode, const UMovieSceneSkeletalAnimationSection* AnimSection, FFrameTime CurrentTime)
		{
			static const bool bLooping = false;

			if (!CanPlayAnimation(SkeletalMeshComponent, InAnimSequence))
			{
				return;
			}
			if (bForceCustomMode)
			{
				SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationCustomMode);
			}
			UAnimSequencerInstance* SequencerInst = GetAnimSequencerInstance(SkeletalMeshComponent);
			if (SequencerInst)
			{
				FMovieSceneAnimTypeID AnimTypeID = SectionToAnimationIDs.GetAnimTypeID(Section);

				Player.SavePreAnimatedState(*SequencerInst, AnimTypeID, FStatelessPreAnimatedTokenProducer(&ResetAnimSequencerInstance));
				TOptional <FRootMotionOverride> RootMotion;
				bool bBlendFirstChildOfRoot = false;
				TOptional<FTransform> RootMotionTransform = AnimSection->GetRootMotion(CurrentTime, bBlendFirstChildOfRoot);
				if (RootMotionTransform.IsSet())
				{
					RootMotion = FRootMotionOverride();
					RootMotion.GetValue().RootMotion = RootMotionTransform.GetValue();
					RootMotion.GetValue().bBlendFirstChildOfRoot = bBlendFirstChildOfRoot;
				}

				SequencerInst->UpdateAnimTrackWithRootMotion(InAnimSequence, GetTypeHash(AnimTypeID),RootMotion, InFromPosition, InToPosition, Weight, bFireNotifies);

			}
			else if (UAnimInstance* AnimInst = GetSourceAnimInstance(SkeletalMeshComponent))
			{
				FMontagePlayerPerSectionData* SectionData = MontageData.Find(Section);

				int32 InstanceId = (SectionData) ? SectionData->MontageInstanceId : INDEX_NONE;

				const float AssetPlayRate = FMath::IsNearlyZero(InAnimSequence->RateScale) ? 1.0f : InAnimSequence->RateScale;
				TWeakObjectPtr<UAnimMontage> Montage = FAnimMontageInstance::SetSequencerMontagePosition(SlotName, AnimInst, InstanceId, InAnimSequence, InFromPosition / AssetPlayRate, InToPosition / AssetPlayRate, Weight, bLooping, bPlaying);

				if (Montage.IsValid())
				{
					FMontagePlayerPerSectionData& DataContainer = MontageData.FindOrAdd(Section);
					DataContainer.Montage = Montage;
					DataContainer.MontageInstanceId = InstanceId;

					FMovieSceneAnimTypeID SlotTypeID = SectionToAnimationIDs.GetAnimTypeID(Section);
					Player.SavePreAnimatedState(*Montage.Get(), SlotTypeID, FStopPlayingMontageTokenProducer(AnimInst, InstanceId));

					// make sure it's playing if the sequence is
					FAnimMontageInstance* Instance = AnimInst->GetMontageInstanceForID(InstanceId);
					Instance->bPlaying = bPlaying;
				}
			}
		}

		void PreviewSetAnimPosition(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, USkeletalMeshComponent* SkeletalMeshComponent,
			FName SlotName, FObjectKey Section, UAnimSequenceBase* InAnimSequence, float InFromPosition, float InToPosition, float Weight,
			bool bFireNotifies, float DeltaTime, bool bPlaying, bool bResetDynamics, bool bForceCustomMode, const UMovieSceneSkeletalAnimationSection* AnimSection,
			FFrameTime CurrentTime)
		{
			static const bool bLooping = false;

			if (!CanPlayAnimation(SkeletalMeshComponent, InAnimSequence))
			{
				return;
			}
			if (bForceCustomMode)
			{
				SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationCustomMode);
			}
			UAnimSequencerInstance* SequencerInst = GetAnimSequencerInstance(SkeletalMeshComponent);
			if (SequencerInst)
			{
				// Unique anim type ID per slot
				FMovieSceneAnimTypeID AnimTypeID = SectionToAnimationIDs.GetAnimTypeID(Section);
				Player.SavePreAnimatedState(*SequencerInst, AnimTypeID, FStatelessPreAnimatedTokenProducer(&ResetAnimSequencerInstance));

				// Set position and weight
				TOptional <FRootMotionOverride> RootMotion;
				bool bBlendFirstChildOfRoot = false;
				TOptional<FTransform> RootMotionTransform = AnimSection->GetRootMotion(CurrentTime, bBlendFirstChildOfRoot);
				if (RootMotionTransform.IsSet())
				{
					RootMotion = FRootMotionOverride();
					RootMotion.GetValue().RootMotion = RootMotionTransform.GetValue();
					RootMotion.GetValue().bBlendFirstChildOfRoot = bBlendFirstChildOfRoot;
				}
				SequencerInst->UpdateAnimTrackWithRootMotion(InAnimSequence, GetTypeHash(AnimTypeID), RootMotion, InFromPosition, InToPosition, Weight, bFireNotifies);

			}
			else if (UAnimInstance* AnimInst = GetSourceAnimInstance(SkeletalMeshComponent))
			{
				FMontagePlayerPerSectionData* SectionData = MontageData.Find(Section);

				int32 InstanceId = (SectionData)? SectionData->MontageInstanceId : INDEX_NONE;

				const float AssetPlayRate = FMath::IsNearlyZero(InAnimSequence->RateScale) ? 1.0f : InAnimSequence->RateScale;
				TWeakObjectPtr<UAnimMontage> Montage = FAnimMontageInstance::PreviewSequencerMontagePosition(SlotName, SkeletalMeshComponent, AnimInst, InstanceId, InAnimSequence, InFromPosition / AssetPlayRate, InToPosition / AssetPlayRate, Weight, bLooping, bFireNotifies, bPlaying);

				if (Montage.IsValid())
				{
					FMontagePlayerPerSectionData& DataContainer = MontageData.FindOrAdd(Section);
					DataContainer.Montage = Montage;
					DataContainer.MontageInstanceId = InstanceId;

					FMovieSceneAnimTypeID AnimTypeID = SectionToAnimationIDs.GetAnimTypeID(InAnimSequence);
					Player.SavePreAnimatedState(*Montage.Get(), AnimTypeID, FStopPlayingMontageTokenProducer(AnimInst, InstanceId));

					FAnimMontageInstance* Instance = AnimInst->GetMontageInstanceForID(InstanceId);
					Instance->bPlaying = bPlaying;
				}
	
				if (bResetDynamics)
				{
					// make sure we reset any simulations
					AnimInst->ResetDynamics(ETeleportType::ResetPhysics);
				}
			}
		}

		TMovieSceneAnimTypeIDContainer<FObjectKey> SectionToAnimationIDs;
		TMap<FObjectKey, FMontagePlayerPerSectionData> MontageData;
	};

} // namespace MovieScene
} // namespace UE

template<> FMovieSceneAnimTypeID GetBlendingDataType<UE::MovieScene::FBlendedAnimation>()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneSkeletalAnimationSectionTemplate::FMovieSceneSkeletalAnimationSectionTemplate(const UMovieSceneSkeletalAnimationSection& InSection)
	: Params(InSection.Params, InSection.GetInclusiveStartFrame(), InSection.GetExclusiveEndFrame())
{
}

void FMovieSceneSkeletalAnimationSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	if (Params.Animation)
	{
		FOptionalMovieSceneBlendType BlendType = GetSourceSection()->GetBlendType();
		check(BlendType.IsValid());

		// Ensure the accumulator knows how to actually apply component transforms
		FMovieSceneBlendingActuatorID ActuatorTypeID = UE::MovieScene::FComponentAnimationActuator::GetActuatorTypeID();
		FMovieSceneBlendingAccumulator& Accumulator = ExecutionTokens.GetBlendingAccumulator();
		if (!Accumulator.FindActuator<UE::MovieScene::FBlendedAnimation>(ActuatorTypeID))
		{
			Accumulator.DefineActuator(ActuatorTypeID, MakeShared<UE::MovieScene::FComponentAnimationActuator>());
		}

		// Calculate the time at which to evaluate the animation
		float EvalTime = Params.MapTimeToAnimation(Context.GetTime(), Context.GetFrameRate());
		float PreviousEvalTime = Params.MapTimeToAnimation(Context.GetPreviousTime(), Context.GetFrameRate());

		float ManualWeight = 1.f;
		Params.Weight.Evaluate(Context.GetTime(), ManualWeight);

		const float Weight = ManualWeight * EvaluateEasing(Context.GetTime());

		const UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(GetSourceSection());
		// Add the blendable to the accumulator
		FMinimalAnimParameters AnimParams(
			Params.Animation, PreviousEvalTime, EvalTime, Weight, ExecutionTokens.GetCurrentScope(), Params.SlotName, GetSourceSection(), Params.bSkipAnimNotifiers, 
			Params.bForceCustomMode, AnimSection,  Context.GetTime()
		);
		ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<UE::MovieScene::FBlendedAnimation>(AnimParams, BlendType.Get(), 1.f));

		if (FMotionVectorSimulation::IsEnabled())
		{
			FFrameTime SimulatedTime = UE::MovieScene::GetSimulatedMotionVectorTime(Context);

			// Calculate the time at which to evaluate the animation
			float CurrentEvalTime = Params.MapTimeToAnimation(Context.GetTime(), Context.GetFrameRate());
			float SimulatedEvalTime = Params.MapTimeToAnimation(SimulatedTime, Context.GetFrameRate());

			float SimulatedManualWeight = 1.f;
			Params.Weight.Evaluate(SimulatedTime, SimulatedManualWeight);

			const float SimulatedWeight = SimulatedManualWeight * EvaluateEasing(SimulatedTime);

			FSimulatedAnimParameters SimulatedAnimParams{ AnimParams };
			SimulatedAnimParams.AnimParams.FromEvalTime = CurrentEvalTime;
			SimulatedAnimParams.AnimParams.ToEvalTime = SimulatedEvalTime;
			SimulatedAnimParams.AnimParams.BlendWeight = SimulatedWeight;
			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<UE::MovieScene::FBlendedAnimation>(SimulatedAnimParams, BlendType.Get(), 1.f));
		}
	}
}

float FMovieSceneSkeletalAnimationSectionTemplateParameters::MapTimeToAnimation(FFrameTime InPosition, FFrameRate InFrameRate) const
{
	const FFrameTime AnimationLength = GetSequenceLength() * InFrameRate;
	const int32 LengthInFrames = AnimationLength.FrameNumber.Value + (int)(AnimationLength.GetSubFrame() + 0.5f) + 1;
	//we only play end if we are not looping, and assuming we are looping if Length is greater than default length;
	const bool bLooping = (SectionEndTime.Value - SectionStartTime.Value + StartFrameOffset + EndFrameOffset) > LengthInFrames;

	InPosition = FMath::Clamp(InPosition, FFrameTime(SectionStartTime), FFrameTime(SectionEndTime -1));
	
	const float SectionPlayRate = PlayRate * Animation->RateScale;
	const float AnimPlayRate = FMath::IsNearlyZero(SectionPlayRate) ? 1.0f : SectionPlayRate;

	const float FirstLoopSeqLength = GetSequenceLength() - InFrameRate.AsSeconds(FirstLoopStartFrameOffset + StartFrameOffset + EndFrameOffset);
	const float SeqLength = GetSequenceLength() - InFrameRate.AsSeconds(StartFrameOffset + EndFrameOffset);

	float AnimPosition = FFrameTime::FromDecimal((InPosition - SectionStartTime).AsDecimal() * AnimPlayRate) / InFrameRate;
	AnimPosition += InFrameRate.AsSeconds(FirstLoopStartFrameOffset);
	if (SeqLength > 0.f && (bLooping || !FMath::IsNearlyEqual(AnimPosition, SeqLength,1e-4f)))
	{
		AnimPosition = FMath::Fmod(AnimPosition, SeqLength);
	}
	AnimPosition += InFrameRate.AsSeconds(StartFrameOffset);
	if (bReverse)
	{
		AnimPosition = GetSequenceLength() - AnimPosition;
	}

	return AnimPosition;
}
