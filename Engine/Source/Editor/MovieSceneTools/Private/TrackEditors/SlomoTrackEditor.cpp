// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/SlomoTrackEditor.h"
#include "EditorStyleSet.h"
#include "Tracks/MovieSceneSlomoTrack.h"


#define LOCTEXT_NAMESPACE "FSlomoTrackEditor"


/* FSlomoTrackEditor static functions
 *****************************************************************************/

TSharedRef<ISequencerTrackEditor> FSlomoTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FSlomoTrackEditor(InSequencer));
}


/* FSlomoTrackEditor structors
 *****************************************************************************/

FSlomoTrackEditor::FSlomoTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FFloatPropertyTrackEditor(InSequencer)
{ }


/* ISequencerTrackEditor interface
 *****************************************************************************/

void FSlomoTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddTimeDilationTrack", "Time Dilation Track"),
		LOCTEXT("AddTimeDilationTrackTooltip", "Adds a new track that controls the world's time dilation."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Slomo"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FSlomoTrackEditor::HandleAddSlomoTrackMenuEntryExecute),
			FCanExecuteAction::CreateRaw(this, &FSlomoTrackEditor::HandleAddSlomoTrackMenuEntryCanExecute)
		)
	);
}

bool FSlomoTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	ETrackSupport TrackSupported = InSequence ? InSequence->IsTrackSupported(UMovieSceneSlomoTrack::StaticClass()) : ETrackSupport::NotSupported;
	return TrackSupported == ETrackSupport::Supported;
}

bool FSlomoTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return (Type == UMovieSceneSlomoTrack::StaticClass());
}

const FSlateBrush* FSlomoTrackEditor::GetIconBrush() const
{
	return FEditorStyle::GetBrush("Sequencer.Tracks.Slomo");
}


/* FSlomoTrackEditor callbacks
 *****************************************************************************/

void FSlomoTrackEditor::HandleAddSlomoTrackMenuEntryExecute()
{
	UMovieScene* MovieScene = GetFocusedMovieScene();

	if (MovieScene == nullptr)
	{
		return;
	}

	if (MovieScene->IsReadOnly())
	{
		return;
	}

	UMovieSceneTrack* SlomoTrack = MovieScene->FindMasterTrack<UMovieSceneSlomoTrack>();

	if (SlomoTrack != nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddSlomoTrack_Transaction", "Add Time Dilation Track"));

	MovieScene->Modify();

	SlomoTrack = FindOrCreateMasterTrack<UMovieSceneSlomoTrack>().Track;
	check(SlomoTrack);

	UMovieSceneSection* NewSection = SlomoTrack->CreateNewSection();
	check(NewSection);

	SlomoTrack->AddSection(*NewSection);

	if (GetSequencer().IsValid())
	{
		GetSequencer()->OnAddTrack(SlomoTrack, FGuid());
	}
}

bool FSlomoTrackEditor::HandleAddSlomoTrackMenuEntryCanExecute() const
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	return ((FocusedMovieScene != nullptr) && (FocusedMovieScene->FindMasterTrack<UMovieSceneSlomoTrack>() == nullptr));
}

#undef LOCTEXT_NAMESPACE
