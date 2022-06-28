// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintChannelHelper.h"

#include "ActorForWorldTransforms.h"
#include "ControlRigSpaceChannelEditors.h"
#include "ISequencer.h"

#include "Constraints/ControlRigTransformableHandle.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "LevelSequence.h"
#include "MovieSceneToolHelpers.h"

#include "TransformConstraint.h"
#include "Algo/Unique.h"
#include "Channels/MovieSceneConstraintChannel.h"

#include "Tools/BakingHelper.h"
#include "Tools/ConstraintBaker.h"

namespace
{
	bool CanAddKey(const FConstraintAndActiveChannel* Channel, const FFrameNumber& InTime, bool& ActiveValue)
	{
		const TMovieSceneChannelData<const bool> ChannelData = Channel->ActiveChannel.GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (Times.IsEmpty())
		{
			ActiveValue = true;
			return true;
		}

		const TArrayView<const bool> Values = ChannelData.GetValues();
		if (InTime < Times[0])
		{
			if (!Values[0])
			{
				ActiveValue = true;
				return true;
			}
			return false;
		}
		
		if (InTime > Times.Last())
		{
			ActiveValue = !Values.Last();
			return true;
		}

		return false;
	}
}

void FConstraintChannelHelper::SmartConstraintKey(UTickableTransformConstraint* InConstraint)
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (!WeakSequencer.IsValid() || !WeakSequencer.Pin()->GetFocusedMovieSceneSequence())
	{
		return;
	}

	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InConstraint->ChildTRSHandle))
	{
		return SmartComponentConstraintKey(InConstraint, WeakSequencer.Pin());
	}
	
	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle))
	{
		return SmartControlConstraintKey(InConstraint, WeakSequencer.Pin());
	}
}

void FConstraintChannelHelper::AddChildTransformKey(
	const UTransformableHandle* InHandle,
	const FFrameNumber& InTime,
	const TSharedPtr<ISequencer>& InSequencer)
{
	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InHandle))
	{
		AActor* Actor = ComponentHandle->Component->GetOwner();
		if (!Actor)
		{
			return;
		}

		const FTransform LocalTransform = ComponentHandle->GetLocalTransform();
		const FGuid Guid = InSequencer->GetHandleToObject(Actor,true);
		if (!Guid.IsValid())
		{
			return;
		}

		const UMovieScene3DTransformSection* TransformSection = FBakingHelper::GetTransformSection(InSequencer.Get(), Guid, LocalTransform);
		if (!TransformSection)
		{
			return;
		}

		FBakingHelper::AddTransformKeys(TransformSection, {InTime}, {LocalTransform}, EMovieSceneTransformChannel::AllTransform);
		return; 
	}
	
	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InHandle);
	if (ControlHandle && ControlHandle->IsValid())
	{
		const FTransform LocalTransform = ControlHandle->GetLocalTransform();
		
		static constexpr bool bNotify = true;
		static constexpr bool bUndo = false;
		static constexpr bool bFixEuler = true;
	
		FRigControlModifiedContext KeyframeContext;
		KeyframeContext.SetKey = EControlRigSetKey::Always;
		KeyframeContext.KeyMask = static_cast<uint32>(EControlRigContextChannelToKey::AllTransform);

		const FFrameRate TickResolution = InSequencer->GetFocusedTickResolution();
		KeyframeContext.LocalTime = TickResolution.AsSeconds(FFrameTime(InTime));
	
		ControlHandle->ControlRig->SetControlLocalTransform(ControlHandle->ControlName, LocalTransform, bNotify, KeyframeContext, bUndo, bFixEuler);
	}
}

void FConstraintChannelHelper::AddConstraintKey(UTickableTransformConstraint* InConstraint)
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (!WeakSequencer.IsValid() || !WeakSequencer.Pin()->GetFocusedMovieSceneSequence())
	{
		return;
	}
	const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	const UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle);
	if (!ControlHandle)
	{
		return;
	}

	if (UMovieSceneControlRigParameterSection* ParamSection = GetControlSection(ControlHandle, Sequencer))
	{
		ParamSection->AddConstraintChannel(InConstraint, true);

		if (FConstraintAndActiveChannel* Channel = ParamSection->GetConstraintChannel(InConstraint->GetFName()))
		{
			ParamSection->Modify();

			const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
			const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
			const FFrameNumber Time = FrameTime.GetFrame();

			bool Value = false;
			CanAddKey(Channel, Time, Value);
			
			TMovieSceneChannelData<bool> ChannelData = Channel->ActiveChannel.GetData();
			ChannelData.AddKey(Time, InConstraint->Active);

			// bool PrevActiveValue = false; Channel->ActiveChannel.Evaluate(Time, PrevActiveValue);
			// bool NextActiveValue = false; Channel->ActiveChannel.Evaluate(Time, PrevActiveValue);

					
			// int32 ExistingIndex = ChannelData.FindKey(Time);
			// if (ExistingIndex != INDEX_NONE)
			// {
			// 	Handle = ChannelInterface.GetHandle(ExistingIndex);
			// 	using namespace UE::MovieScene;
			// 	AssignValue(Channel, Handle, Forward<FMovieSceneControlRigSpaceBaseKey>(Value));
			// }
			// else
			// {
			// 	ExistingIndex = ChannelInterface.AddKey(Time, Forward<FMovieSceneControlRigSpaceBaseKey>(Value));
			// 	Handle = ChannelInterface.GetHandle(ExistingIndex);
			// }
		}
	}
}

UMovieSceneControlRigParameterSection* FConstraintChannelHelper::GetControlSection(
	const UTransformableControlHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!MovieScene)
	{
		return nullptr;
	}

	const TWeakObjectPtr<UControlRig> ControlRig = InHandle->ControlRig.LoadSynchronous();
	if(ControlRig.IsValid())
	{	
		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			UMovieSceneTrack* Track = MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid());
			UMovieSceneControlRigParameterTrack* ControlRigTrack = Cast<UMovieSceneControlRigParameterTrack>(Track);
			if (ControlRigTrack && ControlRigTrack->GetControlRig() == ControlRig)
			{
				return Cast<UMovieSceneControlRigParameterSection>(ControlRigTrack->FindSection(0));
			}
		}
	}

	return nullptr;
}

UMovieScene3DTransformSection* FConstraintChannelHelper::GetTransformSection(
	const UTransformableComponentHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	AActor* Actor = InHandle->Component->GetOwner();
	if (!Actor)
	{
		return nullptr;
	}
	
	const FGuid Guid = InSequencer->GetHandleToObject(Actor,true);
	if (!Guid.IsValid())
	{
		return nullptr;
	}
	
	return FBakingHelper::GetTransformSection(InSequencer.Get(), Guid);
}

TArrayView<FMovieSceneFloatChannel*> FConstraintChannelHelper::GetTransformFloatChannels(
	const UTransformableHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	// InHandle and InSequencer are assumed to be valid at this stage so no need to check
	
	static const TArrayView<FMovieSceneFloatChannel*> EmptyChannelsView;
	
	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InHandle))
	{
		UMovieSceneControlRigParameterSection* Section = GetControlSection(ControlHandle, InSequencer);
		FChannelMapInfo* pChannelIndex = nullptr;
		FRigControlElement* ControlElement = nullptr;
		Tie(ControlElement, pChannelIndex) = FControlRigSpaceChannelHelpers::GetControlAndChannelInfo(
			ControlHandle->ControlRig.Get(),
			Section,
			ControlHandle->ControlName);
    
		if (!pChannelIndex || !ControlElement)
		{
			return EmptyChannelsView;
		}

		// get the number of float channels to treat
		const int32 NumChannels = FControlRigSpaceChannelHelpers::GetNumFloatChannels(ControlElement->Settings.ControlType);
		if (!NumChannels)
		{
			return EmptyChannelsView;
		}

		// return a sub view that just represents the control's channels
		const TArrayView<FMovieSceneFloatChannel*> FloatChannels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		const int32 ChannelStartIndex = pChannelIndex->ChannelIndex;
		return FloatChannels.Slice(ChannelStartIndex, NumChannels);
	}
	
	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InHandle))
	{
		const UMovieScene3DTransformSection* Section = GetTransformSection(ComponentHandle, InSequencer);
		return Section ? Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>() : EmptyChannelsView;
	}
	
	return EmptyChannelsView;
}

namespace
{
	
void GetComponentGlobalTransforms(
	const UTransformableControlHandle* InControlHandle,
	const TSharedPtr<ISequencer>& InSequencer,
	TArray<FFrameNumber> InFrames,
	TArray<FTransform>& OutTransforms)
{
	const USkeletalMeshComponent* SkeletalMeshComponent = InControlHandle->GetSkeletalMesh();
	AActor* Actor = SkeletalMeshComponent ? SkeletalMeshComponent->GetTypedOuter< AActor >() : nullptr;
	if (Actor)
	{
		FActorForWorldTransforms ControlRigActorSelection;
		ControlRigActorSelection.Actor = Actor;
		MovieSceneToolHelpers::GetActorWorldTransforms(InSequencer.Get(), ControlRigActorSelection, InFrames, OutTransforms);		
	}
	else
	{
		OutTransforms.SetNum(InFrames.Num());
		for (FTransform& Transform : OutTransforms)
		{
			Transform = FTransform::Identity;
		}
	}
}

struct FTransformEvaluator
{
public:
	TArray<FTransform> ChildTransforms;
	TArray<FTransform> ParentGlobalTransforms;
	TArray<FTransform> ComponentGlobalTransforms;

	FTransformEvaluator(UTickableTransformConstraint* InConstraint)
		: Constraint(InConstraint)
	{}
	
	void ComputeFrames(const TSharedPtr<ISequencer>& InSequencer, const TArray<FFrameNumber>& InFrames)
	{
		if (!Constraint)
		{
			return;
		}
		
		const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(Constraint->ChildTRSHandle);
		if (!ControlHandle)
		{
			return;
		}

		// todo: compute child, parent and component in one single pass for faster computation
		FConstraintBaker::GetHandleTransforms(
		   ControlHandle->ControlRig->GetWorld(),
		   InSequencer,
		   ControlHandle,
		   InFrames,
		   bLocal,
		   ChildTransforms);
	
		FConstraintBaker::GetHandleTransforms(
			ControlHandle->ControlRig->GetWorld(),
			InSequencer,
			Constraint->ParentTRSHandle,
			InFrames,
			bLocal,
			ParentGlobalTransforms);

		GetComponentGlobalTransforms(
			ControlHandle,
			InSequencer,
			InFrames,
			ComponentGlobalTransforms);
	}
	
private:
	static constexpr bool bLocal = false; // store global transforms
	UTickableTransformConstraint* Constraint = nullptr;
};

}

void FConstraintChannelHelper::SmartControlConstraintKey(
	UTickableTransformConstraint* InConstraint,
	const TSharedPtr<ISequencer>& InSequencer)
{
	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle);
	if (!ControlHandle)
	{
		return;
	}
	
	if (UMovieSceneControlRigParameterSection* Section = GetControlSection(ControlHandle, InSequencer))
	{
		// add the channel
		Section->AddConstraintChannel(InConstraint, true);

		// add key if needed
		if (FConstraintAndActiveChannel* Channel = Section->GetConstraintChannel(InConstraint->GetFName()))
		{
			const FFrameRate TickResolution = InSequencer->GetFocusedTickResolution();
			const FFrameTime FrameTime = InSequencer->GetLocalTime().ConvertTo(TickResolution);
			const FFrameNumber Time = FrameTime.GetFrame();

			bool ActiveValueToBeSet = false;
			if (CanAddKey(Channel, Time, ActiveValueToBeSet))
			{
				// set constraint as dynamic
				InConstraint->bDynamicOffset = true;
				
				UControlRig* ControlRig = ControlHandle->ControlRig.Get();
				const FName& ControlName = ControlHandle->ControlName;
				
				// store the frames to compensate
				const TArrayView<FMovieSceneFloatChannel*> Channels = GetTransformFloatChannels(ControlHandle, InSequencer);
				TArray<FFrameNumber> FramesToCompensate;
				GetFramesToCompensate(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);

				// store child, parent and component global transforms for these frames
				FTransformEvaluator Evaluator(InConstraint);
				Evaluator.ComputeFrames(InSequencer, FramesToCompensate);
				TArray<FTransform>& ChildTransforms = Evaluator.ChildTransforms;
				const TArray<FTransform>& ParentTransforms = Evaluator.ParentGlobalTransforms;
				const TArray<FTransform>& ComponentTransforms = Evaluator.ComponentGlobalTransforms;

				// store tangents at this time
				TArray<FMovieSceneTangentData> Tangents;
				FControlRigSpaceChannelHelpers::EvaluateTangentAtThisTime(ControlRig, Section, ControlName, Time, Tangents);

				const EMovieSceneTransformChannel ChannelsToKey = FConstraintBaker::GetChannelsToKey(InConstraint);
				
				// add child's transform key at Time-1 to keep animation
				{
					const FFrameNumber TimeMinusOne(Time -1);

					const bool bPrevAsLocal = !ActiveValueToBeSet;
					const FTransform& SpaceTransformTime = bPrevAsLocal ? ParentTransforms[0] : // constraint space  
																		  ComponentTransforms[0]; // component space
					const FTransform ChildTransformAtMinusOne = ChildTransforms[0].GetRelativeTransform(SpaceTransformTime);
					FBakingHelper::AddTransformKeys(ControlRig, ControlName, {TimeMinusOne},
						{ChildTransformAtMinusOne}, ChannelsToKey, TickResolution, bPrevAsLocal);
				
					// set tangents at Time-1
					FControlRigSpaceChannelHelpers::SetTangentsAtThisTime(ControlRig, Section, ControlName, TimeMinusOne, Tangents);
				}

				// add active key
				{
					Section->Modify();
					TMovieSceneChannelData<bool> ChannelData = Channel->ActiveChannel.GetData();
					ChannelData.AddKey(Time, ActiveValueToBeSet);
				}

				// compensate
				{
					const bool bNextAsLocal = ActiveValueToBeSet;
					for (int32 Index = 0; Index < ChildTransforms.Num(); ++Index)
					{
						const FTransform& SpaceTransform = bNextAsLocal ? ParentTransforms[Index] : // constraint space 
																		  ComponentTransforms[Index]; // component space
						FTransform& ChildTransform = ChildTransforms[Index];
						ChildTransform = ChildTransform.GetRelativeTransform(SpaceTransform);
					}

					// add keys
					FBakingHelper::AddTransformKeys( ControlRig, ControlHandle->ControlName, FramesToCompensate,
						ChildTransforms, ChannelsToKey, TickResolution, bNextAsLocal);
				
					// set tangents at Time
					FControlRigSpaceChannelHelpers::SetTangentsAtThisTime(ControlRig, Section, ControlName, Time, Tangents);
				}
			}
		}
	}
}

void FConstraintChannelHelper::SmartComponentConstraintKey(
	UTickableTransformConstraint* InConstraint,
	const TSharedPtr<ISequencer>& InSequencer)
{
	const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InConstraint->ChildTRSHandle);
	if (!ComponentHandle)
	{
		return;
	}
}

void FConstraintChannelHelper::GetFramesToCompensate(
	const FMovieSceneConstraintChannel& InActiveChannel,
	const bool InActiveValueToBeSet,
	const FFrameNumber& InTime,
	const TArrayView<FMovieSceneFloatChannel*>& InChannels,
	TArray<FFrameNumber>& OutFramesAfter)
{
	const bool bHasKeys = (InActiveChannel.GetNumKeys() > 0);
	
	OutFramesAfter.Reset();

	// add the current frame
	OutFramesAfter.Add(InTime);

	// add the next frames that need transform compensation 
	for (const FMovieSceneFloatChannel* InChannel: InChannels)
	{
		const TMovieSceneChannelData<const FMovieSceneFloatValue> ChannelData = InChannel->GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (!Times.IsEmpty())
		{
			// look for the first next key frame for this channel 
			const int32 NextTimeIndex = Algo::UpperBound(Times, InTime);
			if (Times.IsValidIndex(NextTimeIndex))
			{
				// store the time while the state is different
				for (int32 Index = NextTimeIndex; Index < Times.Num(); ++Index)
				{
					if (!bHasKeys)
					{
						OutFramesAfter.Add(Times[Index]);
					}
					else
					{
						bool NextValue = false; InActiveChannel.Evaluate(Times[Index], NextValue);
						if (NextValue == InActiveValueToBeSet)
						{
							break;
						}
						OutFramesAfter.Add(Times[Index]);
					}
				}
			}
		}
	}

	// uniqueness
	OutFramesAfter.Sort();
	OutFramesAfter.SetNum(Algo::Unique(OutFramesAfter));
}

// namespace
// {
// 	using IndexRange = ::TRange<int32>;
// 	IndexRange GetRange(const TArrayView<const bool>& Values, const int32 Offset) 
// 	{
// 		IndexRange Range;
// 			
// 		int32 FirstActive = INDEX_NONE;
// 		for (int32 Index = Offset; Index < Values.Num(); ++Index)
// 		{
// 			if (Values[Index])
// 			{
// 				FirstActive = Index;
// 				break;
// 			}
// 		}
//
// 		if (FirstActive == INDEX_NONE)
// 		{
// 			return Range;
// 		}
//
// 		Range.SetLowerBound(TRangeBound<int32>(FirstActive));
// 		Range.SetUpperBound(TRangeBound<int32>(FirstActive));
//
// 		for (int32 NextInactive = FirstActive+1; NextInactive < Values.Num(); ++NextInactive)
// 		{
// 			if (Values[NextInactive] == false)
// 			{
// 				Range.SetUpperBound(TRangeBound<int32>(NextInactive));
// 				return Range;
// 			}
// 		}
// 			
// 		return Range;
// 	};
//
// 	TArray<IndexRange> GetIndexRanges(const FMovieSceneConstraintChannel* Channel)
// 	{
// 		TArray<IndexRange> Ranges;
//
// 		const TMovieSceneChannelData<const bool> ChannelData = Channel->GetData();
// 		const TArrayView<const bool> Values = ChannelData.GetValues();
// 		if (Values.Num() == 1)
// 		{
// 			if (Values[0] == true)
// 			{
// 				Ranges.Emplace(0);
// 			}
// 			return Ranges;
// 		}
// 		
// 		int32 Offset = 0;
// 		while (Values.IsValidIndex(Offset))
// 		{
// 			TRange<int32> Range = GetRange(Values, Offset);
// 			if (!Range.IsEmpty())
// 			{
// 				Ranges.Emplace(Range);
// 				Offset = Range.GetUpperBoundValue()+1;
// 			}
// 			else
// 			{
// 				Offset = INDEX_NONE;
// 			}
// 		}
// 			
// 		return Ranges;
// 	}
// }
//
// void DrawKeys(
// 	FMovieSceneConstraintChannel* Channel,
// 	TArrayView<const FKeyHandle> InKeyHandles,
// 	const UMovieSceneSection* InOwner,
// 	TArrayView<FKeyDrawParams> OutKeyDrawParams)
// {
// 	const UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(InOwner);
// 	if (!Section)
// 	{
// 		return;
// 	}
// 	
// 	static const FName SquareKeyBrushName("Sequencer.KeySquare");
// 	static const FName FilledBorderBrushName("FilledBorder");
//
// 	static FKeyDrawParams Params;
// 	Params.FillBrush = FAppStyle::GetBrush(FilledBorderBrushName);
// 	Params.BorderBrush = FAppStyle::GetBrush(SquareKeyBrushName);
// 	
// 	for (FKeyDrawParams& Param : OutKeyDrawParams)
// 	{
// 		Param = Params;
// 	}
// }
//
// void DrawExtra(FMovieSceneConstraintChannel* Channel, const UMovieSceneSection* Owner,const FGeometry& AllottedGeometry, FSequencerSectionPainter& Painter)
// {
// 	const UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(Owner);
// 	if (!Section || !Channel)
// 	{
// 		return;
// 	}
// 	
// 	// using namespace UE::Sequencer;
//
// 	// get index range
// 	TArray<IndexRange> IndexRanges = GetIndexRanges(Channel);
// 	if (IndexRanges.IsEmpty())
// 	{
// 		return;
// 	}
// 	
// 	// convert to bar range
// 	TArray<FKeyBarCurveModel::FBarRange> Ranges;
// 	const TMovieSceneChannelData<const bool> ChannelData = Channel->GetData();
// 	const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
//
// 	// const bool IsFirstValueTrue = IndexRanges[0].GetLowerBoundValue() == 0;
// 	
// 	for (const IndexRange& ActiveRange: IndexRanges)
// 	{
// 		FKeyBarCurveModel::FBarRange BarRange;
// 		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();
// 		double LowerValue = Times[ActiveRange.GetLowerBoundValue()] / TickResolution;
// 		double UpperValue = Times[ActiveRange.GetUpperBoundValue()] / TickResolution;
// 	
// 		BarRange.Range.SetLowerBound(TRangeBound<double>(LowerValue));
// 		BarRange.Range.SetUpperBound(TRangeBound<double>(UpperValue));
// 	
// 		BarRange.Name = "Constraint";
// 		BarRange.Color = FLinearColor(.2, .5, .1);
// 		static FLinearColor ZebraTint = FLinearColor::White.CopyWithNewOpacity(0.01f);
// 		BarRange.Color = BarRange.Color * (1.f - ZebraTint.A) + ZebraTint * ZebraTint.A;
// 		
// 		Ranges.Add(BarRange);
// 	}
//
// 	// draw bars
// 	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
// 	static constexpr ESlateDrawEffect DrawEffects = ESlateDrawEffect::None;
//
// 	const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont");
// 	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
//
// 	const FVector2D& LocalSize = AllottedGeometry.GetLocalSize();
// 	static constexpr float LaneTop = 0;
//
// 	const FTimeToPixel& TimeToPixel = Painter.GetTimeConverter();
// 	const double InputMin = TimeToPixel.PixelToSeconds(0.f);
// 	const double InputMax = TimeToPixel.PixelToSeconds(Painter.SectionGeometry.GetLocalSize().X);
//
// 	for (int32 Index = 0; Index < Ranges.Num(); ++Index)
// 	{
// 		const FKeyBarCurveModel::FBarRange& Range = Ranges[Index];
//
// 		double LowerSeconds = /*(Index == 0 && IsFirstValueTrue) ? InputMin : */Range.Range.GetLowerBoundValue();
// 		double UpperSeconds = Range.Range.GetUpperBoundValue();
// 		if (UpperSeconds == Range.Range.GetLowerBoundValue())
// 		{
// 			UpperSeconds = InputMax;
// 		}
//
// 		const double BoxStart = TimeToPixel.SecondsToPixel(LowerSeconds);
// 		const double BoxEnd = TimeToPixel.SecondsToPixel(UpperSeconds);
// 		const double BoxSize = BoxEnd - BoxStart;
//
// 		const FVector2D Size = FVector2D(BoxSize, LocalSize.Y);
// 		const FVector2D Translation = FVector2D(BoxStart, LaneTop);
// 		const FPaintGeometry BoxGeometry = AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Translation));
// 		
// 		FSlateDrawElement::MakeBox(Painter.DrawElements, Painter.LayerId, BoxGeometry, WhiteBrush, DrawEffects, Range.Color);
// 	}
// }
