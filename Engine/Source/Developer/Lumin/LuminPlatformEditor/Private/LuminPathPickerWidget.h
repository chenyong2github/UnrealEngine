// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

FUNC_DECLARE_DELEGATE(FOnPickPath, FReply, const FString&);
FUNC_DECLARE_DELEGATE(FOnClearPath, FReply);
FUNC_DECLARE_DELEGATE(FOnChoosePath, FReply, TAttribute<FString>, const FOnPickPath&);

class SLuminPathPickerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLuminPathPickerWidget)
		{}
		SLATE_ATTRIBUTE(FString, FilePickerTitle)
		SLATE_ATTRIBUTE(FString, FileFilter)
		SLATE_ATTRIBUTE(FOnPickPath, OnPickPath)
		SLATE_ATTRIBUTE(FOnClearPath, OnClearPath)
		SLATE_ATTRIBUTE(FOnChoosePath, OnChoosePath)
		SLATE_ATTRIBUTE(FString, StartDirectory)
		SLATE_ATTRIBUTE(FText, PathLabel)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

private:
	FString FilePickerTitle;
	FString FileFilter;
	TAttribute<FString> CurrDirectory;

	FReply OnPickDirectory();
	FReply OnClearSelectedDirectory();
	FOnPickPath OnPickPath;
	FOnClearPath OnClearPath;
};


