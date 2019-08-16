// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/SlateDelegates.h"

class FMenuBuilder;

/**
* SPerPlatformPropertiesWidget
*/
class PROPERTYEDITOR_API SPerPlatformPropertiesWidget : public SCompoundWidget
{
public:
	typedef typename TSlateDelegates<FName>::FOnGenerateWidget FOnGenerateWidget;

	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnPlatformAction, FName);

	SLATE_BEGIN_ARGS(SPerPlatformPropertiesWidget)
	: _OnGenerateWidget()
	{}

	SLATE_EVENT(FOnGenerateWidget, OnGenerateWidget)
	SLATE_EVENT(FOnPlatformAction, OnAddPlatform)
	SLATE_EVENT(FOnPlatformAction, OnRemovePlatform)

	SLATE_ATTRIBUTE(TArray<FName>, PlatformOverrideNames)

	SLATE_END_ARGS()

	/**
	* Construct this widget
	*
	* @param	InArgs	The declaration data for this widget
	*/
	void Construct(const typename SPerPlatformPropertiesWidget::FArguments& InArgs);

protected:
	void ConstructChildren();

	TSharedRef<SWidget> MakePerPlatformWidget(FName InName, FText InDisplayText, const TArray<FName>& InPlatformOverrides, FMenuBuilder& InAddPlatformMenuBuilder);

	void AddPlatform(FName PlatformName);

	FReply RemovePlatform(FName PlatformName);

	EActiveTimerReturnType CheckPlatformCount(double InCurrentTime, float InDeltaSeconds);

	void AddPlatformToMenu(const FName& PlatformName, const FTextFormat Format, FMenuBuilder& AddPlatformMenuBuilder);

	FOnGenerateWidget OnGenerateWidget;
	FOnPlatformAction OnAddPlatform;
	FOnPlatformAction OnRemovePlatform;
	TAttribute<TArray<FName>> PlatformOverrideNames;
	int32 LastPlatformOverrideNames;
	bool bAddedMenuItem;
};

