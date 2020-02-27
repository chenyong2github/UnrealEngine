// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Widgets/SCompoundWidget.h"
#include "LuminRuntimeSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SLuminLocalizedIconWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLuminLocalizedIconWidget)
		{
		}
		SLATE_ATTRIBUTE(FString, GameLuminPath)
		SLATE_ATTRIBUTE(FLocalizedIconInfo, LocalizedIconInfo)
		SLATE_ATTRIBUTE(class SLuminLocalizedIconListWidget*, ListWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	const FLocalizedIconInfo& GetLocalizedIconInfo() const;

private:
	FString GameLuminPath;
	FLocalizedIconInfo LocalizedIconInfo;
	class SLuminLocalizedIconListWidget* ListWidget;

	void OnPickLocale(const FString& Locale);
	FReply OnPickIconModelPath(const FString& DirPath);
	FReply OnPickIconPortalPath(const FString& DirPath);
	FReply OnClearIconModelPath();
	FReply OnClearIconPortalPath();
	FReply OnRemove();
	bool CopyDir(FString SourceDir, FString TargetDir);
};
