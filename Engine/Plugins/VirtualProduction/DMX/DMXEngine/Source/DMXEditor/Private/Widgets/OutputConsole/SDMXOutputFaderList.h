// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SNumericEntryBox.h"

struct FDMXOutputConsoleFaderDescriptor;
class FDMXEditor;
class SDMXFader;
class UDMXLibrary;

class SScrollBox;


/** A list of faders, along with a button to add a fader and macros to alter the fader's values */
class SDMXOutputFaderList
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXOutputFaderList)
	{}

		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	~SDMXOutputFaderList();

	/** Saves faders. The view will automatically be restored when the widget is shown again. */
	void SaveFaders();

	/** Restores the faders from when it was last saved */
	void RestoreFaders();

	/** Stops all ocillators */
	void StopOscillators();

private:
	/** Generates a widget to add faders */
	TSharedRef<SWidget> GenerateAddFadersWidget();

protected:
	/** Called when the editor is shut down while the widget is still being displayed */
	void OnEditorShutDown();

public:
	/** Selects specified fader */
	void SelectFader(const TSharedPtr<SDMXFader>& FaderToSelect);

	/** Applies the sine wave macro to either all or selected fader */
	void ApplySineWaveMacro(bool bAffectAllFaders);

	/** Applies the max value macro to either all or selected fader */
	void ApplyMinValueMacro(bool bAffectAllFaders);

	/** Applies the max value macro to either all or selected fader */
	void ApplyMaxValueMacro(bool bAffectAllFaders);

	/** Returns the selected fader */
	const TWeakPtr<SDMXFader>& GetWeakSelectedFader() const { return WeakSelectedFader; }

	/** Called when the add fader button is clicked */
	FReply HandleAddFadersClicked();

	/** Called when a fader send DMX check box state changes  */
	void HandleMasterFaderChanged(uint8 NewValue);

	/** Adds as many faders as specified by NumFadersToAdd */
	void AddFaders(const FString& InName = TEXT(""));

	/** Adds a fader from a fader descriptor, useful for saveing/loading */
	void AddFader(const FDMXOutputConsoleFaderDescriptor& FaderDescriptor);

	/** Clears all faders */
	void ClearFaders();

	/** Deletes the selected fader */
	void DeleteSelectedFader();

	/** Returns all faders */
	const TArray<TSharedPtr<SDMXFader>>& GetFaders() const { return Faders; }

public:
	/** Pointer to the fader that is currently being selected */
	TWeakPtr<SDMXFader> WeakSelectedFader;

private:
	/** Called when the Sort Faders button was clicked */
	FReply OnSortFadersClicked();

	/** Called when a fader wants to be deleted */
	void OnFaderRequestsDelete(TSharedRef<SDMXFader> FaderToDelete);

	/** Called when a fader wants to be selected */
	void OnFaderRequestsSelect(TSharedRef<SDMXFader> FaderToSelect);

	/** The master fader that controlls all faders */
	TSharedPtr<SSpinBox<uint8>> MasterFader;

	/** The displayed fader widgets */
	TArray<TSharedPtr<SDMXFader>> Faders;

	/** Scrollbox containing the fader widgets */
	TSharedPtr<SScrollBox> FaderScrollBox;

	/** The universe ID used when new faders are created */
	int32 NewFaderUniverseID = 1;

	/** The Starting Adress used when new faders are created */
	int32 NewFaderStartingAddress = 1;

	/** The Number of faders added when the 'add new faders' button is clicked */
	int32 NumFadersToAdd = 1;

	/** True when the sine wave oscillator is running */
	bool bRunSineWaveOscillator = false;

	/** True when macros should affect all faders */
	bool bMacrosAffectAllFaders = false;

	/** Timer to tick the sine wave oscillator */
	FTimerHandle SineWaveOscTimer;

	/** Current value of the sine wave oscillator */
	float SinWavRadians = 0.0f;
};
