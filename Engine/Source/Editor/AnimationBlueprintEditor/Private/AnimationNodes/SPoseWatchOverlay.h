// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SButton.h"

class UAnimBlueprint;
class UEdGraphNode;
struct FOverlayWidgetInfo;
class UAnimGraphNode_Base;

class SPoseWatchOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPoseWatchOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphNode* InNode);
	FVector2D GetOverlayOffset() const;
	bool IsPoseWatchValid() const;

private:
	void HandlePoseWatchesChanged(UAnimBlueprint* InAnimBlueprint, UEdGraphNode* InNode);

	FSlateColor GetPoseViewColor() const;
	const FSlateBrush* GetPoseViewIcon() const;
	FReply TogglePoseWatchVisibility();

	TWeakObjectPtr<UEdGraphNode> GraphNode;
	TWeakObjectPtr<class UPoseWatch> PoseWatch;

	static const FSlateBrush* IconVisible;
	static const FSlateBrush* IconNotVisible;
};