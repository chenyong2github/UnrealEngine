// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKeyArea.h"
#include "ISequencerModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SNullWidget.h"
#include "ISequencerChannelInterface.h"
#include "SequencerClipboardReconciler.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "CurveModel.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "MovieSceneSequence.h"

IKeyArea::IKeyArea(TWeakPtr<ISequencerSection> InSection, FMovieSceneChannelHandle InChannel)
	: TreeSerialNumber(0)
	, ChannelHandle(InChannel)
{
	Reinitialize(InSection, InChannel);
}

void IKeyArea::Reinitialize(TWeakPtr<ISequencerSection> InSection, FMovieSceneChannelHandle InChannel)
{
	WeakSection = InSection;
	ChannelHandle = InChannel;
	Color = FLinearColor::White;

	if (const FMovieSceneChannelMetaData* MetaData = ChannelHandle.GetMetaData())
	{
		Color = MetaData->Color;
		ChannelName = MetaData->Name;
		DisplayText = MetaData->DisplayText;
	}

	UMovieSceneSection*       SectionObject = InSection.Pin()->GetSectionObject();
	UMovieScenePropertyTrack* PropertyTrack = SectionObject->GetTypedOuter<UMovieScenePropertyTrack>();
	if (PropertyTrack && PropertyTrack->GetPropertyPath() != NAME_None)
	{
		PropertyBindings = FTrackInstancePropertyBindings(PropertyTrack->GetPropertyName(), PropertyTrack->GetPropertyPath().ToString());
	}
}

FMovieSceneChannel* IKeyArea::ResolveChannel() const
{
	return ChannelHandle.Get();
}

UMovieSceneSection* IKeyArea::GetOwningSection() const
{
	TSharedPtr<ISequencerSection> SectionInterface = WeakSection.Pin();
	return SectionInterface ? SectionInterface->GetSectionObject() : nullptr;;
}

TSharedPtr<ISequencerSection> IKeyArea::GetSectionInterface() const
{
	return WeakSection.Pin();
}

FName IKeyArea::GetName() const
{
	return ChannelName;
}

void IKeyArea::SetName(FName InName)
{
	ChannelName = InName;
}

ISequencerChannelInterface* IKeyArea::FindChannelEditorInterface() const
{
	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	ISequencerChannelInterface* EditorInterface = SequencerModule.FindChannelEditorInterface(ChannelHandle.GetChannelTypeName());
	ensureMsgf(EditorInterface, TEXT("No channel interface found for type '%s'. Did you forget to call ISequencerModule::RegisterChannelInterface<ChannelType>()?"), *ChannelHandle.GetChannelTypeName().ToString());
	return EditorInterface;
}

FKeyHandle IKeyArea::AddOrUpdateKey(FFrameNumber Time, const FGuid& ObjectBindingID, ISequencer& InSequencer)
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	FMovieSceneChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection* Section = GetOwningSection();

	// The extended editor data may be null, but is passed to the interface regardless
	const void* RawExtendedData = ChannelHandle.GetExtendedEditorData();

	if (EditorInterface && Channel)
	{
		FTrackInstancePropertyBindings* BindingsPtr = PropertyBindings.IsSet() ? &PropertyBindings.GetValue() : nullptr;
		return EditorInterface->AddOrUpdateKey_Raw(Channel, Section, RawExtendedData, Time, InSequencer, ObjectBindingID, BindingsPtr);
	}

	return FKeyHandle();
}

FKeyHandle IKeyArea::DuplicateKey(FKeyHandle InKeyHandle) const
{
	FKeyHandle NewHandle = FKeyHandle::Invalid();

	if (FMovieSceneChannel* Channel = ChannelHandle.Get())
	{
		Channel->DuplicateKeys(TArrayView<const FKeyHandle>(&InKeyHandle, 1), TArrayView<FKeyHandle>(&NewHandle, 1));
	}

	return NewHandle;
}

void IKeyArea::SetKeyTimes(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<const FFrameNumber> InKeyTimes) const
{
	check(InKeyHandles.Num() == InKeyTimes.Num());

	if (FMovieSceneChannel* Channel = ChannelHandle.Get())
	{
		Channel->SetKeyTimes(InKeyHandles, InKeyTimes);
	}
}

void IKeyArea::GetKeyTimes(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<FFrameNumber> OutTimes) const
{
	if (FMovieSceneChannel* Channel = ChannelHandle.Get())
	{
		Channel->GetKeyTimes(InKeyHandles, OutTimes);
	}
}

void IKeyArea::GetKeyInfo(TArray<FKeyHandle>* OutHandles, TArray<FFrameNumber>* OutTimes, const TRange<FFrameNumber>& WithinRange) const
{
	if (FMovieSceneChannel* Channel = ChannelHandle.Get())
	{
		Channel->GetKeys(WithinRange, OutTimes, OutHandles);
	}
}

TSharedPtr<FStructOnScope> IKeyArea::GetKeyStruct(FKeyHandle KeyHandle) const
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	if (EditorInterface)
	{
		return EditorInterface->GetKeyStruct_Raw(ChannelHandle, KeyHandle);
	}
	return nullptr;
}

void IKeyArea::DrawKeys(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<FKeyDrawParams> OutKeyDrawParams)
{
	check(InKeyHandles.Num() == OutKeyDrawParams.Num());

	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	FMovieSceneChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection* OwningSection = GetOwningSection();

	if (EditorInterface && Channel && OwningSection)
	{
		return EditorInterface->DrawKeys_Raw(Channel, InKeyHandles, OwningSection, OutKeyDrawParams);
	}
}

bool IKeyArea::CanCreateKeyEditor() const
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	const FMovieSceneChannel* Channel = ChannelHandle.Get();

	return EditorInterface && Channel && EditorInterface->CanCreateKeyEditor_Raw(Channel);
}

TSharedRef<SWidget> IKeyArea::CreateKeyEditor(TWeakPtr<ISequencer> Sequencer, const FGuid& ObjectBindingID)
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	UMovieSceneSection* OwningSection = GetOwningSection();

	TWeakPtr<FTrackInstancePropertyBindings> PropertyBindingsPtr;
	if (PropertyBindings.IsSet())
	{
		PropertyBindingsPtr = TSharedPtr<FTrackInstancePropertyBindings>(AsShared(), &PropertyBindings.GetValue());
	}

	if (EditorInterface && OwningSection)
	{
		return EditorInterface->CreateKeyEditor_Raw(ChannelHandle, OwningSection, ObjectBindingID, PropertyBindingsPtr, Sequencer);
	}
	return SNullWidget::NullWidget;
}

void IKeyArea::CopyKeys(FMovieSceneClipboardBuilder& ClipboardBuilder, TArrayView<const FKeyHandle> KeyMask) const
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	FMovieSceneChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection* OwningSection = GetOwningSection();

	if (EditorInterface && Channel && OwningSection)
	{
		EditorInterface->CopyKeys_Raw(Channel, OwningSection, ChannelName, ClipboardBuilder, KeyMask);
	}
}

void IKeyArea::PasteKeys(const FMovieSceneClipboardKeyTrack& KeyTrack, const FMovieSceneClipboardEnvironment& SrcEnvironment, const FSequencerPasteEnvironment& DstEnvironment)
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	FMovieSceneChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection* OwningSection = GetOwningSection();
	if (EditorInterface && Channel && OwningSection)
	{
		TArray<FKeyHandle> PastedKeys;
		EditorInterface->PasteKeys_Raw(Channel, OwningSection, KeyTrack, SrcEnvironment, DstEnvironment, PastedKeys);

		for (FKeyHandle KeyHandle : PastedKeys)
		{
			DstEnvironment.ReportPastedKey(KeyHandle, *this);
		}
	}
}

FText GetOwningObjectBindingName(UMovieSceneTrack* InTrack, ISequencer& InSequencer)
{
	check(InTrack);

	UMovieSceneSequence* FocusedSequence = InSequencer.GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = FocusedSequence->GetMovieScene();

	FGuid PossessableGuid;
	if (MovieScene->FindTrackBinding(*InTrack, PossessableGuid))
	{
		return MovieScene->GetObjectDisplayName(PossessableGuid);
	}

	// Couldn't find an owning track, so must not be nestled inside of something!
	return FText();
}

TUniquePtr<FCurveModel> IKeyArea::CreateCurveEditorModel(TSharedRef<ISequencer> InSequencer) const
{
	ISequencerChannelInterface* EditorInterface = FindChannelEditorInterface();
	UMovieSceneSection* OwningSection = GetOwningSection();
	if (EditorInterface && OwningSection && ChannelHandle.Get() != nullptr)
	{
		TUniquePtr<FCurveModel> CurveModel = EditorInterface->CreateCurveEditorModel_Raw(ChannelHandle, OwningSection, InSequencer);
		if (CurveModel.IsValid())
		{
			// Build long, short and context names for this curve to maximize information shown in the Curve Editor UI.
			UMovieSceneTrack* OwningTrack = OwningSection->GetTypedOuter<UMovieSceneTrack>();
			FText OwningTrackName;
			FText ObjectBindingName;

			if (OwningTrack)
			{
				OwningTrackName = OwningTrack->GetDisplayName();
				
				// This track might be inside an object binding and we'd like to prepend the object binding's name for more context.
				ObjectBindingName = GetOwningObjectBindingName(OwningTrack, *InSequencer);
			}

			// Not all tracks have all the information so we need to format it differently depending on how many are valid.
			TArray<FText> ValidNames;

			if (!ObjectBindingName.IsEmptyOrWhitespace())
			{
				ValidNames.Add(ObjectBindingName);
			}
			if (!OwningTrackName.IsEmptyOrWhitespace())
			{
				ValidNames.Add(OwningTrackName);
			}
			if (!ChannelHandle.GetMetaData()->Group.IsEmptyOrWhitespace())
			{
				ValidNames.Add(ChannelHandle.GetMetaData()->Group);
			}
			if (!DisplayText.IsEmptyOrWhitespace())
			{
				ValidNames.Add(DisplayText);
			}

			// Now we loop through and string them together into one big format string.
			FText LongDisplayNameFormatString;
			for (int32 NameIndex = 0; NameIndex < ValidNames.Num(); NameIndex++)
			{
				const bool bLastEntry = NameIndex == ValidNames.Num() - 1;
				if (!bLastEntry)
				{
					LongDisplayNameFormatString = FText::Format(NSLOCTEXT("SequencerIKeyArea", "CurveLongDisplayNameFormat", "{0}`{{1}`}."), LongDisplayNameFormatString, NameIndex);
				}
				else
				{
					LongDisplayNameFormatString = FText::Format(NSLOCTEXT("SequencerIKeyArea", "CurveLongDisplayNameFormatEnd", "{0}`{{1}`}"), LongDisplayNameFormatString, NameIndex);
				}
			}

			FText LongDisplayName = FText::Format(LongDisplayNameFormatString, FFormatOrderedArguments(ValidNames));
			const FText ShortDisplayName = DisplayText;
			const FString IntentName = ChannelHandle.GetMetaData()->Group.IsEmptyOrWhitespace() ? DisplayText.ToString() : FString::Format(TEXT("{0}.{1}"), { ChannelHandle.GetMetaData()->Group.ToString(), DisplayText.ToString() });

			CurveModel->SetShortDisplayName(DisplayText);
			CurveModel->SetLongDisplayName(LongDisplayName);
			CurveModel->SetIntentionName(IntentName);

			if (Color.IsSet())
			{
				CurveModel->SetColor(Color.GetValue());
			}
		}
		return CurveModel;
	}

	return nullptr;
}
