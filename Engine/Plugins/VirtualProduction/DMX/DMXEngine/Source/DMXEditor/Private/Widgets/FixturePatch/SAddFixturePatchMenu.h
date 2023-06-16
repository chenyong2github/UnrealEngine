// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Widgets/SCompoundWidget.h"

class FDMXEditor;
class FReply;
template <typename OptionType> class SComboBox; 
template <typename EntityType> class SDMXEntityPickerButton;
class UDMXAddFixturePatchMenuData;
class UDMXEntity;
class UDMXEntityFixtureType;


namespace UE::DMXEditor::FixturePatchEditor
{
	/** Editor for Fixture Patches */
	class SAddFixturePatchMenu final
		: public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAddFixturePatchMenu)
		{}
		SLATE_END_ARGS()

		virtual ~SAddFixturePatchMenu();

		/** Constructs this widget */
		void Construct(const FArguments& InArgs, TWeakPtr<FDMXEditor> InWeakDMXEditor);

		/** Refreshes the mode combo box on the next tick */
		void RequestRefresh();

	private:
		/** Generates an entry in the mode combo box */
		TSharedRef<SWidget> GenerateModeComboBoxEntry(const TSharedPtr<uint32> InMode) const;

		/** Refreshes the mode combo box */
		void ForceRefresh();

		/** Called when a fixture type was selected */
		void OnFixtureTypeSelected(UDMXEntity* InSelectedFixtureType);

		/** Called when a mode was selected */
		void OnModeSelected(TSharedPtr<uint32> InSelectedMode, ESelectInfo::Type SelectInfo);

		/** Called when the 'Add' button was clicked */
		FReply OnAddFixturePatchButtonClicked();

		/** Returns the text of the active mode */
		FText GetActiveModeText() const;

		/** Returns if a valid fixture type with a mode is selected */
		bool HasValidFixtureTypeAndMode() const;

		/** Current fixture type */
		TWeakObjectPtr<UDMXEntityFixtureType> WeakFixtureType;

		/** The number of fixture patches to add */
		int32 ActiveModeIndex = 0;

		/** The number of fixture patches to add */
		uint32 NumFixturePatchesToAdd = 1;

		/** Timer handle for the request refresh combo box timer */
		FTimerHandle RequestRefreshModeComboBoxTimerHandle;

		/** Widget to select the fixture type */
		TSharedPtr<SDMXEntityPickerButton<UDMXEntityFixtureType>> FixtureTypeSelector;

		/** Sources for the mode combo box */
		TArray<TSharedPtr<uint32>> ModeSources;

		/** Combo box to selected the mode */
		TSharedPtr<SComboBox<TSharedPtr<uint32>>> ModeComboBox;

		/** The DMX editor this widget displays */
		TWeakPtr<FDMXEditor> WeakDMXEditor;
	};
}
