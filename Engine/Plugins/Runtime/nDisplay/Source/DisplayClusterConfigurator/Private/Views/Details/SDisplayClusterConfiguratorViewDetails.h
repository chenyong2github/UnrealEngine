// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SDisplayClusterConfiguratorViewBase.h"

#include "EditorUndoClient.h"
#include "Misc/NotifyHook.h"

class FDisplayClusterConfiguratorToolkit;
class IDetailsView;


class SDisplayClusterConfiguratorViewDetails
	: public SDisplayClusterConfiguratorViewBase
	, public FEditorUndoClient
	, public FNotifyHook
{

public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewDetails)
	{ }

	SLATE_END_ARGS()

public:
	SDisplayClusterConfiguratorViewDetails();
	~SDisplayClusterConfiguratorViewDetails();

public:
	void Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End of SWidget interface

	//~ Begin FNotifyHook Interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	//~ End FNotifyHook Interface

	void ShowDetailsObjects(const TArray<UObject*>& Objects);

	const TArray<UObject*>& GetSelectedObjects() const;

private:
	/** Update the inspector window to show information on the supplied objects */
	void UpdateFromObjects(const TArray<UObject*>& PropertyObjects);

	void OnObjectSelected();

private:
	/** Property viewing widget */
	TSharedPtr<IDetailsView> PropertyView;

	/** When TRUE, the SGraphNodeDetailsWidget needs to refresh the details view on Tick */
	bool bRefreshOnTick;

	//bool bCanEditProperties;

	/** Holds the property objects that need to be displayed by the inspector starting on the next tick */
	TArray<UObject*> RefreshPropertyObjects;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
