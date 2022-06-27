// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"

class FWidgetBlueprintEditor;
class IDetailsView;
class IStructureDetailsView;
class SBorder;
class SMVVMViewBindingListView;
class UBlueprintExtension;
class UMVVMWidgetBlueprintExtension_View;

class SMVVMViewBindingPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMViewBindingPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor>, bool bInIsDrawerTab);
	virtual ~SMVVMViewBindingPanel();

	//~ Begin SWidget Interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override;
	//~ End SWidget Interface

	void OnBindingListSelectionChanged(int32 Index);

	static void RegisterSettingsMenu();

private:
	void HandleBlueprintViewChangedDelegate();

	TSharedRef<SWidget> GenerateCreateViewWidget();
	FReply HandleCreateViewClicked();

	TSharedRef<SWidget> GenerateEditViewWidget();

	void AddDefaultBinding();
	bool CanAddBinding() const;
	FText GetAddBindingToolTip() const;

	TSharedRef<SWidget> GenerateSettingsMenu();

	void ShowManageViewModelsWindow();

	TSharedRef<SWidget> CreateDrawerDockButton();
	FReply CreateDrawerDockButtonClicked();

	void HandleExtensionAdded(UBlueprintExtension* NewExtension);

private:
	TWeakPtr<FWidgetBlueprintEditor> WeakBlueprintEditor;
	TSharedPtr<SMVVMViewBindingListView> ListView;
	TSharedPtr<SBorder> DetailContainer;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<IStructureDetailsView> StructDetailsView;
	TWeakObjectPtr<UMVVMWidgetBlueprintExtension_View> MVVMExtension;
	FDelegateHandle BlueprintViewChangedDelegateHandle;
	bool bIsDrawerTab;
};
