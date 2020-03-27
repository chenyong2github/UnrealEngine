// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "DMXProtocolCommon.h"

class SDMXFader;
class FDMXEditor;
class UDMXEntityFader;
class SScrollBox;
class UDMXLibrary;

class SDMXOutputFaderList
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXOutputFaderList)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_ARGUMENT(TWeakObjectPtr<UDMXEntityFader>, FaderTemplate)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	void DeselectFaders();

	void UpdateFaderTemplateObject(const TWeakObjectPtr<UDMXEntityFader>& InFaderObject);

	void ResetFaderBackgrounds();

	void RemoveFader(TSharedPtr<SDMXFader> FaderToRemove);

	const TWeakPtr<SDMXFader>& GetWeakSelectedFaderWidget() const { return WeakSelectedFaderWidget; }

	const TArray<TSharedPtr<SDMXFader>>& GetFaderWidgets() const { return FaderWidgets; }

private:
	//~ Begin SWidget implementation
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ Public SWidget implementation

	FReply HandleAddFaderClicked();

	FReply HandleUpdateFaderClicked();

	/**
	 * Called when a fader value changes.
	 * Updates the value in the correct Fragment Map and sends it over.
	 */
	void HandleFaderValueChanged(TSharedRef<SDMXFader> InFaderWidget);

	/**
	 * Called when a fader send DMX check box state changes.
	 * It creates or removes entries in the FragmentMaps entries depending on the Fader's Send state.
	 */
	void HandleFaderSendStateChanged(TSharedRef<SDMXFader> InFaderWidget);

	void SetFaderProperties(const TSharedPtr<SDMXFader>& InFaderWidget, const TWeakObjectPtr<UDMXEntityFader>& InFaderObject, bool bIsTransferObject);

	void TransferCreatedFaderObjectProperties(const TWeakObjectPtr<UDMXEntityFader>& TransferTo, const TWeakObjectPtr<UDMXEntityFader>& TransferFrom);
	void TransferSelectedFaderObjectProperties(const TWeakObjectPtr<UDMXEntityFader>& TransferTo, const TWeakObjectPtr<UDMXEntityFader>& TransferFrom);

	void AddFader(const FString& InName = TEXT(""));

	/**
	 * Called when a Fader's Channel or Universe is removed to try and free memory by deleting
	 * FragmentMaps or addresses that are not in use anymore.
	 * It prevents values not being controlled by faders anymore from being sent.
	 */
	void CompactFragmentMaps(uint16 RemovedUniverseID, uint16 RemovedAddress, const TSharedRef<SDMXFader> FaderInstigator);

public:
	TWeakPtr<SDMXFader> WeakSelectedFaderWidget;

private:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> WeakDMXEditor;

	TSharedPtr<SScrollBox> FaderSlots;

	TArray<TSharedPtr<SDMXFader>> FaderWidgets;

	TWeakObjectPtr<UDMXEntityFader> WeakFaderTemplate;

	TWeakObjectPtr<UDMXLibrary> WeakDMXLibrary;

	/**
	 * Keeps fragment maps for each universe in each protocol.
	 * <Protocol Name => <Universe ID => Fragment Map> >
	 */
	TMap<FName, TMap<uint16, IDMXFragmentMap> > FragmentMaps;
};
