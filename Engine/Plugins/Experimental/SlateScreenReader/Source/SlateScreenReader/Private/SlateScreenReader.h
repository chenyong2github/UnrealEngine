// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/ScreenReaderBase.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"

class SWidget;
class iAccessibleWidget;
class FWidgetPath;
class FWeakWidgetPath;
struct FFocusEvent;

/**
 * A basic screen reader that can work for desktop and consoles that use Slate.
 * All feedback to users are done through speech.
 */
class FSlateScreenReader
	: public FScreenReaderBase
{
public:
	FSlateScreenReader() = delete;
	FSlateScreenReader(const FSlateScreenReader& Other) = delete;
	explicit FSlateScreenReader(const TSharedRef<GenericApplication>& InPlatformApplication);

	FSlateScreenReader& operator=(const FSlateScreenReader& Other) = delete;
	virtual ~FSlateScreenReader();
	
	void HandleSlateFocusChanging(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldFocusWidgetPath, const TSharedPtr<SWidget>& InOldWidget, const FWidgetPath& InNewWidgetPath, const TSharedPtr<SWidget>& InNewFocusWidget);
protected:
	virtual void OnActivate() override;
	virtual void OnDeactivate() override;
};
