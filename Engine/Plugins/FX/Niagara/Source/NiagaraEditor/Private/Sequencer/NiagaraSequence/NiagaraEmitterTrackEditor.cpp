// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterTrackEditor.h"
#include "NiagaraEmitter.h"
#include "NiagaraRendererProperties.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Sequencer/NiagaraSequence/NiagaraSequence.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "EditorStyleSet.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Modules/ModuleManager.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"

#define LOCTEXT_NAMESPACE "NiagaraEmitterTrackEditor"

class SEmitterTrackWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEmitterTrackWidget) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UMovieSceneNiagaraEmitterTrack& InEmitterTrack)
	{
		EmitterTrack = &InEmitterTrack;

		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
		TSharedRef<SHorizontalBox> TrackBox = SNew(SHorizontalBox)
			// Track initialization error icon.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				SNew(SImage)
				.Visibility(this, &SEmitterTrackWidget::GetTrackErrorIconVisibility)
				.Image(FEditorStyle::GetBrush("Icons.Info"))
				.ToolTipText(this, &SEmitterTrackWidget::GetTrackErrorIconToolTip)
			]
			// Stack issues icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				NiagaraEditorModule.GetWidgetProvider()->CreateStackIssueIcon( 
					*EmitterTrack->GetEmitterHandleViewModel()->GetEmitterStackViewModel(),
					*EmitterTrack->GetEmitterHandleViewModel()->GetEmitterStackViewModel()->GetRootEntry())
			]
			// Isolate toggle
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.HAlign(HAlign_Center)
				.ContentPadding(1)
				.ToolTipText(this, &SEmitterTrackWidget::GetToggleIsolateToolTip)
				.OnClicked(this, &SEmitterTrackWidget::OnToggleIsolateButtonClicked)
				.Visibility(this, &SEmitterTrackWidget::GetIsolateToggleVisibility)
				.IsFocusable(false)
				.Content()
				[
					SNew(SImage)
					.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Isolate"))
					.ColorAndOpacity(this, &SEmitterTrackWidget::GetToggleIsolateImageColor)
				]
			];

		// Renderer buttons
		TrackBox->AddSlot()
			.AutoWidth()
			[
				SAssignNew(RenderersBox, SHorizontalBox)
			];
		ConstructRendererWidgets();
		EmitterTrack->GetEmitterHandleViewModel()->GetEmitterViewModel()->GetEmitter()->OnRenderersChanged().AddSP(this, &SEmitterTrackWidget::ConstructRendererWidgets);

		// Enabled checkbox.
		TrackBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("EnabledTooltip", "Toggle whether or not this emitter is enabled."))
				.IsChecked(this, &SEmitterTrackWidget::GetEnabledCheckState)
				.OnCheckStateChanged(this, &SEmitterTrackWidget::OnEnabledCheckStateChanged)
				.Visibility(this, &SEmitterTrackWidget::GetEnableCheckboxVisibility)
			];

		ChildSlot
		[
			TrackBox
		];
	}

	~SEmitterTrackWidget()
	{
		if (EmitterTrack.IsValid() && EmitterTrack->GetEmitterHandleViewModel().IsValid())
		{
			UNiagaraEmitter* Emitter = EmitterTrack->GetEmitterHandleViewModel()->GetEmitterViewModel()->GetEmitter();
			if (Emitter != nullptr)
			{
				Emitter->OnRenderersChanged().RemoveAll(this);
			}
		}
	}

private:
	void ConstructRendererWidgets()
	{
		RenderersBox->ClearChildren();

		if(EmitterTrack.IsValid())
		{
			TArray<UNiagaraStackEntry*> RendererEntryData;
			EmitterTrack->GetEmitterHandleViewModel()->GetRendererEntries(RendererEntryData);
			for (UNiagaraStackEntry* RendererEntry : RendererEntryData)
			{
				if (UNiagaraStackRendererItem* RendererItem = Cast<UNiagaraStackRendererItem>(RendererEntry))
				{
					UNiagaraRendererProperties* Renderer = RendererItem->GetRendererProperties();
					RenderersBox->AddSlot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(3, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
							.IsFocusable(false)
							.ToolTipText(FText::Format(LOCTEXT("RenderButtonToolTip", "{0} - Press to select."), FText::FromString(FName::NameToDisplayString(Renderer->GetName(), false))))
							.OnClicked(this, &SEmitterTrackWidget::OnRenderButtonClicked, RendererEntry)
							[
								SNew(SImage)
								.Image(FSlateIconFinder::FindIconBrushForClass(Renderer->GetClass()))
							]
						];
				}
			}
		}
	}

	EVisibility GetTrackErrorIconVisibility() const 
	{
		return EmitterTrack.IsValid() && EmitterTrack.Get()->GetSectionInitializationErrors().Num() > 0
			? EVisibility::Visible
			: EVisibility::Collapsed;
	}

	FText GetTrackErrorIconToolTip() const
	{
		if(TrackErrorIconToolTip.IsSet() == false && EmitterTrack.IsValid())
		{
			FString TrackErrorIconToolTipBuilder;
			for (const FText& SectionInitializationError : EmitterTrack.Get()->GetSectionInitializationErrors())
			{
				if (TrackErrorIconToolTipBuilder.IsEmpty() == false)
				{
					TrackErrorIconToolTipBuilder.Append(TEXT("\n"));
				}
				TrackErrorIconToolTipBuilder.Append(SectionInitializationError.ToString());
			}
			TrackErrorIconToolTip = FText::FromString(TrackErrorIconToolTipBuilder);
		}
		return TrackErrorIconToolTip.GetValue();
	}

	ECheckBoxState GetEnabledCheckState() const
	{
		return EmitterTrack.IsValid() && EmitterTrack->GetEmitterHandleViewModel()->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	void OnEnabledCheckStateChanged(ECheckBoxState InCheckState)
	{
		if (EmitterTrack.IsValid())
		{
			EmitterTrack->GetEmitterHandleViewModel()->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
		}
	}

	FReply OnToggleIsolateButtonClicked()
	{
		TArray<FGuid> EmitterIdsToIsolate;
		if (EmitterTrack.IsValid())
		{
			if (EmitterTrack->GetEmitterHandleViewModel()->GetIsIsolated() == false)
			{
				EmitterIdsToIsolate.Add(EmitterTrack->GetEmitterHandleViewModel()->GetId());
			}
			EmitterTrack->GetSystemViewModel().IsolateEmitters(EmitterIdsToIsolate);
		}
		return FReply::Handled();
	}

	FText GetToggleIsolateToolTip() const
	{
		return EmitterTrack.IsValid() && EmitterTrack->GetEmitterHandleViewModel()->GetIsIsolated()
			? LOCTEXT("TurnOffEmitterIsolation", "Disable emitter isolation.")
			: LOCTEXT("IsolateThisEmitter", "Enable isolation for this emitter.");
	}

	FSlateColor GetToggleIsolateImageColor() const
	{
		return EmitterTrack.IsValid() && EmitterTrack->GetEmitterHandleViewModel()->GetIsIsolated()
			? FEditorStyle::GetSlateColor("SelectionColor")
			: FLinearColor::Gray;
	}

	FReply OnRenderButtonClicked(UNiagaraStackEntry* InRendererEntry)
	{
		if (EmitterTrack.IsValid())
		{
			TArray<UNiagaraStackEntry*> SelectedEntries;
			SelectedEntries.Add(InRendererEntry);
			TArray<UNiagaraStackEntry*> DeselectedEntries;
			EmitterTrack->GetSystemViewModel().GetSelectionViewModel()->UpdateSelectedEntries(SelectedEntries, DeselectedEntries, true);
		}
		return FReply::Handled();
	}

	EVisibility GetEnableCheckboxVisibility() const
	{
		return EmitterTrack.IsValid() && EmitterTrack->GetSystemViewModel().GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset ? EVisibility::Visible : EVisibility::Collapsed;
	}

	EVisibility GetIsolateToggleVisibility() const
	{
		return EmitterTrack.IsValid() && EmitterTrack->GetSystemViewModel().GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset ? EVisibility::Visible : EVisibility::Collapsed;
	}

private:
	TWeakObjectPtr<UMovieSceneNiagaraEmitterTrack> EmitterTrack;
	mutable TOptional<FText> TrackErrorIconToolTip;
	TSharedPtr<SHorizontalBox> RenderersBox;
};

FNiagaraEmitterTrackEditor::FNiagaraEmitterTrackEditor(TSharedPtr<ISequencer> Sequencer) 
	: FMovieSceneTrackEditor(Sequencer.ToSharedRef())
{
}

TSharedRef<ISequencerTrackEditor> FNiagaraEmitterTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FNiagaraEmitterTrackEditor(InSequencer));
}

bool FNiagaraEmitterTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> TrackClass) const 
{
	if (TrackClass == UMovieSceneNiagaraEmitterTrack::StaticClass())
	{
		return true;
	}
	return false;
}

TSharedRef<ISequencerSection> FNiagaraEmitterTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	UMovieSceneNiagaraEmitterSectionBase* EmitterSection = CastChecked<UMovieSceneNiagaraEmitterSectionBase>(&SectionObject);
	return EmitterSection->MakeSectionInterface();
}

bool FNiagaraEmitterTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	UNiagaraEmitter* EmitterAsset = Cast<UNiagaraEmitter>(Asset);
	UNiagaraSequence* NiagaraSequence = Cast<UNiagaraSequence>(GetSequencer()->GetRootMovieSceneSequence());
	if (EmitterAsset != nullptr && NiagaraSequence != nullptr && NiagaraSequence->GetSystemViewModel().GetCanModifyEmittersFromTimeline())
	{
		NiagaraSequence->GetSystemViewModel().AddEmitter(*EmitterAsset);
	}
	return false;
}

void FNiagaraEmitterTrackEditor::BuildTrackContextMenu( FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track )
{
	UMovieSceneNiagaraEmitterTrack* EmitterTrack = CastChecked<UMovieSceneNiagaraEmitterTrack>(Track);
	FNiagaraSystemViewModel& SystemViewModel = EmitterTrack->GetSystemViewModel();

	if (SystemViewModel.GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		FNiagaraEditorUtilities::AddEmitterContextMenuActions(MenuBuilder, EmitterTrack->GetEmitterHandleViewModel());
	}
}

TSharedPtr<SWidget> FNiagaraEmitterTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	return SNew(SEmitterTrackWidget, *CastChecked<UMovieSceneNiagaraEmitterTrack>(Track));
}

#undef LOCTEXT_NAMESPACE