// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "EditorViewportLayout.h"
#include "Widgets/Layout/SSplitter.h"
#include "AssetEditorViewportLayout.h"

class FEditorViewportLayoutThreePanes : public FAssetEditorViewportLayout
{
protected:
	/**
	 * Creates the viewports and splitter for the two panes vertical layout                   
	 */
	virtual TSharedRef<SWidget> MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString) override;

	virtual TSharedRef<SWidget> MakeThreePanelWidget(
		TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		const TSharedRef<SWidget>& ViewportKey0, const TSharedRef<SWidget>& ViewportKey1, const TSharedRef<SWidget>& ViewportKey2,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage) = 0;

protected:
	/** The splitter widgets */
	TSharedPtr< class SSplitter > PrimarySplitterWidget;
	TSharedPtr< class SSplitter > SecondarySplitterWidget;
};

// FEditorViewportLayoutThreePanesLeft /////////////////////////////

class FEditorViewportLayoutThreePanesLeft : public FEditorViewportLayoutThreePanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::ThreePanesLeft; }

	virtual TSharedRef<SWidget> MakeThreePanelWidget(
		TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		const TSharedRef<SWidget>& ViewportKey0, const TSharedRef<SWidget>& ViewportKey1, const TSharedRef<SWidget>& ViewportKey2,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage) override;
};


// FEditorViewportLayoutThreePanesRight /////////////////////////////

class FEditorViewportLayoutThreePanesRight : public FEditorViewportLayoutThreePanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::ThreePanesRight; }

	virtual TSharedRef<SWidget> MakeThreePanelWidget(
		TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		const TSharedRef<SWidget>& ViewportKey0, const TSharedRef<SWidget>& ViewportKey1, const TSharedRef<SWidget>& ViewportKey2,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage) override;
};


// FEditorViewportLayoutThreePanesTop /////////////////////////////

class FEditorViewportLayoutThreePanesTop : public FEditorViewportLayoutThreePanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::ThreePanesTop; }

	virtual TSharedRef<SWidget> MakeThreePanelWidget(
		TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		const TSharedRef<SWidget>& ViewportKey0, const TSharedRef<SWidget>& ViewportKey1, const TSharedRef<SWidget>& ViewportKey2,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage) override;
};


// FEditorViewportLayoutThreePanesBottom /////////////////////////////

class FEditorViewportLayoutThreePanesBottom : public FEditorViewportLayoutThreePanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::ThreePanesBottom; }

	virtual TSharedRef<SWidget> MakeThreePanelWidget(
		TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		const TSharedRef<SWidget>& ViewportKey0, const TSharedRef<SWidget>& ViewportKey1, const TSharedRef<SWidget>& ViewportKey2,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage) override;
};
