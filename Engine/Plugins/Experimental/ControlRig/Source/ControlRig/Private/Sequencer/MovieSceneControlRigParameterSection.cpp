// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Animation/AnimSequence.h"
#include "Logging/MessageLog.h"
#include "Compilation/MovieSceneTemplateInterrogation.h"
#include "MovieScene.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Rigs/FKControlRig.h"
#include "Animation/AnimSequence.h"
#include "ControlRig/Private/Units/Execution/RigUnit_InverseExecution.h"
#include "Misc/ScopedSlowTask.h"
#include "MovieSceneTimeHelpers.h"

#define LOCTEXT_NAMESPACE "MovieSceneControlParameterRigSection"

#if WITH_EDITOR

struct FParameterFloatChannelEditorData
{
	FParameterFloatChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, const FText& GroupName, int SortStartIndex)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		FString NameAsString = InName.ToString();
		{
			MetaData.SetIdentifiers(InName, GroupName, GroupName);
			MetaData.bEnabled = bEnabledOverride;
			MetaData.SortOrder = SortStartIndex++;
			MetaData.bCanCollapseToTrack = false;
		}

		ExternalValues.OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return GetValue(InControlRig, InName,InObject, Bindings); };
		
		ExternalValues.OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		
	}

	static TOptional<float> GetValue(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl)
			{
				float Val = RigControl->Value.Get<float>();
				return Val;
		
			}
		}
		return TOptional<float>();
	}
	
	static void GetChannelValueAndWeight(FName ParameterName, UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		OutValue = 0.0f;
		OutWeight = 1.0f;

		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();

		if (Track)
		{
			FMovieSceneEvaluationTrack EvalTrack = CastChecked<IMovieSceneTrackTemplateProducer>(Track)->GenerateTrackTemplate(Track);
			FMovieSceneInterrogationData InterrogationData;
			RootTemplate.CopyActuators(InterrogationData.GetAccumulator());

			FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, TickResolution));
			EvalTrack.Interrogate(Context, InterrogationData, Object);

			float Val = 0.0f;
			for (const FFloatInterrogationData& InVector : InterrogationData.Iterate<FFloatInterrogationData>(UMovieSceneControlRigParameterSection::GetFloatInterrogationKey()))
			{
				if (InVector.ParameterName == ParameterName)
				{
					Val = InVector.Val;
					break;
				}
			}
			OutValue = Val;
		}
		OutWeight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, KeyTime);
	}

	FText							GroupName;
	FMovieSceneChannelMetaData      MetaData;
	TMovieSceneExternalValue<float> ExternalValues;
	FName ParameterName;
	UControlRig *ControlRig;
};

//Set up with all 4 Channels so it can be used by all vector types.
struct FParameterVectorChannelEditorData
{
	FParameterVectorChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, const FText& GroupName, int SortStartIndex, int32 NumChannels)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		FString NameAsString = InName.ToString();
		FString TotalName = NameAsString;

		{
			TotalName += ".X";
			MetaData[0].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelX);
			TotalName = NameAsString;
			MetaData[0].Group = GroupName;
			MetaData[0].bEnabled = bEnabledOverride;
			MetaData[0].SortOrder = SortStartIndex++;
			MetaData[0].bCanCollapseToTrack = false;
		}
		{
			TotalName += ".Y";
			MetaData[1].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelY);
			TotalName = NameAsString;
			MetaData[1].Group = GroupName;
			MetaData[1].bEnabled = bEnabledOverride;
			MetaData[1].SortOrder = SortStartIndex++;
			MetaData[1].bCanCollapseToTrack = false;
		}
		{
			TotalName += ".Z";
			MetaData[2].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelZ);
			TotalName = NameAsString;
			MetaData[2].Group = GroupName;
			MetaData[2].bEnabled = bEnabledOverride;
			MetaData[2].SortOrder = SortStartIndex++;
			MetaData[2].bCanCollapseToTrack = false;
		}
		{
			TotalName += ".W";
			MetaData[3].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelW);
			TotalName = NameAsString;
			MetaData[3].Group = GroupName;
			MetaData[3].bEnabled = bEnabledOverride;
			MetaData[3].SortOrder = SortStartIndex++;
			MetaData[3].bCanCollapseToTrack = false;
		}

		ExternalValues[0].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelX(InObject, InControlRig, InName, NumChannels); };
		ExternalValues[1].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelY(InObject, InControlRig, InName, NumChannels); };
		ExternalValues[2].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelZ(InObject, InControlRig, InName, NumChannels); };
		ExternalValues[3].OnGetExternalValue = [InControlRig, InName,NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelW(InObject, InControlRig, InName, NumChannels); };

		ExternalValues[0].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 0, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		ExternalValues[1].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 1, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		ExternalValues[2].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 2, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };
		ExternalValues[3].OnGetCurrentValueAndWeight = [InName, NumChannels](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight) { GetChannelValueAndWeight(InName, NumChannels, 3, Object, SectionToKey, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight); };

	}

	static FVector4 GetPropertyValue(UControlRig* ControlRig, FName ParameterName, UObject& InObject,int32 NumChannels)
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl)
			{
		
				if (NumChannels == 2)
				{
					FVector2D Vector = RigControl->Value.Get<FVector2D>();
					return FVector4(Vector.X, Vector.Y, 0.f, 0.f);
				}
				else if (NumChannels == 3)
				{
					FVector Vector = RigControl->Value.Get<FVector>();
					return FVector4(Vector.X, Vector.Y, Vector.Z, 0.f);
				}
				else
				{
					return RigControl->Value.Get<FVector4>();
				}
			}
		}
		return FVector4();
	}

	static TOptional<float> ExtractChannelX(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).X;
	}
	static TOptional<float> ExtractChannelY(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).Y;
	}
	static TOptional<float> ExtractChannelZ(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).Z;
	}
	static TOptional<float> ExtractChannelW(UObject& InObject, UControlRig* ControlRig, FName ParameterName, int32 NumChannels)
	{
		return GetPropertyValue(ControlRig, ParameterName, InObject, NumChannels).W;
	}

	static void GetChannelValueAndWeight(FName ParameterName, int32 NumChannels, int32 Index, UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		OutValue = 0.0f;
		OutWeight = 1.0f;
		if (Index >= NumChannels)
		{
			return;
		}

		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();

		if (Track)
		{
			FMovieSceneEvaluationTrack EvalTrack = CastChecked<IMovieSceneTrackTemplateProducer>(Track)->GenerateTrackTemplate(Track);
			FMovieSceneInterrogationData InterrogationData;
			RootTemplate.CopyActuators(InterrogationData.GetAccumulator());

			FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, TickResolution));
			EvalTrack.Interrogate(Context, InterrogationData, Object);

			switch (NumChannels)
			{
			case 2:
			{
				FVector2D Val(0.0f, 0.0f);
				for (const FVector2DInterrogationData& InVector : InterrogationData.Iterate<FVector2DInterrogationData>(UMovieSceneControlRigParameterSection::GetVector2DInterrogationKey()))
				{
					if (InVector.ParameterName == ParameterName)
					{
						Val = InVector.Val;
						break;
					}
				}
				switch (Index)
				{
				case 0:
					OutValue = Val.X;
					break;
				case 1:
					OutValue = Val.Y;
					break;
				}
			}
			break;
			case 3:
			{
				FVector Val(0.0f, 0.0f, 0.0f);
				for (const FVectorInterrogationData& InVector : InterrogationData.Iterate<FVectorInterrogationData>(UMovieSceneControlRigParameterSection::GetVectorInterrogationKey()))
				{
					if (InVector.ParameterName == ParameterName)
					{
						Val = InVector.Val;
						break;
					}
				}
				switch (Index)
				{
				case 0:
					OutValue = Val.X;
					break;
				case 1:
					OutValue = Val.Y;
					break;
				case 2:
					OutValue = Val.Z;
					break;
				}
			}
			break;
			case 4:
			{
				/* No Interrogation for Vector4, todo if we do add later
				FVector4 Val(0.0f, 0.0f, 0.0f, 0.0f);
				for (const FVector4InterrogationData& InVector : InterrogationData.Iterate<FVector4InterrogationData>(UMovieSceneControlRigParameterSection::GetVector4InterrogationKey()))
				{
					if (InVector.ParameterName == ParameterName)
					{
						Val = InVector.Val;
						break;
					}
				}
				switch (Index)
				{
				case 0:
					OutValue = Val.X;
					break;
				case 1:
					OutValue = Val.Y;
					break;
				case 2:
					OutValue = Val.Z;
					break;
				case 3:
					OutValue = Val.W;
					break;
				}
				*/
			}
			
			break;
			}
		}
		OutWeight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, KeyTime);
	}
	FText							GroupName;
	FMovieSceneChannelMetaData      MetaData[4];
	TMovieSceneExternalValue<float> ExternalValues[4];
	FName ParameterName;
	UControlRig *ControlRig;
};

struct FParameterTransformChannelEditorData
{
	FParameterTransformChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, EMovieSceneTransformChannel Mask, 
		const FText& GroupName, int SortStartIndex)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		//FText LocationGroup = NSLOCTEXT("MovieSceneControlParameterRigSection", "Location", "Location");
		//FText RotationGroup = NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation", "Rotation");
		//FText ScaleGroup = NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale", "Scale");

		FString NameAsString = InName.ToString();
		FString TotalName = NameAsString;
		FText TransformGroup = FText::Format(NSLOCTEXT("MovieSceneControlParameterRigSection", "MovieSceneControlParameterRigSectionGroupName", "{0}"), GroupName);

		{
			//MetaData[0].SetIdentifiers("Location.X", FCommonChannelData::ChannelX, LocationGroup);
			TotalName += ".Location.X";
			MetaData[0].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.X", "Location.X"), TransformGroup);
			TotalName = NameAsString;

			MetaData[0].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::TranslationX);
			MetaData[0].Color = FCommonChannelData::RedChannelColor;
			MetaData[0].SortOrder = SortStartIndex++;
			MetaData[0].bCanCollapseToTrack = false;

			//MetaData[1].SetIdentifiers("Location.Y", FCommonChannelData::ChannelY, LocationGroup);
			TotalName += ".Location.Y";
			MetaData[1].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.Y", "Location.Y"), TransformGroup);
			TotalName = NameAsString;

			MetaData[1].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::TranslationY);
			MetaData[1].Color = FCommonChannelData::GreenChannelColor;
			MetaData[1].SortOrder = SortStartIndex++;
			MetaData[1].bCanCollapseToTrack = false;

			//MetaData[2].SetIdentifiers("Location.Z", FCommonChannelData::ChannelZ, LocationGroup);
			TotalName += ".Location.Z";
			MetaData[2].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Location.Z", "Location.Z"), TransformGroup);
			TotalName = NameAsString;

			MetaData[2].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::TranslationZ);
			MetaData[2].Color = FCommonChannelData::BlueChannelColor;
			MetaData[2].SortOrder = SortStartIndex++;
			MetaData[2].bCanCollapseToTrack = false;
		}
		{
			//MetaData[3].SetIdentifiers("Rotation.X", NSLOCTEXT("MovieSceneTransformSection", "RotationX", "Roll"), RotationGroup);
			TotalName += ".Rotation.X";
			MetaData[3].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.X", "Rotation.Roll"), TransformGroup);
			TotalName = NameAsString;

			MetaData[3].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::RotationX);
			MetaData[3].Color = FCommonChannelData::RedChannelColor;
			MetaData[3].SortOrder = SortStartIndex++;
			MetaData[3].bCanCollapseToTrack = false;

			//MetaData[4].SetIdentifiers("Rotation.Y", NSLOCTEXT("MovieSceneTransformSection", "RotationY", "Pitch"), RotationGroup);
			TotalName += ".Rotation.Y";
			MetaData[4].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.Y", "Rotation.Pitch"), TransformGroup);
			TotalName = NameAsString;

			MetaData[4].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::RotationY);
			MetaData[4].Color = FCommonChannelData::GreenChannelColor;
			MetaData[4].SortOrder = SortStartIndex++;
			MetaData[4].bCanCollapseToTrack = false;

			//MetaData[5].SetIdentifiers("Rotation.Z", NSLOCTEXT("MovieSceneTransformSection", "RotationZ", "Yaw"), RotationGroup);
			TotalName += ".Rotation.Z";
			MetaData[5].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Rotation.Z", "Rotation.Yaw"), TransformGroup);
			TotalName = NameAsString;

			MetaData[5].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::RotationZ);
			MetaData[5].Color = FCommonChannelData::BlueChannelColor;
			MetaData[5].SortOrder = SortStartIndex++;
			MetaData[5].bCanCollapseToTrack = false;
		}
		{
			//MetaData[6].SetIdentifiers("Scale.X", FCommonChannelData::ChannelX, ScaleGroup);
			TotalName += ".Scale.X";
			MetaData[6].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.X", "Scale.X"), TransformGroup);
			TotalName = NameAsString;

			MetaData[6].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::ScaleX);
			MetaData[6].Color = FCommonChannelData::RedChannelColor;
			MetaData[6].SortOrder = SortStartIndex++;
			MetaData[6].bCanCollapseToTrack = false;

			//MetaData[7].SetIdentifiers("Scale.Y", FCommonChannelData::ChannelY, ScaleGroup);
			TotalName += ".Scale.Y";
			MetaData[7].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.Y", "Scale.Y"), TransformGroup);
			TotalName = NameAsString;

			MetaData[7].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::ScaleY);
			MetaData[7].Color = FCommonChannelData::GreenChannelColor;
			MetaData[7].SortOrder = SortStartIndex++;
			MetaData[7].bCanCollapseToTrack = false;

			//MetaData[8].SetIdentifiers("Scale.Z", FCommonChannelData::ChannelZ, ScaleGroup);
			TotalName += ".Scale.Z";
			MetaData[8].SetIdentifiers(FName(*TotalName), NSLOCTEXT("MovieSceneControlParameterRigSection", "Scale.Z", "Scale.Z"), TransformGroup);
			TotalName = NameAsString;

			MetaData[8].bEnabled = bEnabledOverride && EnumHasAllFlags(Mask, EMovieSceneTransformChannel::ScaleZ);
			MetaData[8].Color = FCommonChannelData::BlueChannelColor;
			MetaData[8].SortOrder = SortStartIndex++;
			MetaData[8].bCanCollapseToTrack = false;
		}
		{
			//MetaData[9].SetIdentifiers("Weight", NSLOCTEXT("MovieSceneTransformSection", "Weight", "Weight"));
			//MetaData[9].bEnabled = EnumHasAllFlags(Mask, EMovieSceneTransformChannel::Weight);
		}

		ExternalValues[0].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Translation = GetTranslation(InControlRig, InName, InObject, Bindings);
			return Translation.IsSet() ? Translation->X : TOptional<float>();
		};

		ExternalValues[1].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Translation = GetTranslation(InControlRig, InName, InObject, Bindings);
			return Translation.IsSet() ? Translation->Y : TOptional<float>();
		};
		ExternalValues[2].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Translation = GetTranslation(InControlRig, InName, InObject, Bindings);
			return Translation.IsSet() ? Translation->Z : TOptional<float>();
		};
		ExternalValues[3].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FRotator> Rotator = GetRotator(InControlRig, InName, InObject, Bindings);
			return Rotator.IsSet() ? Rotator->Roll : TOptional<float>();
		};
		ExternalValues[4].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FRotator> Rotator = GetRotator(InControlRig, InName, InObject, Bindings);
			return Rotator.IsSet() ? Rotator->Pitch : TOptional<float>();
		};
		ExternalValues[5].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FRotator> Rotator = GetRotator(InControlRig, InName, InObject, Bindings);
			return Rotator.IsSet() ? Rotator->Yaw : TOptional<float>();
		};
		ExternalValues[6].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Scale = GetScale(InControlRig, InName, InObject, Bindings);
			return Scale.IsSet() ? Scale->X : TOptional<float>();
		};
		ExternalValues[7].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Scale = GetScale(InControlRig, InName, InObject, Bindings);
			return Scale.IsSet() ? Scale->Y : TOptional<float>();
		};
		ExternalValues[8].OnGetExternalValue = [InControlRig, InName](UObject& InObject, FTrackInstancePropertyBindings* Bindings)
		{
			TOptional<FVector> Scale = GetScale(InControlRig, InName, InObject, Bindings);
			return Scale.IsSet() ? Scale->Z : TOptional<float>();
		};

		ExternalValues[0].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 0, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[1].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 1, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[2].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 2, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[3].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 3, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[4].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 4, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[5].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 5, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[6].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 6, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[7].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 7, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};
		ExternalValues[8].OnGetCurrentValueAndWeight = [InName](UObject* Object, UMovieSceneSection*  SectionToKey, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
			float& OutValue, float& OutWeight)
		{
			GetValueAndWeight(InName, Object, SectionToKey, 8, KeyTime, TickResolution, RootTemplate, OutValue, OutWeight);
		};

	}

	static TOptional<FVector> GetTranslation(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl)
			{
				if (RigControl->ControlType == ERigControlType::Transform)
				{
					FTransform Transform = RigControl->Value.Get<FTransform>();
					return Transform.GetTranslation();
				}
				else if  (RigControl->ControlType == ERigControlType::TransformNoScale)
				{
					FTransformNoScale NoScale = RigControl->Value.Get<FTransformNoScale>();
					FTransform Transform = NoScale;
					return Transform.GetTranslation();
				}
				else if (RigControl->ControlType == ERigControlType::EulerTransform)
				{
					FEulerTransform Euler = RigControl->Value.Get<FEulerTransform>();
					return Euler.Location;
				}
			}
		}
		return TOptional<FVector>();
	}

	static TOptional<FRotator> GetRotator(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{

		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl)
			{
				if (RigControl->ControlType == ERigControlType::Transform)
				{
					FTransform Transform = RigControl->Value.Get<FTransform>();
					return Transform.GetRotation().Rotator();
				}
				else if (RigControl->ControlType == ERigControlType::TransformNoScale)
				{
					FTransformNoScale NoScale = RigControl->Value.Get<FTransformNoScale>();
					FTransform Transform = NoScale;
					return Transform.GetRotation().Rotator();
				}
				else if (RigControl->ControlType == ERigControlType::EulerTransform)
				{
					FEulerTransform Euler = RigControl->Value.Get<FEulerTransform>();
					return Euler.Rotation;
			}
		}
		}
		return TOptional<FRotator>();
	}

	static TOptional<FVector> GetScale(UControlRig* ControlRig, FName ParameterName, UObject& InObject, FTrackInstancePropertyBindings* Bindings)
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl)
			{
				if (RigControl->ControlType == ERigControlType::Transform)
				{
					FTransform Transform = RigControl->Value.Get<FTransform>();
					return Transform.GetScale3D();
				}
				else if (RigControl->ControlType == ERigControlType::EulerTransform)
				{
					FEulerTransform Euler = RigControl->Value.Get<FEulerTransform>();
					return Euler.Scale;
				}
			}
		}
		return TOptional<FVector>();
	}

	static void GetValueAndWeight(FName ParameterName, UObject* Object, UMovieSceneSection*  SectionToKey, int32 Index, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();
		FMovieSceneEvaluationTrack EvalTrack = CastChecked<UMovieSceneControlRigParameterTrack>(Track)->GenerateTrackTemplate(Track);
		FMovieSceneInterrogationData InterrogationData;
		RootTemplate.CopyActuators(InterrogationData.GetAccumulator());

		FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, TickResolution));
		EvalTrack.Interrogate(Context, InterrogationData, Object);

		FVector CurrentPos; FRotator CurrentRot;
		FVector CurrentScale;

		for (const FTransformInterrogationData& Transform : InterrogationData.Iterate<FTransformInterrogationData>(UMovieSceneControlRigParameterSection::GetTransformInterrogationKey()))
		{
			if (Transform.ParameterName == ParameterName)
			{
				CurrentPos = Transform.Val.GetTranslation();
				CurrentRot = Transform.Val.GetRotation().Rotator();
				CurrentScale = Transform.Val.GetScale3D();
				break;
			}
		}

		switch (Index)
		{
		case 0:
			OutValue = CurrentPos.X;
			break;
		case 1:
			OutValue = CurrentPos.Y;
			break;
		case 2:
			OutValue = CurrentPos.Z;
			break;
		case 3:
			OutValue = CurrentRot.Roll;
			break;
		case 4:
			OutValue = CurrentRot.Pitch;
			break;
		case 5:
			OutValue = CurrentRot.Yaw;
			break;
		case 6:
			OutValue = CurrentScale.X;
			break;
		case 7:
			OutValue = CurrentScale.Y;
			break;
		case 8:
			OutValue = CurrentScale.Z;
			break;

		}
		OutWeight = MovieSceneHelpers::CalculateWeightForBlending(SectionToKey, KeyTime);
	}
		
public:

	FText							GroupName;
	FMovieSceneChannelMetaData      MetaData[9];
	TMovieSceneExternalValue<float> ExternalValues[9];
	FName ParameterName;
	UControlRig *ControlRig;
};

#endif // WITH_EDITOR

UMovieSceneControlRigParameterSection::UMovieSceneControlRigParameterSection() :bDoNotKey(false)
{
	// Section template relies on always restoring state for objects when they are no longer animating. This is how it releases animation control.
	EvalOptions.CompletionMode = EMovieSceneCompletionMode::RestoreState;
	TransformMask = EMovieSceneTransformChannel::AllTransform;

	Weight.SetDefault(1.0f);

#if WITH_EDITOR

	static const FMovieSceneChannelMetaData MetaData("Weight", LOCTEXT("WeightChannelText", "Weight"));
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Weight, MetaData, TMovieSceneExternalValue<float>());

#else

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Weight);

#endif
}

void UMovieSceneControlRigParameterSection::SetBlendType(EMovieSceneBlendType InBlendType)
{
	if (GetSupportedBlendTypes().Contains(InBlendType))
	{
		BlendType = InBlendType;
		if (ControlRig)
		{
			const FChannelMapInfo* ChannelInfo = nullptr;

			// Set Defaults based upon Type
			TArrayView<FMovieSceneFloatChannel*> FloatChannels = ChannelProxy->GetChannels<FMovieSceneFloatChannel>();
			const TArray<FRigControl>& Controls = ControlRig->AvailableControls();

			for (const FRigControl& RigControl : Controls)
			{
				switch (RigControl.ControlType)
				{

				case ERigControlType::Scale:
				{
					ChannelInfo = ControlChannelMap.Find(RigControl.Name);
					if (ChannelInfo)
					{
						if (InBlendType == EMovieSceneBlendType::Absolute)
						{
							FloatChannels[ChannelInfo->ChannelIndex]->SetDefault(1.0f);
							FloatChannels[ChannelInfo->ChannelIndex+1]->SetDefault(1.0f);
							FloatChannels[ChannelInfo->ChannelIndex+2]->SetDefault(1.0f);
						}
						else
						{
							FloatChannels[ChannelInfo->ChannelIndex]->SetDefault(0.0f);
							FloatChannels[ChannelInfo->ChannelIndex + 1]->SetDefault(0.0f);
							FloatChannels[ChannelInfo->ChannelIndex + 2]->SetDefault(0.0f);
						}
					}
				}
				break;
				case ERigControlType::Transform:
				case ERigControlType::EulerTransform:
				{
					ChannelInfo = ControlChannelMap.Find(RigControl.Name);
					if (ChannelInfo)
					{
						if (InBlendType == EMovieSceneBlendType::Absolute)
						{
							FloatChannels[ChannelInfo->ChannelIndex + 6]->SetDefault(1.0f);
							FloatChannels[ChannelInfo->ChannelIndex + 7]->SetDefault(1.0f);
							FloatChannels[ChannelInfo->ChannelIndex + 8]->SetDefault(1.0f);
						}
						else
						{
							FloatChannels[ChannelInfo->ChannelIndex + 6]->SetDefault(0.0f);
							FloatChannels[ChannelInfo->ChannelIndex + 7]->SetDefault(0.0f);
							FloatChannels[ChannelInfo->ChannelIndex + 8]->SetDefault(0.0f);
						}
					}
				}
				break;
				}
			};
		}
	}
}


void UMovieSceneControlRigParameterSection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

}
 
void UMovieSceneControlRigParameterSection::PostEditImport()
{
	Super::PostEditImport();
	if (UMovieSceneControlRigParameterTrack* Track = Cast< UMovieSceneControlRigParameterTrack>(GetOuter()))
	{
		SetControlRig(Track->GetControlRig());
	}
	ReconstructChannelProxy(true);
}

void UMovieSceneControlRigParameterSection::PostLoad()
{
	Super::PostLoad();

}
bool UMovieSceneControlRigParameterSection::HasScalarParameter(FName InParameterName) const
{
	for (const FScalarParameterNameAndCurve& ScalarParameterNameAndCurve : ScalarParameterNamesAndCurves)
	{
		if (ScalarParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasBoolParameter(FName InParameterName) const
{
	for (const FBoolParameterNameAndCurve& BoolParameterNameAndCurve : BoolParameterNamesAndCurves)
	{
		if (BoolParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasEnumParameter(FName InParameterName) const
{
	for (const FEnumParameterNameAndCurve& EnumParameterNameAndCurve : EnumParameterNamesAndCurves)
	{
		if (EnumParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasIntegerParameter(FName InParameterName) const
{
	for (const FIntegerParameterNameAndCurve& IntegerParameterNameAndCurve : IntegerParameterNamesAndCurves)
	{
		if (IntegerParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasVector2DParameter(FName InParameterName) const
{
	for (const FVector2DParameterNameAndCurves& Vector2DParameterNameAndCurve : Vector2DParameterNamesAndCurves)
	{
		if (Vector2DParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasVectorParameter(FName InParameterName) const
{
	for (const FVectorParameterNameAndCurves& VectorParameterNameAndCurve : VectorParameterNamesAndCurves)
	{
		if (VectorParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasColorParameter(FName InParameterName) const
{
	for (const FColorParameterNameAndCurves& ColorParameterNameAndCurve : ColorParameterNamesAndCurves)
	{
		if (ColorParameterNameAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::HasTransformParameter(FName InParameterName) const
{
	for (const FTransformParameterNameAndCurves& TransformParameterNamesAndCurve : TransformParameterNamesAndCurves)
	{
		if (TransformParameterNamesAndCurve.ParameterName == InParameterName)
		{
			return true;
		}
	}
	return false;
}

void UMovieSceneControlRigParameterSection::AddScalarParameter(FName InParameterName, TOptional<float> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneFloatChannel* ExistingChannel = nullptr;
	if (!HasScalarParameter(InParameterName))
	{
		const int32 NewIndex = ScalarParameterNamesAndCurves.Add(FScalarParameterNameAndCurve(InParameterName));
		ExistingChannel = &ScalarParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}


void UMovieSceneControlRigParameterSection::AddBoolParameter(FName InParameterName, TOptional<bool> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneBoolChannel* ExistingChannel = nullptr;
	if (!HasBoolParameter(InParameterName))
	{
		const int32 NewIndex = BoolParameterNamesAndCurves.Add(FBoolParameterNameAndCurve(InParameterName));
		ExistingChannel = &BoolParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(false);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}
void UMovieSceneControlRigParameterSection::AddEnumParameter(FName InParameterName, UEnum* Enum,TOptional<uint8> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneByteChannel* ExistingChannel = nullptr;
	if (!HasEnumParameter(InParameterName))
	{
		const int32 NewIndex = EnumParameterNamesAndCurves.Add(FEnumParameterNameAndCurve(InParameterName));
		ExistingChannel = &EnumParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(false);
		}
		ExistingChannel->SetEnum(Enum);
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}

void UMovieSceneControlRigParameterSection::AddIntegerParameter(FName InParameterName, TOptional<int32> DefaultValue, bool bReconstructChannel)
{
	FMovieSceneIntegerChannel* ExistingChannel = nullptr;
	if (!HasIntegerParameter(InParameterName))
	{
		const int32 NewIndex = IntegerParameterNamesAndCurves.Add(FIntegerParameterNameAndCurve(InParameterName));
		ExistingChannel = &IntegerParameterNamesAndCurves[NewIndex].ParameterCurve;
		if (DefaultValue.IsSet())
		{
			ExistingChannel->SetDefault(DefaultValue.GetValue());
		}
		else
		{
			ExistingChannel->SetDefault(false);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}

void UMovieSceneControlRigParameterSection::AddVector2DParameter(FName InParameterName, TOptional<FVector2D> DefaultValue, bool bReconstructChannel)
{
	FVector2DParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasVector2DParameter(InParameterName))
	{
		int32 NewIndex = Vector2DParameterNamesAndCurves.Add(FVector2DParameterNameAndCurves(InParameterName));
		ExistingCurves = &Vector2DParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			ExistingCurves->XCurve.SetDefault(DefaultValue.GetValue().X);
			ExistingCurves->YCurve.SetDefault(DefaultValue.GetValue().Y);
		}
		else
		{
			ExistingCurves->XCurve.SetDefault(0.0f);
			ExistingCurves->YCurve.SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}

void UMovieSceneControlRigParameterSection::AddVectorParameter(FName InParameterName, TOptional<FVector> DefaultValue, bool bReconstructChannel)
{
	FVectorParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasVectorParameter(InParameterName))
	{
		int32 NewIndex = VectorParameterNamesAndCurves.Add(FVectorParameterNameAndCurves(InParameterName));
		ExistingCurves = &VectorParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			ExistingCurves->XCurve.SetDefault(DefaultValue.GetValue().X);
			ExistingCurves->YCurve.SetDefault(DefaultValue.GetValue().Y);
			ExistingCurves->ZCurve.SetDefault(DefaultValue.GetValue().Z);

		}
		else
		{
			ExistingCurves->XCurve.SetDefault(0.0f);
			ExistingCurves->YCurve.SetDefault(0.0f);
			ExistingCurves->ZCurve.SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}

void UMovieSceneControlRigParameterSection::AddColorParameter(FName InParameterName, TOptional<FLinearColor> DefaultValue, bool bReconstructChannel)
{
	FColorParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasColorParameter(InParameterName))
	{
		int32 NewIndex = ColorParameterNamesAndCurves.Add(FColorParameterNameAndCurves(InParameterName));
		ExistingCurves = &ColorParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			ExistingCurves->RedCurve.SetDefault(DefaultValue.GetValue().R);
			ExistingCurves->GreenCurve.SetDefault(DefaultValue.GetValue().G);
			ExistingCurves->BlueCurve.SetDefault(DefaultValue.GetValue().B);
			ExistingCurves->AlphaCurve.SetDefault(DefaultValue.GetValue().A);
		}
		else
		{
			ExistingCurves->RedCurve.SetDefault(0.0f);
			ExistingCurves->GreenCurve.SetDefault(0.0f);
			ExistingCurves->BlueCurve.SetDefault(0.0f);
			ExistingCurves->AlphaCurve.SetDefault(0.0f);
		}
		if (bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}

void UMovieSceneControlRigParameterSection::AddTransformParameter(FName InParameterName, TOptional<FTransform> DefaultValue, bool bReconstructChannel)
{
	FTransformParameterNameAndCurves* ExistingCurves = nullptr;

	if (!HasTransformParameter(InParameterName))
	{
		int32 NewIndex = TransformParameterNamesAndCurves.Add(FTransformParameterNameAndCurves(InParameterName));
		ExistingCurves = &TransformParameterNamesAndCurves[NewIndex];
		if (DefaultValue.IsSet())
		{
			FTransform& InValue = DefaultValue.GetValue();
			FVector Translation = InValue.GetTranslation();
			FRotator Rotator = InValue.GetRotation().Rotator();
			FVector Scale = InValue.GetScale3D();
			ExistingCurves->Translation[0].SetDefault(Translation[0]);
			ExistingCurves->Translation[1].SetDefault(Translation[1]);
			ExistingCurves->Translation[2].SetDefault(Translation[2]);

			ExistingCurves->Rotation[0].SetDefault(Rotator.Roll);
			ExistingCurves->Rotation[1].SetDefault(Rotator.Pitch);
			ExistingCurves->Rotation[2].SetDefault(Rotator.Yaw);

			ExistingCurves->Scale[0].SetDefault(Scale[0]);
			ExistingCurves->Scale[1].SetDefault(Scale[1]);
			ExistingCurves->Scale[2].SetDefault(Scale[2]);

		}
		else if (GetBlendType() == EMovieSceneBlendType::Additive)
		{
			ExistingCurves->Translation[0].SetDefault(0.0f);
			ExistingCurves->Translation[1].SetDefault(0.0f);
			ExistingCurves->Translation[2].SetDefault(0.0f);

			ExistingCurves->Rotation[0].SetDefault(0.0f);
			ExistingCurves->Rotation[1].SetDefault(0.0f);
			ExistingCurves->Rotation[2].SetDefault(0.0f);

			ExistingCurves->Scale[0].SetDefault(0.0f);
			ExistingCurves->Scale[1].SetDefault(0.0f);
			ExistingCurves->Scale[2].SetDefault(0.0f);
		}
		if(bReconstructChannel)
		{
			ReconstructChannelProxy(true);
		}
	}
}


void UMovieSceneControlRigParameterSection::ReconstructChannelProxy(bool bForce)
{
	FMovieSceneChannelProxyData Channels;
	if(bForce || ControlsMask!= OldControlsMask)
	{
		ControlChannelMap.Empty();

		OldControlsMask = ControlsMask;
		// Need to create the channels in sorted orders
		if (ControlRig)
		{
			TArray<FRigControl> SortedControls;
			ControlRig->GetControlsInOrder(SortedControls);
			int32 ControlIndex = 0; 
			int32 TotalIndex = 0; 
			int32 FloatChannelIndex = 0;
			int32 BoolChannelIndex = 0;
			int32 EnumChannelIndex = 0;
			int32 IntegerChannelIndex = 0;
			const FName BoolChannelTypeName = FMovieSceneBoolChannel::StaticStruct()->GetFName();
			const FName EnumChannelTypeName = FMovieSceneByteChannel::StaticStruct()->GetFName();
			const FName IntegerChannelTypeName = FMovieSceneIntegerChannel::StaticStruct()->GetFName();
			for (const FRigControl& RigControl : SortedControls)
			{

				if (!RigControl.bAnimatable)
				{
					continue;
				}
#if WITH_EDITOR
				switch (RigControl.ControlType)
				{
				case ERigControlType::Float:
				{
					for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
					{
						if (RigControl.Name == Scalar.ParameterName)
						{
							bool bEnabled = ControlsMask[ControlIndex];

							FText Group;
							if(!RigControl.ParentName.IsNone())
							{
								FRigControl& ParentControl = ControlRig->GetControlHierarchy()[RigControl.ParentName];
								switch(ParentControl.ControlType)
								{
									case ERigControlType::Position:
									case ERigControlType::Scale:
									case ERigControlType::Rotator:
									case ERigControlType::Transform:
									case ERigControlType::EulerTransform:
									case ERigControlType::TransformNoScale:
									{
										Group = FText::FromName(ParentControl.GetDisplayName());
										break;
									}
								}
							}

							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));
								Group = FText::FromName(RigControl.GetDisplayName());
								ControlIndex++;  //up the index only if no parent
							}
							else
							{
								FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(RigControl.ParentName);
								if (pChannelIndex)
								{
									ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex,pChannelIndex->ControlIndex));
								}
								else
								{
									ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));

								}
							}

							FParameterFloatChannelEditorData EditorData(ControlRig, Scalar.ParameterName, bEnabled, Group, TotalIndex);
							EditorData.MetaData.DisplayText = FText::FromName(RigControl.GetDisplayName());
							Channels.Add(Scalar.ParameterCurve, EditorData.MetaData, EditorData.ExternalValues);
							FloatChannelIndex += 1;
							TotalIndex += 1;
							break;
						}
					}
					break;
				}
				case ERigControlType::Bool:
				{
					for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
					{
						if (RigControl.Name == Bool.ParameterName)
						{
							bool bEnabled = ControlsMask[ControlIndex];

							FText Group;
							if(!RigControl.ParentName.IsNone())
							{
								FRigControl& ParentControl = ControlRig->GetControlHierarchy()[RigControl.ParentName];
								switch(ParentControl.ControlType)
								{
									case ERigControlType::Position:
									case ERigControlType::Scale:
									case ERigControlType::Rotator:
									case ERigControlType::Transform:
									case ERigControlType::EulerTransform:
									case ERigControlType::TransformNoScale:
									{
										Group = FText::FromName(ParentControl.GetDisplayName());
										break;
									}
								}
							}

							if (Group.IsEmpty())
							{
								ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, BoolChannelIndex, INDEX_NONE,BoolChannelTypeName));
								Group = FText::FromName(RigControl.GetDisplayName());
								ControlIndex++;  //up the index only if no parent
							}
							else
							{
								FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(RigControl.ParentName);
								if (pChannelIndex)
								{
									ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, BoolChannelIndex,pChannelIndex->ControlIndex,BoolChannelTypeName));
								}
								else
								{
									ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, BoolChannelIndex,INDEX_NONE, BoolChannelTypeName));
								}
							}

							FMovieSceneChannelMetaData MetaData(Bool.ParameterName, Group, Group, bEnabled);
							MetaData.DisplayText = FText::FromName(RigControl.GetDisplayName());
							MetaData.SortOrder = TotalIndex++;
							BoolChannelIndex += 1;
							// Prevent single channels from collapsing to the track node
							MetaData.bCanCollapseToTrack = false;
							Channels.Add(Bool.ParameterCurve, MetaData, TMovieSceneExternalValue<bool>());
							break;
						}
					}
					break;
				}
				case ERigControlType::Integer:
				{
					if (RigControl.ControlEnum)
					{
						for (FEnumParameterNameAndCurve& Enum : GetEnumParameterNamesAndCurves())
						{
							if (RigControl.Name == Enum.ParameterName)
							{
								bool bEnabled = ControlsMask[ControlIndex];

								FText Group;
								if (!RigControl.ParentName.IsNone())
								{
									FRigControl& ParentControl = ControlRig->GetControlHierarchy()[RigControl.ParentName];
									switch (ParentControl.ControlType)
									{
									case ERigControlType::Position:
									case ERigControlType::Scale:
									case ERigControlType::Rotator:
									case ERigControlType::Transform:
									case ERigControlType::EulerTransform:
									case ERigControlType::TransformNoScale:
									{
										Group = FText::FromName(ParentControl.GetDisplayName());
										break;
									}
									}
								}

								if (Group.IsEmpty())
								{
									ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, EnumChannelIndex,INDEX_NONE, EnumChannelTypeName));
									Group = FText::FromName(RigControl.GetDisplayName());
									ControlIndex++;  //up the index only if no parent
								}
								else
								{
									FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(RigControl.ParentName);
									if (pChannelIndex)
									{
										ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, EnumChannelIndex, pChannelIndex->ControlIndex, EnumChannelTypeName));
									}
									else
									{
										ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, EnumChannelIndex, INDEX_NONE, EnumChannelTypeName));
									}
								}

								FMovieSceneChannelMetaData MetaData(Enum.ParameterName, Group, Group, bEnabled);
								MetaData.DisplayText = FText::FromName(RigControl.GetDisplayName());
								EnumChannelIndex += 1;
								MetaData.SortOrder = TotalIndex++;
								// Prevent single channels from collapsing to the track node
								MetaData.bCanCollapseToTrack = false;
								Channels.Add(Enum.ParameterCurve, MetaData, TMovieSceneExternalValue<uint8>());
								break;
							}
						}
					}
					else
					{
						for (FIntegerParameterNameAndCurve& Integer : GetIntegerParameterNamesAndCurves())
						{
							if (RigControl.Name == Integer.ParameterName)
							{
								bool bEnabled = ControlsMask[ControlIndex];

								FText Group;
								if (!RigControl.ParentName.IsNone())
								{
									FRigControl& ParentControl = ControlRig->GetControlHierarchy()[RigControl.ParentName];
									switch (ParentControl.ControlType)
									{
									case ERigControlType::Position:
									case ERigControlType::Scale:
									case ERigControlType::Rotator:
									case ERigControlType::Transform:
									case ERigControlType::EulerTransform:
									case ERigControlType::TransformNoScale:
									{
										Group = FText::FromName(ParentControl.GetDisplayName());
										break;
									}
									}
								}
								if (Group.IsEmpty())
								{
									ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, IntegerChannelIndex,INDEX_NONE,IntegerChannelTypeName));
									Group = FText::FromName(RigControl.GetDisplayName());
									ControlIndex++;  //up the index only if no parent
								}
								else
								{
									FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(RigControl.ParentName);
									if (pChannelIndex)
									{
										ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, IntegerChannelIndex, pChannelIndex->ControlIndex,IntegerChannelTypeName));
									}
									else
									{
										ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, IntegerChannelIndex, INDEX_NONE, IntegerChannelTypeName));
									}
								}

								FMovieSceneChannelMetaData MetaData(Integer.ParameterName, Group, Group, bEnabled);
								MetaData.DisplayText = FText::FromName(RigControl.GetDisplayName());
								IntegerChannelIndex += 1;
								MetaData.SortOrder = TotalIndex++;
								// Prevent single channels from collapsing to the track node
								MetaData.bCanCollapseToTrack = false;
								Channels.Add(Integer.ParameterCurve, MetaData, TMovieSceneExternalValue<int32>());
								break;
							}
						}

					}
					break;
				}
				case ERigControlType::Vector2D:
				{
					for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
					{
						if (RigControl.Name == Vector2D.ParameterName)
						{
							ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));
							bool bEnabled = ControlsMask[ControlIndex++];
							FText Group = FText::FromName(RigControl.GetDisplayName());
							FParameterVectorChannelEditorData EditorData(ControlRig, Vector2D.ParameterName, bEnabled, Group, TotalIndex, 2);
							Channels.Add(Vector2D.XCurve, EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Vector2D.YCurve, EditorData.MetaData[1], EditorData.ExternalValues[1]);
							FloatChannelIndex += 2;
							TotalIndex += 2;
							break;
						}
					}
					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
					{
						if (RigControl.Name == Vector.ParameterName)
						{
							if (RigControl.ControlType == ERigControlType::Scale)
							{
								if (BlendType == EMovieSceneBlendType::Additive)
								{
									Vector.XCurve.SetDefault(0.0f);
									Vector.YCurve.SetDefault(0.0f);
									Vector.ZCurve.SetDefault(0.0f);
								}
								else
								{
									Vector.XCurve.SetDefault(1.0f);
									Vector.YCurve.SetDefault(1.0f);
									Vector.ZCurve.SetDefault(1.0f);
								}
							}
							ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));
							bool bEnabled = ControlsMask[ControlIndex++];
							FText Group = FText::FromName(RigControl.GetDisplayName());
							FParameterVectorChannelEditorData EditorData(ControlRig, Vector.ParameterName, bEnabled, Group, TotalIndex, 3);
							Channels.Add(Vector.XCurve, EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Vector.YCurve, EditorData.MetaData[1], EditorData.ExternalValues[1]);
							Channels.Add(Vector.ZCurve, EditorData.MetaData[2], EditorData.ExternalValues[2]);
							FloatChannelIndex += 3;
							TotalIndex += 3;
							break;
						}
					}
					break;
				}

				case ERigControlType::TransformNoScale:
				case ERigControlType::Transform:
				case ERigControlType::EulerTransform:
				{
					for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
					{
						if (RigControl.Name == Transform.ParameterName)
						{
							ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));
							bool bEnabled = ControlsMask[ControlIndex++];
							FText Group = FText::FromName(RigControl.GetDisplayName());

							FParameterTransformChannelEditorData EditorData(ControlRig, Transform.ParameterName, bEnabled, TransformMask.GetChannels(), Group, 
								TotalIndex);

							Channels.Add(Transform.Translation[0], EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Transform.Translation[1], EditorData.MetaData[1], EditorData.ExternalValues[1]);
							Channels.Add(Transform.Translation[2], EditorData.MetaData[2], EditorData.ExternalValues[2]);

							Channels.Add(Transform.Rotation[0], EditorData.MetaData[3], EditorData.ExternalValues[3]);
							Channels.Add(Transform.Rotation[1], EditorData.MetaData[4], EditorData.ExternalValues[4]);
							Channels.Add(Transform.Rotation[2], EditorData.MetaData[5], EditorData.ExternalValues[5]);

							if (RigControl.ControlType == ERigControlType::Transform || RigControl.ControlType == ERigControlType::EulerTransform)
							{
								if (BlendType == EMovieSceneBlendType::Additive)
								{
									Transform.Scale[0].SetDefault(0.0f);
									Transform.Scale[1].SetDefault(0.0f);
									Transform.Scale[2].SetDefault(0.0f);
								}
								else
								{
									Transform.Scale[0].SetDefault(1.0f);
									Transform.Scale[1].SetDefault(1.0f);
									Transform.Scale[2].SetDefault(1.0f);
								}
								Channels.Add(Transform.Scale[0], EditorData.MetaData[6], EditorData.ExternalValues[6]);
								Channels.Add(Transform.Scale[1], EditorData.MetaData[7], EditorData.ExternalValues[7]);
								Channels.Add(Transform.Scale[2], EditorData.MetaData[8], EditorData.ExternalValues[8]);
								FloatChannelIndex += 9;
								TotalIndex += 9;

							}
							else
							{
								FloatChannelIndex += 6;
								TotalIndex += 6;

							}
							break;
						}
					}
				}
				default:
					break;
				}
#else
				switch (RigControl.ControlType)
				{
				case ERigControlType::Float:
				{
					for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
					{
						if (RigControl.Name == Scalar.ParameterName)
						{
							ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex,FloatChannelIndex));
							Channels.Add(Scalar.ParameterCurve);
							FloatChannelIndex += 1;
							TotalIndex += 1;
							ControlIndex++;
							break;
						}
					}
					break;
				}
				case ERigControlType::Bool:
				{
					for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
					{
						if (RigControl.Name == Bool.ParameterName)
						{
							ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, BoolChannelIndex));
							Channels.Add(Bool.ParameterCurve);
							BoolChannelIndex += 1;
							TotalIndex += 1;
							ControlIndex++;
							break;
						}
					}
					break;
				}
				case ERigControlType::Integer:
				{
					if (RigControl.ControlEnum)
					{
						for (FEnumParameterNameAndCurve& Enum : GetEnumParameterNamesAndCurves())
						{
							if (RigControl.Name == Enum.ParameterName)
							{
								ControlChannelMap.Add(Enum.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, EnumChannelIndex));
								Channels.Add(Enum.ParameterCurve);
								EnumChannelIndex += 1;
								TotalIndex += 1;
								ControlIndex++;
								break;
							}
						}
					}
					else
					{
						for (FIntegerParameterNameAndCurve& Integer : GetIntegerParameterNamesAndCurves())
						{
							if (RigControl.Name == Integer.ParameterName)
							{
								ControlChannelMap.Add(Integer.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, IntegerChannelIndex));
								Channels.Add(Integer.ParameterCurve);
								IntegerChannelIndex += 1;
								TotalIndex += 1;
								ControlIndex++;
								break;
							}
						}
					}
					break;
				}
				case ERigControlType::Vector2D:
				{
					for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
					{
						if (RigControl.Name == Vector2D.ParameterName)
						{
							ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));
							Channels.Add(Vector2D.XCurve);
							Channels.Add(Vector2D.YCurve);
							FloatChannelIndex += 2;
							TotalIndex += 2;
							ControlIndex++;
							break;
						}
					}
					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
					{
						if (RigControl.Name == Vector.ParameterName)
						{
							ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex, FloatChannelIndex));
							Channels.Add(Vector.XCurve);
							Channels.Add(Vector.YCurve);
							Channels.Add(Vector.ZCurve);
							FloatChannelIndex += 3;
							TotalIndex += 3;
							ControlIndex++;
							break;
						}
					}
					break;
				}
				/*
				for (FColorParameterNameAndCurves& Color : GetColorParameterNamesAndCurves())
				{
					Channels.Add(Color.RedCurve);
					Channels.Add(Color.GreenCurve);
					Channels.Add(Color.BlueCurve);
					Channels.Add(Color.AlphaCurve);
					break
				}
				*/
				case ERigControlType::TransformNoScale:
				case ERigControlType::Transform:
				case ERigControlType::EulerTransform:
				{
					for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
					{
						if (RigControl.Name == Transform.ParameterName)
						{
							ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(ControlIndex, TotalIndex,FloatChannelIndex));
							Channels.Add(Transform.Translation[0]);
							Channels.Add(Transform.Translation[1]);
							Channels.Add(Transform.Translation[2]);

							Channels.Add(Transform.Rotation[0]);
							Channels.Add(Transform.Rotation[1]);
							Channels.Add(Transform.Rotation[2]);

							if (RigControl.ControlType == ERigControlType::Transform || RigControl.ControlType == ERigControlType::EulerTransform)
							{
								Channels.Add(Transform.Scale[0]);
								Channels.Add(Transform.Scale[1]);
								Channels.Add(Transform.Scale[2]);
								FloatChannelIndex += 9;
								TotalIndex += 9;
							}
							else
							{
								FloatChannelIndex += 6;
								TotalIndex += 6;
							}

							ControlIndex++;
							break;
						}
					}
					break;
				}
				}
#endif
			}
		
#if WITH_EDITOR
			FMovieSceneChannelMetaData      MetaData;
			MetaData.SetIdentifiers("Weight", NSLOCTEXT("MovieSceneTransformSection", "Weight", "Weight"));
			MetaData.bEnabled = EnumHasAllFlags(TransformMask.GetChannels(), EMovieSceneTransformChannel::Weight);
			MetaData.SortOrder = TotalIndex++;
			MetaData.bCanCollapseToTrack = false;
			TMovieSceneExternalValue<float> ExVal;
			Channels.Add(Weight, MetaData, ExVal);
#else
			Channels.Add(Weight);

#endif
		}


		ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	}
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetFloatInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetVector2DInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetVectorInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetVector4InterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

FMovieSceneInterrogationKey UMovieSceneControlRigParameterSection::GetTransformInterrogationKey()
{
	static FMovieSceneAnimTypeID TypeID = FMovieSceneAnimTypeID::Unique();
	return TypeID;
}

float UMovieSceneControlRigParameterSection::GetTotalWeightValue(FFrameTime InTime) const
{
	float WeightVal = EvaluateEasing(InTime);
	if (EnumHasAllFlags(TransformMask.GetChannels(), EMovieSceneTransformChannel::Weight))
	{
		float ManualWeightVal = 1.f;
		Weight.Evaluate(InTime, ManualWeightVal);
		WeightVal *= ManualWeightVal;
	}
	return WeightVal;
}

void UMovieSceneControlRigParameterSection::RecreateWithThisControlRig(UControlRig* InControlRig, bool bSetDefault)
{
	SetControlRig(InControlRig);
	/* Don't delete old tracks but eventually show that they aren't associated.. but
	then how to delete?
	BoolParameterNamesAndCurves.Empty();
	EnumParameterNamesAndCurves.Empty();
	IntegerParameterNamesAndCurves.Empty();
	ScalarParameterNamesAndCurves.Empty();
	Vector2DParameterNamesAndCurves.Empty();
	VectorParameterNamesAndCurves.Empty();
	ColorParameterNamesAndCurves.Empty();
	TransformParameterNamesAndCurves.Empty();
	*/
	TArray<bool> OnArray;
	const TArray<FRigControl>& Controls = ControlRig->AvailableControls();
	OnArray.Init(true, Controls.Num());
	SetControlsMask(OnArray);

	TArray<FRigControl> SortedControls;
	ControlRig->GetControlsInOrder(SortedControls);

	for (const FRigControl& RigControl : SortedControls)
	{
		if (!RigControl.bAnimatable)
		{
			continue;
		}

		switch (RigControl.ControlType)
		{
		case ERigControlType::Float:
		{
			TOptional<float> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				DefaultValue = RigControl.Value.Get<float>();
			}
			AddScalarParameter(RigControl.Name, DefaultValue, false);
			break;
		}
		case ERigControlType::Bool:
		{
			TOptional<bool> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				DefaultValue = RigControl.Value.Get<bool>();
			}
			AddBoolParameter(RigControl.Name, DefaultValue, false);
			break;
		}
		case ERigControlType::Integer:
		{
			if (RigControl.ControlEnum)
			{
				TOptional<uint8> DefaultValue;
				if (bSetDefault)
				{
					//or use IntialValue?
					DefaultValue = (uint8)RigControl.Value.Get<int32>();
				}
				AddEnumParameter(RigControl.Name, RigControl.ControlEnum, DefaultValue, false);
			}
			else
			{
				TOptional<int32> DefaultValue;
				if (bSetDefault)
				{
					//or use IntialValue?
					DefaultValue = RigControl.Value.Get<int32>();
				}
				AddIntegerParameter(RigControl.Name, DefaultValue, false);
			}
			break;
		}
		case ERigControlType::Vector2D:
		{
			TOptional<FVector2D> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				DefaultValue = RigControl.Value.Get<FVector2D>();
			}
			AddVector2DParameter(RigControl.Name, DefaultValue, false);
			break;
		}

		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		{
			TOptional<FVector> DefaultValue;
			if (bSetDefault)
			{
				//or use IntialValue?
				DefaultValue = RigControl.Value.Get<FVector>();
			}
			AddVectorParameter(RigControl.Name, DefaultValue, false);
			//mz todo specify rotator special so we can do quat interps
			break;
		}
		case ERigControlType::EulerTransform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::Transform:
		{
			TOptional<FTransform> DefaultValue;
			if (bSetDefault)
			{
				if (RigControl.ControlType == ERigControlType::Transform)
				{
					DefaultValue = RigControl.Value.Get<FTransform>();
				}
				else if (RigControl.ControlType == ERigControlType::EulerTransform)

				{
					FEulerTransform Euler = RigControl.Value.Get<FEulerTransform>();
					DefaultValue = Euler;
				}
				else
				{
					FTransformNoScale NoScale = RigControl.Value.Get<FTransformNoScale>();
					DefaultValue = NoScale;
				}
			}
			AddTransformParameter(RigControl.Name, DefaultValue, false);
			break;
		}

		default:
			break;
		}
	}
	ReconstructChannelProxy(true);
}

void UMovieSceneControlRigParameterSection::SetControlRig(UControlRig* InControlRig)
{
	ControlRig = InControlRig;
	ControlRigClass = ControlRig ? ControlRig->GetClass() : nullptr;
}

#if WITH_EDITOR

void UMovieSceneControlRigParameterSection::RecordControlRigKey(FFrameNumber FrameNumber, bool bSetDefault, bool bAutoKey)
{
	if (ControlRig)
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		TArrayView<FMovieSceneBoolChannel*> BoolChannels = GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
		TArrayView<FMovieSceneIntegerChannel*> IntChannels = GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>();
		TArrayView<FMovieSceneByteChannel*> EnumChannels = GetChannelProxy().GetChannels<FMovieSceneByteChannel>();

		TArray<FRigControl> Controls;
		ControlRig->GetControlsInOrder(Controls);
		
		for (const FRigControl& RigControl : Controls)
		{
			if (!RigControl.bAnimatable)
			{
				continue;
			}
			FChannelMapInfo* pChannelIndex = ControlChannelMap.Find(RigControl.Name);
			if (pChannelIndex == nullptr)
			{
				continue;
			}
			int32 ChannelIndex = pChannelIndex->ChannelIndex;

	
			switch (RigControl.ControlType)
			{
				case ERigControlType::Bool:
				{
					bool Val = RigControl.Value.Get<bool>();
					if (bSetDefault)
					{
						BoolChannels[ChannelIndex]->SetDefault(Val);
					}
					BoolChannels[ChannelIndex]->GetData().AddKey(FrameNumber, Val);
					break;
				}
				case ERigControlType::Integer:
				{
					if (RigControl.ControlEnum)
					{
						uint8 Val = (uint8)RigControl.Value.Get<uint8>();
						if (bSetDefault)
						{
							EnumChannels[ChannelIndex]->SetDefault(Val);
						}
						EnumChannels[ChannelIndex]->GetData().AddKey(FrameNumber, Val);
					}
					else
					{
						int32 Val = RigControl.Value.Get<int32>();
						if (bSetDefault)
						{
							IntChannels[ChannelIndex]->SetDefault(Val);
						}
						IntChannels[ChannelIndex]->GetData().AddKey(FrameNumber, Val);
					}
					break;
				}
				case ERigControlType::Float:
				{
					float Val = RigControl.Value.Get<float>();
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(Val);
					}
					if (bAutoKey)
					{
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val, ERichCurveTangentMode::RCTM_Auto);
					}
					else
					{
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val);
					}
					break;
				}
				case ERigControlType::Vector2D:
				{
					FVector2D Val = RigControl.Value.Get<FVector2D>();
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
					}
					if (bAutoKey)
					{
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.X, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.Y, ERichCurveTangentMode::RCTM_Auto);
					}
					else
					{
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.X);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.Y);
					}

					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					FVector Val = RigControl.Value.Get<FVector>();
					if (RigControl.ControlType == ERigControlType::Rotator &&
						FloatChannels[ChannelIndex]->GetNumKeys() > 0)
					{
						float LastVal = FloatChannels[ChannelIndex]->GetValues()[FloatChannels[ChannelIndex]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, Val.X);
						LastVal = FloatChannels[ChannelIndex + 1]->GetValues()[FloatChannels[ChannelIndex + 1]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, Val.Y);
						LastVal = FloatChannels[ChannelIndex + 2]->GetValues()[FloatChannels[ChannelIndex + 2]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, Val.Z);
					}
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(Val.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(Val.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(Val.Z);
					}
					if (bAutoKey)
					{
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.X, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.Y, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, Val.Z, ERichCurveTangentMode::RCTM_Auto);
					}
					else		
					{
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.X);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.Y);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, Val.Z);
					}
					break;
				}

				case ERigControlType::Transform:
				case ERigControlType::TransformNoScale:
				case ERigControlType::EulerTransform:
				{
					FTransform Val = FTransform::Identity;
					if (RigControl.ControlType == ERigControlType::TransformNoScale)
					{
						FTransformNoScale NoScale = RigControl.Value.Get<FTransformNoScale>();
						Val = NoScale;
					}
					else if (RigControl.ControlType == ERigControlType::EulerTransform)
					{
						FEulerTransform Euler = RigControl.Value.Get < FEulerTransform >();
						Val = Euler.ToFTransform();
					}
					else
					{
						Val = RigControl.Value.Get<FTransform>();
					}
					FVector CurrentVector = Val.GetTranslation();
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);
					}
					if(bAutoKey)
					{
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.X, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.Y, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.Z, ERichCurveTangentMode::RCTM_Auto);
					}
					else
					{
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.X);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.Y);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.Z);
					}
					CurrentVector = Val.GetRotation().Euler();
					if (FloatChannels[ChannelIndex]->GetNumKeys() > 0)
					{
						float LastVal = FloatChannels[ChannelIndex]->GetValues()[FloatChannels[ChannelIndex]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, CurrentVector.X);
						LastVal = FloatChannels[ChannelIndex + 1]->GetValues()[FloatChannels[ChannelIndex + 1]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, CurrentVector.Y);
						LastVal = FloatChannels[ChannelIndex + 2]->GetValues()[FloatChannels[ChannelIndex + 2]->GetNumKeys() - 1].Value;
						FMath::WindRelativeAnglesDegrees(LastVal, CurrentVector.Z);
					}
					if (bSetDefault)
					{
						FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
						FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
						FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);
					}
					if (bAutoKey)
					{
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.X, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.Y, ERichCurveTangentMode::RCTM_Auto);
						FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.Z, ERichCurveTangentMode::RCTM_Auto);
					}
					else
					{
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.X);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.Y);
						FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.Z);
					}

					if (RigControl.ControlType == ERigControlType::Transform || RigControl.ControlType == ERigControlType::EulerTransform)
					{
						CurrentVector = Val.GetScale3D();
						if (bSetDefault)
						{
							FloatChannels[ChannelIndex]->SetDefault(CurrentVector.X);
							FloatChannels[ChannelIndex + 1]->SetDefault(CurrentVector.Y);
							FloatChannels[ChannelIndex + 2]->SetDefault(CurrentVector.Z);
						}
						if (bAutoKey)
						{
							FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.X, ERichCurveTangentMode::RCTM_Auto);
							FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.Y, ERichCurveTangentMode::RCTM_Auto);
							FloatChannels[ChannelIndex++]->AddCubicKey(FrameNumber, CurrentVector.Z, ERichCurveTangentMode::RCTM_Auto);
						}
						else
						{
							FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.X);
							FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.Y);
							FloatChannels[ChannelIndex++]->AddLinearKey(FrameNumber, CurrentVector.Z);
						}

					}
					break;
				}
			}
		}
	}
}

bool UMovieSceneControlRigParameterSection::LoadAnimSequenceIntoThisSection(UAnimSequence* AnimSequence, UMovieScene* MovieScene,USkeleton* Skeleton,
	bool bKeyReduce, float Tolerance, FFrameNumber InStartFrame)
{
	UFKControlRig* AutoRig = Cast<UFKControlRig>(ControlRig);
	if (!AutoRig && !ControlRig->SupportsEvent(FRigUnit_InverseExecution::EventName))
	{
		return false;
	}

	TArrayView<FMovieSceneFloatChannel*> FloatChannels = GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (FloatChannels.Num() <= 0)
	{
		return false;
	}

	FRigBoneHierarchy& SourceBones = ControlRig->GetBoneHierarchy();
	FRigCurveContainer& SourceCurves = ControlRig->GetCurveContainer();
	
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	float Length = AnimSequence->GetPlayLength();
	float FrameRate = AnimSequence->GetFrameRate();

	FFrameNumber StartFrame = UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()) + InStartFrame;
	FFrameNumber EndFrame = TickResolution.AsFrameNumber(Length) + StartFrame;

	Modify();
	if (HasStartFrame() && HasEndFrame())
	{
		StartFrame = GetInclusiveStartFrame();
		EndFrame = StartFrame + EndFrame;
		SetEndFrame(EndFrame);
	}
	ControlRig->Modify();

	int32 NumFrames = Length * FrameRate;
	NumFrames = AnimSequence->GetNumberOfFrames();
	FFrameNumber FrameRateInFrameNumber = TickResolution.AsFrameNumber(1.0f / FrameRate);
	int32 ExtraProgress = bKeyReduce ? FloatChannels.Num() : 0;
	
	FScopedSlowTask Progress(NumFrames + ExtraProgress, LOCTEXT("BakingToControlRig_SlowTask", "Baking To Control Rig..."));	
	Progress.MakeDialog(true);

	//Make sure we are reset and run setup event  before evaluating
	TArray<FRigElementKey> ControlsToReset = ControlRig->GetHierarchy()->GetAllItems();
	for (const FRigElementKey& ControlToReset : ControlsToReset)
	{
		if (ControlToReset.Type == ERigElementType::Control)
		{
			FRigControl* Control = ControlRig->FindControl(ControlToReset.Name);
			if (Control && !Control->bIsTransientControl)
			{
				FTransform Transform = ControlRig->GetControlHierarchy().GetLocalTransform(ControlToReset.Name, ERigControlValueType::Initial);
				ControlRig->GetControlHierarchy().SetLocalTransform(ControlToReset.Name, Transform);
			}
		}
	}
	SourceBones.ResetTransforms();
	SourceCurves.ResetValues();
	ControlRig->Execute(EControlRigState::Update, TEXT("Setup"));

	for (int32 Index = 0; Index < NumFrames; ++Index)
	{
		float SequenceSecond = AnimSequence->GetTimeAtFrame(Index);
		FFrameNumber FrameNumber = StartFrame + (FrameRateInFrameNumber * Index);

		for (FFloatCurve& Curve : AnimSequence->RawCurveData.FloatCurves)
		{
			float Val = Curve.FloatCurve.Eval(SequenceSecond);
			SourceCurves.SetValue(Curve.Name.DisplayName,Val);
		}
		for (int32 TrackIndex = 0; TrackIndex < AnimSequence->GetRawAnimationData().Num(); ++TrackIndex)
		{
			// verify if this bone exists in skeleton
			int32 BoneTreeIndex = AnimSequence->GetSkeletonIndexFromRawDataTrackIndex(TrackIndex);
			if (BoneTreeIndex != INDEX_NONE)
			{
				FName BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(BoneTreeIndex);
				FTransform BoneTransform;
				AnimSequence->GetRawAnimationTrack(TrackIndex);
				AnimSequence->ExtractBoneTransform(AnimSequence->GetRawAnimationTrack(TrackIndex), BoneTransform, SequenceSecond);
				SourceBones.SetLocalTransform(BoneName, BoneTransform);
			}
		}
		if (Index == 0)
		{
			//to make sure the first frame looks good we need to do this first. UE-100069
			ControlRig->Execute(EControlRigState::Update, FRigUnit_InverseExecution::EventName);
		}
		ControlRig->Execute(EControlRigState::Update, FRigUnit_InverseExecution::EventName);

		RecordControlRigKey(FrameNumber, true, bKeyReduce);
		Progress.EnterProgressFrame(1);
		if (Progress.ShouldCancel())
		{
			return false;
		}
	}

	if (bKeyReduce)
	{
		FKeyDataOptimizationParams Params;
		Params.bAutoSetInterpolation = true;
		Params.Tolerance = Tolerance;
		for (FMovieSceneFloatChannel* Channel : FloatChannels)
		{
			Channel->Optimize(Params); //should also auto tangent
			Progress.EnterProgressFrame(1);
			if (Progress.ShouldCancel())
			{
				return false;
			}
		}
	}
	
	return true;
}

#endif


void UMovieSceneControlRigParameterSection::AddEnumParameterKey(FName InParameterName, FFrameNumber InTime, uint8 InValue)
{
	FMovieSceneByteChannel* ExistingChannel = nullptr;
	for (FEnumParameterNameAndCurve& EnumParameterNameAndCurve : EnumParameterNamesAndCurves)
	{
		if (EnumParameterNameAndCurve.ParameterName == InParameterName)
		{
			ExistingChannel = &EnumParameterNameAndCurve.ParameterCurve;
			break;
		}
	}
	if (ExistingChannel == nullptr)
	{
		const int32 NewIndex = EnumParameterNamesAndCurves.Add(FEnumParameterNameAndCurve(InParameterName));
		ExistingChannel = &EnumParameterNamesAndCurves[NewIndex].ParameterCurve;

		ReconstructChannelProxy(true);
	}

	ExistingChannel->GetData().UpdateOrAddKey(InTime, InValue);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}


void UMovieSceneControlRigParameterSection::AddIntegerParameterKey(FName InParameterName, FFrameNumber InTime, int32 InValue)
{
	FMovieSceneIntegerChannel* ExistingChannel = nullptr;
	for (FIntegerParameterNameAndCurve& IntegerParameterNameAndCurve : IntegerParameterNamesAndCurves)
	{
		if (IntegerParameterNameAndCurve.ParameterName == InParameterName)
		{
			ExistingChannel = &IntegerParameterNameAndCurve.ParameterCurve;
			break;
		}
	}
	if (ExistingChannel == nullptr)
	{
		const int32 NewIndex = IntegerParameterNamesAndCurves.Add(FIntegerParameterNameAndCurve(InParameterName));
		ExistingChannel = &IntegerParameterNamesAndCurves[NewIndex].ParameterCurve;

		ReconstructChannelProxy(true);
	}

	ExistingChannel->GetData().UpdateOrAddKey(InTime, InValue);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

bool UMovieSceneControlRigParameterSection::RemoveEnumParameter(FName InParameterName)
{
	for (int32 i = 0; i < EnumParameterNamesAndCurves.Num(); i++)
	{
		if (EnumParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			EnumParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

bool UMovieSceneControlRigParameterSection::RemoveIntegerParameter(FName InParameterName)
{
	for (int32 i = 0; i < IntegerParameterNamesAndCurves.Num(); i++)
	{
		if (IntegerParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			IntegerParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}


TArray<FEnumParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetEnumParameterNamesAndCurves()
{
	return EnumParameterNamesAndCurves;
}

const TArray<FEnumParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetEnumParameterNamesAndCurves() const
{
	return EnumParameterNamesAndCurves;
}

TArray<FIntegerParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetIntegerParameterNamesAndCurves()
{
	return IntegerParameterNamesAndCurves;
}

const TArray<FIntegerParameterNameAndCurve>& UMovieSceneControlRigParameterSection::GetIntegerParameterNamesAndCurves() const
{
	return IntegerParameterNamesAndCurves;
}

void UMovieSceneControlRigParameterSection::ClearAllParameters()
{
	BoolParameterNamesAndCurves.SetNum(0);
	ScalarParameterNamesAndCurves.SetNum(0);
	Vector2DParameterNamesAndCurves.SetNum(0);
	VectorParameterNamesAndCurves.SetNum(0);
	ColorParameterNamesAndCurves.SetNum(0);
	TransformParameterNamesAndCurves.SetNum(0);
	EnumParameterNamesAndCurves.SetNum(0);
	IntegerParameterNamesAndCurves.SetNum(0);
}
FEnumParameterNameAndCurve::FEnumParameterNameAndCurve(FName InParameterName)
{
	ParameterName = InParameterName;
}

FIntegerParameterNameAndCurve::FIntegerParameterNameAndCurve(FName InParameterName)
{
	ParameterName = InParameterName;
}

#undef LOCTEXT_NAMESPACE 
