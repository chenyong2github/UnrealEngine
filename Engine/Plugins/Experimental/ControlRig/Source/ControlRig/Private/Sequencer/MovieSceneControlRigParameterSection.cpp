// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Animation/AnimSequence.h"
#include "Logging/MessageLog.h"
#include "MovieScene.h"
#include "Sequencer/ControlRigSequence.h"
#include "Sequencer/ControlRigBindingTemplate.h"
#include "Sequencer/MovieSceneControlRigInstanceData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/ControlRigSortedControls.h"


#define LOCTEXT_NAMESPACE "MovieSceneControlParameterRigSection"

#if WITH_EDITOR

struct FParameterFloatChannelEditorData
{
	FParameterFloatChannelEditorData(UControlRig *InControlRig, const FName& InName, bool bEnabledOverride, const FText& GroupName, int SortStartIndex)
	{
		ControlRig = InControlRig;
		ParameterName = InName;
		FString NameAsString = InName.ToString();
		FText Group = FText::FromString(NameAsString);
		{
			MetaData.SetIdentifiers(InName, FText::FromName(InName),FText());
			MetaData.Group = Group;
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
			FMovieSceneEvaluationTrack EvalTrack = Track->GenerateTrackTemplate();
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
		FText Group = FText::FromString(NameAsString);

		{
			TotalName += ".X";
			MetaData[0].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelX);
			TotalName = NameAsString;
			MetaData[0].Group = Group;
			MetaData[0].bEnabled = bEnabledOverride;
			MetaData[0].SortOrder = SortStartIndex++;
			MetaData[0].bCanCollapseToTrack = false;
		}
		{
			TotalName += ".Y";
			MetaData[1].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelY);
			TotalName = NameAsString;
			MetaData[1].Group = Group;
			MetaData[1].bEnabled = bEnabledOverride;
			MetaData[1].SortOrder = SortStartIndex++;
			MetaData[1].bCanCollapseToTrack = false;
		}
		{
			TotalName += ".Z";
			MetaData[2].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelZ);
			TotalName = NameAsString;
			MetaData[2].Group = Group;
			MetaData[2].bEnabled = bEnabledOverride;
			MetaData[2].SortOrder = SortStartIndex++;
			MetaData[2].bCanCollapseToTrack = false;
		}
		{
			TotalName += ".W";
			MetaData[3].SetIdentifiers(FName(*TotalName), FCommonChannelData::ChannelW);
			TotalName = NameAsString;
			MetaData[3].Group = Group;
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
			FMovieSceneEvaluationTrack EvalTrack = Track->GenerateTrackTemplate();
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
				FTransform Transform = RigControl->Value.Get<FTransform>();
				return Transform.GetScale3D();
			}
		}
		return TOptional<FVector>();
	}

	static void GetValueAndWeight(FName ParameterName, UObject* Object, UMovieSceneSection*  SectionToKey, int32 Index, FFrameNumber KeyTime, FFrameRate TickResolution, FMovieSceneRootEvaluationTemplateInstance& RootTemplate,
		float& OutValue, float& OutWeight)
	{
		UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();
		FMovieSceneEvaluationTrack EvalTrack = Track->GenerateTrackTemplate();
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

void UMovieSceneControlRigParameterSection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

}

void UMovieSceneControlRigParameterSection::PostEditImport()
{
	Super::PostEditImport();
	if (UMovieSceneControlRigParameterTrack* Track = Cast< UMovieSceneControlRigParameterTrack>(GetOuter()))
	{
		ControlRig = Track->GetControlRig();
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
			FControlRigSortedControls::GetControlsInOrder(ControlRig, SortedControls);
			int32 Index = 0;
			int32 ChannelIndex = 0;
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
							ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							bool bEnabled = ControlsMask[Index++];
							FString ParameterString = Scalar.ParameterName.ToString();
							FText Group = FText::FromString(ParameterString);
							FParameterFloatChannelEditorData EditorData(ControlRig, Scalar.ParameterName, bEnabled, Group, ChannelIndex);
							Channels.Add(Scalar.ParameterCurve, EditorData.MetaData, EditorData.ExternalValues);
							ChannelIndex += 1;
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
							ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							bool bEnabled = ControlsMask[Index++];
							FMovieSceneChannelMetaData MetaData(Bool.ParameterName, FText::FromName(Bool.ParameterName), FText::FromName(Bool.ParameterName), bEnabled);
							MetaData.SortOrder = ChannelIndex++;
							// Prevent single channels from collapsing to the track node
							MetaData.bCanCollapseToTrack = false;
							Channels.Add(Bool.ParameterCurve, MetaData, TMovieSceneExternalValue<bool>());
							break;
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
							ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							bool bEnabled = ControlsMask[Index++];
							FString ParameterString = Vector2D.ParameterName.ToString();
							FText Group = FText::FromString(ParameterString);
							FParameterVectorChannelEditorData EditorData(ControlRig, Vector2D.ParameterName, bEnabled, Group, ChannelIndex, 2);
							Channels.Add(Vector2D.XCurve, EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Vector2D.YCurve, EditorData.MetaData[1], EditorData.ExternalValues[1]);
							ChannelIndex += 2;
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
							ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							bool bEnabled = ControlsMask[Index++];
							FString ParameterString = Vector.ParameterName.ToString();
							FText Group = FText::FromString(ParameterString);
							FParameterVectorChannelEditorData EditorData(ControlRig, Vector.ParameterName, bEnabled, Group, ChannelIndex, 3);
							Channels.Add(Vector.XCurve, EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Vector.YCurve, EditorData.MetaData[1], EditorData.ExternalValues[1]);
							Channels.Add(Vector.ZCurve, EditorData.MetaData[2], EditorData.ExternalValues[2]);
							ChannelIndex += 3;
							break;
						}
					}
					break;
				}
				/*No colors or Vector4's yet
				case ERigControlType::Color:
				{
					for (FColorParameterNameAndCurves& Color : GetColorParameterNamesAndCurves())
					{
						if(RigControl.Name == Color.ParameterName)
						{
							ControlChannelMap.Add(Color.ParameterName, ChannelIndex);

							bool bEnabled = ControlsMask[Index++];
							FString ParameterString = Color.ParameterName.ToString();
							FText Group = FText::FromString(ParameterString);

							FMovieSceneChannelMetaData MetaData_R(*(ParameterString + TEXT("R")), FCommonChannelData::ChannelR, Group, bEnabled);
							MetaData_R.SortOrder = ChannelIndex++;
							MetaData_R.Color = FCommonChannelData::RedChannelColor;

							FMovieSceneChannelMetaData MetaData_G(*(ParameterString + TEXT("G")), FCommonChannelData::ChannelG, Group, bEnabled);
							MetaData_G.SortOrder = ChannelIndex++;
							MetaData_G.Color = FCommonChannelData::GreenChannelColor;

							FMovieSceneChannelMetaData MetaData_B(*(ParameterString + TEXT("B")), FCommonChannelData::ChannelB, Group, bEnabled);
							MetaData_B.SortOrder = ChannelIndex++;
							MetaData_B.Color = FCommonChannelData::BlueChannelColor;

							FMovieSceneChannelMetaData MetaData_A(*(ParameterString + TEXT("A")), FCommonChannelData::ChannelA, Group, bEnabled);
							MetaData_A.SortOrder = ChannelIndex++;

							Channels.Add(Color.RedCurve, MetaData_R, TMovieSceneExternalValue<float>());
							Channels.Add(Color.GreenCurve, MetaData_G, TMovieSceneExternalValue<float>());
							Channels.Add(Color.BlueCurve, MetaData_B, TMovieSceneExternalValue<float>());
							Channels.Add(Color.AlphaCurve, MetaData_A, TMovieSceneExternalValue<float>());
							break;
						}

					}
					break;
				}
				*/
				case ERigControlType::TransformNoScale:
				case ERigControlType::Transform:
				{
					for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
					{
						if (RigControl.Name == Transform.ParameterName)
						{
							ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							bool bEnabled = ControlsMask[Index++];
							FString ParameterString = Transform.ParameterName.ToString();
							FText Group = FText::FromString(ParameterString);

							FParameterTransformChannelEditorData EditorData(ControlRig, Transform.ParameterName, bEnabled, TransformMask.GetChannels(), Group, ChannelIndex);

							Channels.Add(Transform.Translation[0], EditorData.MetaData[0], EditorData.ExternalValues[0]);
							Channels.Add(Transform.Translation[1], EditorData.MetaData[1], EditorData.ExternalValues[1]);
							Channels.Add(Transform.Translation[2], EditorData.MetaData[2], EditorData.ExternalValues[2]);

							Channels.Add(Transform.Rotation[0], EditorData.MetaData[3], EditorData.ExternalValues[3]);
							Channels.Add(Transform.Rotation[1], EditorData.MetaData[4], EditorData.ExternalValues[4]);
							Channels.Add(Transform.Rotation[2], EditorData.MetaData[5], EditorData.ExternalValues[5]);

							if (RigControl.ControlType == ERigControlType::Transform)
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
								ChannelIndex += 9;
							}
							else
							{
								ChannelIndex += 6;
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
							ControlChannelMap.Add(Scalar.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							Channels.Add(Scalar.ParameterCurve);
							ChannelIndex += 1;
							Index++;
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
							ControlChannelMap.Add(Bool.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							Channels.Add(Bool.ParameterCurve);
							ChannelIndex += 1;
							Index++;
							break;
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
							ControlChannelMap.Add(Vector2D.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							Channels.Add(Vector2D.XCurve);
							Channels.Add(Vector2D.YCurve);
							ChannelIndex += 2;
							Index++;
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
							ControlChannelMap.Add(Vector.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							Channels.Add(Vector.XCurve);
							Channels.Add(Vector.YCurve);
							Channels.Add(Vector.ZCurve);
							ChannelIndex += 3;
							Index++;
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
				{
					for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
					{
						if (RigControl.Name == Transform.ParameterName)
						{
							ControlChannelMap.Add(Transform.ParameterName, FChannelMapInfo(Index, ChannelIndex));
							Channels.Add(Transform.Translation[0]);
							Channels.Add(Transform.Translation[1]);
							Channels.Add(Transform.Translation[2]);

							Channels.Add(Transform.Rotation[0]);
							Channels.Add(Transform.Rotation[1]);
							Channels.Add(Transform.Rotation[2]);

							if (RigControl.ControlType == ERigControlType::Transform)
							{
								Channels.Add(Transform.Scale[0]);
								Channels.Add(Transform.Scale[1]);
								Channels.Add(Transform.Scale[2]);
								ChannelIndex += 9;
							}
							else
							{
								ChannelIndex += 6;
							}

							Index++;
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
			MetaData.SortOrder = ChannelIndex++;
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

#undef LOCTEXT_NAMESPACE 
