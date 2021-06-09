// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerContextMenus.h"
#include "Modules/ModuleManager.h"
#include "EditorStyleSet.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "SequencerCommonHelpers.h"
#include "SequencerCommands.h"
#include "SSequencer.h"
#include "SectionLayout.h"
#include "SSequencerSection.h"
#include "SequencerSettings.h"
#include "ISequencerHotspot.h"
#include "SequencerHotspots.h"
#include "ScopedTransaction.h"
#include "MovieSceneToolHelpers.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneKeyStruct.h"
#include "Framework/Commands/GenericCommands.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "Sections/MovieSceneSubSection.h"
#include "Curves/IntegralCurve.h"
#include "Editor.h"
#include "SequencerUtilities.h"
#include "ClassViewerModule.h"
#include "Generators/MovieSceneEasingFunction.h"
#include "ClassViewerFilter.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "ISequencerChannelInterface.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "SKeyEditInterface.h"
#include "MovieSceneTimeHelpers.h"
#include "FrameNumberDetailsCustomization.h"
#include "MovieSceneSectionDetailsCustomization.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "Channels/MovieSceneChannel.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Algo/AnyOf.h"


#define LOCTEXT_NAMESPACE "SequencerContextMenus"

static void CreateKeyStructForSelection(TSharedPtr<ISequencer> InSequencer, TSharedPtr<FStructOnScope>& OutKeyStruct, TWeakObjectPtr<UMovieSceneSection>& OutKeyStructSection)
{
	const TSet<FSequencerSelectedKey>& SelectedKeys = InSequencer->GetSelection().GetSelectedKeys();

	if (SelectedKeys.Num() == 1)
	{
		for (const FSequencerSelectedKey& Key : SelectedKeys)
		{
			if (Key.KeyArea.IsValid() && Key.KeyHandle.IsSet())
			{
				OutKeyStruct = Key.KeyArea->GetKeyStruct(Key.KeyHandle.GetValue());
				OutKeyStructSection = Key.KeyArea->GetOwningSection();
				return;
			}
		}
	}
	else
	{
		TArray<FKeyHandle> KeyHandles;
		UMovieSceneSection* CommonSection = nullptr;
		for (const FSequencerSelectedKey& Key : SelectedKeys)
		{
			if (Key.KeyArea.IsValid() && Key.KeyHandle.IsSet())
			{
				KeyHandles.Add(Key.KeyHandle.GetValue());

				if (!CommonSection)
				{
					CommonSection = Key.KeyArea->GetOwningSection();
				}
				else if (CommonSection != Key.KeyArea->GetOwningSection())
				{
					CommonSection = nullptr;
					return;
				}
			}
		}

		if (CommonSection)
		{
			OutKeyStruct = CommonSection->GetKeyStruct(KeyHandles);
			OutKeyStructSection = CommonSection;
		}
	}
}

void FKeyContextMenu::BuildMenu(FMenuBuilder& MenuBuilder, FSequencer& InSequencer)
{
	TSharedRef<FKeyContextMenu> Menu = MakeShareable(new FKeyContextMenu(InSequencer));
	Menu->PopulateMenu(MenuBuilder);
}

void FKeyContextMenu::PopulateMenu(FMenuBuilder& MenuBuilder)
{
	FSequencer* SequencerPtr = &Sequencer.Get();
	TSharedRef<FKeyContextMenu> Shared = AsShared();

	CreateKeyStructForSelection(Sequencer, KeyStruct, KeyStructSection);

	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");

		FSelectedKeysByChannel SelectedKeysByChannel(SequencerPtr->GetSelection().GetSelectedKeys().Array());

		TMap<FName, TArray<FExtendKeyMenuParams>> ChannelAndHandlesByType;
		for (FSelectedChannelInfo& ChannelInfo : SelectedKeysByChannel.SelectedChannels)
		{
			FExtendKeyMenuParams ExtendKeyMenuParams;
			ExtendKeyMenuParams.Section = ChannelInfo.OwningSection;
			ExtendKeyMenuParams.Channel = ChannelInfo.Channel;
			ExtendKeyMenuParams.Handles = MoveTemp(ChannelInfo.KeyHandles);

			ChannelAndHandlesByType.FindOrAdd(ChannelInfo.Channel.GetChannelTypeName()).Add(MoveTemp(ExtendKeyMenuParams));
		}

		for (auto& Pair : ChannelAndHandlesByType)
		{
			ISequencerChannelInterface* ChannelInterface = SequencerModule.FindChannelEditorInterface(Pair.Key);
			if (ChannelInterface)
			{
				ChannelInterface->ExtendKeyMenu_Raw(MenuBuilder, MoveTemp(Pair.Value), Sequencer);
			}
		}
	}

	if(KeyStruct.IsValid())
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("KeyProperties", "Properties"),
			LOCTEXT("KeyPropertiesTooltip", "Modify the key properties"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ Shared->AddPropertiesMenu(SubMenuBuilder); }),
			FUIAction (
				FExecuteAction(),
				// @todo sequencer: only one struct per structure view supported right now :/
				FCanExecuteAction::CreateLambda([=]{ return KeyStruct.IsValid(); })
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}

	MenuBuilder.BeginSection("SequencerKeyEdit", LOCTEXT("EditMenu", "Edit"));
	{
		TSharedPtr<ISequencerHotspot> Hotspot = SequencerPtr->GetHotspot();

		if (Hotspot.IsValid() && Hotspot->GetType() == ESequencerHotspot::Key)
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		}
	}
	MenuBuilder.EndSection(); // SequencerKeyEdit



	MenuBuilder.BeginSection("SequencerKeys", LOCTEXT("KeysMenu", "Keys"));
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("SetKeyTime", "Set Key Time"), LOCTEXT("SetKeyTimeTooltip", "Set the key to a specified time"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(SequencerPtr, &FSequencer::SetKeyTime),
				FCanExecuteAction::CreateSP(SequencerPtr, &FSequencer::CanSetKeyTime))
		);

		MenuBuilder.AddMenuEntry(LOCTEXT("Rekey", "Rekey"), LOCTEXT("RekeyTooltip", "Set the selected key's time to the current time"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(SequencerPtr, &FSequencer::Rekey),
				FCanExecuteAction::CreateSP(SequencerPtr, &FSequencer::CanRekey))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("SnapToFrame", "Snap to Frame"),
			LOCTEXT("SnapToFrameToolTip", "Snap selected keys to frame"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(SequencerPtr, &FSequencer::SnapToFrame),
				FCanExecuteAction::CreateSP(SequencerPtr, &FSequencer::CanSnapToFrame))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteKey", "Delete"),
			LOCTEXT("DeleteKeyToolTip", "Deletes the selected keys"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(SequencerPtr, &FSequencer::DeleteSelectedKeys))
		);
	}
	MenuBuilder.EndSection(); // SequencerKeys
}

void FKeyContextMenu::AddPropertiesMenu(FMenuBuilder& MenuBuilder)
{
	auto UpdateAndRetrieveEditData = [this]
	{
		FKeyEditData EditData;
		CreateKeyStructForSelection(Sequencer, EditData.KeyStruct, EditData.OwningSection);
		return EditData;
	};

	MenuBuilder.AddWidget(
		SNew(SKeyEditInterface, Sequencer)
		.EditData_Lambda(UpdateAndRetrieveEditData)
	, FText::GetEmpty(), true);
}


FSectionContextMenu::FSectionContextMenu(FSequencer& InSeqeuncer, FFrameTime InMouseDownTime)
	: Sequencer(StaticCastSharedRef<FSequencer>(InSeqeuncer.AsShared()))
	, MouseDownTime(InMouseDownTime)
{
	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		if (UMovieSceneSection* Section = WeakSection.Get())
		{
			FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
			for (const FMovieSceneChannelEntry& Entry : ChannelProxy.GetAllEntries())
			{
				FName ChannelTypeName = Entry.GetChannelTypeName();

				TArray<UMovieSceneSection*>& SectionArray = SectionsByType.FindOrAdd(ChannelTypeName);
				SectionArray.Add(Section);

				TArray<FMovieSceneChannelHandle>& ChannelHandles = ChannelsByType.FindOrAdd(ChannelTypeName);

				const int32 NumChannels = Entry.GetChannels().Num();
				for (int32 Index = 0; Index < NumChannels; ++Index)
				{
					ChannelHandles.Add(ChannelProxy.MakeHandle(ChannelTypeName, Index));
				}
			}
		}
	}
}

void FSectionContextMenu::BuildMenu(FMenuBuilder& MenuBuilder, FSequencer& InSequencer, FFrameTime InMouseDownTime)
{
	TSharedRef<FSectionContextMenu> Menu = MakeShareable(new FSectionContextMenu(InSequencer, InMouseDownTime));
	Menu->PopulateMenu(MenuBuilder);
}


void FSectionContextMenu::PopulateMenu(FMenuBuilder& MenuBuilder)
{
	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FSectionContextMenu> Shared = AsShared();

	// Clean SectionGroups to prevent any potential stale references from affecting the context menu entries
	Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->CleanSectionGroups();
	
	// These are potentially expensive checks in large sequences, and won't change while context menu is open
	const bool bCanGroup = Sequencer->CanGroupSelectedSections();
	const bool bCanUngroup = Sequencer->CanUngroupSelectedSections();

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");

	for (auto& Pair : ChannelsByType)
	{
		const TArray<UMovieSceneSection*>& Sections = SectionsByType.FindChecked(Pair.Key);

		ISequencerChannelInterface* ChannelInterface = SequencerModule.FindChannelEditorInterface(Pair.Key);
		if (ChannelInterface)
		{
			ChannelInterface->ExtendSectionMenu_Raw(MenuBuilder, Pair.Value, Sections, Sequencer);
		}
	}

	MenuBuilder.AddSubMenu(
		LOCTEXT("SectionProperties", "Properties"),
		LOCTEXT("SectionPropertiesTooltip", "Modify the section properties"),
		FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
		{
			TArray<TWeakObjectPtr<UObject>> Sections;
			{
				for (TWeakObjectPtr<UMovieSceneSection> Section : Sequencer->GetSelection().GetSelectedSections())
				{
					if (Section.IsValid())
					{
						Sections.Add(Section);
					}
				}
			}
			SequencerHelpers::AddPropertiesMenu(*Sequencer, SubMenuBuilder, Sections);
		})
	);

	MenuBuilder.BeginSection("SequencerKeyEdit", LOCTEXT("EditMenu", "Edit"));
	{
		TSharedPtr<FPasteFromHistoryContextMenu> PasteFromHistoryMenu;
		TSharedPtr<FPasteContextMenu> PasteMenu;

		if (Sequencer->GetClipboardStack().Num() != 0)
		{
			FPasteContextMenuArgs PasteArgs = FPasteContextMenuArgs::PasteAt(MouseDownTime.FrameNumber);
			PasteMenu = FPasteContextMenu::CreateMenu(*Sequencer, PasteArgs);
			PasteFromHistoryMenu = FPasteFromHistoryContextMenu::CreateMenu(*Sequencer, PasteArgs);
		}

		MenuBuilder.AddSubMenu(
			LOCTEXT("Paste", "Paste"),
			FText(),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ if (PasteMenu.IsValid()) { PasteMenu->PopulateMenu(SubMenuBuilder); } }),
			FUIAction (
				FExecuteAction(),
				FCanExecuteAction::CreateLambda([=]{ return PasteMenu.IsValid() && PasteMenu->IsValidPaste(); })
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddSubMenu(
			LOCTEXT("PasteFromHistory", "Paste From History"),
			FText(),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ if (PasteFromHistoryMenu.IsValid()) { PasteFromHistoryMenu->PopulateMenu(SubMenuBuilder); } }),
			FUIAction (
				FExecuteAction(),
				FCanExecuteAction::CreateLambda([=]{ return PasteFromHistoryMenu.IsValid(); })
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection(); // SequencerKeyEdit

	MenuBuilder.BeginSection("SequencerSections", LOCTEXT("SectionsMenu", "Sections"));
	{
		if (CanPrimeForRecording())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("PrimeForRecording", "Primed For Recording"),
				LOCTEXT("PrimeForRecordingTooltip", "Prime this track for recording a new sequence."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([=] { return Shared->TogglePrimeForRecording(); }),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([=] { return Shared->IsPrimedForRecording() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}

		if (CanSelectAllKeys())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("SelectAllKeys", "Select All Keys"),
				LOCTEXT("SelectAllKeysTooltip", "Select all keys in section"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([=] { return Shared->SelectAllKeys(); }))
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CopyAllKeys", "Copy All Keys"),
				LOCTEXT("CopyAllKeysTooltip", "Copy all keys in section"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([=] { return Shared->CopyAllKeys(); }))
			);
		}

		MenuBuilder.AddSubMenu(
			LOCTEXT("EditSection", "Edit"),
			LOCTEXT("EditSectionTooltip", "Edit section"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& InMenuBuilder) { Shared->AddEditMenu(InMenuBuilder); }));

		MenuBuilder.AddSubMenu(
			LOCTEXT("OrderSection", "Order"),
			LOCTEXT("OrderSectionTooltip", "Order section"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder) { Shared->AddOrderMenu(SubMenuBuilder); }));

		if (GetSupportedBlendTypes().Num() > 1)
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("BlendTypeSection", "Blend Type"),
				LOCTEXT("BlendTypeSectionTooltip", "Change the way in which this section blends with other sections of the same type"),
				FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder) { Shared->AddBlendTypeMenu(SubMenuBuilder); }));
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleSectionActive", "Active"),
			LOCTEXT("ToggleSectionActiveTooltip", "Toggle section active/inactive"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([=] { Shared->ToggleSectionActive(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([=] { return Shared->IsSectionActive(); })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("Sequencer", "ToggleSectionLocked", "Locked"),
			NSLOCTEXT("Sequencer", "ToggleSectionLockedTooltip", "Toggle section locked/unlocked"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([=] { Shared->ToggleSectionLocked(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([=] { return Shared->IsSectionLocked(); })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("GroupSections", "Group"),
			LOCTEXT("GroupSectionsTooltip", "Group selected sections together so that when any section is moved, all sections in that group move together."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(Sequencer, &FSequencer::GroupSelectedSections),
				FCanExecuteAction::CreateLambda([bCanGroup] { return bCanGroup; })
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("UngroupSections", "Ungroup"),
			LOCTEXT("UngroupSectionsTooltip", "Ungroup selected sections"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(Sequencer, &FSequencer::UngroupSelectedSections),
				FCanExecuteAction::CreateLambda([bCanUngroup] { return bCanUngroup; })
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		// @todo Sequencer this should delete all selected sections
		// delete/selection needs to be rethought in general
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteSection", "Delete"),
			LOCTEXT("DeleteSectionToolTip", "Deletes this section"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([=] { return Shared->DeleteSection(); }))
		);


		if (CanSetSectionToKey())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("KeySection", "Key This Section"),
				LOCTEXT("KeySection_ToolTip", "This section will get changed when we modify the property externally"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([=] { return Shared->SetSectionToKey(); }))
			);
		}
	}
	MenuBuilder.EndSection(); // SequencerSections
}


void FSectionContextMenu::AddEditMenu(FMenuBuilder& MenuBuilder)
{
	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FSectionContextMenu> Shared = AsShared();

	MenuBuilder.BeginSection("Trimming", LOCTEXT("TrimmingSectionMenu", "Trimming"));

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().TrimSectionLeft);

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().TrimSectionRight);

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SplitSection);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteKeysWhenTrimming", "Delete Keys"),
		LOCTEXT("DeleteKeysWhenTrimmingTooltip", "Delete keys outside of the trimmed range"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=] { Sequencer->GetSequencerSettings()->SetDeleteKeysWhenTrimming(!Sequencer->GetSequencerSettings()->GetDeleteKeysWhenTrimming()); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([=] { return Sequencer->GetSequencerSettings()->GetDeleteKeysWhenTrimming(); })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.EndSection();
		
	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AutoSizeSection", "Auto Size"),
		LOCTEXT("AutoSizeSectionTooltip", "Auto size the section length to the duration of the source of this section (ie. audio, animation or shot length)"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->AutoSizeSection(); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanAutoSize(); }))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SyncSectionsUsingSourceTimecode", "Synchronize using Source Timecode"),
		LOCTEXT("SyncSectionsUsingSourceTimecodeTooltip", "Sync selected sections using the source timecode.  The first selected section will be unchanged and subsequent sections will be adjusted according to their source timecode as relative to the first section's."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=] { return Shared->Sequencer->SyncSectionsUsingSourceTimecode(); }),
			FCanExecuteAction::CreateLambda([=]{ return (Shared->Sequencer->GetSelection().GetSelectedSections().Num() > 1); }))
	);

	MenuBuilder.BeginSection("SequencerInterpolation", LOCTEXT("KeyInterpolationMenu", "Key Interpolation"));
	
	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetKeyInterpolationAuto", "Cubic (Auto)"),
		LOCTEXT("SetKeyInterpolationAutoTooltip", "Set key interpolation to auto"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyAuto"),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->SetInterpTangentMode(RCIM_Cubic, RCTM_Auto); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanSetInterpTangentMode(); }),
			FIsActionChecked::CreateLambda([=]{ return Shared->IsInterpTangentModeSelected(RCIM_Cubic, RCTM_Auto); }) ),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetKeyInterpolationUser", "Cubic (User)"),
		LOCTEXT("SetKeyInterpolationUserTooltip", "Set key interpolation to user"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyUser"),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->SetInterpTangentMode(RCIM_Cubic, RCTM_User); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanSetInterpTangentMode(); }),
			FIsActionChecked::CreateLambda([=]{ return Shared->IsInterpTangentModeSelected(RCIM_Cubic, RCTM_User); }) ),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetKeyInterpolationBreak", "Cubic (Break)"),
		LOCTEXT("SetKeyInterpolationBreakTooltip", "Set key interpolation to break"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyBreak"),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->SetInterpTangentMode(RCIM_Cubic, RCTM_Break); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanSetInterpTangentMode(); }),
			FIsActionChecked::CreateLambda([=]{ return Shared->IsInterpTangentModeSelected(RCIM_Cubic, RCTM_Break); }) ),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetKeyInterpolationLinear", "Linear"),
		LOCTEXT("SetKeyInterpolationLinearTooltip", "Set key interpolation to linear"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyLinear"),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->SetInterpTangentMode(RCIM_Linear, RCTM_Auto); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanSetInterpTangentMode(); }),
			FIsActionChecked::CreateLambda([=]{ return Shared->IsInterpTangentModeSelected(RCIM_Linear, RCTM_Auto); }) ),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetKeyInterpolationConstant", "Constant"),
		LOCTEXT("SetKeyInterpolationConstantTooltip", "Set key interpolation to constant"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyConstant"),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->SetInterpTangentMode(RCIM_Constant, RCTM_Auto); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanSetInterpTangentMode(); }),
			FIsActionChecked::CreateLambda([=]{ return Shared->IsInterpTangentModeSelected(RCIM_Constant, RCTM_Auto); }) ),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.EndSection(); // SequencerInterpolation

	MenuBuilder.BeginSection("Key Editing", LOCTEXT("KeyEditingSectionMenus", "Key Editing"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ReduceKeysSection", "Reduce Keys"),
		LOCTEXT("ReduceKeysTooltip", "Reduce keys in this section"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Shared->ReduceKeys(); }),
			FCanExecuteAction::CreateLambda([=]{ return Shared->CanReduceKeys(); }))
	);

	auto OnReduceKeysToleranceChanged = [=](float NewValue) {
		Sequencer->GetSequencerSettings()->SetReduceKeysTolerance(NewValue);
	};

	MenuBuilder.AddWidget(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
		+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpinBox<float>)
				.Style(&FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("Sequencer.HyperlinkSpinBox"))
				.OnValueCommitted_Lambda([=](float Value, ETextCommit::Type) { OnReduceKeysToleranceChanged(Value); })
				.OnValueChanged_Lambda(OnReduceKeysToleranceChanged)
				.MinValue(0)
				.MaxValue(TOptional<float>())
				.Value_Lambda([=]() -> float {
				return Sequencer->GetSequencerSettings()->GetReduceKeysTolerance();
				})
			],
		LOCTEXT("ReduceKeysTolerance", "Tolerance"));

	MenuBuilder.EndSection();
}

FMovieSceneBlendTypeField FSectionContextMenu::GetSupportedBlendTypes() const
{
	FMovieSceneBlendTypeField BlendTypes = FMovieSceneBlendTypeField::All();

	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		if (UMovieSceneSection* Section = WeakSection.Get())
		{
			// Remove unsupported blend types
			BlendTypes.Remove(Section->GetSupportedBlendTypes().Invert());
		}
	}

	return BlendTypes;
}

void FSectionContextMenu::AddOrderMenu(FMenuBuilder& MenuBuilder)
{
	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FSectionContextMenu> Shared = AsShared();

	MenuBuilder.AddMenuEntry(LOCTEXT("BringToFront", "Bring To Front"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([=]{ return Shared->BringToFront(); })));

	MenuBuilder.AddMenuEntry(LOCTEXT("SendToBack", "Send To Back"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([=]{ return Shared->SendToBack(); })));

	MenuBuilder.AddMenuEntry(LOCTEXT("BringForward", "Bring Forward"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([=]{ return Shared->BringForward(); })));

	MenuBuilder.AddMenuEntry(LOCTEXT("SendBackward", "Send Backward"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([=]{ return Shared->SendBackward(); })));
}

void FSectionContextMenu::AddBlendTypeMenu(FMenuBuilder& MenuBuilder)
{
	TArray<TWeakObjectPtr<UMovieSceneSection>> Sections;

	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		if (WeakSection.IsValid())
		{
			Sections.Add(WeakSection);
		}
	}

	TWeakPtr<FSequencer> WeakSequencer = Sequencer;
	FSequencerUtilities::PopulateMenu_SetBlendType(MenuBuilder, Sections, WeakSequencer);
}

void FSectionContextMenu::SelectAllKeys()
{
	for (const TWeakObjectPtr<UMovieSceneSection>& WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		UMovieSceneSection* Section = WeakSection.Get();
		TOptional<FSectionHandle> SectionHandle = Sequencer->GetNodeTree()->GetSectionHandle(Section);
		if (!SectionHandle)
		{
			continue;
		}

		FSectionLayout Layout(*SectionHandle->GetTrackNode(), SectionHandle->GetSectionIndex());
		for (const FSectionLayoutElement& Element : Layout.GetElements())
		{
			for (TSharedRef<IKeyArea> KeyArea : Element.GetKeyAreas())
			{
				TArray<FKeyHandle> Handles;
				KeyArea->GetKeyHandles(Handles);

				for (FKeyHandle KeyHandle : Handles)
				{
					FSequencerSelectedKey SelectKey(*Section, KeyArea, KeyHandle);
					Sequencer->GetSelection().AddToSelection(SelectKey);
				}
			}
		}
	}
}

void FSectionContextMenu::CopyAllKeys()
{
	SelectAllKeys();
	Sequencer->CopySelectedKeys();
}

void FSectionContextMenu::TogglePrimeForRecording() const
{
	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(WeakSection.Get());
		if (SubSection)
		{
			SubSection->SetAsRecording(SubSection != UMovieSceneSubSection::GetRecordingSection());
			break;
		}
	}
}


bool FSectionContextMenu::IsPrimedForRecording() const
{
	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(WeakSection.Get());
		if (SubSection)
		{
			return SubSection == UMovieSceneSubSection::GetRecordingSection();
		}
	}

	return false;
}

bool FSectionContextMenu::CanPrimeForRecording() const
{
	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		UMovieSceneSubSection* SubSection = ExactCast<UMovieSceneSubSection>(WeakSection.Get());
		if (SubSection)
		{
			return true;
		}
	}

	return false;
}


void FSectionContextMenu::SetSectionToKey()
{
	if (Sequencer->GetSelection().GetSelectedSections().Num() != 1)
	{
		return;
	}

	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		if (UMovieSceneSection* Section = WeakSection.Get())
		{
			UMovieSceneTrack* Track = Section->GetTypedOuter<UMovieSceneTrack>();
			if (Track)
			{
				FScopedTransaction Transaction(LOCTEXT("SetSectionToKey", "Set Section To Key"));
				Track->Modify();
				Track->SetSectionToKey(Section);
			}
		}
		break;
	}
}

bool FSectionContextMenu::CanSetSectionToKey() const
{
	if (Sequencer->GetSelection().GetSelectedSections().Num() != 1)
	{
		return false;
	}

	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
	{
		if (UMovieSceneSection* Section = WeakSection.Get())
		{
			UMovieSceneTrack* Track = Section->GetTypedOuter<UMovieSceneTrack>();
			if (Track && Section->GetBlendType().IsValid() && (Section->GetBlendType().Get() == EMovieSceneBlendType::Absolute || Section->GetBlendType().Get() == EMovieSceneBlendType::Additive))
			{
				return true;
			}
		}

		break;
	}
	return false;
}

bool FSectionContextMenu::CanSelectAllKeys() const
{
	for (const TTuple<FName, TArray<FMovieSceneChannelHandle>>& Pair : ChannelsByType)
	{
		for (const FMovieSceneChannelHandle& Handle : Pair.Value)
		{
			const FMovieSceneChannel* Channel = Handle.Get();
			if (Channel && Channel->GetNumKeys() != 0)
			{
				return true;
			}
		}
	}
	return false;
}

void FSectionContextMenu::AutoSizeSection()
{
	FScopedTransaction AutoSizeSectionTransaction(LOCTEXT("AutoSizeSection_Transaction", "Auto Size Section"));

	for (auto Section : Sequencer->GetSelection().GetSelectedSections())
	{
		if (Section.IsValid() && Section->GetAutoSizeRange().IsSet())
		{
			TOptional<TRange<FFrameNumber> > DefaultSectionLength = Section->GetAutoSizeRange();

			if (DefaultSectionLength.IsSet())
			{
				Section->SetRange(DefaultSectionLength.GetValue());
			}
		}
	}

	Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
}


void FSectionContextMenu::ReduceKeys()
{
	FScopedTransaction ReduceKeysTransaction(LOCTEXT("ReduceKeys_Transaction", "Reduce Keys"));

	TSet<TSharedPtr<IKeyArea> > KeyAreas;
	for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
	}

	if (KeyAreas.Num() == 0)
	{
		const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedNodes = Sequencer->GetSelection().GetNodesWithSelectedKeysOrSections();
		for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : SelectedNodes)
		{
			SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
		}
	}


	FKeyDataOptimizationParams Params;
	Params.bAutoSetInterpolation = true;
	Params.Tolerance = Sequencer->GetSequencerSettings()->GetReduceKeysTolerance();

	for (TSharedPtr<IKeyArea> KeyArea : KeyAreas)
	{
		if (KeyArea.IsValid())
		{
			UMovieSceneSection* Section = KeyArea->GetOwningSection();
			if (Section)
			{
				Section->Modify();

				for (const FMovieSceneChannelEntry& Entry : Section->GetChannelProxy().GetAllEntries())
				{
					for (FMovieSceneChannel* Channel : Entry.GetChannels())
					{
						Channel->Optimize(Params);
					}
				}
			}
		}
	}

	Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::TrackValueChanged );
}

bool FSectionContextMenu::CanAutoSize() const
{
	for (auto Section : Sequencer->GetSelection().GetSelectedSections())
	{
		if (Section.IsValid() && Section->GetAutoSizeRange().IsSet())
		{
			return true;
		}
	}
	return false;
}

bool FSectionContextMenu::CanReduceKeys() const
{
	TSet<TSharedPtr<IKeyArea> > KeyAreas;
	for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
	}

	if (KeyAreas.Num() == 0)
	{
		const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedNodes = Sequencer->GetSelection().GetNodesWithSelectedKeysOrSections();
		for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : SelectedNodes)
		{
			SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
		}
	}

	return KeyAreas.Num() != 0;
}

void FSectionContextMenu::SetInterpTangentMode(ERichCurveInterpMode InterpMode, ERichCurveTangentMode TangentMode)
{
	FScopedTransaction SetInterpTangentModeTransaction(LOCTEXT("SetInterpTangentMode_Transaction", "Set Interpolation and Tangent Mode"));

	TSet<TSharedPtr<IKeyArea> > KeyAreas;
	for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
	}

	if (KeyAreas.Num() == 0)
	{
		const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedNodes = Sequencer->GetSelection().GetNodesWithSelectedKeysOrSections();
		for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : SelectedNodes)
		{
			SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
		}
	}

	bool bAnythingChanged = false;

	for (TSharedPtr<IKeyArea> KeyArea : KeyAreas)
	{
		if (KeyArea.IsValid())
		{
			UMovieSceneSection* Section = KeyArea->GetOwningSection();
			if (Section)
			{
				Section->Modify();

				for (FMovieSceneFloatChannel* FloatChannel : Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>())
				{
					TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = FloatChannel->GetData();
					TArrayView<FMovieSceneFloatValue> Values = ChannelData.GetValues();

					for (int32 KeyIndex = 0; KeyIndex < FloatChannel->GetNumKeys(); ++KeyIndex)
					{
						Values[KeyIndex].InterpMode = InterpMode;
						Values[KeyIndex].TangentMode = TangentMode;
						bAnythingChanged = true;
					}

					FloatChannel->AutoSetTangents();
				}
			}
		}
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::TrackValueChanged );
	}
}

bool FSectionContextMenu::CanSetInterpTangentMode() const
{
	TSet<TSharedPtr<IKeyArea> > KeyAreas;
	for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
	}

	if (KeyAreas.Num() == 0)
	{
		const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedNodes = Sequencer->GetSelection().GetNodesWithSelectedKeysOrSections();
		for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : SelectedNodes)
		{
			SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
		}
	}

	for (TSharedPtr<IKeyArea> KeyArea : KeyAreas)
	{
		if (KeyArea.IsValid())
		{
			UMovieSceneSection* Section = KeyArea->GetOwningSection();
			if (Section)
			{
				return Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num() != 0;
			}
		}
	}

	return false;
}
				

bool FSectionContextMenu::IsInterpTangentModeSelected(ERichCurveInterpMode InterpMode, ERichCurveTangentMode TangentMode) const
{
	TSet<TSharedPtr<IKeyArea> > KeyAreas;
	for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
	}

	if (KeyAreas.Num() == 0)
	{
		const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedNodes = Sequencer->GetSelection().GetNodesWithSelectedKeysOrSections();
		for (const TSharedRef<FSequencerDisplayNode>& DisplayNode : SelectedNodes)
		{
			SequencerHelpers::GetAllKeyAreas(DisplayNode, KeyAreas);
		}
	}

	int32 NumKeys = 0;
	for (TSharedPtr<IKeyArea> KeyArea : KeyAreas)
	{
		if (KeyArea.IsValid())
		{
			UMovieSceneSection* Section = KeyArea->GetOwningSection();
			if (Section)
			{
				for (FMovieSceneFloatChannel* FloatChannel : Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>())
				{
					TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = FloatChannel->GetData();
					TArrayView<FMovieSceneFloatValue> Values = ChannelData.GetValues();

					NumKeys += FloatChannel->GetNumKeys();
					for (int32 KeyIndex = 0; KeyIndex < FloatChannel->GetNumKeys(); ++KeyIndex)
					{
						if (Values[KeyIndex].InterpMode != InterpMode || Values[KeyIndex].TangentMode != TangentMode)
						{
							return false;
						}
					}
				}
			}
		}
	}

	return NumKeys != 0;
}


void FSectionContextMenu::ToggleSectionActive()
{
	FScopedTransaction ToggleSectionActiveTransaction( LOCTEXT("ToggleSectionActive_Transaction", "Toggle Section Active") );
	bool bIsActive = !IsSectionActive();
	bool bAnythingChanged = false;

	for (auto Section : Sequencer->GetSelection().GetSelectedSections())
	{
		if (Section.IsValid())
		{
			bAnythingChanged = true;
			Section->Modify();
			Section->SetIsActive(bIsActive);
		}
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::TrackValueChanged );
	}
	else
	{
		ToggleSectionActiveTransaction.Cancel();
	}
}

bool FSectionContextMenu::IsSectionActive() const
{
	// Active only if all are active
	for (auto Section : Sequencer->GetSelection().GetSelectedSections())
	{
		if (Section.IsValid() && !Section->IsActive())
		{
			return false;
		}
	}

	return true;
}


void FSectionContextMenu::ToggleSectionLocked()
{
	FScopedTransaction ToggleSectionLockedTransaction( NSLOCTEXT("Sequencer", "ToggleSectionLocked_Transaction", "Toggle Section Locked") );
	bool bIsLocked = !IsSectionLocked();
	bool bAnythingChanged = false;

	for (auto Section : Sequencer->GetSelection().GetSelectedSections())
	{
		if (Section.IsValid())
		{
			bAnythingChanged = true;
			Section->Modify();
			Section->SetIsLocked(bIsLocked);
		}
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::TrackValueChanged );
	}
	else
	{
		ToggleSectionLockedTransaction.Cancel();
	}
}


bool FSectionContextMenu::IsSectionLocked() const
{
	// Locked only if all are locked
	for (auto Section : Sequencer->GetSelection().GetSelectedSections())
	{
		if (Section.IsValid() && !Section->IsLocked())
		{
			return false;
		}
	}

	return true;
}


void FSectionContextMenu::DeleteSection()
{
	Sequencer->DeleteSections(Sequencer->GetSelection().GetSelectedSections());
}


/** Information pertaining to a specific row in a track, required for z-ordering operations */
struct FTrackSectionRow
{
	/** The minimum z-order value for all the sections in this row */
	int32 MinOrderValue;

	/** The maximum z-order value for all the sections in this row */
	int32 MaxOrderValue;

	/** All the sections contained in this row */
	TArray<UMovieSceneSection*> Sections;

	/** A set of sections that are to be operated on */
	TSet<UMovieSceneSection*> SectionToReOrder;

	void AddSection(UMovieSceneSection* InSection)
	{
		Sections.Add(InSection);
		MinOrderValue = FMath::Min(MinOrderValue, InSection->GetOverlapPriority());
		MaxOrderValue = FMath::Max(MaxOrderValue, InSection->GetOverlapPriority());
	}
};


/** Generate the data required for re-ordering rows based on the current sequencer selection */
/** @note: Produces a map of track -> rows, keyed on row index. Only returns rows that contain selected sections */
TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>> GenerateTrackRowsFromSelection(FSequencer& Sequencer)
{
	TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>> TrackRows;

	for (const TWeakObjectPtr<UMovieSceneSection>& SectionPtr : Sequencer.GetSelection().GetSelectedSections())
	{
		UMovieSceneSection* Section = SectionPtr.Get();
		if (!Section)
		{
			continue;
		}

		UMovieSceneTrack* Track = Section->GetTypedOuter<UMovieSceneTrack>();
		if (!Track)
		{
			continue;
		}

		FTrackSectionRow& Row = TrackRows.FindOrAdd(Track).FindOrAdd(Section->GetRowIndex());
		Row.SectionToReOrder.Add(Section);
	}

	// Now ensure all rows that we're operating on are fully populated
	for (auto& Pair : TrackRows)
	{
		UMovieSceneTrack* Track = Pair.Key;
		for (auto& RowPair : Pair.Value)
		{
			const int32 RowIndex = RowPair.Key;
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (Section->GetRowIndex() == RowIndex)
				{
					RowPair.Value.AddSection(Section);
				}
			}
		}
	}

	return TrackRows;
}


/** Modify all the sections contained within the specified data structure */
void ModifySections(TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>>& TrackRows)
{
	for (auto& Pair : TrackRows)
	{
		UMovieSceneTrack* Track = Pair.Key;
		for (auto& RowPair : Pair.Value)
		{
			for (UMovieSceneSection* Section : RowPair.Value.Sections)
			{
				Section->Modify();
			}
		}
	}
}


void FSectionContextMenu::BringToFront()
{
	TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>> TrackRows = GenerateTrackRowsFromSelection(*Sequencer);
	if (TrackRows.Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("BringToFrontTransaction", "Bring to Front"));
	ModifySections(TrackRows);

	for (auto& Pair : TrackRows)
	{
		UMovieSceneTrack* Track = Pair.Key;
		TMap<int32, FTrackSectionRow>& Rows = Pair.Value;

		for (auto& RowPair : Rows)
		{
			FTrackSectionRow& Row = RowPair.Value;

			Row.Sections.StableSort([&](UMovieSceneSection& A, UMovieSceneSection& B){
				bool bIsActiveA = Row.SectionToReOrder.Contains(&A);
				bool bIsActiveB = Row.SectionToReOrder.Contains(&B);

				// Sort secondarily on overlap priority
				if (bIsActiveA == bIsActiveB)
				{
					return A.GetOverlapPriority() < B.GetOverlapPriority();
				}
				// Sort and primarily on whether we're sending to the back or not (bIsActive)
				else
				{
					return !bIsActiveA;
				}
			});

			int32 CurrentPriority = Row.MinOrderValue;
			for (UMovieSceneSection* Section : Row.Sections)
			{
				Section->SetOverlapPriority(CurrentPriority++);
			}
		}
	}

	Sequencer->SetLocalTimeDirectly(Sequencer->GetLocalTime().Time);
}


void FSectionContextMenu::SendToBack()
{
	TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>> TrackRows = GenerateTrackRowsFromSelection(*Sequencer);
	if (TrackRows.Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SendToBackTransaction", "Send to Back"));
	ModifySections(TrackRows);

	for (auto& Pair : TrackRows)
	{
		UMovieSceneTrack* Track = Pair.Key;
		TMap<int32, FTrackSectionRow>& Rows = Pair.Value;

		for (auto& RowPair : Rows)
		{
			FTrackSectionRow& Row = RowPair.Value;

			Row.Sections.StableSort([&](UMovieSceneSection& A, UMovieSceneSection& B){
				bool bIsActiveA = Row.SectionToReOrder.Contains(&A);
				bool bIsActiveB = Row.SectionToReOrder.Contains(&B);

				// Sort secondarily on overlap priority
				if (bIsActiveA == bIsActiveB)
				{
					return A.GetOverlapPriority() < B.GetOverlapPriority();
				}
				// Sort and primarily on whether we're bringing to the front or not (bIsActive)
				else
				{
					return bIsActiveA;
				}
			});

			int32 CurrentPriority = Row.MinOrderValue;
			for (UMovieSceneSection* Section : Row.Sections)
			{
				Section->SetOverlapPriority(CurrentPriority++);
			}
		}
	}

	Sequencer->SetLocalTimeDirectly(Sequencer->GetLocalTime().Time);
}


void FSectionContextMenu::BringForward()
{
	TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>> TrackRows = GenerateTrackRowsFromSelection(*Sequencer);
	if (TrackRows.Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("BringForwardTransaction", "Bring Forward"));
	ModifySections(TrackRows);

	for (auto& Pair : TrackRows)
	{
		UMovieSceneTrack* Track = Pair.Key;
		TMap<int32, FTrackSectionRow>& Rows = Pair.Value;

		for (auto& RowPair : Rows)
		{
			FTrackSectionRow& Row = RowPair.Value;

			Row.Sections.Sort([&](UMovieSceneSection& A, UMovieSceneSection& B){
				return A.GetOverlapPriority() < B.GetOverlapPriority();
			});

			for (int32 SectionIndex = Row.Sections.Num() - 1; SectionIndex > 0; --SectionIndex)
			{
				UMovieSceneSection* ThisSection = Row.Sections[SectionIndex];
				if (Row.SectionToReOrder.Contains(ThisSection))
				{
					UMovieSceneSection* OtherSection = Row.Sections[SectionIndex + 1];

					Row.Sections.Swap(SectionIndex, SectionIndex+1);

					const int32 SwappedPriority = OtherSection->GetOverlapPriority();
					OtherSection->SetOverlapPriority(ThisSection->GetOverlapPriority());
					ThisSection->SetOverlapPriority(SwappedPriority);
				}
			}
		}
	}

	Sequencer->SetLocalTimeDirectly(Sequencer->GetLocalTime().Time);
}


void FSectionContextMenu::SendBackward()
{
	TMap<UMovieSceneTrack*, TMap<int32, FTrackSectionRow>> TrackRows = GenerateTrackRowsFromSelection(*Sequencer);
	if (TrackRows.Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SendBackwardTransaction", "Send Backward"));
	ModifySections(TrackRows);

	for (auto& Pair : TrackRows)
	{
		UMovieSceneTrack* Track = Pair.Key;
		TMap<int32, FTrackSectionRow>& Rows = Pair.Value;

		for (auto& RowPair : Rows)
		{
			FTrackSectionRow& Row = RowPair.Value;

			Row.Sections.Sort([&](UMovieSceneSection& A, UMovieSceneSection& B){
				return A.GetOverlapPriority() < B.GetOverlapPriority();
			});

			for (int32 SectionIndex = 1; SectionIndex < Row.Sections.Num(); ++SectionIndex)
			{
				UMovieSceneSection* ThisSection = Row.Sections[SectionIndex];
				if (Row.SectionToReOrder.Contains(ThisSection))
				{
					UMovieSceneSection* OtherSection = Row.Sections[SectionIndex - 1];

					Row.Sections.Swap(SectionIndex, SectionIndex - 1);

					const int32 SwappedPriority = OtherSection->GetOverlapPriority();
					OtherSection->SetOverlapPriority(ThisSection->GetOverlapPriority());
					ThisSection->SetOverlapPriority(SwappedPriority);
				}
			}
		}
	}

	Sequencer->SetLocalTimeDirectly(Sequencer->GetLocalTime().Time);
}


bool FPasteContextMenu::BuildMenu(FMenuBuilder& MenuBuilder, FSequencer& InSequencer, const FPasteContextMenuArgs& Args)
{
	TSharedRef<FPasteContextMenu> Menu = MakeShareable(new FPasteContextMenu(InSequencer, Args));
	Menu->Setup();
	if (!Menu->IsValidPaste())
	{
		return false;
	}

	Menu->PopulateMenu(MenuBuilder);
	return true;
}


TSharedRef<FPasteContextMenu> FPasteContextMenu::CreateMenu(FSequencer& InSequencer, const FPasteContextMenuArgs& Args)
{
	TSharedRef<FPasteContextMenu> Menu = MakeShareable(new FPasteContextMenu(InSequencer, Args));
	Menu->Setup();
	return Menu;
}


TArray<TSharedRef<FSequencerSectionKeyAreaNode>> KeyAreaNodesBuffer;

void FPasteContextMenu::GatherPasteDestinationsForNode(FSequencerDisplayNode& InNode, UMovieSceneSection* InSection, const FName& CurrentScope, TMap<FName, FSequencerClipboardReconciler>& Map)
{
	KeyAreaNodesBuffer.Reset();
	if (InNode.GetType() == ESequencerNode::KeyArea)
	{
		KeyAreaNodesBuffer.Add(StaticCastSharedRef<FSequencerSectionKeyAreaNode>(InNode.AsShared()));
	}
	else
	{
		InNode.GetChildKeyAreaNodesRecursively(KeyAreaNodesBuffer);
	}

	if (!KeyAreaNodesBuffer.Num())
	{
		return;
	}

	FName ThisScope;
	{
		FString ThisScopeString;
		if (!CurrentScope.IsNone())
		{
			ThisScopeString.Append(CurrentScope.ToString());
			ThisScopeString.AppendChar('.');
		}
		ThisScopeString.Append(InNode.GetDisplayName().ToString());
		ThisScope = *ThisScopeString;
	}

	FSequencerClipboardReconciler* Reconciler = Map.Find(ThisScope);
	if (!Reconciler)
	{
		Reconciler = &Map.Add(ThisScope, FSequencerClipboardReconciler(Args.Clipboard.ToSharedRef()));
	}

	FSequencerClipboardPasteGroup Group = Reconciler->AddDestinationGroup();
	for (const TSharedRef<FSequencerSectionKeyAreaNode>& KeyAreaNode : KeyAreaNodesBuffer)
	{
		TSharedPtr<IKeyArea> KeyArea = KeyAreaNode->GetKeyArea(InSection);
		if (KeyArea.IsValid())
		{
			Group.Add(*KeyArea.Get());
		}
	}

	// Add children
	for (const TSharedPtr<FSequencerDisplayNode> Child : InNode.GetChildNodes())
	{
		GatherPasteDestinationsForNode(*Child, InSection, ThisScope, Map);
	}
}


void GetFullNodePath(FSequencerDisplayNode& InNode, FString& Path)
{
	TSharedPtr<FSequencerDisplayNode> Parent = InNode.GetParent();
	if (Parent.IsValid())
	{
		GetFullNodePath(*Parent, Path);
	}

	if (!Path.IsEmpty())
	{
		Path.AppendChar('.');
	}

	Path.Append(InNode.GetDisplayName().ToString());
}


TSharedPtr<FSequencerTrackNode> GetTrackFromNode(FSequencerDisplayNode& InNode, FString& Scope)
{
	if (InNode.GetType() == ESequencerNode::Track)
	{
		return StaticCastSharedRef<FSequencerTrackNode>(InNode.AsShared());
	}
	else if (InNode.GetType() == ESequencerNode::Object)
	{
		return nullptr;
	}

	TSharedPtr<FSequencerDisplayNode> Parent = InNode.GetParent();
	if (Parent.IsValid())
	{
		TSharedPtr<FSequencerTrackNode> Track = GetTrackFromNode(*Parent, Scope);
		if (Track.IsValid())
		{
			FString ThisScope = InNode.GetDisplayName().ToString();
			if (!Scope.IsEmpty())
			{
				ThisScope.AppendChar('.');
				ThisScope.Append(Scope);
				Scope = MoveTemp(ThisScope);
			}
			return Track;
		}
	}

	return nullptr;
}


void FPasteContextMenu::Setup()
{
	if (!Args.Clipboard.IsValid())
	{
		if (Sequencer->GetClipboardStack().Num() != 0)
		{
			Args.Clipboard = Sequencer->GetClipboardStack().Last();
		}
		else
		{
			return;
		}
	}

	// Gather a list of sections we want to paste into
	TArray<FSectionHandle> SectionHandles;

	if (Args.DestinationNodes.Num())
	{
		// If we have exactly one channel to paste, first check if we have exactly one valid target channel selected to support copying between channels e.g. from Tranform.x to Transform.y
		if (Args.Clipboard->GetKeyTrackGroups().Num() == 1)
		{
			for (const TSharedRef<FSequencerDisplayNode>& Node : Args.DestinationNodes)
			{
				if (Node->GetType() != ESequencerNode::KeyArea && Node->GetType() != ESequencerNode::Category)
				{
					continue;
				}

				FString Scope;
				TSharedPtr<FSequencerTrackNode> TrackNode = GetTrackFromNode(*Node, Scope);
				if (!TrackNode.IsValid())
				{
					continue;
				}

				FPasteDestination& Destination = PasteDestinations[PasteDestinations.AddDefaulted()];

				TArray<UMovieSceneSection*> Sections;
				for (TSharedRef<ISequencerSection> Section : TrackNode->GetSections())
				{
					if (Section.Get().GetSectionObject())
					{
						GatherPasteDestinationsForNode(*Node, Section.Get().GetSectionObject(), NAME_None, Destination.Reconcilers);
					}
				}

				// Reconcile and remove invalid pastes
				for (auto It = Destination.Reconcilers.CreateIterator(); It; ++It)
				{
					if (!It.Value().Reconcile() || !It.Value().CanAutoPaste())
					{
						It.RemoveCurrent();
					}
				}

				if (!Destination.Reconcilers.Num())
				{
					PasteDestinations.RemoveAt(PasteDestinations.Num() - 1, 1, false);
				}
			}

			int32 ExactMatchCount = 0;
			for (int32 PasteDestinationIndex = 0; PasteDestinationIndex < PasteDestinations.Num(); ++PasteDestinationIndex)
			{
				if (PasteDestinations[PasteDestinationIndex].Reconcilers.Num() == 1)
				{
					++ExactMatchCount;
				}
			}

			if (ExactMatchCount > 0 && ExactMatchCount == PasteDestinations.Num())
			{
				bPasteFirstOnly = false;
				return;
			}

			// Otherwise reset our list and move on
			PasteDestinations.Reset();
		}

		// Build a list of sections based on selected tracks
		for (const TSharedRef<FSequencerDisplayNode>& Node : Args.DestinationNodes)
		{
			FString Scope;
			TSharedPtr<FSequencerTrackNode> TrackNode = GetTrackFromNode(*Node, Scope);
			if (!TrackNode.IsValid())
			{
				continue;
			}

			TArray<UMovieSceneSection*> Sections;
			for (TSharedRef<ISequencerSection> Section : TrackNode->GetSections())
			{
				if (Section.Get().GetSectionObject())
				{
					Sections.Add(Section.Get().GetSectionObject());
				}
			}

			UMovieSceneSection* Section = MovieSceneHelpers::FindNearestSectionAtTime(Sections, Args.PasteAtTime);
			int32 SectionIndex = INDEX_NONE;
			if (Section)
			{
				SectionIndex = Sections.IndexOfByKey(Section);
			}

			if (SectionIndex != INDEX_NONE)
			{
				SectionHandles.Add(FSectionHandle(TrackNode.ToSharedRef(), SectionIndex));
			}
		}
	}
	else
	{
		// Use the selected sections
		for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sequencer->GetSelection().GetSelectedSections())
		{
			if (TOptional<FSectionHandle> SectionHandle = Sequencer->GetNodeTree()->GetSectionHandle(WeakSection.Get()))
			{
				SectionHandles.Add(SectionHandle.GetValue());
			}
		}
	}

	TMap<FName, TArray<FSectionHandle>> SectionsByType;
	for (const FSectionHandle& Section : SectionHandles)
	{
		UMovieSceneTrack* Track = Section.GetTrackNode()->GetTrack();
		if (Track)
		{
			SectionsByType.FindOrAdd(Track->GetClass()->GetFName()).Add(Section);
		}
	}

	for (const TTuple<FName, TArray<FSectionHandle>>& Pair : SectionsByType)
	{
		FPasteDestination& Destination = PasteDestinations[PasteDestinations.AddDefaulted()];
		if (Pair.Value.Num() == 1)
		{
			FString Path;
			GetFullNodePath(*Pair.Value[0].GetTrackNode(), Path);
			Destination.Name = FText::FromString(Path);
		}
		else
		{
			Destination.Name = FText::Format(LOCTEXT("PasteMenuHeaderFormat", "{0} ({1} tracks)"), FText::FromName(Pair.Key), FText::AsNumber(Pair.Value.Num()));
		}

		for (const FSectionHandle& Section : Pair.Value)
		{
			GatherPasteDestinationsForNode(*Section.GetTrackNode(), Section.GetSectionObject(), NAME_None, Destination.Reconcilers);
		}

		// Reconcile and remove invalid pastes
		for (auto It = Destination.Reconcilers.CreateIterator(); It; ++It)
		{
			if (!It.Value().Reconcile())
			{
				It.RemoveCurrent();
			}
		}
		if (!Destination.Reconcilers.Num())
		{
			PasteDestinations.RemoveAt(PasteDestinations.Num() - 1, 1, false);
		}
	}
}


bool FPasteContextMenu::IsValidPaste() const
{
	return Args.Clipboard.IsValid() && PasteDestinations.Num() != 0;
}


void FPasteContextMenu::PopulateMenu(FMenuBuilder& MenuBuilder)
{
	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FPasteContextMenu> Shared = AsShared();

	bool bElevateMenu = PasteDestinations.Num() == 1;
	for (int32 Index = 0; Index < PasteDestinations.Num(); ++Index)
	{
		if (bElevateMenu)
		{
			MenuBuilder.BeginSection("PasteInto", FText::Format(LOCTEXT("PasteIntoTitle", "Paste Into {0}"), PasteDestinations[Index].Name));
			AddPasteMenuForTrackType(MenuBuilder, Index);
			MenuBuilder.EndSection();
			break;
		}

		MenuBuilder.AddSubMenu(
			PasteDestinations[Index].Name,
			FText(),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ Shared->AddPasteMenuForTrackType(SubMenuBuilder, Index); })
		);
	}
}


void FPasteContextMenu::AddPasteMenuForTrackType(FMenuBuilder& MenuBuilder, int32 DestinationIndex)
{
	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FPasteContextMenu> Shared = AsShared();

	for (auto& Pair : PasteDestinations[DestinationIndex].Reconcilers)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromName(Pair.Key),
			FText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([=](){ 
				TSet<FSequencerSelectedKey> NewSelection;
				Shared->BeginPasteInto();
				const bool bAnythingPasted = Shared->PasteInto(DestinationIndex, Pair.Key, NewSelection); 
				Shared->EndPasteInto(bAnythingPasted, NewSelection);
				})
			)
		);
	}
}


bool FPasteContextMenu::AutoPaste()
{
	TSet<FSequencerSelectedKey> NewSelection;
	BeginPasteInto();

	bool bAnythingPasted = false;
	for (int32 PasteDestinationIndex = 0; PasteDestinationIndex < PasteDestinations.Num(); ++PasteDestinationIndex)
	{
		for (auto& Pair : PasteDestinations[PasteDestinationIndex].Reconcilers)
		{
			if (Pair.Value.CanAutoPaste())
			{
				if (PasteInto(PasteDestinationIndex, Pair.Key, NewSelection))
				{
					bAnythingPasted = true;

					if (bPasteFirstOnly)
					{
						break;
					}
				}
			}
		}
	}

	EndPasteInto(bAnythingPasted, NewSelection);

	return bAnythingPasted;
}

void FPasteContextMenu::BeginPasteInto()
{
	GEditor->BeginTransaction(LOCTEXT("PasteKeysTransaction", "Paste Keys"));
}

void FPasteContextMenu::EndPasteInto(bool bAnythingPasted, const TSet<FSequencerSelectedKey>& NewSelection)
{
	if (!bAnythingPasted)
	{
		GEditor->CancelTransaction(0);
		return;
	}

	GEditor->EndTransaction();

	SSequencerSection::ThrobKeySelection();

	FSequencerSelection& Selection = Sequencer->GetSelection();
	Selection.SuspendBroadcast();
	Selection.EmptySelectedSections();
	Selection.EmptySelectedKeys();

	for (const FSequencerSelectedKey& Key : NewSelection)
	{
		Selection.AddToSelection(Key);
	}
	Selection.ResumeBroadcast();
	Selection.GetOnKeySelectionChanged().Broadcast();
	Selection.GetOnSectionSelectionChanged().Broadcast();

	Sequencer->OnClipboardUsed(Args.Clipboard);
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
}

bool FPasteContextMenu::PasteInto(int32 DestinationIndex, FName KeyAreaName, TSet<FSequencerSelectedKey>& NewSelection)
{
	FSequencerClipboardReconciler& Reconciler = PasteDestinations[DestinationIndex].Reconcilers[KeyAreaName];

	FSequencerPasteEnvironment PasteEnvironment;
	PasteEnvironment.TickResolution = Sequencer->GetFocusedTickResolution();
	PasteEnvironment.CardinalTime = Args.PasteAtTime;
	PasteEnvironment.OnKeyPasted = [&](FKeyHandle Handle, IKeyArea& KeyArea){
		NewSelection.Add(FSequencerSelectedKey(*KeyArea.GetOwningSection(), KeyArea.AsShared(), Handle));
	};

	return Reconciler.Paste(PasteEnvironment);
}


bool FPasteFromHistoryContextMenu::BuildMenu(FMenuBuilder& MenuBuilder, FSequencer& InSequencer, const FPasteContextMenuArgs& Args)
{
	if (InSequencer.GetClipboardStack().Num() == 0)
	{
		return false;
	}

	TSharedRef<FPasteFromHistoryContextMenu> Menu = MakeShareable(new FPasteFromHistoryContextMenu(InSequencer, Args));
	Menu->PopulateMenu(MenuBuilder);
	return true;
}


TSharedPtr<FPasteFromHistoryContextMenu> FPasteFromHistoryContextMenu::CreateMenu(FSequencer& InSequencer, const FPasteContextMenuArgs& Args)
{
	if (InSequencer.GetClipboardStack().Num() == 0)
	{
		return nullptr;
	}

	return MakeShareable(new FPasteFromHistoryContextMenu(InSequencer, Args));
}


void FPasteFromHistoryContextMenu::PopulateMenu(FMenuBuilder& MenuBuilder)
{
	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FPasteFromHistoryContextMenu> Shared = AsShared();

	MenuBuilder.BeginSection("SequencerPasteHistory", LOCTEXT("PasteFromHistory", "Paste From History"));

	for (int32 Index = Sequencer->GetClipboardStack().Num() - 1; Index >= 0; --Index)
	{
		FPasteContextMenuArgs ThisPasteArgs = Args;
		ThisPasteArgs.Clipboard = Sequencer->GetClipboardStack()[Index];

		TSharedRef<FPasteContextMenu> PasteMenu = FPasteContextMenu::CreateMenu(*Sequencer, ThisPasteArgs);

		MenuBuilder.AddSubMenu(
			ThisPasteArgs.Clipboard->GetDisplayText(),
			FText(),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ PasteMenu->PopulateMenu(SubMenuBuilder); }),
			FUIAction (
				FExecuteAction(),
				FCanExecuteAction::CreateLambda([=]{ return PasteMenu->IsValidPaste(); })
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}

	MenuBuilder.EndSection();
}

void FEasingContextMenu::BuildMenu(FMenuBuilder& MenuBuilder, const TArray<FEasingAreaHandle>& InEasings, FSequencer& Sequencer, FFrameTime InMouseDownTime)
{
	TSharedRef<FEasingContextMenu> EasingMenu = MakeShareable(new FEasingContextMenu(InEasings, Sequencer));
	EasingMenu->PopulateMenu(MenuBuilder);

	MenuBuilder.AddMenuSeparator();

	FSectionContextMenu::BuildMenu(MenuBuilder, Sequencer, InMouseDownTime);
}

void FEasingContextMenu::PopulateMenu(FMenuBuilder& MenuBuilder)
{
	FText SectionText = Easings.Num() == 1 ? LOCTEXT("EasingCurve", "Easing Curve") : FText::Format(LOCTEXT("EasingCurvesFormat", "Easing Curves ({0} curves)"), FText::AsNumber(Easings.Num()));
	const bool bReadOnly = Algo::AnyOf(Easings, [](const FEasingAreaHandle& Handle) -> bool
		{
			const UMovieSceneSection* Section = Handle.WeakSection.Get();
			const UMovieSceneTrack* SectionTrack = Section->GetTypedOuter<UMovieSceneTrack>();
			FMovieSceneSupportsEasingParams Params(Section);
			return !EnumHasAllFlags(SectionTrack->SupportsEasing(Params), EMovieSceneTrackEasingSupportFlags::ManualEasing);
		});

	MenuBuilder.BeginSection("SequencerEasingEdit", SectionText);
	{
		// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
		TSharedRef<FEasingContextMenu> Shared = AsShared();

		auto OnBeginSliderMovement = [=]
		{
			GEditor->BeginTransaction(LOCTEXT("SetEasingTimeText", "Set Easing Length"));
		};
		auto OnEndSliderMovement = [=](double NewLength)
		{
			if (GEditor->IsTransactionActive())
			{
				GEditor->EndTransaction();
			}
		};
		auto OnValueCommitted = [=](double NewLength, ETextCommit::Type CommitInfo)
		{
			if (CommitInfo == ETextCommit::OnEnter || CommitInfo == ETextCommit::OnUserMovedFocus)
			{
				FScopedTransaction Transaction(LOCTEXT("SetEasingTimeText", "Set Easing Length"));
				Shared->OnUpdateLength((int32)NewLength);
			}
		};

		TSharedRef<SWidget> SpinBox = SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.f,0.f))
			[
				SNew(SBox)
				.HAlign(HAlign_Right)
				[
					SNew(SNumericEntryBox<double>)
					.SpinBoxStyle(&FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("Sequencer.HyperlinkSpinBox"))
					.EditableTextBoxStyle(&FEditorStyle::GetWidgetStyle<FEditableTextBoxStyle>("Sequencer.HyperlinkTextBox"))
					// Don't update the value when undetermined text changes
					.OnUndeterminedValueChanged_Lambda([](FText){})
					.AllowSpin(true)
					.IsEnabled(!bReadOnly)
					.MinValue(0.f)
					.MaxValue(TOptional<double>())
					.MaxSliderValue(TOptional<double>())
					.MinSliderValue(0.f)
					.Delta_Lambda([=]() -> double { return Sequencer->GetDisplayRateDeltaFrameCount(); })
					.Value_Lambda([=] {
						TOptional<int32> Current = Shared->GetCurrentLength();
						if (Current.IsSet())
						{
							return TOptional<double>(Current.GetValue());
						}
						return TOptional<double>();
					})
					.OnValueChanged_Lambda([=](double NewLength){ Shared->OnUpdateLength(NewLength); })
					.OnValueCommitted_Lambda(OnValueCommitted)
					.OnBeginSliderMovement_Lambda(OnBeginSliderMovement)
					.OnEndSliderMovement_Lambda(OnEndSliderMovement)
					.BorderForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
					.TypeInterface(Sequencer->GetNumericTypeInterface())
				]
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsEnabled(!bReadOnly)
				.IsChecked_Lambda([=]{ return Shared->GetAutoEasingCheckState(); })
				.OnCheckStateChanged_Lambda([=](ECheckBoxState CheckState){ return Shared->SetAutoEasing(CheckState == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AutomaticEasingText", "Auto?"))
				]
			];
		MenuBuilder.AddWidget(SpinBox, LOCTEXT("EasingAmountLabel", "Easing Length"));

		MenuBuilder.AddSubMenu(
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([=]{ return Shared->GetEasingTypeText(); })),
			LOCTEXT("EasingTypeToolTip", "Change the type of curve used for the easing"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ Shared->EasingTypeMenu(SubMenuBuilder); })
		);

		MenuBuilder.AddSubMenu(
			LOCTEXT("EasingOptions", "Options"),
			LOCTEXT("EasingOptionsToolTip", "Edit easing settings for this curve"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){ Shared->EasingOptionsMenu(SubMenuBuilder); })
		);
	}
	MenuBuilder.EndSection();
}

TOptional<int32> FEasingContextMenu::GetCurrentLength() const
{
	TOptional<int32> Value;

	for (const FEasingAreaHandle& Handle : Easings)
	{
		UMovieSceneSection* Section = Handle.WeakSection.Get();
		if (Section)
		{
			if (Handle.EasingType == ESequencerEasingType::In && Section->Easing.GetEaseInDuration() == Value.Get(Section->Easing.GetEaseInDuration()))
			{
				Value = Section->Easing.GetEaseInDuration();
			}
			else if (Handle.EasingType == ESequencerEasingType::Out && Section->Easing.GetEaseOutDuration() == Value.Get(Section->Easing.GetEaseOutDuration()))
			{
				Value = Section->Easing.GetEaseOutDuration();
			}
			else
			{
				return TOptional<int32>();
			}
		}
	}

	return Value;
}

void FEasingContextMenu::OnUpdateLength(int32 NewLength)
{
	for (const FEasingAreaHandle& Handle : Easings)
	{
		if (UMovieSceneSection* Section = Handle.WeakSection.Get())
		{
			Section->Modify();
			if (Handle.EasingType == ESequencerEasingType::In)
			{
				Section->Easing.bManualEaseIn = true;
				Section->Easing.ManualEaseInDuration = FMath::Min(UE::MovieScene::DiscreteSize(Section->GetRange()), NewLength);
			}
			else
			{
				Section->Easing.bManualEaseOut = true;
				Section->Easing.ManualEaseOutDuration = FMath::Min(UE::MovieScene::DiscreteSize(Section->GetRange()), NewLength);
			}
		}
	}
}

ECheckBoxState FEasingContextMenu::GetAutoEasingCheckState() const
{
	TOptional<bool> IsChecked;
	for (const FEasingAreaHandle& Handle : Easings)
	{
		if (UMovieSceneSection* Section = Handle.WeakSection.Get())
		{
			if (Handle.EasingType == ESequencerEasingType::In)
			{
				if (IsChecked.IsSet() && IsChecked.GetValue() != !Section->Easing.bManualEaseIn)
				{
					return ECheckBoxState::Undetermined;
				}
				IsChecked = !Section->Easing.bManualEaseIn;
			}
			else
			{
				if (IsChecked.IsSet() && IsChecked.GetValue() != !Section->Easing.bManualEaseOut)
				{
					return ECheckBoxState::Undetermined;
				}
				IsChecked = !Section->Easing.bManualEaseOut;
			}
		}
	}
	return IsChecked.IsSet() ? IsChecked.GetValue() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked : ECheckBoxState::Undetermined;
}

void FEasingContextMenu::SetAutoEasing(bool bAutoEasing)
{
	FScopedTransaction Transaction(LOCTEXT("SetAutoEasingText", "Set Automatic Easing"));

	TArray<UMovieSceneTrack*> AllTracks;

	for (const FEasingAreaHandle& Handle : Easings)
	{
		if (UMovieSceneSection* Section = Handle.WeakSection.Get())
		{
			AllTracks.AddUnique(Section->GetTypedOuter<UMovieSceneTrack>());

			Section->Modify();
			if (Handle.EasingType == ESequencerEasingType::In)
			{
				Section->Easing.bManualEaseIn = !bAutoEasing;
			}
			else
			{
				Section->Easing.bManualEaseOut = !bAutoEasing;
			}
		}
	}

	for (UMovieSceneTrack* Track : AllTracks)
	{
		Track->UpdateEasing();
	}
}

FText FEasingContextMenu::GetEasingTypeText() const
{
	FText CurrentText;
	UClass* ClassType = nullptr;
	for (const FEasingAreaHandle& Handle : Easings)
	{
		if (UMovieSceneSection* Section = Handle.WeakSection.Get())
		{
			UObject* Object = Handle.EasingType == ESequencerEasingType::In ? Section->Easing.EaseIn.GetObject() : Section->Easing.EaseOut.GetObject();
			if (Object)
			{
				if (!ClassType)
				{
					ClassType = Object->GetClass();
				}
				else if (Object->GetClass() != ClassType)
				{
					CurrentText = LOCTEXT("MultipleEasingTypesText", "<Multiple>");
					break;
				}
			}
		}
	}
	if (CurrentText.IsEmpty())
	{
		CurrentText = ClassType ? ClassType->GetDisplayNameText() : LOCTEXT("NoneEasingText", "None");
	}

	return FText::Format(LOCTEXT("EasingTypeTextFormat", "Method ({0})"), CurrentText);
}

void FEasingContextMenu::EasingTypeMenu(FMenuBuilder& MenuBuilder)
{
	struct FFilter : IClassViewerFilter
	{
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			bool bIsCorrectInterface = InClass->ImplementsInterface(UMovieSceneEasingFunction::StaticClass());
			bool bMatchesFlags = !InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
			return bIsCorrectInterface && bMatchesFlags;
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			bool bIsCorrectInterface = InUnloadedClassData->ImplementsInterface(UMovieSceneEasingFunction::StaticClass());
			bool bMatchesFlags = !InUnloadedClassData->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
			return bIsCorrectInterface && bMatchesFlags;
		}
	};

	FClassViewerModule& ClassViewer = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	FClassViewerInitializationOptions InitOptions;
	InitOptions.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
	InitOptions.ClassFilter = MakeShared<FFilter>();

	// Copy a reference to the context menu by value into each lambda handler to ensure the type stays alive until the menu is closed
	TSharedRef<FEasingContextMenu> Shared = AsShared();

	TSharedRef<SWidget> ClassViewerWidget = ClassViewer.CreateClassViewer(InitOptions, FOnClassPicked::CreateLambda([=](UClass* NewClass) { Shared->OnEasingTypeChanged(NewClass); }));

	MenuBuilder.AddWidget(ClassViewerWidget, FText(), true, false);
}

void FEasingContextMenu::OnEasingTypeChanged(UClass* NewClass)
{
	FScopedTransaction Transaction(LOCTEXT("SetEasingType", "Set Easing Method"));

	for (const FEasingAreaHandle& Handle : Easings)
	{
		UMovieSceneSection* Section = Handle.WeakSection.Get();
		if (!Section)
		{
			continue;
		}

		Section->Modify();

		TScriptInterface<IMovieSceneEasingFunction>& EaseObject = Handle.EasingType == ESequencerEasingType::In ? Section->Easing.EaseIn : Section->Easing.EaseOut;
		if (!EaseObject.GetObject() || EaseObject.GetObject()->GetClass() != NewClass)
		{
			UObject* NewEasingFunction = NewObject<UObject>(Section, NewClass);

			EaseObject.SetObject(NewEasingFunction);
			EaseObject.SetInterface(Cast<IMovieSceneEasingFunction>(NewEasingFunction));
		}
	}
}

void FEasingContextMenu::EasingOptionsMenu(FMenuBuilder& MenuBuilder)
{
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ false,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bShowScrollBar = false;

	TSharedRef<IDetailsView> DetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	
	TArray<UObject*> Objects;
	for (const FEasingAreaHandle& Handle : Easings)
	{
		if (UMovieSceneSection* Section = Handle.WeakSection.Get())
		{
			if (Handle.EasingType == ESequencerEasingType::In)
			{
				UObject* EaseInObject = Section->Easing.EaseIn.GetObject();
				EaseInObject->SetFlags(RF_Transactional);
				Objects.AddUnique(EaseInObject);
			}
			else
			{
				UObject* EaseOutObject = Section->Easing.EaseOut.GetObject();
				EaseOutObject->SetFlags(RF_Transactional);
				Objects.AddUnique(EaseOutObject);
			}
		}
	}

	DetailsView->SetObjects(Objects, true);

	MenuBuilder.AddWidget(DetailsView, FText(), true, false);
}




#undef LOCTEXT_NAMESPACE
