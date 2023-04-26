// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"


class FMenuBuilder;
class FSpawnTabArgs;
class FTabManager;
class SDockTab;
class UToolMenu;

namespace UE::MVVM
{

class FDebugSnapshot;

class SDetailsTab;
class SMessagesLog;
class SSelectionTab;
class SViewModelBindingDetail;

class SMainDebug : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMainDebug) { }
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& InParentTab);

private:
	void BuildToolMenu();
	void HandlePullDownWindowMenu(FMenuBuilder& MenuBuilder);
	TSharedRef<SWidget> HandleSnapshotMenuContent();

	void HandleTakeSnapshot();
	void HandleLoadSnapshot();
	void HandleSaveSnapshot();
	bool HasValidSnapshot() const;

	void HandleObjectSelectionChanged();

	TSharedRef<SWidget> CreateDockingArea(const TSharedRef<SDockTab>& InParentTab);
	TSharedRef<SDockTab> SpawnSelectionTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnBindingTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDetailTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnMessagesTab(const FSpawnTabArgs& Args);

private:
	TSharedPtr<FTabManager> TabManager;

	TWeakPtr<SDetailsTab> DetailView;
	TWeakPtr<SMessagesLog> MessageLog;
	TWeakPtr<SSelectionTab> SelectionView;
	TWeakPtr<SViewModelBindingDetail> ViewModelBindingDetail;

	TSharedPtr<FDebugSnapshot> Snapshot;
};

} //namespace
