// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"
#include "Widgets/SBoxPanel.h"
#include "SNiagaraOverviewStack.h"

class UNiagaraOverviewNode;
class UNiagaraStackViewModel;
class UNiagaraSystemSelectionViewModel;
class FNiagaraEmitterHandleViewModel;
class FAssetThumbnailPool;
class FAssetThumbnail;
struct FRendererPreviewData;


class SNiagaraOverviewStackNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewStackNode) {}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode);

protected:
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	void FillThumbnailBar(UObject* ChangedObject, const bool bIsTriggeredByObjectUpdate);
private:
	EVisibility GetIssueIconVisibility() const;
	EVisibility GetEnabledCheckBoxVisibility() const;
	ECheckBoxState GetEnabledCheckState() const;
	void OnEnabledCheckStateChanged(ECheckBoxState InCheckState);
	TSharedRef<SWidget> CreateThumbnailWidget(float InThumbnailSize, FRendererPreviewData InData);
	FReply OnClickedRenderingPreview(const FGeometry& InGeometry, const FPointerEvent& InEvent, class UNiagaraStackEntry* InEntry);
	FText GetToggleIsolateToolTip() const;
	FReply OnToggleIsolateButtonClicked();
	EVisibility GetToggleIsolateVisibility() const;
	FSlateColor GetToggleIsolateImageColor() const;

private:
	UNiagaraOverviewNode* OverviewStackNode;
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraSystemSelectionViewModel* OverviewSelectionViewModel;
	TWeakPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModelWeak;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	/** Thumbnail widget containers */
	TSharedPtr<SHorizontalBox> ThumbnailBar;
	TArray<FRendererPreviewData> PreviewData;
};