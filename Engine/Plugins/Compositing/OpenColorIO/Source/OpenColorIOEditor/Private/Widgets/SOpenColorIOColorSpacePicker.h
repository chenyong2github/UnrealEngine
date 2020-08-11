// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "OpenColorIOColorSpace.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class FViewportClient;
class SComboButton;
class UOpenColorIOConfiguration;

DECLARE_DELEGATE_OneParam(FOnColorSpaceChanged, const FOpenColorIOColorSpace& /*ColorSpace*/);

class SOpenColorIOColorSpacePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenColorIOColorSpacePicker) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UOpenColorIOConfiguration>, Config)
		SLATE_ARGUMENT(FOpenColorIOColorSpace, InitialColorSpace)
		SLATE_ARGUMENT(FOpenColorIOColorSpace, RestrictedColor)
		SLATE_EVENT(FOnColorSpaceChanged, OnColorSpaceChanged)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	/** Update current configuration asset for this picker */
	void SetConfiguration(TWeakObjectPtr<UOpenColorIOConfiguration> NewConfiguration);

	/** Update restricted color space for this picker */
	void SetRestrictedColorSpace(const FOpenColorIOColorSpace& RestrictedColorSpace);

protected:
	
	/** Called when a selection has been made */
	void SetCurrentColorSpace(const FOpenColorIOColorSpace& NewColorSpace);

	/** Handles color space list menu creation */
	TSharedRef<SWidget> HandleColorSpaceComboButtonMenuContent();
	
	/** Reset to default triggered in UI */
	FReply OnResetToDefault();

	/** Whether or not ResetToDefault button should be shown */
	EVisibility ShouldShowResetToDefaultButton() const;

protected:
	TSharedPtr<SComboButton> SelectionButton;
	TWeakObjectPtr<UOpenColorIOConfiguration> Configuration;
	FOpenColorIOColorSpace ColorSpaceSelection;
	FOpenColorIOColorSpace RestrictedColorSpace;
	FOnColorSpaceChanged OnColorSpaceChanged;
};
