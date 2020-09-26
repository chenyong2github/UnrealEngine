// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "Views/SDisplayClusterConfiguratorViewBase.h"

#include "EditorUndoClient.h"

class FDisplayClusterConfiguratorToolkit;
class SWidget;

class SDisplayClusterConfiguratorViewLog
	: public SDisplayClusterConfiguratorViewBase
	, public FEditorUndoClient
{

public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewLog)
	{}

	SLATE_END_ARGS()

public:
	SDisplayClusterConfiguratorViewLog()
	{}

	~SDisplayClusterConfiguratorViewLog();

	void Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<SWidget>& InListingWidget);

private:
	TSharedPtr<SWidget> LogListingWidget;
};