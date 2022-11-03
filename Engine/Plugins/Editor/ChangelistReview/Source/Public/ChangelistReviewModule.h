// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//#include "SSourceControlReview.h"
#include "Modules/ModuleManager.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"

class UContentBrowserAliasDataSource;

class FChangelistReviewModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	void ShowReviewTab();
	bool CanShowReviewTab() const;
private:
	TSharedRef<SDockTab> CreateReviewTab(const FSpawnTabArgs& Args);
	TSharedPtr<SWidget> CreateReviewUI();
	
	TWeakPtr<SDockTab> ReviewTab;
	//TWeakPtr<SSourceControlReview> ReviewWidget;
};
