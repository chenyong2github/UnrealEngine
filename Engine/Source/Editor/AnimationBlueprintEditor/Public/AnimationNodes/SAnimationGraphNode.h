// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Styling/SlateColor.h"
#include "SNodePanel.h"
#include "KismetNodes/SGraphNodeK2Base.h"
#include "Engine/PoseWatch.h"

class UAnimGraphNode_Base;
class IDetailTreeNode;
class IPropertyRowGenerator;

class ANIMATIONBLUEPRINTEDITOR_API SAnimationGraphNode : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SAnimationGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode);

	// Tweak any created pin widgets so they respond to bindings
	static void ReconfigurePinWidgetsForPropertyBindings(UAnimGraphNode_Base* InAnimGraphNode, TSharedRef<SGraphNode> InGraphNodeWidget, TFunctionRef<TSharedPtr<SGraphPin>(UEdGraphPin*)> InFindWidgetForPin);

protected:
	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// End of SWidget interface

	// SGraphNode interface
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const override;
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> InNodeTitle) override;
	virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override;
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;
	// End of SGraphNode interface

private:
	// Return Pose View Colour for slate indicator
	FSlateColor GetPoseViewColour() const;

	FReply SpawnColourPicker();

	// Handle the node informing us that the title has changed
	void HandleNodeTitleChanged();

	/** Keep a reference to the indicator widget handing around */
	TSharedPtr<SWidget> IndicatorWidget;

	/** Keep a reference to the pose view indicator widget handing around */
	TSharedPtr<SWidget> PoseViewWidget;

	/** Cache the node title so we can invalidate it */
	TSharedPtr<SNodeTitle> NodeTitle;

	TWeakObjectPtr<class UPoseWatch> PoseWatch;

	// Property row generator used to display function properties on nodes
	TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator;
	
	// Hold a reference to the root ptr of the details tree we use to display function properties
	TArray<TSharedPtr<IDetailTreeNode>> DetailNodes;
};
