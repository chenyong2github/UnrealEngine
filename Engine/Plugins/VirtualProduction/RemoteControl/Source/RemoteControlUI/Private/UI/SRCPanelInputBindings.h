// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SRCPanelTreeNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FRemoteControlEntity;
class SRCPanelExposedEntitiesList;
struct SRCPanelTreeNode;
class URemoteControlPreset;

/**
 * Interface that displays binding protocols.
 */
class SRCPanelInputBindings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCPanelInputBindings)
		: _EditMode(true)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);
	
	/** Get the exposed entity list. */
	TSharedPtr<SRCPanelExposedEntitiesList> GetEntityList() { return EntityList; }

private:
	/** Create the details view for the entity currently selected. */
	TSharedRef<SWidget> CreateEntityDetailsView();

	/** Update the details view following entity selection change.  */
	void UpdateEntityDetailsView(const TSharedPtr<SRCPanelTreeNode>& SelectedNode);

private:
	/** Holds the field list. */
	TSharedPtr<SRCPanelExposedEntitiesList> EntityList;

	/** Holds the field's details. */
	TSharedPtr<class IStructureDetailsView> EntityDetailsView;

	/**
	 * Pointer to the currently selected node.
	 * Used in order to ensure that the generated details view keeps a valid pointer to the selected entity.
	 */
	TSharedPtr<FRemoteControlEntity> SelectedEntity;

	/** The preset that holds the bindings. */
	TWeakObjectPtr<URemoteControlPreset> Preset;
};