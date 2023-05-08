// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

namespace ESelectInfo { enum Type : int; }
template <typename OptionType> class SComboBox;

class FPCGEditor;
class UPCGComponent;
class UPCGGraph;

class FPCGEditorGraphDebugObjectInstance
{
public:
	FPCGEditorGraphDebugObjectInstance() = default;
	FPCGEditorGraphDebugObjectInstance(TWeakObjectPtr<UPCGComponent> InPCGComponent);

	void SetLabelFromPCGComponent(TWeakObjectPtr<UPCGComponent> InPCGComponent);

	FText GetDebugObjectText() const
	{
		return FText::FromString(Label);
	}

	TWeakObjectPtr<UPCGComponent> GetPCGComponent() const
	{
		return PCGComponent;
	}
	
private:
	TWeakObjectPtr<UPCGComponent> PCGComponent = nullptr;
	FString Label = TEXT("No debug object selected");
};


class SPCGEditorGraphDebugObjectWidget: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGEditorGraphDebugObjectWidget) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FPCGEditor> InPCGEditor);

private:
	void OnComboBoxOpening();
	void OnSelectionChanged(TSharedPtr<FPCGEditorGraphDebugObjectInstance> NewSelection, ESelectInfo::Type SelectInfo) const;
	TSharedRef<SWidget> OnGenerateWidget(TSharedPtr<FPCGEditorGraphDebugObjectInstance> InDebugObjectInstance) const;

	UPCGGraph* GetPCGGraph() const;
	
	FText GetSelectedDebugObjectText() const;
	void SelectedDebugObject_OnClicked() const;
	bool IsSelectDebugObjectButtonEnabled() const;
	
	void SetDebugObjectFromSelection_OnClicked();
	bool IsSetDebugObjectFromSelectionButtonEnabled() const;
	
	/** Pointer back to the PCG editor that owns us */
	TWeakPtr<FPCGEditor> PCGEditorPtr;

	TArray<TSharedPtr<FPCGEditorGraphDebugObjectInstance>> DebugObjects;
	TSharedPtr<SComboBox<TSharedPtr<FPCGEditorGraphDebugObjectInstance>>> DebugObjectsComboBox;
};
