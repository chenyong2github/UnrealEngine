// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimTypes.h"
#include "AnimationUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Containers/ArrayView.h"
#include "AnimNotifyState_MotionWarping.h"
#include "ContextualAnimUtilities.h"
#include "RootMotionModifier.h"
#include "ContextualAnimSelectionCriterion.h"
#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimSceneAsset.h"
#include "GameFramework/Character.h"

DEFINE_LOG_CATEGORY(LogContextualAnim);

const FContextualAnimSceneBinding FContextualAnimSceneBinding::InvalidBinding;
const FContextualAnimTrack FContextualAnimTrack::EmptyTrack;
const FContextualAnimIKTarget FContextualAnimIKTarget::InvalidIKTarget;
const FContextualAnimIKTargetDefContainer FContextualAnimIKTargetDefContainer::EmptyContainer;

// FContextualAnimAlignmentTrackContainer
///////////////////////////////////////////////////////////////////////

FTransform FContextualAnimAlignmentTrackContainer::ExtractTransformAtTime(const FName& TrackName, float Time) const
{
	const int32 TrackIndex = Tracks.TrackNames.IndexOfByKey(TrackName);
	return ExtractTransformAtTime(TrackIndex, Time);
}

FTransform FContextualAnimAlignmentTrackContainer::ExtractTransformAtTime(int32 TrackIndex, float Time) const
{
	FTransform AlignmentTransform = FTransform::Identity;

	if (Tracks.AnimationTracks.IsValidIndex(TrackIndex))
	{
		const FRawAnimSequenceTrack& Track = Tracks.AnimationTracks[TrackIndex];
		const int32 TotalFrames = Track.PosKeys.Num();
		const float TrackLength = (TotalFrames - 1) * SampleInterval;
		FAnimationUtils::ExtractTransformFromTrack(Time, TotalFrames, TrackLength, Track, EAnimInterpolationType::Linear, AlignmentTransform);
	}

	return AlignmentTransform;
}

float FContextualAnimTrack::GetSyncTimeForWarpSection(int32 WarpSectionIndex) const
{
	float StartTime, EndTime;
	GetStartAndEndTimeForWarpSection(WarpSectionIndex, StartTime, EndTime);
	return EndTime;
}

float FContextualAnimTrack::GetSyncTimeForWarpSection(const FName& WarpSectionName) const
{
	float StartTime, EndTime;
	GetStartAndEndTimeForWarpSection(WarpSectionName, StartTime, EndTime);
	return EndTime;
}

void FContextualAnimTrack::GetStartAndEndTimeForWarpSection(int32 WarpSectionIndex, float& OutStartTime, float& OutEndTime) const
{
	//@TODO: We need a better way to identify warping sections withing the animation. This is just a temp solution
	//@TODO: We should cache this data

	OutStartTime = 0.f;
	OutEndTime = 0.f;

	if (Animation && WarpSectionIndex >= 0)
	{
		FName LastWarpTargetName = NAME_None;
		int32 LastWarpSectionIndex = INDEX_NONE;

		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if (const UAnimNotifyState_MotionWarping* Notify = Cast<const UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				if (const URootMotionModifier_Warp* Modifier = Cast<const URootMotionModifier_Warp>(Notify->RootMotionModifier))
				{
					const FName WarpTargetName = Modifier->WarpTargetName;
					if (WarpTargetName != NAME_None)
					{
						// First valid warping window. Initialize everything
						if (LastWarpSectionIndex == INDEX_NONE)
						{
							LastWarpTargetName = WarpTargetName;
							OutStartTime = NotifyEvent.GetTriggerTime();
							OutEndTime = NotifyEvent.GetEndTriggerTime();
							LastWarpSectionIndex = 0;
						}
						// If we hit another warping window but the sync point is the same as the previous, update SyncTime.
						// This is to deal with cases where a first short window is used to face the alignment point and a second one to perform the rest of the warp
						else if (WarpTargetName == LastWarpTargetName)
						{
							OutStartTime = NotifyEvent.GetTriggerTime();
							OutEndTime = NotifyEvent.GetEndTriggerTime();
						}
						// If we hit another warping window but with a different sync point name means that we have hit the first window of another warping section
						else
						{
							// If we haven't reached the desired WarpSection yet. Update control vars and keep moving
							if (WarpSectionIndex > LastWarpSectionIndex)
							{
								LastWarpTargetName = WarpTargetName;
								OutStartTime = NotifyEvent.GetTriggerTime();
								OutEndTime = NotifyEvent.GetEndTriggerTime();
								LastWarpSectionIndex++;
							}
							// Otherwise, stop here and return the value of the last window we found
							else
							{
								break;
							}
						}
					}
				}
			}
		}
	}
}

void FContextualAnimTrack::GetStartAndEndTimeForWarpSection(const FName& WarpSectionName, float& OutStartTime, float& OutEndTime) const
{
	//@TODO: We need a better way to identify warping sections within the animation. This is just a temp solution
	//@TODO: We should cache this data

	OutStartTime = 0.f;
	OutEndTime = 0.f;

	int32 Index = INDEX_NONE;
	float LastEndTime = 0.f;
	if (Animation && WarpSectionName != NAME_None)
	{
		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if (const UAnimNotifyState_MotionWarping* Notify = Cast<const UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				if (const URootMotionModifier_Warp* Config = Cast<const URootMotionModifier_Warp>(Notify->RootMotionModifier))
				{
					const FName WarpTargetName = Config->WarpTargetName;
					if (WarpSectionName == WarpTargetName)
					{
						const float NotifyEndTriggerTime = NotifyEvent.GetEndTriggerTime();
						if(NotifyEndTriggerTime > LastEndTime)
						{
							LastEndTime = NotifyEndTriggerTime;
							Index = Idx;
						}
					}
				}
			}
		}
	}

	if(Index != INDEX_NONE)
	{
		const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Index];
		OutStartTime = NotifyEvent.GetTriggerTime();
		OutEndTime = NotifyEvent.GetEndTriggerTime();
	}
}

float FContextualAnimTrack::FindBestAnimStartTime(const FVector& LocalLocation) const
{
	float BestTime = 0.f;

	if (AnimMaxStartTime < 0.f)
	{
		return BestTime;
	}

	const FVector SyncPointLocation = GetAlignmentTransformAtSyncTime().GetLocation();
	const float PerfectDistToSyncPointSq = GetAlignmentTransformAtEntryTime().GetTranslation().SizeSquared2D();
	const float ActualDistToSyncPointSq = FVector::DistSquared2D(LocalLocation, SyncPointLocation);

	if (ActualDistToSyncPointSq < PerfectDistToSyncPointSq)
	{
		float BestDistance = MAX_FLT;
		TArrayView<const FVector3f> PosKeys(AlignmentData.Tracks.AnimationTracks[0].PosKeys.GetData(), AlignmentData.Tracks.AnimationTracks[0].PosKeys.Num());

		//@TODO: Very simple search for now. Replace with Distance Matching + Pose Matching
		for (int32 Idx = 0; Idx < PosKeys.Num(); Idx++)
		{
			const float Time = Idx * AlignmentData.SampleInterval;
			if (AnimMaxStartTime > 0.f && Time >= AnimMaxStartTime)
			{
				break;
			}

			const float DistFromCurrentFrameToSyncPointSq = FVector::DistSquared2D(SyncPointLocation, (FVector)PosKeys[Idx]);
			if (DistFromCurrentFrameToSyncPointSq < ActualDistToSyncPointSq)
			{
				BestTime = Time;
				break;
			}
		}
	}

	return BestTime;
}

bool FContextualAnimTrack::DoesQuerierPassSelectionCriteria(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	for (const UContextualAnimSelectionCriterion* Criterion : SelectionCriteria)
	{
		if (Criterion && !Criterion->DoesQuerierPassCondition(Primary, Querier))
		{
			return false;
		}
	}

	return true;
}

FTransform FContextualAnimTrack::GetRootTransformAtTime(float Time) const
{
	FTransform RootTransform = FTransform::Identity;
	if (Animation)
	{
		RootTransform = UContextualAnimUtilities::ExtractRootTransformFromAnimation(Animation, Time);
	}

	return RootTransform * MeshToScene;
}

// FContextualAnimSceneBindingContext
///////////////////////////////////////////////////////////////////////

void FContextualAnimSceneBindingContext::SetExternalTransform(const FTransform& InTransform)
{
	ExternalTransform = InTransform;
}

FTransform FContextualAnimSceneBindingContext::GetTransform() const
{
	if (ExternalTransform.IsSet())
	{
		return ExternalTransform.GetValue();
	}
	else if (AActor* ActorPtr = GetActor())
	{
		return ActorPtr->GetActorTransform();
	}

	return FTransform::Identity;
}

FVector FContextualAnimSceneBindingContext::GetVelocity() const
{
	if (ExternalVelocity.IsSet())
	{
		return ExternalVelocity.GetValue();
	}
	else if (AActor* ActorPtr = GetActor())
	{
		return ActorPtr->GetVelocity();
	}

	return FVector::ZeroVector;
}

// FContextualAnimSceneBinding
///////////////////////////////////////////////////////////////////////

FContextualAnimSceneBinding::FContextualAnimSceneBinding(const FContextualAnimSceneBindingContext& InContext, const UContextualAnimSceneAsset& InSceneAsset, const FContextualAnimTrack& InAnimTrack)
{
	check(InSceneAsset.HasValidData());

	Context = InContext;
	SceneAsset = &InSceneAsset;
	AnimTrackPtr = &InAnimTrack;
	RoleDefPtr = InSceneAsset.GetRolesAsset()->FindRoleDefinitionByName(InAnimTrack.Role);
	check(RoleDefPtr);
}

UContextualAnimSceneActorComponent* FContextualAnimSceneBinding::GetSceneActorComponent() const
{
	//@TODO: Cache this during the binding
	AActor* Actor = Context.GetActor();
	return Actor ? Actor->FindComponentByClass<UContextualAnimSceneActorComponent>() : nullptr;
}

UAnimInstance* FContextualAnimSceneBinding::GetAnimInstance() const
{
	return UContextualAnimUtilities::TryGetAnimInstance(GetActor());
}

USkeletalMeshComponent* FContextualAnimSceneBinding::GetSkeletalMeshComponent() const
{
	return UContextualAnimUtilities::TryGetSkeletalMeshComponent(GetActor());
}

FAnimMontageInstance* FContextualAnimSceneBinding::GetAnimMontageInstance() const
{
	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		return AnimInstance->GetActiveMontageInstance();
	}

	return nullptr;
}

float FContextualAnimSceneBinding::GetAnimMontageTime() const
{
	const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance();
	return MontageInstance ? MontageInstance->GetPosition() : -1.f;
}

FName FContextualAnimSceneBinding::GetCurrentSection() const
{
	const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance();
	return MontageInstance ? MontageInstance->GetCurrentSection() : NAME_None;
}

int32 FContextualAnimSceneBinding::GetCurrentSectionIndex() const
{
	if (const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance())
	{
		float CurrentPosition;
		return MontageInstance->Montage->GetAnimCompositeSectionIndexFromPos(MontageInstance->GetPosition(), CurrentPosition);
	}

	return INDEX_NONE;
}

const FContextualAnimIKTargetDefContainer& FContextualAnimSceneBinding::GetIKTargetDefs() const
{
	return GetSceneAsset().GetIKTargetDefsForRoleInSection(GetAnimTrack().SectionIdx, GetAnimTrack().Role);
}

// FContextualAnimSceneBindings
///////////////////////////////////////////////////////////////////////

bool FContextualAnimSceneBindings::TryCreateBindings(const UContextualAnimSceneAsset& SceneAsset, int32 SectionIdx, int32 AnimSetIdx, const TMap<FName, FContextualAnimSceneBindingContext>& Params, FContextualAnimSceneBindings& OutBindings)
{
	check(SceneAsset.HasValidData());

	OutBindings.Reset();

	// Find the actor that should be bound to the primary Role.
	const FName PrimaryRole = SceneAsset.GetPrimaryRole();
	const FContextualAnimSceneBindingContext* PrimaryPtr = Params.Find(PrimaryRole);
	if (PrimaryPtr == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimSceneBindings::TryCreateBindings Failed. Reason: Can't find valid actor for primary role. SceneAsset: %s PrimaryRole: %s"),
			*GetNameSafe(&SceneAsset), *PrimaryRole.ToString());

		return false;
	}

	// First, try to bind primary track. 
	// @TODO: Revisit this, passing the same data twice (as primary and querier) feels weird, but this allow us to run the selection mechanism even on the primary actor.

	const FContextualAnimTrack* PrimaryAnimTrack = SceneAsset.GetAnimTrack(SectionIdx, AnimSetIdx, PrimaryRole);
	if (PrimaryAnimTrack && PrimaryAnimTrack->DoesQuerierPassSelectionCriteria(*PrimaryPtr, *PrimaryPtr))
	{
		OutBindings.Add(FContextualAnimSceneBinding(*PrimaryPtr, SceneAsset, *PrimaryAnimTrack));
	}
	else
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimSceneBindings::TryCreateBindings Failed. Reason: Can't find valid track for primary actor. SceneAsset: %s Role: %s Actor: %s SectionIdx: %d AnimSetIdx: %d"),
			*GetNameSafe(&SceneAsset), *PrimaryRole.ToString(), *GetNameSafe(PrimaryPtr->GetActor()), SectionIdx, AnimSetIdx);

		return false;
	}

	// Now try to bind secondary tracks

	for (const auto& Pair : Params)
	{
		FName RoleToBind = Pair.Key;
		if (RoleToBind != PrimaryRole)
		{
			const FContextualAnimTrack* AnimTrack = SceneAsset.GetAnimTrack(SectionIdx, AnimSetIdx, RoleToBind);
			if (AnimTrack && AnimTrack->DoesQuerierPassSelectionCriteria(*PrimaryPtr, Pair.Value))
			{
				OutBindings.Add(FContextualAnimSceneBinding(Pair.Value, SceneAsset, *AnimTrack));
			}
			else
			{
				UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimSceneBindings::TryCreateBindings Failed. Reason: Can't find valid track for secondary actor. SceneAsset: %s Role: %s Actor: %s SectionIdx: %d AnimSetIdx: %d"),
					*GetNameSafe(&SceneAsset), *RoleToBind.ToString(), *GetNameSafe(Pair.Value.GetActor()), SectionIdx, AnimSetIdx);

				return false;
			}
		}
	}

	// Success if all the roles were filled
	return (OutBindings.Num() == SceneAsset.GetNumRoles());
}

bool FContextualAnimSceneBindings::TryCreateBindings(const UContextualAnimSceneAsset& SceneAsset, int32 SectionIdx, int32 AnimSetIdx, const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Secondary, FContextualAnimSceneBindings& OutBindings)
{
	check(SceneAsset.HasValidData());

	OutBindings.Reset();

	const TArray<FContextualAnimRoleDefinition>& Roles = SceneAsset.GetRolesAsset()->Roles;

	if (Roles.Num() > 2)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimSceneBindings::TryCreateBindings Failed. Reason: Trying to create bindings with two actors for a SceneAsset with more than two roles. SceneAsset: %s Num Roles: %d SectionIdx: %d AnimSetIdx: %d"),
			*GetNameSafe(&SceneAsset), Roles.Num(), SectionIdx, AnimSetIdx);

		return false;
	}

	for(const FContextualAnimRoleDefinition& RoleDef : Roles)
	{
		const FName PrimaryRole = SceneAsset.GetPrimaryRole();
		if(RoleDef.Name == PrimaryRole)
		{
			const FContextualAnimTrack* AnimTrack = SceneAsset.GetAnimTrack(SectionIdx, AnimSetIdx, PrimaryRole);
			if (AnimTrack && AnimTrack->DoesQuerierPassSelectionCriteria(Primary, Primary))
			{
				OutBindings.Add(FContextualAnimSceneBinding(Primary, SceneAsset, *AnimTrack));
			}
			else
			{
				UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimSceneBindings::TryCreateBindings Failed. Reason: Can't find valid track for primary actor. SceneAsset: %s Role: %s Actor: %s SectionIdx: %d AnimSetIdx: %d"),
					*GetNameSafe(&SceneAsset), *RoleDef.Name.ToString(), *GetNameSafe(Primary.GetActor()), SectionIdx, AnimSetIdx);

				return false;
			}
		}
		else // Secondary Role
		{
			const FContextualAnimTrack* AnimTrack = SceneAsset.GetAnimTrack(SectionIdx, AnimSetIdx, RoleDef.Name);
			if (AnimTrack && AnimTrack->DoesQuerierPassSelectionCriteria(Primary, Secondary))
			{
				OutBindings.Add(FContextualAnimSceneBinding(Secondary, SceneAsset, *AnimTrack));
			}
			else
			{
				UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimSceneBindings::TryCreateBindings Failed. Reason: Can't find valid track for secondary actor. SceneAsset: %s Role: %s Actor: %s SectionIdx: %d AnimSetIdx: %d"),
					*GetNameSafe(&SceneAsset), *RoleDef.Name.ToString(), *GetNameSafe(Secondary.GetActor()), SectionIdx, AnimSetIdx);

				return false;
			}
		}
	}

	// Success if all the roles were filled
	return (OutBindings.Num() == SceneAsset.GetNumRoles());
}

void FContextualAnimSceneBindings::CalculateAnimSetPivots(TArray<FContextualAnimSetPivot>& OutScenePivots) const
{
	if (const UContextualAnimSceneAsset* SceneAsset = GetSceneAsset())
	{
		const int32 SectionIdx = GetSectionIdx();
		for (const FContextualAnimSetPivotDefinition& Def : SceneAsset->GetAnimSetPivotDefinitionsInSection(SectionIdx))
		{
			FContextualAnimSetPivot& ScenePivotRuntime = OutScenePivots.AddDefaulted_GetRef();
			CalculateAnimSetPivot(Def, ScenePivotRuntime);
		}
	}
}

bool FContextualAnimSceneBindings::CalculateAnimSetPivot(const FContextualAnimSetPivotDefinition& AnimSetPivotDef, FContextualAnimSetPivot& OutScenePivot) const
{
	if (const FContextualAnimSceneBinding* Binding = FindBindingByRole(AnimSetPivotDef.Origin))
	{
		OutScenePivot.Name = AnimSetPivotDef.Name;
		if (AnimSetPivotDef.bAlongClosestDistance)
		{
			if (const FContextualAnimSceneBinding* OtherBinding = FindBindingByRole(AnimSetPivotDef.OtherRole))
			{
				const FTransform T1 = Binding->GetTransform();
				const FTransform T2 = OtherBinding->GetTransform();

				OutScenePivot.Transform.SetLocation(FMath::Lerp<FVector>(T1.GetLocation(), T2.GetLocation(), AnimSetPivotDef.Weight));
				OutScenePivot.Transform.SetRotation((T2.GetLocation() - T1.GetLocation()).GetSafeNormal2D().ToOrientationQuat());
				return true;
			}
		}
		else
		{
			OutScenePivot.Transform = Binding->GetTransform();
			return true;
		}
	}

	return false;
}