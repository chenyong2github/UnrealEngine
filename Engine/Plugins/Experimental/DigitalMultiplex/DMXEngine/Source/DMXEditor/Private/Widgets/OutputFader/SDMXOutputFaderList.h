// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

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

	void SetFaderProperties(const TSharedPtr<SDMXFader>& InFaderWidget, const TWeakObjectPtr<UDMXEntityFader>& InFaderObject, bool bIsTransferObject);

	void TransferCreatedFaderObjectProperties(const TWeakObjectPtr<UDMXEntityFader>& TransferTo, const TWeakObjectPtr<UDMXEntityFader>& TransferFrom);
	void TransferSelectedFaderObjectProperties(const TWeakObjectPtr<UDMXEntityFader>& TransferTo, const TWeakObjectPtr<UDMXEntityFader>& TransferFrom);

	void AddFader(const FString& InName = TEXT(""));

public:
	TWeakPtr<SDMXFader> WeakSelectedFaderWidget;

private:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> WeakDMXEditor;

	TSharedPtr<SScrollBox> FaderSlots;

	TArray<TSharedPtr<SDMXFader>> FaderWidgets;

	TWeakObjectPtr<UDMXEntityFader> WeakFaderTemplate;

	TWeakObjectPtr<UDMXLibrary> WeakDMXLibrary;
};
