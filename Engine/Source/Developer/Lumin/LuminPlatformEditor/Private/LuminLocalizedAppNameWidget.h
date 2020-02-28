// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Widgets/SCompoundWidget.h"
#include "LuminRuntimeSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SLuminLocalizedAppNameWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLuminLocalizedAppNameWidget)
		{
		}
		SLATE_ATTRIBUTE(FLocalizedAppName, LocalizedAppName)
		SLATE_ATTRIBUTE(class SLuminLocalizedAppNameListWidget*, ListWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	const FLocalizedAppName& GetLocalizedAppName() const;

private:
	FLocalizedAppName LocalizedAppName;
	class SLuminLocalizedAppNameListWidget* ListWidget;

	void OnPickLocale(const FString& Locale);
	void OnEditAppName(const FText& NewAppName);
	FReply OnRemove();
};
