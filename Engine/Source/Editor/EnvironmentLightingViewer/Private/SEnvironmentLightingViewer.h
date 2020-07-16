// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"


class STextBlock;
class SButton;
struct FPropertyAndParent;

#define ENVLIGHT_MAX_DETAILSVIEWS 5

class SEnvironmentLightingViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEnvironmentLightingViewer)
	{
	}
	SLATE_END_ARGS()

	/**
	* Construct the widget
	*
	* @param	InArgs			A declaration from which to construct the widget
	*/
	void Construct(const FArguments& InArgs);

	/** Gets the widget contents of the app */
	virtual TSharedRef<SWidget> GetContent();

	virtual ~SEnvironmentLightingViewer();

	/** SWidget interface */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	TSharedPtr<class IDetailsView> DetailsViews[ENVLIGHT_MAX_DETAILSVIEWS];
	FLinearColor DefaultForegroundColor;

	TSharedPtr<class SCheckBox> CheckBoxAtmosphericLightsOnly;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> ComboBoxDetailFilter;
	TArray<TSharedPtr<FString>> ComboBoxDetailFilterOptions;
	int32 SelectedComboBoxDetailFilterOptions;

	TSharedPtr<class SButton> ButtonCreateSkyLight;
	TSharedPtr<class SButton> ButtonCreateAtmosphericLight0;
	TSharedPtr<class SButton> ButtonCreateAtmosphericLight1;
	TSharedPtr<class SButton> ButtonCreateSkyAtmosphere;
	TSharedPtr<class SButton> ButtonCreateVolumetricCloud;

	FReply OnButtonCreateSkyLight();
	FReply OnButtonCreateAtmosphericLight(uint32 Index);
	FReply OnButtonCreateSkyAtmosphere();
	FReply OnButtonCreateVolumetricCloud();

	TSharedRef<SWidget> ComboBoxDetailFilterWidget(TSharedPtr<FString> InItem);
	void ComboBoxDetailFilterWidgetSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedComboBoxDetailFilterTextLabel() const;

	bool GetIsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const;
};
