// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/ParameterSection.h"
#include "ISectionLayoutBuilder.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneParameterSection.h"
#include "SequencerSectionPainter.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "CommonMovieSceneTools.h"
#include "MovieSceneSectionHelpers.h"
#include "IKeyArea.h"
#include "EditorStyleSet.h"
#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "ParameterSection"

FReply FParameterSection::OnKeyDoubleClicked(const TArray<FKeyHandle> &KeyHandles )
{
	UMovieSceneParameterSection* ParameterSection = Cast<UMovieSceneParameterSection>( WeakSection.Get() );
	if (!ParameterSection)
	{
		return FReply::Handled();
	}

	for (FColorParameterNameAndCurves& NameAndCurve : ParameterSection->GetColorParameterNamesAndCurves())
	{
		FMovieSceneKeyColorPicker KeyColorPicker(ParameterSection, &NameAndCurve.RedCurve, &NameAndCurve.GreenCurve, &NameAndCurve.BlueCurve, &NameAndCurve.AlphaCurve, KeyHandles);
	}

	return FReply::Handled();
}

int32 FParameterSection::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	int32 LayerId = FSequencerSection::OnPaintSection(Painter);

	for (FKeyAreaElement KeyAreaElement : Painter.KeyAreaElements)
	{
		FMovieSceneFloatChannel* RChannel = nullptr;
		FMovieSceneFloatChannel* GChannel = nullptr;
		FMovieSceneFloatChannel* BChannel = nullptr;
		FMovieSceneFloatChannel* AChannel = nullptr;
		
		for (TSharedRef<IKeyArea> KeyArea : KeyAreaElement.KeyAreas)
		{
			const FMovieSceneChannelMetaData* MetaData = KeyArea->GetChannel().GetMetaData();
			if (MetaData)
			{
				if (MetaData->DisplayText.EqualTo(FCommonChannelData::ChannelR))
				{
					RChannel = static_cast<FMovieSceneFloatChannel*>(KeyArea->ResolveChannel());
				}
				if (MetaData->DisplayText.EqualTo(FCommonChannelData::ChannelG))
				{
					GChannel = static_cast<FMovieSceneFloatChannel*>(KeyArea->ResolveChannel());
				}
				if (MetaData->DisplayText.EqualTo(FCommonChannelData::ChannelB))
				{
					BChannel = static_cast<FMovieSceneFloatChannel*>(KeyArea->ResolveChannel());
				}
				if (MetaData->DisplayText.EqualTo(FCommonChannelData::ChannelA))
				{
					AChannel = static_cast<FMovieSceneFloatChannel*>(KeyArea->ResolveChannel());
				}
			}
		}

		if (RChannel && GChannel && BChannel && AChannel)
		{
			const ESlateDrawEffect DrawEffects = Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

			const FTimeToPixel& TimeConverter = Painter.GetTimeConverter();

			FGeometry KeyAreaGeometry = KeyAreaElement.KeyAreaGeometry;

			const float StartTime       = TimeConverter.PixelToSeconds(0.f);
			const float EndTime         = TimeConverter.PixelToSeconds(KeyAreaGeometry.GetLocalSize().X);
			const float SectionDuration = EndTime - StartTime;

			const float KeyAreaHeight = KeyAreaElement.Type == FKeyAreaElement::Group ? KeyAreaGeometry.Size.Y / 3.f - 4.0f : KeyAreaGeometry.Size.Y - 3.0f;

			FVector2D GradientSize = FVector2D( KeyAreaGeometry.Size.X - 2.f, KeyAreaHeight );
			if ( GradientSize.X >= 1.f )
			{
				FPaintGeometry PaintGeometry = KeyAreaGeometry.ToPaintGeometry( FVector2D( 1.f, 1.f ), GradientSize );

				// If we are showing a background pattern and the colors is transparent, draw a checker pattern
				FSlateDrawElement::MakeBox(
					Painter.DrawElements,
					LayerId,
					PaintGeometry,
					FEditorStyle::GetBrush( "Checker" ),
					DrawEffects);

				FLinearColor DefaultColor = FLinearColor::Black;
				DefaultColor.R = RChannel->GetDefault().IsSet() ? RChannel->GetDefault().GetValue() : 0.f;
				DefaultColor.G = GChannel->GetDefault().IsSet() ? GChannel->GetDefault().GetValue() : 0.f;
				DefaultColor.B = BChannel->GetDefault().IsSet() ? BChannel->GetDefault().GetValue() : 0.f;
				DefaultColor.A = AChannel->GetDefault().IsSet() ? AChannel->GetDefault().GetValue() : 0.f;
					
				TArray<FMovieSceneFloatChannel*> ColorChannels;
				ColorChannels.Add(RChannel);
				ColorChannels.Add(GChannel);
				ColorChannels.Add(BChannel);
				ColorChannels.Add(AChannel);

				TArray< TTuple<float, FLinearColor> > ColorKeys;
				MovieSceneSectionHelpers::ConsolidateColorCurves( ColorKeys, DefaultColor, ColorChannels, TimeConverter );

				TArray<FSlateGradientStop> GradientStops;

				for (const TTuple<float, FLinearColor>& ColorStop : ColorKeys)
				{
					const float Time = ColorStop.Get<0>();

					// HACK: The color is converted to SRgb and then reinterpreted as linear here because gradients are converted to FColor
					// without the SRgb conversion before being passed to the renderer for some reason.
					const FLinearColor Color = ColorStop.Get<1>().ToFColor( true ).ReinterpretAsLinear();

					float TimeFraction = (Time - StartTime) / SectionDuration;
					GradientStops.Add( FSlateGradientStop( FVector2D( TimeFraction * KeyAreaGeometry.Size.X, 0 ), Color ) );
				}

				if ( GradientStops.Num() > 0 )
				{
					FSlateDrawElement::MakeGradient(
						Painter.DrawElements,
						Painter.LayerId + 1,
						PaintGeometry,
						GradientStops,
						Orient_Vertical,
						DrawEffects
						);
				}
			}
		}
	}

	return LayerId;
}

bool FParameterSection::RequestDeleteCategory( const TArray<FName>& CategoryNamePath )
{
	const FScopedTransaction Transaction( LOCTEXT( "DeleteVectorOrColorParameter", "Delete vector or color parameter" ) );
	UMovieSceneParameterSection* ParameterSection = Cast<UMovieSceneParameterSection>( WeakSection.Get() );
	if( ParameterSection->Modify() )
	{
		bool bVectorParameterDeleted = ParameterSection->RemoveVectorParameter( CategoryNamePath[0] );
		bool bColorParameterDeleted = ParameterSection->RemoveColorParameter( CategoryNamePath[0] );
		return bVectorParameterDeleted || bColorParameterDeleted;
	}
	return false;
}


bool FParameterSection::RequestDeleteKeyArea( const TArray<FName>& KeyAreaNamePath )
{
	// Only handle paths with a single name, in all other cases the user is deleting a component of a vector parameter.
	if ( KeyAreaNamePath.Num() == 1)
	{
		const FScopedTransaction Transaction( LOCTEXT( "DeleteScalarParameter", "Delete scalar parameter" ) );
		UMovieSceneParameterSection* ParameterSection = Cast<UMovieSceneParameterSection>( WeakSection.Get() );
		if (ParameterSection->TryModify())
		{
			return ParameterSection->RemoveScalarParameter( KeyAreaNamePath[0] );
		}
	}
	return false;
}


#undef LOCTEXT_NAMESPACE
