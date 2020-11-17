// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/SlateDelegates.h"
#include "IDetailChildrenBuilder.h"
#include "PerPlatformPropertyCustomization.h"

class FMenuBuilder;


/**
* SPerPlatformPropertiesWidget
*/
class SPerPlatformPropertiesRow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SPerPlatformPropertiesRow)
		{}
		
		SLATE_EVENT(FOnGenerateWidget, OnGenerateWidget)
		SLATE_EVENT(FOnPlatformOverrideAction, OnRemovePlatform)

	SLATE_END_ARGS()

	/**
	* Construct this widget
	*
	* @param	InArgs	The declaration data for this widget
	*/
	void Construct(const FArguments& InArgs, FName PlatformName);

protected:
	TSharedRef<SWidget> MakePerPlatformWidget(FName InName);

	FReply RemovePlatform(FName PlatformName);

	EActiveTimerReturnType CheckPlatformCount(double InCurrentTime, float InDeltaSeconds);

	void AddPlatformToMenu(const FName& PlatformName, const FTextFormat Format, FMenuBuilder& AddPlatformMenuBuilder);

	FOnGenerateWidget OnGenerateWidget;
	FOnPlatformOverrideAction OnRemovePlatform;
	TAttribute<TArray<FName>> PlatformOverrideNames;
	int32 LastPlatformOverrideNames;
	bool bAddedMenuItem;
};

