// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNodeAnimState.h"

class UAnimStateAliasNode;

class SGraphNodeAnimStateAlias : public SGraphNodeAnimState
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeAnimStateAlias) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAnimStateAliasNode* InNode);

	// SNodePanel::SNode interface
	virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override;
	virtual TSharedPtr<SToolTip> GetComplexTooltip() override;
	// End of SNodePanel::SNode interface

	static void GetStateInfoPopup(UEdGraphNode* GraphNode, TArray<FGraphInformationPopupInfo>& Popups);

protected:
	virtual FSlateColor GetBorderBackgroundColor_Internal(FLinearColor InactiveStateColor, FLinearColor ActiveStateColorDim, FLinearColor ActiveStateColorBright) const;

	virtual FText GetPreviewCornerText() const override;
	virtual const FSlateBrush* GetNameIcon() const override;
};
