// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Widgets/SCompoundWidget.h"

class UDMXPixelMapping;
class UDMXPixelMappingBaseComponent;
class FDMXPixelMappingToolkit;
class UDMXPixelMappingRendererComponent;

class IDetailsView;
class FReply;
class SScrollBox;
class SVerticalBox;


/** Displays the input sources of the pixel mapping */
class SDMXPixelMappingInputSourceView
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingInputSourceView) { }
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit);

	/** Refreshes the widget on the next tick */
	void RequestRefresh();

private:
	/** Refreshes the widget direct */
	void ForceRefresh();

	/** Called when a component was selected */
	void OnComponentSelected();

	/** Called when a component was added to or removed from the pixel mapping */
	void OnComponentAddedOrRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component);

	/** Called when the add button was clicked */
	FReply OnAddButtonClicked();

	/** Called when the select renederer component button was clicked*/
	FReply OnSelectRendererComponentButtonClicked(UDMXPixelMappingRendererComponent* RendererComponent);

	/** Number of renderer components in the pixel mapping */
	int32 NumRendererComponents = 0;

	/** Returns the renderer components in this pixelmapping */
	TArray<UDMXPixelMappingRendererComponent*> GetRendererComponents() const;

	/** Scrollbox that holds the details views */
	TSharedPtr<SScrollBox> DetailsViewsScrollBox;

	/** Timer handle for the Request Refresh method */
	FTimerHandle RefreshTimerHandle;

	/** The toolkit of this editor */
	TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
};
