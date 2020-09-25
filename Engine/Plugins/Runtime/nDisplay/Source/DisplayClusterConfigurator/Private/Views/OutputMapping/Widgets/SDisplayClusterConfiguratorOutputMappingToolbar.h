// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SViewportToolBar.h"

class FExtender;
class FUICommandList;

class SDisplayClusterConfiguratorOutputMappingToolbar
	: public SViewportToolBar
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorOutputMappingToolbar) {}
		SLATE_ARGUMENT(TSharedPtr<FUICommandList>, CommandList)
		SLATE_ARGUMENT(TSharedPtr<FExtender>, Extenders)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Static: Creates a widget for the main tool bar
	 *
	 * @return	New widget
	 */
	TSharedRef< SWidget > MakeToolBar(const TSharedPtr< FExtender > InExtenders);

private:
	/** Command list */
	TSharedPtr<FUICommandList> CommandList;
};
