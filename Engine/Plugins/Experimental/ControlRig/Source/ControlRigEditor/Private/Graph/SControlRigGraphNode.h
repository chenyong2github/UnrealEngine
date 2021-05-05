// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphNode.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "RigVMModel/RigVMPin.h"
#include "ControlRigBlueprint.h"

class UControlRigGraphNode;
class STableViewBase;
class SOverlay;
class SGraphPin;
class UEdGraphPin;
class SScrollBar;

class SControlRigGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SControlRigGraphNode)
		: _GraphNodeObj(nullptr)
		{}

	SLATE_ARGUMENT(UControlRigGraphNode*, GraphNodeObj)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SGraphNode interface
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> InNodeTitle) override;
	virtual void EndUserInteraction() const override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter) override;
	virtual void AddPin( const TSharedRef<SGraphPin>& PinToAdd ) override;
	virtual void SetDefaultTitleAreaWidget(TSharedRef<SOverlay> DefaultTitleAreaWidget) override
	{
		TitleAreaWidget = DefaultTitleAreaWidget;
	}
	virtual const FSlateBrush * GetNodeBodyBrush() const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual TSharedPtr<SGraphPin> GetHoveredPin( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) const override;
	virtual void GetOverlayBrushes(bool bSelected, const FVector2D WidgetSize, TArray<FOverlayBrushInfo>& Brushes) const override;
	virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override;
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const override;

	virtual void RefreshErrorInfo() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	bool UseLowDetailPinContent() const;

	bool UseLowDetailNodeContent() const;

	TOptional<FVector2D> GetLowDetailDesiredSize() const;

	EVisibility GetTitleVisibility() const;

	EVisibility GetExecutionTreeVisibility() const;

	EVisibility GetInputTreeVisibility() const;

	EVisibility GetInputOutputTreeVisibility() const;

	EVisibility GetOutputTreeVisibility() const;

	EVisibility GetVariableListVisibility() const;

	TSharedRef<ITableRow> MakePinTableRowWidget(URigVMPin* InItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeVariableTableRowWidget(TSharedPtr<FRigVMExternalVariable> InVariable, const TSharedRef<STableViewBase>& OwnerTable);

	void HandleGetChildrenForTree(URigVMPin* InItem, TArray<URigVMPin*>& OutChildren);

	void HandleExpansionChanged(URigVMPin* InItem, bool bExpanded);
	void HandleExpandRecursively(URigVMPin* InItem, bool bExpanded, TSharedPtr<STreeView<URigVMPin*>>* TreeWidgetPtr);

	FText GetPinLabel(TWeakPtr<SGraphPin> GraphPin) const;

	FSlateColor GetPinTextColor(TWeakPtr<SGraphPin> GraphPin) const;
	FSlateColor GetVariableLabelTextColor(TWeakObjectPtr<URigVMFunctionReferenceNode> FunctionReferenceNode, FName InVariableName) const;
	FText GetVariableLabelTooltipText(TWeakObjectPtr<UControlRigBlueprint> InBlueprint, FName InVariableName) const;

	TSharedRef<SWidget> AddContainerPinContent(URigVMPin* InItem, FText InTooltipText);

	FReply HandleAddArrayElement(URigVMPin* InItem);

	void HandleNodeTitleDirtied();

	FText GetInstructionCountText() const;

private:

	int32 GetNodeTopologyVersion() const;
	
	/** Cached widget title area */
	TSharedPtr<SOverlay> TitleAreaWidget;

	/** Widget representing collapsible execution pins */
	TSharedPtr<STreeView<URigVMPin*>> ExecutionTree;

	/** Widget representing collapsible input pins */
	TSharedPtr<STreeView<URigVMPin*>> InputTree;

	/** Widget representing collapsible input-output pins */
	TSharedPtr<STreeView<URigVMPin*>> InputOutputTree;

	/** Widget representing collapsible output pins */
	TSharedPtr<STreeView<URigVMPin*>> OutputTree;

	/** Widget representing the variable remapping information */
	TSharedPtr<SListView<TSharedPtr<FRigVMExternalVariable>>> VariableRemappingList;

	/** Dummy scrollbar, as we cant create a tree view without one! */
	TSharedPtr<SScrollBar> ScrollBar;

	/** Map of pin->widget */
	TMap<const UEdGraphPin*, TSharedPtr<SGraphPin>> PinWidgetMap;

	/** Map of pin widgets to extra pin widgets */
	TMap<TSharedRef<SWidget>, TSharedRef<SGraphPin>> ExtraWidgetToPinMap;

	int32 NodeErrorType;

	TSharedPtr<SImage> VisualDebugIndicatorWidget;
	TSharedPtr<STextBlock> InstructionCountTextBlockWidget;

	static const FSlateBrush* CachedImg_CR_Pin_Connected;
	static const FSlateBrush* CachedImg_CR_Pin_Disconnected;

	/** Cache the node title so we can invalidate it */
	TSharedPtr<SNodeTitle> NodeTitle;

	TWeakObjectPtr<UControlRigBlueprint> Blueprint;

	FVector2D LastHighDetailSize;
};
