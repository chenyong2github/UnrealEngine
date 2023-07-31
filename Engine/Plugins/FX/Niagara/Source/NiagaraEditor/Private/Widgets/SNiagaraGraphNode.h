// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"
#include "NiagaraNode.h"

/** A graph node widget representing a niagara node. */
class SNiagaraGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SNiagaraGraphNode) {}
	SLATE_END_ARGS();

	SNiagaraGraphNode();
	virtual ~SNiagaraGraphNode();

	void Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode);

	//~ SGraphNode api
	virtual void UpdateGraphNode() override;
	virtual void CreateInputSideAddButton(TSharedPtr<SVerticalBox> InputBox) override;
	virtual void CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox) override;
	virtual void UpdateErrorInfo() override;
	virtual void CreatePinWidgets() override;

protected:
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void UpdateGraphNodeCompact();
	virtual bool ShouldDrawCompact() const { return CompactTitle.IsEmpty() == false; }
	
	FText GetNodeCompactTitle() const { return CompactTitle; }
	FOptionalSize GetCompactMinDesiredPinBoxSize() const;
	/** To allow customization of the node title font size */
	TAttribute<FSlateFontInfo> GetCompactNodeTitleFont();

	void LoadCachedIcons();

	void RegisterNiagaraGraphNode(UEdGraphNode* InNode);
	void HandleNiagaraNodeChanged(UNiagaraNode* InNode);
	TWeakObjectPtr<UNiagaraNode> NiagaraNode;
	FGuid LastSyncedNodeChangeId;
	
	FText CompactTitle;
	bool bShowPinNamesInCompactMode = false;
	TOptional<float> CompactNodeTitleFontSizeOverride;
	
	static const FSlateBrush* CachedOuterIcon;
	static const FSlateBrush* CachedInnerIcon;

	// We keep track of the bigger of the two pin boxes so that the smaller one can scale up, so that the middle part is centered
	double CompactNodeMaxPinBoxX;
	bool bSyncPinBoxSizes = false;
};