// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Evaluation/MovieSceneEvaluationCustomVersion.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneSkeletalAnimationTemplate.h"
#include "Compilation/IMovieSceneTemplateGenerator.h"
#include "Compilation/MovieSceneEvaluationTreePopulationRules.h"
#include "MovieScene.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/CustomAttributesRuntime.h"
#include "SkeletalDebugRendering.h"
#include "Rendering/SkeletalMeshRenderData.h"

#define LOCTEXT_NAMESPACE "MovieSceneSkeletalAnimationTrack"


/* UMovieSceneSkeletalAnimationTrack structors
 *****************************************************************************/

UMovieSceneSkeletalAnimationTrack::UMovieSceneSkeletalAnimationTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bUseLegacySectionIndexBlend(false)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(124, 15, 124, 65);
	bSupportsDefaultSections = false;
	bAutoMatchClipsRootMotions = false;
	bShowRootMotionTrail = false;
#endif

	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);

	EvalOptions.bEvaluateNearestSection_DEPRECATED = EvalOptions.bCanEvaluateNearestSection = true;
}


/* UMovieSceneSkeletalAnimationTrack interface
 *****************************************************************************/

FMovieSceneEvalTemplatePtr UMovieSceneSkeletalAnimationTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneSkeletalAnimationSectionTemplate(*CastChecked<const UMovieSceneSkeletalAnimationSection>(&InSection));
}

UMovieSceneSection* UMovieSceneSkeletalAnimationTrack::AddNewAnimationOnRow(FFrameNumber KeyTime, UAnimSequenceBase* AnimSequence, int32 RowIndex)
{
	UMovieSceneSkeletalAnimationSection* NewSection = Cast<UMovieSceneSkeletalAnimationSection>(CreateNewSection());
	{
		FFrameTime AnimationLength = AnimSequence->SequenceLength * GetTypedOuter<UMovieScene>()->GetTickResolution();
		int32 IFrameNumber = AnimationLength.FrameNumber.Value + (int)(AnimationLength.GetSubFrame() + 0.5f) + 1;
		NewSection->InitialPlacementOnRow(AnimationSections, KeyTime, IFrameNumber, RowIndex);
		NewSection->Params.Animation = AnimSequence;
	}

	AddSection(*NewSection);

	return NewSection;
}


TArray<UMovieSceneSection*> UMovieSceneSkeletalAnimationTrack::GetAnimSectionsAtTime(FFrameNumber Time)
{
	TArray<UMovieSceneSection*> Sections;
	for (UMovieSceneSection* Section : AnimationSections)
	{
		if (Section->IsTimeWithinSection(Time))
		{
			Sections.Add(Section);
		}
	}

	return Sections;
}


/* UMovieSceneTrack interface
 *****************************************************************************/

void UMovieSceneSkeletalAnimationTrack::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FMovieSceneEvaluationCustomVersion::GUID) < FMovieSceneEvaluationCustomVersion::AddBlendingSupport)
	{
		bUseLegacySectionIndexBlend = true;
	}
}
#if WITH_EDITOR
void UMovieSceneSkeletalAnimationTrack::PostEditImport()
{
	SetUpRootMotions(true);
}

void UMovieSceneSkeletalAnimationTrack::PostEditUndo()
{
	SetUpRootMotions(true);
}


#endif


const TArray<UMovieSceneSection*>& UMovieSceneSkeletalAnimationTrack::GetAllSections() const
{
	return AnimationSections;
}


bool UMovieSceneSkeletalAnimationTrack::SupportsMultipleRows() const
{
	return true;
}

bool UMovieSceneSkeletalAnimationTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneSkeletalAnimationSection::StaticClass();
}

UMovieSceneSection* UMovieSceneSkeletalAnimationTrack::CreateNewSection()
{
	return NewObject<UMovieSceneSkeletalAnimationSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneSkeletalAnimationTrack::RemoveAllAnimationData()
{
	AnimationSections.Empty();

}

bool UMovieSceneSkeletalAnimationTrack::HasSection(const UMovieSceneSection& Section) const
{
	return AnimationSections.Contains(&Section);
}

void UMovieSceneSkeletalAnimationTrack::AddSection(UMovieSceneSection& Section)
{
	AnimationSections.Add(&Section);
	UMovieSceneSkeletalAnimationSection* AnimSection = Cast< UMovieSceneSkeletalAnimationSection>(&Section);
	if (AnimSection)
	{
		if (bAutoMatchClipsRootMotions)
		{
			AutoMatchSectionRoot(AnimSection);
		}
		SetUpRootMotions(true);
	}
}

void UMovieSceneSkeletalAnimationTrack::UpdateEasing()
{
	Super::UpdateEasing();
	SetRootMotionsDirty();
}

void UMovieSceneSkeletalAnimationTrack::RemoveSection(UMovieSceneSection& Section)
{
	AnimationSections.Remove(&Section);
	SetUpRootMotions(true);
}

void UMovieSceneSkeletalAnimationTrack::RemoveSectionAt(int32 SectionIndex)
{
	AnimationSections.RemoveAt(SectionIndex);
	SetUpRootMotions(true);
}

bool UMovieSceneSkeletalAnimationTrack::IsEmpty() const
{
	return AnimationSections.Num() == 0;
}

#if WITH_EDITORONLY_DATA

FText UMovieSceneSkeletalAnimationTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Animation");
}

#endif

bool UMovieSceneSkeletalAnimationTrack::PopulateEvaluationTree(TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData) const
{
	using namespace UE::MovieScene;

	if (!bUseLegacySectionIndexBlend)
	{
		FEvaluationTreePopulationRules::HighPassPerRow(AnimationSections, OutData);
	}
	else
	{
		// Use legacy blending... when there's overlapping, the section that makes it into the evaluation tree is
		// the one that appears later in the container arary of section data.
		//
		auto SortByLatestInArrayAndRow = [](const FEvaluationTreePopulationRules::FSortedSection& A, const FEvaluationTreePopulationRules::FSortedSection& B)
		{
			if (A.Row() == B.Row())
			{
				return A.Index > B.Index;
			}
			
			return A.Row() < B.Row();
		};

		UE::MovieScene::FEvaluationTreePopulationRules::HighPassCustomPerRow(AnimationSections, OutData, SortByLatestInArrayAndRow);
	}
	return true;
}

#if WITH_EDITOR
void UMovieSceneSkeletalAnimationTrack::OnSectionMoved(UMovieSceneSection& Section, const FMovieSceneSectionMovedParams& Params)
{
	SortSections();
	SetUpRootMotions(true);
}
#endif

void UMovieSceneSkeletalAnimationTrack::SortSections()
{
	AnimationSections.Sort([](UMovieSceneSection& A,  UMovieSceneSection& B) {return ((A).GetTrueRange().GetLowerBoundValue() < (B).GetTrueRange().GetLowerBoundValue());});
}

static void BlendTheseTransformsByWeight(FTransform& OutTransform, const TArray<FTransform>& Transforms, const TArray<float>& Weights)
{
	int32 NumBlends = Transforms.Num();
	check(Transforms.Num() == Weights.Num());
	if (NumBlends == 0)
	{
		OutTransform = FTransform::Identity;
	}
	else if (NumBlends == 1)
	{
		OutTransform = Transforms[0];
	}
	else
	{
		FVector		OutTranslation(0.0f, 0.0f, 0.0f);
		FVector		OutScale(0.0f, 0.0f, 0.0f);

		//rotation will get set to the first weighted and then made closest to that so linear interp works.
		FQuat FirstRot = Transforms[0].GetRotation();
		FQuat		OutRotation(FirstRot.X * Weights[0], FirstRot.Y * Weights[0], FirstRot.Z * Weights[0], FirstRot.W * Weights[0]);

		for (int32 Index = 0; Index < NumBlends; ++Index)
		{
			OutTranslation += Transforms[Index].GetTranslation() * Weights[Index];
			OutScale +=  Transforms[Index].GetScale3D() * Weights[Index];
			if (Index != 0)
			{
				FQuat Quat = Transforms[Index].GetRotation();
				Quat.EnforceShortestArcWith(FirstRot);
				Quat *= Weights[Index];
				OutRotation += Quat;
			}
		}

		OutRotation.Normalize();
		OutTransform = FTransform(OutRotation, OutTranslation, OutScale);
	}
}
void UMovieSceneSkeletalAnimationTrack::SetRootMotionsDirty()
{
	RootMotionParams.bRootMotionsDirty = true;
}

struct FSkelBoneLength
{
	FSkelBoneLength(FCompactPoseBoneIndex InPoseIndex, float InBL) :PoseBoneIndex(InPoseIndex), BoneLength(InBL) {};
	FCompactPoseBoneIndex PoseBoneIndex;
	float BoneLength; //squared
};

static void CalculateDistanceMap(USkeletalMeshComponent* SkelMeshComp, UAnimSequenceBase* FirstAnimSeq, UAnimSequenceBase* SecondAnimSeq, float StartFirstAnimTime, float FrameRate,
	TArray<TArray<float>>& OutDistanceDifferences)
{

	int32 FirstAnimNumFrames = (FirstAnimSeq->SequenceLength - StartFirstAnimTime) * FrameRate + 1;
	int32 SecondAnimNumFrames = SecondAnimSeq->SequenceLength * FrameRate + 1;
	OutDistanceDifferences.SetNum(FirstAnimNumFrames);
	float FirstAnimIndex = 0.0f;
	float FrameRateDiff = 1.0f / FrameRate;
	FCompactPose FirstAnimPose, SecondAnimPose;
	FCSPose<FCompactPose> FirstMeshPoses, SecondMeshPoses;
	FirstAnimPose.ResetToRefPose(SkelMeshComp->GetAnimInstance()->GetRequiredBones());
	SecondAnimPose.ResetToRefPose(SkelMeshComp->GetAnimInstance()->GetRequiredBones());

	FBlendedCurve FirstOutCurve, SecondOutCurve;
	FStackCustomAttributes FirstTempAttributes, SecondTempAttributes;
	FAnimationPoseData FirstAnimPoseData(FirstAnimPose, FirstOutCurve, FirstTempAttributes);
	FAnimationPoseData SecondAnimPoseData(SecondAnimPose, SecondOutCurve, SecondTempAttributes);

	//sort by bone lengths just do the first half
	//this should avoid us overvalueing to many small values.
	/*
	TArray<FSkelBoneLength> BoneLengths;
	BoneLengths.SetNum(FirstAnimPose.GetNumBones());
	int32 Index = 0;
	for (FCompactPoseBoneIndex PoseBoneIndex : FirstAnimPose.ForEachBoneIndex())
	{
		FTransform LocalTransform = FirstMeshPoses.GetLocalSpaceTransform(PoseBoneIndex);
		float BoneLengthVal = LocalTransform.GetLocation().SizeSquared();
		BoneLengths[Index++] = FSkelBoneLength(PoseBoneIndex, BoneLengthVal);
	}
	BoneLengths.Sort([](const FSkelBoneLength& Item1, const FSkelBoneLength& Item2) {
		return Item1.BoneLength > Item2.BoneLength;
		});
		*/
	FBlendedCurve OutCurve;
	const FBoneContainer& RequiredBones = FirstAnimPoseData.GetPose().GetBoneContainer();
	for (TArray<float>& FloatArray : OutDistanceDifferences)
	{
		FloatArray.SetNum(SecondAnimNumFrames);
		float FirstAnimTime = FirstAnimIndex * FrameRateDiff + StartFirstAnimTime;
		FirstAnimIndex += 1.0f;
		FAnimExtractContext FirstExtractionContext(FirstAnimTime, false);
		FirstAnimSeq->GetAnimationPose(FirstAnimPoseData, FirstExtractionContext);
		FirstMeshPoses.InitPose(FirstAnimPoseData.GetPose());
		float SecondAnimIndex = 0.0f;
		for (float& DistVal : FloatArray)
		{
			DistVal = 0.0f;
			float SecondAnimTime = SecondAnimIndex * FrameRateDiff;
			SecondAnimIndex += 1.0f;
			FAnimExtractContext SecondExtractionContext(SecondAnimTime, false);
			SecondAnimSeq->GetAnimationPose(SecondAnimPoseData, SecondExtractionContext);
			SecondMeshPoses.InitPose(SecondAnimPoseData.GetPose());

			float DiffVal = 0.0f;
			for (FCompactPoseBoneIndex PoseBoneIndex : FirstAnimPoseData.GetPose().ForEachBoneIndex())
			{
				FTransform FirstTransform = FirstMeshPoses.GetComponentSpaceTransform(PoseBoneIndex);
				FTransform SecondTransform = SecondMeshPoses.GetComponentSpaceTransform(PoseBoneIndex);
				if (PoseBoneIndex != 0)
				{
					DistVal += (FirstTransform.GetTranslation() - SecondTransform.GetTranslation()).SizeSquared();
				}
			}
		}
	}
}
//outer is startanimtime to firstanim->seqlength...
//inner is 0 to secondanim->seqlength...
//for this function just find the smallest in the second...
//return the end anim time
static float GetBestBlendPointTimeAtStart(UAnimSequenceBase* FirstAnimSeq, UAnimSequenceBase* SecondAnimSeq, float StartFirstAnimTime, float FrameRate,
	TArray<TArray<float>>& DistanceDifferences)
{

	//int32 FirstAnimNumFrames = (FirstAnimSeq->SequenceLength - StartFirstAnimTime) * FrameRate + 1;
	int32 SecondAnimNumFrames = SecondAnimSeq->SequenceLength * FrameRate + 1;
	if (SecondAnimNumFrames <= 0)
	{
		return 0.0f;
	}
	TArray<float>& Distances = DistanceDifferences[0];
	float MinVal = Distances[0];
	int32 SmallIndex = 0;
	for (int32 Index = 1; Index < SecondAnimNumFrames; ++Index)
	{
		float NewMin = Distances[Index];
		if (NewMin < MinVal)
		{
			MinVal = NewMin;
			SmallIndex = Index;
		}
	}
	return SmallIndex * 1.0f / FrameRate;
}

void UMovieSceneSkeletalAnimationTrack::FindBestBlendPoint(USkeletalMeshComponent* SkelMeshComp, UMovieSceneSkeletalAnimationSection* FirstSection)
{
	UMovieScene* MovieScene = GetTypedOuter<UMovieScene>();
	if (MovieScene && FirstSection && FirstSection->Params.Animation)
	{
		SortSections();
		for (int32 Index = 0; Index <  AnimationSections.Num(); ++Index)
		{
			UMovieSceneSection* Section = AnimationSections[Index];
			UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section);
			if (AnimSection && AnimSection == FirstSection)
			{
				if (++Index < AnimationSections.Num())
				{
					float FirstFrameTime = 0;
					FFrameNumber BeginOfSecond = AnimationSections[Index]->GetInclusiveStartFrame();
					FFrameNumber EndOfFirst =  FirstSection->GetExclusiveEndFrame();
					if (BeginOfSecond < EndOfFirst)
					{
						FFrameRate TickResolution = MovieScene->GetTickResolution();
						FirstFrameTime = FirstSection->MapTimeToAnimation(FFrameTime(BeginOfSecond), TickResolution);
					}
					TArray<TArray<float>> OutDistanceDifferences;
					FFrameRate DisplayRate = MovieScene->GetDisplayRate();
					float FrameRate = DisplayRate.AsDecimal();
					UMovieSceneSkeletalAnimationSection* NextSection = Cast<UMovieSceneSkeletalAnimationSection>(AnimationSections[Index]);
					CalculateDistanceMap(SkelMeshComp, FirstSection->Params.Animation, NextSection->Params.Animation,
						0.0f, FrameRate, OutDistanceDifferences);
					//get range
					FFrameRate TickResolution = MovieScene->GetTickResolution();
					FFrameNumber CurrentTime = AnimSection->GetRange().GetLowerBoundValue();
					float BestBlend = GetBestBlendPointTimeAtStart(FirstSection->Params.Animation, NextSection->Params.Animation, FirstFrameTime, FrameRate, OutDistanceDifferences);
					CurrentTime += TickResolution.AsFrameNumber(BestBlend);
					FFrameNumber CurrentNextPosition = NextSection->GetRange().GetLowerBoundValue();
					FFrameNumber DeltaTime = CurrentTime - CurrentNextPosition;
					NextSection->MoveSection(DeltaTime);
					SortSections();
					SetUpRootMotions(true);

				}
			}
		}
	}
}

void UMovieSceneSkeletalAnimationTrack::SetUpRootMotions(bool bForce)
{
	UMovieScene* MovieScene = GetTypedOuter<UMovieScene>();
	if (!MovieScene)
	{
		return;
	}

	if (bForce || RootMotionParams.bRootMotionsDirty)
	{
		RootMotionParams.bRootMotionsDirty = false;
		if (AnimationSections.Num() == 0)
		{
			RootMotionParams.RootTransforms.SetNum(0);
			return;
		}
		SortSections();
		//Set the TempOffset.
		FTransform InitialTransform = FTransform::Identity;
		UMovieSceneSkeletalAnimationSection* PrevAnimSection = nullptr;
		for (UMovieSceneSection* Section : AnimationSections)
		{
			UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section);
			if (AnimSection)
			{
				if (PrevAnimSection)
				{
					AnimSection->TempOffsetTransform = PrevAnimSection->GetOffsetTransform() * InitialTransform;
					InitialTransform = AnimSection->TempOffsetTransform;
				}
				else
				{
					AnimSection->TempOffsetTransform = FTransform::Identity;
				}
				PrevAnimSection = AnimSection;
			}
		}

		TArray< UMovieSceneSkeletalAnimationSection*> SectionsAtCurrentTime;

		RootMotionParams.StartFrame = AnimationSections[0]->GetInclusiveStartFrame();
		RootMotionParams.EndFrame = AnimationSections[AnimationSections.Num() - 1]->GetExclusiveEndFrame();

		FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		RootMotionParams.FrameTick = FFrameTime(TickResolution.AsFrameNumber(1.0).Value / DisplayRate.AsFrameNumber(1.0).Value);

		int32 NumTotal = (RootMotionParams.EndFrame.FrameNumber.Value - RootMotionParams.StartFrame.FrameNumber.Value) / (RootMotionParams.FrameTick.FrameNumber.Value) + 1;
		RootMotionParams.RootTransforms.SetNum(NumTotal);
		TArray<FTransform> CurrentTransforms;
		TArray<float> CurrentWeights;


		//UMovieSceneSkeletalAnimationSection* AnimSection = Cast< UMovieSceneSkeletalAnimationSection>(AnimationSections[SmallestActiveSection]);
		//AnimSection->AllocateRootMotionCache(FrameRateInSeconds);
		FFrameTime PreviousFrame = RootMotionParams.StartFrame;
		int32 Index = 0;
		
		for (FFrameTime FrameNumber = RootMotionParams.StartFrame; FrameNumber <= RootMotionParams.EndFrame; FrameNumber += RootMotionParams.FrameTick)
		{
			CurrentTransforms.SetNum(0);
			CurrentWeights.SetNum(0);
			FTransform CurrentTransform = FTransform::Identity;
			float CurrentWeight;
			UMovieSceneSkeletalAnimationSection* PrevSection = nullptr;
			for (UMovieSceneSection* Section : AnimationSections)
			{
				if (Section->GetRange().Contains(FrameNumber.FrameNumber))
				{
					//mz todo handles 
					UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section);
					if (AnimSection->GetRootMotionTransform( FrameNumber.FrameNumber, TickResolution, CurrentTransform, CurrentWeight))
					{
						CurrentTransform = CurrentTransform * AnimSection->TempOffsetTransform;
						CurrentTransforms.Add(CurrentTransform);
						CurrentWeights.Add(CurrentWeight);
					}
					PrevSection = AnimSection;
				}
			}


			if (CurrentWeights.Num() > 0)
			{
				float TotalWeight = 0.0f;
				for (int32 WeightIndex = 0; WeightIndex < CurrentWeights.Num(); ++WeightIndex)
				{
					TotalWeight += CurrentWeights[WeightIndex];
				}
				if (!FMath::IsNearlyEqual(TotalWeight, 1.0f))
				{
					for (int32 DivideIndex = 0; DivideIndex < CurrentWeights.Num(); ++DivideIndex)
					{
						CurrentWeights[DivideIndex] /= TotalWeight;
					}
				}
			}
			BlendTheseTransformsByWeight(CurrentTransform, CurrentTransforms, CurrentWeights);
			RootMotionParams.RootTransforms[Index] = CurrentTransform;
			++Index;
			PreviousFrame = FrameNumber;

		}
	}
}

static FTransform GetWorldTransformForBone(UAnimSequence* AnimSequence, USkeletalMeshComponent* MeshComponent,const FName& InBoneName, float Seconds)
{
	FName BoneName = InBoneName;
	FTransform  WorldTransform = FTransform::Identity;

	do
	{
		int32 BoneIndex = MeshComponent->GetBoneIndex(BoneName);
		FTransform BoneTransform;
		int32 TrackIndex;

		for (TrackIndex = 0; TrackIndex < AnimSequence->GetRawTrackToSkeletonMapTable().Num(); ++TrackIndex)
		{
			if (AnimSequence->GetRawTrackToSkeletonMapTable()[TrackIndex].BoneTreeIndex == BoneIndex)
			{
				break;
			}
		}

		if (TrackIndex == AnimSequence->GetRawTrackToSkeletonMapTable().Num())
		{
			break;
		}

		AnimSequence->GetBoneTransform(BoneTransform, TrackIndex, Seconds, true);
		WorldTransform *= BoneTransform;

		BoneName = MeshComponent->GetParentBone(BoneName);
	} while (BoneName != NAME_None);

	//WorldTransform.SetToRelativeTransform(MeshComponent->GetComponentTransform());

	return WorldTransform;
}


void UMovieSceneSkeletalAnimationTrack::MatchSectionByBoneTransform(bool bMatchWithPrevious, USkeletalMeshComponent* SkelMeshComp, UMovieSceneSkeletalAnimationSection* CurrentSection, FFrameTime CurrentFrame, FFrameRate FrameRate,
	const FName& BoneName, FTransform& SecondSectionRootDiff, FVector& TranslationDiff, FQuat& RotationDiff) //add options for z and for rotation.
{
	SortSections();
	UMovieSceneSection* PrevSection = nullptr;
	UMovieSceneSection* NextSection = nullptr;
	for (int32 Index = 0; Index <  AnimationSections.Num(); ++Index)
	{
		UMovieSceneSection* Section = AnimationSections[Index];
		if (Section == CurrentSection)
		{
			if (++Index < AnimationSections.Num())
			{
				NextSection = AnimationSections[Index];
			}
			break;
		}
		PrevSection = Section;
	}

	if (bMatchWithPrevious && PrevSection)
	{
		UMovieSceneSkeletalAnimationSection* FirstSection = Cast<UMovieSceneSkeletalAnimationSection>(PrevSection);
		UAnimSequence* FirstAnimSequence = Cast<UAnimSequence>(FirstSection->Params.Animation);
		UAnimSequence* SecondAnimSequence = Cast<UAnimSequence>(CurrentSection->Params.Animation);

		if (FirstAnimSequence && SecondAnimSequence)
		{
			float FirstSectionTime = FirstSection->MapTimeToAnimation(CurrentFrame, FrameRate);
			FTransform  FirstTransform = GetWorldTransformForBone(FirstAnimSequence, SkelMeshComp, BoneName, FirstSectionTime);
			//multiply it by the offset transform
			//We know do this is SetUpRootMotions so we can move clips around before this one
			//FTransform OffsetTransform = FirstSection->GetOffsetTransform();
			//FirstTransform = FirstTransform * OffsetTransform;
			float SecondSectionTime = CurrentSection->MapTimeToAnimation(CurrentFrame, FrameRate);
			FTransform  SecondTransform = GetWorldTransformForBone(SecondAnimSequence, SkelMeshComp, BoneName, SecondSectionTime);
			FTransform SecondInverse = SecondTransform.Inverse();
			SecondSectionRootDiff = SecondInverse * FirstTransform;
			TranslationDiff = - SecondTransform.GetTranslation() + FirstTransform.GetTranslation();
			RotationDiff = SecondInverse.GetRotation() * FirstTransform.GetRotation();

		}
	}
	else if (bMatchWithPrevious == false && NextSection) //match with next
	{
		UMovieSceneSkeletalAnimationSection* SecondSection = Cast<UMovieSceneSkeletalAnimationSection>(NextSection);
		UAnimSequence* FirstAnimSequence = Cast<UAnimSequence>(CurrentSection->Params.Animation);
		UAnimSequence* SecondAnimSequence = Cast<UAnimSequence>(SecondSection->Params.Animation);

		if (FirstAnimSequence && SecondAnimSequence)
		{
			float FirstSectionTime = CurrentSection->MapTimeToAnimation(CurrentFrame, FrameRate);
			FTransform  FirstTransform = GetWorldTransformForBone(FirstAnimSequence, SkelMeshComp, BoneName, FirstSectionTime);
			//multiply it by the offset transform
			//We know do this is SetUpRootMotions so we can move clips around before this one
			//FTransform OffsetTransform = FirstSection->GetOffsetTransform();
			//FirstTransform = FirstTransform * OffsetTransform;
			float SecondSectionTime = SecondSection->MapTimeToAnimation(CurrentFrame, FrameRate);
			FTransform  SecondTransform = GetWorldTransformForBone(SecondAnimSequence, SkelMeshComp, BoneName, SecondSectionTime);
			FTransform FirstInverse = FirstTransform.Inverse();
			SecondSectionRootDiff = SecondTransform * FirstInverse;
			TranslationDiff = SecondTransform.GetTranslation() - FirstTransform.GetTranslation();
			RotationDiff = SecondTransform.GetRotation() * FirstInverse.GetRotation();
		}
	}
}

void UMovieSceneSkeletalAnimationTrack::ToggleAutoMatchClipsRootMotions()
{
	bAutoMatchClipsRootMotions = bAutoMatchClipsRootMotions ? false : true;
	SetUpRootMotions(true);
}

#if WITH_EDITORONLY_DATA

void UMovieSceneSkeletalAnimationTrack::ToggleShowRootMotionTrail()
{
	bShowRootMotionTrail = bShowRootMotionTrail ? false : true;
}
#endif
FTransform FMovieSceneSkeletalAnimRootMotionTrackParams::GetRootMotion(FFrameTime CurrentTime)  const
{
	if (RootTransforms.Num() > 0)
	{
		if (CurrentTime >= StartFrame && CurrentTime <= EndFrame)
		{
			float FIndex = (float)(CurrentTime.FrameNumber.Value - StartFrame.FrameNumber.Value) / (float)(FrameTick.FrameNumber.Value);
			int Index = (int)(FIndex);
			FIndex -= (float)(Index);
			FTransform Transform = RootTransforms[Index];
			if (FIndex > 0.001f)
			{
				//may hit a bug here with <= failing.
				if (Index < RootTransforms.Num() - 1)
				{
					FTransform Next = RootTransforms[Index + 1];
					Transform.Blend(Transform, Next, FIndex);
				}
				else
				{
					Transform = RootTransforms[RootTransforms.Num() - 1];
				}
			}
			return Transform;
		}
		else if (CurrentTime > EndFrame)
		{
			return RootTransforms[RootTransforms.Num() - 1];
		}
		else
		{
			return RootTransforms[0];
		}
	}
	return FTransform::Identity;
}


//MZ To Do need way to get passed the skelmeshcomp when we add or move a section.
void UMovieSceneSkeletalAnimationTrack::AutoMatchSectionRoot(UMovieSceneSkeletalAnimationSection* CurrentSection)
{
	return;
	UMovieScene* MovieScene = GetTypedOuter<UMovieScene>();
	if (AnimationSections.Num() > 0 && MovieScene && CurrentSection)
	{
		SortSections();

		for (int32 Index = 0; Index < AnimationSections.Num(); ++Index)
		{
			UMovieSceneSection* Section = AnimationSections[Index];
			if (Section && Section == CurrentSection)
			{
				CurrentSection->bMatchWithPrevious = (Index == 0) ? false : true;
				FFrameTime FrameTime = (Index == 0) ? CurrentSection->GetRange().GetUpperBoundValue() : CurrentSection->GetRange().GetLowerBoundValue();
				USkeletalMeshComponent* SkelMeshComp = nullptr;
				CurrentSection->MatchSectionByBoneTransform(SkelMeshComp, FrameTime, MovieScene->GetTickResolution(), CurrentSection->MatchedBoneName);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
