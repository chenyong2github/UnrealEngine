// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SDisplayClusterConfiguratorViewBase.h"

#include "EditorUndoClient.h"

class FDisplayClusterConfiguratorToolkit;
class FDisplayClusterConfiguratorViewOutputMapping;
class FUICommandList;
class SDPIScaler;
class SDisplayClusterConfiguratorGraphEditor;
class SDisplayClusterConfiguratorRuler;

struct FGeometry;

class SDisplayClusterConfiguratorViewOutputMapping
	: public SDisplayClusterConfiguratorViewBase
	, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewOutputMapping)
	{}

	SLATE_END_ARGS()

public:
	void Construct(
		const FArguments& InArgs, 
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, 
		const TSharedRef<SDisplayClusterConfiguratorGraphEditor>& InGraphEditor, 
		const TSharedRef<FDisplayClusterConfiguratorViewOutputMapping> InViewOutputMapping);

	//~ Begin SWidget Interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SWidget Interface

private:
	FGeometry MakeGeometryWindowLocal(const FGeometry& WidgetGeometry) const;

	EVisibility GetRulerVisibility() const;

	/** Creates graph controls */
	TSharedRef<SWidget> CreateOverlayUI();

	void BindCommands();

	FText GetCursorPositionText() const;

	EVisibility GetCursorPositionTextVisibility() const;

	void ZoomToFit();

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<SDisplayClusterConfiguratorGraphEditor> GraphEditorPtr;

	TWeakPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMappingPtr;

	TSharedPtr<SDPIScaler> PreviewSurface;

	TSharedPtr<SDisplayClusterConfiguratorRuler> TopRuler;

	TSharedPtr<SDisplayClusterConfiguratorRuler> SideRuler;

	/** The graph toolbar command list */
	TSharedPtr<FUICommandList> CommandList;
};
