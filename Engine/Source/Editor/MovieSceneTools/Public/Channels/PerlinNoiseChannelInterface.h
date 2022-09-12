// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISequencerChannelInterface.h"
#include "Channels/IMovieSceneChannelOverrideProvider.h"
#include "Channels/MovieSceneSectionChannelOverrideRegistry.h"
#include "CurveKeyEditors/SequencerKeyEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneSection.h"
#include "PropertyEditorModule.h"
#include "TimeToPixel.h"
#include "ToolMenus.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Text/STextBlock.h"

template<typename ChannelType, typename NumericType>
class SNumericTextBlockKeyEditor : public SCompoundWidget
{
	using FKeyEditor = TSequencerKeyEditor<ChannelType, NumericType>;
	
	FKeyEditor KeyEditor;
	TSharedPtr<INumericTypeInterface<NumericType>> Interface;

	SLATE_BEGIN_ARGS(SNumericTextBlockKeyEditor){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FKeyEditor& InKeyEditor)
	{
		KeyEditor = InKeyEditor;
		Interface = MakeShareable(new TDefaultNumericTypeInterface<NumericType>);

		ChildSlot
		[
			SNew(STextBlock)
			.Text(this, &SNumericTextBlockKeyEditor<ChannelType, NumericType>::GetValueAsText)
		];
	}

	FText GetValueAsText() const
	{
		const FString ValueString = Interface->ToString(KeyEditor.GetCurrentValue());
		return FText::FromString(ValueString);
	}
};

template<typename ChannelContainerType>
struct TPerlinNoiseChannelSectionMenuExtension : TSharedFromThis<TPerlinNoiseChannelSectionMenuExtension<ChannelContainerType>>
{
	using ChannelType = typename ChannelContainerType::ChannelType;

	TPerlinNoiseChannelSectionMenuExtension(TArrayView<UMovieSceneSection* const> InSections)
		: Sections(InSections)
	{}

	void ExtendMenu(FMenuBuilder& MenuBuilder)
	{
		TSharedRef<TPerlinNoiseChannelSectionMenuExtension<ChannelContainerType>> SharedThis = this->AsShared();

		MenuBuilder.AddSubMenu(
			NSLOCTEXT("PerlinNoiseChannelInterface", "PerlinNoiseChannelsMenu", "Perlin Noise Channels"),
			NSLOCTEXT("PerlinNoiseChannelInterface", "PerlinNoiseChannelsMenuToolTip", "Edit parameters for Perlin Noise channels"),
			FNewMenuDelegate::CreateLambda([SharedThis](FMenuBuilder& MenuBuilder) { SharedThis->BuildChannelOverrideParametersMenu(MenuBuilder); })
		);
	}

private:

	void BuildChannelOverrideParametersMenu(FMenuBuilder& MenuBuilder)
	{
		FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowScrollBar = false;

		TSharedRef<IDetailsView> DetailsView = EditModule.CreateDetailView(DetailsViewArgs);

		TArray<UObject*> Objects;
		for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sections)
		{
			UMovieSceneSection* Section = WeakSection.Get();
			if (!Section)
			{
				continue;
			}

			IMovieSceneChannelOverrideProvider* OverrideProvider = Cast<IMovieSceneChannelOverrideProvider>(Section);
			if (!OverrideProvider)
			{
				continue;
			}
			
			UMovieSceneSectionChannelOverrideRegistry* OverrideRegistry = OverrideProvider->GetChannelOverrideRegistry(false);
			if (!OverrideRegistry)
			{
				continue;
			}
	
			TArray<ChannelContainerType*> ChannelContainers;
			OverrideRegistry->GetChannels<ChannelContainerType>(ChannelContainers);
			Objects.Append(ChannelContainers);
		}

		DetailsView->SetObjects(Objects, true);

		MenuBuilder.AddWidget(DetailsView, FText(), true, false);
	}


private:

	TArray<TWeakObjectPtr<UMovieSceneSection>> Sections;
};

template<typename ChannelContainerType>
struct TPerlinNoiseChannelInterface : ISequencerChannelInterface
{
	using ChannelType = typename ChannelContainerType::ChannelType;

	virtual FKeyHandle AddOrUpdateKey_Raw(FMovieSceneChannel* Channel, UMovieSceneSection* SectionToKey, const void* ExtendedEditorData, FFrameNumber InTime, ISequencer& InSequencer, const FGuid& ObjectBindingID, FTrackInstancePropertyBindings* PropertyBindings) const override
	{
		return FKeyHandle::Invalid();
	}

	virtual void CopyKeys_Raw(FMovieSceneChannel* Channel, const UMovieSceneSection* Section, FName KeyAreaName, FMovieSceneClipboardBuilder& ClipboardBuilder, TArrayView<const FKeyHandle> KeyMask) const override
	{
	}

	virtual void PasteKeys_Raw(FMovieSceneChannel* Channel, UMovieSceneSection* Section, const FMovieSceneClipboardKeyTrack& KeyTrack, const FMovieSceneClipboardEnvironment& SrcEnvironment, const FSequencerPasteEnvironment& DstEnvironment, TArray<FKeyHandle>& OutPastedKeys) const override
	{
	}

	virtual TSharedPtr<FStructOnScope> GetKeyStruct_Raw(FMovieSceneChannelHandle Channel, FKeyHandle KeyHandle) const override
	{
		return nullptr;
	}

	virtual bool CanCreateKeyEditor_Raw(const FMovieSceneChannel* Channel) const override
	{
		return true;
	}

	virtual TSharedRef<SWidget> CreateKeyEditor_Raw(const FMovieSceneChannelHandle& Channel, UMovieSceneSection* Section, const FGuid& InObjectBindingID, TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings, TWeakPtr<ISequencer> Sequencer) const override
	{
		const TMovieSceneExternalValue<typename ChannelType::CurveValueType>* ExternalValue = Channel.Cast<ChannelType>().GetExtendedEditorData();
		if (!ExternalValue)
		{
			return SNullWidget::NullWidget;
		}

		TSequencerKeyEditor<ChannelType, typename ChannelType::CurveValueType> KeyEditor(
			InObjectBindingID, Channel.Cast<ChannelType>(),
			Section, Sequencer, PropertyBindings, ExternalValue->OnGetExternalValue
			);

		using KeyEditorType = SNumericTextBlockKeyEditor<ChannelType, typename ChannelType::CurveValueType>;
		return SNew(KeyEditorType, KeyEditor);
	}

	virtual void ExtendKeyMenu_Raw(FMenuBuilder& MenuBuilder, TSharedPtr<FExtender> MenuExtender, TArrayView<const FExtendKeyMenuParams> Parameters, TWeakPtr<ISequencer> InSequencer) const override
	{
	}

	virtual void ExtendSectionMenu_Raw(FMenuBuilder& MenuBuilder, TSharedPtr<FExtender> MenuExtender, TArrayView<const FMovieSceneChannelHandle> Channels, TArrayView<UMovieSceneSection* const> Sections, TWeakPtr<ISequencer> InSequencer) const override
	{
		using MenuExtenderType = TPerlinNoiseChannelSectionMenuExtension<ChannelContainerType>;
		TSharedRef<MenuExtenderType> Extension = MakeShared<MenuExtenderType>(Sections);

		MenuExtender->AddMenuExtension("SequencerChannels", EExtensionHook::First, nullptr, FMenuExtensionDelegate::CreateLambda([Extension](FMenuBuilder& MenuBuilder) { Extension->ExtendMenu(MenuBuilder); }));
	}

	virtual void DrawKeys_Raw(FMovieSceneChannel* Channel, TArrayView<const FKeyHandle> InKeyHandles, const UMovieSceneSection* InOwner, TArrayView<FKeyDrawParams> OutKeyDrawParams) const override
	{
	}

	virtual bool ShouldShowCurve_Raw(const FMovieSceneChannel* Channel, UMovieSceneSection* InSection) const override
	{
		return true;
	}

	virtual bool SupportsCurveEditorModels_Raw(const FMovieSceneChannelHandle& InChannel) const override
	{
		return false;
	}

	virtual TUniquePtr<FCurveModel> CreateCurveEditorModel_Raw(const FMovieSceneChannelHandle& Channel, UMovieSceneSection* OwningSection, TSharedRef<ISequencer> InSequencer) const override
	{
		return nullptr;
	}

	virtual int32 DrawExtra_Raw(FMovieSceneChannel* InChannel, const UMovieSceneSection* InOwner, const FSequencerChannelPaintArgs& PaintArgs, int32 LayerId) const override
	{
		using namespace UE::Sequencer;

		FLinearColor FillColor(1, 1, 1, 0.334f);
		ChannelType* TypedChannel = static_cast<ChannelType*>(InChannel);

		TArray<FVector2D> CurvePoints;
		CurvePoints.Reserve(PaintArgs.Geometry.Size.X / 2.0);

		const double Amplitude = TypedChannel->GetParams().Amplitude;
		const double YOffset = PaintArgs.Geometry.Size.Y / 2.0;
		const double YScale = (Amplitude != 0) ? (PaintArgs.Geometry.Size.Y / Amplitude / 2.0) : 1;

		for (double X = 0; X < PaintArgs.Geometry.Size.X; X += 2.f)
		{
			double Seconds = PaintArgs.TimeToPixel.PixelToSeconds(X);
			double Y = (double)TypedChannel->Evaluate(Seconds);
			CurvePoints.Add(FVector2D(X, (Y * YScale) + YOffset));
		}

		FSlateDrawElement::MakeLines(
				PaintArgs.DrawElements,
				LayerId,
				PaintArgs.Geometry.ToPaintGeometry(),
				CurvePoints,
				ESlateDrawEffect::PreMultipliedAlpha,
				FillColor,
				true);

		return LayerId + 1;
	}

	virtual TSharedPtr<UE::Sequencer::FChannelModel> CreateChannelModel_Raw(const FMovieSceneChannelHandle& InChannelHandle, FName InChannelName) const override
	{
		return nullptr;
	}
	virtual TSharedPtr<UE::Sequencer::STrackAreaLaneView> CreateChannelView_Raw(const FMovieSceneChannelHandle& InChannelHandle, TWeakPtr<UE::Sequencer::FChannelModel> InWeakModel, const UE::Sequencer::FCreateTrackLaneViewParams& Parameters) const override
	{
		return nullptr;
	}
};

