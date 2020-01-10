// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/TemplateSequenceTrackEditor.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "CommonMovieSceneTools.h"
#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/TemplateSequenceSection.h"
#include "SequencerSectionPainter.h"
#include "TemplateSequence.h"
#include "Tracks/TemplateSequenceTrack.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FTemplateSequenceTrackEditor"

FTemplateSequenceTrackEditor::FTemplateSequenceTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

TSharedRef<ISequencerTrackEditor> FTemplateSequenceTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FTemplateSequenceTrackEditor(InSequencer));
}

bool FTemplateSequenceTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UTemplateSequenceTrack::StaticClass();
}

void FTemplateSequenceTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddTemplateSequence", "Template Sequence"), NSLOCTEXT("Sequencer", "AddTemplateSequenceTooltip", "Adds a track that can play a template sequence asset using the parent binding."),
		FNewMenuDelegate::CreateRaw(this, &FTemplateSequenceTrackEditor::AddTemplateSequenceSubMenu, ObjectBindings)
	);
}

TSharedRef<ISequencerSection> FTemplateSequenceTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check( SupportsType( SectionObject.GetOuter()->GetClass() ) );
	
	return MakeShareable(new FTemplateSequenceSection(*CastChecked<UTemplateSequenceSection>(&SectionObject)));
}

void FTemplateSequenceTrackEditor::AddTemplateSequenceSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FTemplateSequenceTrackEditor::OnTemplateSequenceAssetSelected, ObjectBindings);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this, &FTemplateSequenceTrackEditor::OnTemplateSequenceAssetEnterPressed, ObjectBindings);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.Filter.ClassNames.Add(UTemplateSequence::StaticClass()->GetFName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}

void FTemplateSequenceTrackEditor::OnTemplateSequenceAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings)
{
	FSlateApplication::Get().DismissAllMenus();

	UTemplateSequence* SelectedSequence = Cast<UTemplateSequence>(AssetData.GetAsset());
	if (SelectedSequence != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddTemplateSequence_Transaction", "Add Template Animation"));

		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FTemplateSequenceTrackEditor::AddKeyInternal, ObjectBindings, SelectedSequence));
	}
}

void FTemplateSequenceTrackEditor::OnTemplateSequenceAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings)
{
	if (AssetData.Num() > 0)
	{
		OnTemplateSequenceAssetSelected(AssetData[0].GetAsset(), ObjectBindings);
	}
}

FKeyPropertyResult FTemplateSequenceTrackEditor::AddKeyInternal(FFrameNumber KeyTime, TArray<FGuid> ObjectBindings, UTemplateSequence* TemplateSequence)
{
	FKeyPropertyResult KeyPropertyResult;

	bool bHandleCreated = false;
	bool bTrackCreated = false;
	bool bTrackModified = false;

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (SequencerPtr.IsValid())
	{
		for (const FGuid& ObjectBindingGuid : ObjectBindings)
		{
			UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBindingGuid);

			FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject(Object);
			FGuid ObjectHandle = HandleResult.Handle;
			KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;

			if (ObjectHandle.IsValid())
			{
				FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, UTemplateSequenceTrack::StaticClass());
				UMovieSceneTrack* Track = TrackResult.Track;
				KeyPropertyResult.bTrackCreated |= TrackResult.bWasCreated;

				if (ensure(Track))
				{
					UMovieSceneSection* NewSection = Cast<UTemplateSequenceTrack>(Track)->AddNewTemplateSequenceSection(KeyTime, TemplateSequence);
					KeyPropertyResult.bTrackModified = true;
					
					GetSequencer()->EmptySelection();
					GetSequencer()->SelectSection(NewSection);
					GetSequencer()->ThrobSectionSelection();
				}
			}
		}
	}

	return KeyPropertyResult;
}

FTemplateSequenceSection::FTemplateSequenceSection(UTemplateSequenceSection& InSection)
	: WeakSection(&InSection)
{
}

UMovieSceneSection* FTemplateSequenceSection::GetSectionObject()
{
	return WeakSection.Get();
}

bool FTemplateSequenceSection::IsReadOnly() const
{
	return WeakSection.IsValid() ? WeakSection.Get()->IsReadOnly() : false;
}

FText FTemplateSequenceSection::GetSectionTitle() const
{
	if (const UTemplateSequenceSection* Section = WeakSection.Get())
	{
		if (const UMovieSceneSequence* TemplateSequence = Section->GetSequence())
		{
			return FText::FromString(TemplateSequence->GetName());
		}
	}
	return LOCTEXT("NoTemplateSequenceSection", "No Template Animation");
}

int32 FTemplateSequenceSection::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	return Painter.PaintSectionBackground();
}

#undef LOCTEXT_NAMESPACE
