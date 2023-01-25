// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTimeChangeUndoRedoProxy.h"
#include "Sequencer.h"
#include "ScopedTransaction.h"
#include "CoreGlobals.h"

#define LOCTEXT_NAMESPACE "Sequencer"

USequencerTimeChangeUndoRedoProxy::~USequencerTimeChangeUndoRedoProxy()
{
	if (OnActivateSequenceChangedHandle.IsValid() && WeakSequencer.IsValid())
	{
		WeakSequencer.Pin()->OnActivateSequence().Remove(OnActivateSequenceChangedHandle);
	}
}

void USequencerTimeChangeUndoRedoProxy::SetSequencer(TSharedRef<FSequencer> InSequencer)
{
	WeakSequencer = InSequencer;
	OnActivateSequenceChangedHandle = InSequencer->OnActivateSequence().AddUObject(this, &USequencerTimeChangeUndoRedoProxy::OnActivateSequenceChanged);
	OnActivateSequenceChanged(InSequencer->GetFocusedTemplateID());
}

void USequencerTimeChangeUndoRedoProxy::OnActivateSequenceChanged(FMovieSceneSequenceIDRef ID)
{
	if (TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin())
	{
		if (SequencerPtr->GetFocusedMovieSceneSequence() && SequencerPtr->GetFocusedMovieSceneSequence()->GetMovieScene())
		{
			using namespace UE::MovieScene;
			bTimeWasSet = false;
			MovieSceneModified.Unlink();
			SequencerPtr->GetFocusedMovieSceneSequence()->GetMovieScene()->UMovieSceneSignedObject::EventHandlers.Link(MovieSceneModified, this);
		}
	}
}

void USequencerTimeChangeUndoRedoProxy::OnModifiedIndirectly(UMovieSceneSignedObject* Object)
{
	if (Object->IsA<UMovieSceneSection>() && WeakSequencer.IsValid())
	{
		FQualifiedFrameTime InTime = WeakSequencer.Pin()->GetGlobalTime();

		if (bTimeWasSet)
		{
			if (!GIsTransacting && (InTime.Time != Time.Time || InTime.Rate != Time.Rate))
			{
				const FScopedTransaction Transaction(LOCTEXT("TimeChanged", "Time Changed"));
				Modify();
			}
		}
		bTimeWasSet = true;
		Time = InTime;
	}
}

void USequencerTimeChangeUndoRedoProxy::PostEditUndo()
{
	if (WeakSequencer.IsValid())
	{
		WeakSequencer.Pin()->SetGlobalTime(Time.Time, true);
	}
}

#undef LOCTEXT_NAMESPACE

