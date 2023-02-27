// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IContentBrowserSingleton.h"
#include "IDocumentation.h"
#include "PersonaTabs.h"
#include "Widgets/SWindow.h"
#include "Widgets/SCompoundWidget.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "AnimAssetFindReplace.h"

class SBox;
class SAutoCompleteSearchBox;

struct FAnimAssetFindReplaceSummoner : public FWorkflowTabFactory
{
public:
	FAnimAssetFindReplaceSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp, const FAnimAssetFindReplaceConfig& InConfig);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual TSharedPtr<SToolTip> CreateTabToolTipWidget(const FWorkflowTabSpawnInfo& Info) const override;

private:
	FAnimAssetFindReplaceConfig Config;
};

class SAnimAssetFindReplace : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAnimAssetFindReplace) {}
	
	SLATE_ARGUMENT(FAnimAssetFindReplaceConfig, Config)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Sets the type of thing we are finding/replacing, then flags the UI for a refresh */
	void SetFindReplaceType(EAnimAssetFindReplaceType InType);

private:
	void RefreshSearchResults();

	bool HandleFilterAsset(const FAssetData& InAssetData);

	bool FilterByCurve(const FAssetData& InAssetData, bool& bOutIsOldAsset) const;
	
	bool FilterByNotify(const FAssetData& InAssetData, bool& bOutIsOldAsset) const;

	void GetMatchingCurveNamesForAsset(const FAssetData& InAssetData, TArray<FString>& OutCurveNames) const;

	void GetMatchingNotifyNamesForAsset(const FAssetData& InAssetData, TArray<FString>& OutNotifyNames) const;

	FReply HandleReplace();

	FReply HandleReplaceAll();

	void ReplaceInAssets(const TArray<FAssetData>& InAssetDatas);

	void ReplaceInAsset(const FAssetData& InAssetData) const;

	void ReplaceCurvesInAsset(const FAssetData& InAssetData) const;

	void ReplaceNotifiesInAsset(const FAssetData& InAssetData) const;
	
	FReply HandleRemove();

	FReply HandleRemoveAll();
	
	void RemoveInAssets(const TArray<FAssetData>& InAssetDatas);

	void RemoveInAsset(const FAssetData& InAssetData) const;

	void RemoveCurvesInAsset(const FAssetData& InAssetData) const;

	void RemoveNotifiesInAsset(const FAssetData& InAssetData) const;

	bool ShouldFilterOutAsset(const FAssetData& InAssetData, bool& bOutIsOldAsset) const;

	bool NameMatches(const FString& InNameString) const;

	void RefreshAutoCompleteItems();

	FARFilter MakeARFilter() const;

	bool IsAssetWithoutTagOldAsset(FName InTag, const FAssetData& InAssetData) const;

private:
	FAssetPickerConfig AssetPickerConfig;

	EAnimAssetFindReplaceMode Mode = EAnimAssetFindReplaceMode::Find;

	EAnimAssetFindReplaceType Type = EAnimAssetFindReplaceType::Curves;

	FString FindString;

	FString ReplaceString;

	FRefreshAssetViewDelegate RefreshAssetViewDelegate;

	FGetCurrentSelectionDelegate GetCurrentSelectionDelegate;

	FSetARFilterDelegate SetARFilterDelegate;

	bool bFindWholeWord = true;

	bool bAssetsSelected = false;

	bool bFoundAssets = false;

	TArray<FAssetData> OldAssets;

	ESearchCase::Type SearchCase = ESearchCase::IgnoreCase;

	TStrongObjectPtr<UAnimAssetFindReplaceContext> ToolbarContext;

	TSharedPtr<TArray<TSharedPtr<FString>>> AutoCompleteItems;

	TSharedPtr<SAutoCompleteSearchBox> FindSearchBox;
	
	TSharedPtr<SAutoCompleteSearchBox> ReplaceSearchBox;

	FAssetData SkeletonFilter;
};