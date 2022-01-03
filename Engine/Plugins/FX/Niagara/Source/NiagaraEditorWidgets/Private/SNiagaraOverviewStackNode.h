// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"
#include "Widgets/SBoxPanel.h"
#include "SNiagaraOverviewStack.h"

class UNiagaraOverviewNode;
class UNiagaraStackViewModel;
class UNiagaraSystemSelectionViewModel;
class FNiagaraEmitterHandleViewModel;
class FAssetThumbnail;
class UNiagaraStackEntry;

class SNiagaraOverviewStackNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewStackNode) {}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool ShouldAllowCulling() const override { return false; }

protected:
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	virtual TSharedRef<SWidget> CreateTitleRightWidget() override;
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	void StackViewModelStructureChanged(ENiagaraStructureChangedFlags Flags);
	void StackViewModelDataObjectChanged(TArray<UObject*> ChangedObjects, ENiagaraDataObjectChange ChangeType);
	void FillTopContentBar();
	void OnMaterialCompiled(class UMaterialInterface* MaterialInterface);

	void CreateBottomSummaryExpander();
private:
	EVisibility GetIssueIconVisibility() const;
	EVisibility GetEnabledCheckBoxVisibility() const;
	ECheckBoxState GetEnabledCheckState() const;
	void OnEnabledCheckStateChanged(ECheckBoxState InCheckState);
	TSharedRef<SWidget> CreateThumbnailWidget(UNiagaraStackEntry* InData, TSharedPtr<SWidget> InWidget, TSharedPtr<SWidget> InTooltipWidget);
	FReply OnClickedRenderingPreview(const FGeometry& InGeometry, const FPointerEvent& InEvent, class UNiagaraStackEntry* InEntry);
	FText GetToggleIsolateToolTip() const;
	FReply OnToggleIsolateButtonClicked();
	EVisibility GetToggleIsolateVisibility() const;
	FSlateColor GetToggleIsolateImageColor() const;
	void SetIsHoveringThumbnail(const FGeometry& InGeometry, const FPointerEvent& InEvent, const bool bInHoveringThumbnail)
	{
		SetIsHoveringThumbnail(InEvent, bInHoveringThumbnail);
	}
	void SetIsHoveringThumbnail(const FPointerEvent& InEvent, const bool bInHoveringThumbnail)
	{
		bIsHoveringThumbnail = bInHoveringThumbnail;
	}
	bool IsHoveringThumbnail()
	{
		return bIsHoveringThumbnail;
	}

	FReply OnCycleThroughIssues();
	FReply OpenParentEmitter();
	EVisibility GetOpenParentEmitterVisibility() const;

	const FSlateBrush* GetSummaryViewButtonBrush() const;
	FText GetSummaryViewCollapseTooltipText() const;
	FReply ExpandSummaryViewClicked();
private:
	UNiagaraOverviewNode* OverviewStackNode;
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraSystemSelectionViewModel* OverviewSelectionViewModel;
	TWeakPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModelWeak;
	/** The top content bar houses the isolate button and the thumbnails */
	TSharedPtr<SHorizontalBox> TopContentBar;
	TSharedPtr<SWidget> BottomSummaryExpander;

	TArray<UNiagaraStackEntry*> PreviewStackEntries;
	bool bIsHoveringThumbnail;
	bool bTopContentBarRefreshPending;
	int32 CurrentIssueIndex;
};