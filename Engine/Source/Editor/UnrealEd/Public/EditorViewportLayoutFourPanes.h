// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "EditorViewportLayout.h"
#include "Widgets/Layout/SSplitter.h"
#include "AssetEditorViewportLayout.h"

class FEditorViewportLayoutFourPanes : public FAssetEditorViewportLayout
{
protected:
	/**
	 * Creates the viewports and splitter for the four-pane layout              
	 */
	virtual TSharedRef<SWidget> MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString) override;

	virtual TSharedRef<SWidget> MakeFourPanelWidget(
		TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1) = 0;

protected:
	/** The splitter widgets */
	TSharedPtr< class SSplitter > PrimarySplitterWidget;
	TSharedPtr< class SSplitter > SecondarySplitterWidget;
};


// FEditorlViewportLayoutFourPanesLeft /////////////////////////////

class FEditorViewportLayoutFourPanesLeft : public FEditorViewportLayoutFourPanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::FourPanesLeft; }

	virtual TSharedRef<SWidget> MakeFourPanelWidget(
		TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1) override;
};


// FEditorViewportLayoutFourPanesRight /////////////////////////////

class FEditorViewportLayoutFourPanesRight : public FEditorViewportLayoutFourPanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::FourPanesRight; }

	virtual TSharedRef<SWidget> MakeFourPanelWidget(
		TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1) override;
};


// FEditorViewportLayoutFourPanesTop /////////////////////////////

class FEditorViewportLayoutFourPanesTop : public FEditorViewportLayoutFourPanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::FourPanesTop; }

	virtual TSharedRef<SWidget> MakeFourPanelWidget(
		TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1) override;
};


// FEditorViewportLayoutFourPanesBottom /////////////////////////////

class FEditorViewportLayoutFourPanesBottom : public FEditorViewportLayoutFourPanes
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::FourPanesBottom; }

	virtual TSharedRef<SWidget> MakeFourPanelWidget(
		TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
		TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
		float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1) override;
};
