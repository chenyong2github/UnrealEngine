// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class URemoteControlPreset;
class SRCPanelExposedEntitiesList;

/**
 * Interface that displays binding protocols.
 */
class SRCPanelInputBindings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCPanelInputBindings)
		: _EditMode(true)
		, _Preset(nullptr)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
		SLATE_ATTRIBUTE(URemoteControlPreset*, Preset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, URemoteControlPreset* Preset);

	/** Get the exposed entity list. */
	TSharedPtr<SRCPanelExposedEntitiesList> GetEntityList() { return EntityList; }

private:
	/** Holds the field list. */
	TSharedPtr<SRCPanelExposedEntitiesList> EntityList;
};