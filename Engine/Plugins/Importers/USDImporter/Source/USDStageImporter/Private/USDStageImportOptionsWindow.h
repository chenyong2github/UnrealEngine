// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"

class UUsdStageImportOptions;

class SUsdOptionsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUsdOptionsWindow)
		: _ImportOptions(nullptr)
	{}

	SLATE_ARGUMENT(UObject*, ImportOptions)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

public:
	static bool ShowImportOptions(UUsdStageImportOptions& ImportOptions);

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override;
	FReply OnImport();
	FReply OnCancel();
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	bool ShouldImport() const;

private:
	UObject* ImportOptions;
	TWeakPtr< SWindow > Window;
	bool bShouldImport;
};
