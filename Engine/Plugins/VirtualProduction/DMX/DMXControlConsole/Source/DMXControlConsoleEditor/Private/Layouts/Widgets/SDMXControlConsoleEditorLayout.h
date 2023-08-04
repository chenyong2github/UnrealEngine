// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Widgets/SCompoundWidget.h"


namespace UE::DMXControlConsoleEditor::Layout::Private
{ 
	/** Base widget for Control Console layout */
	class DMXCONTROLCONSOLEEDITOR_API SDMXControlConsoleEditorLayout
		: public SCompoundWidget
	{
	public:
		/** Requests this layout to be refreshed */
		virtual void RequestRefresh();

	protected:
		/** True if refreshing layout is allowed */
		virtual bool CanRefresh() const { return false; }

		/** Refreshes layout */
		virtual void Refresh();

		/** Called when an Element is added to this layout */
		virtual void OnLayoutElementAdded() = 0;

		/** Should be called when a Fader Group was deleted from the this view displays */
		virtual void OnLayoutElementRemoved() = 0;

		/** Timer handle in use while refreshing layout is requested but not carried out yet */
		FTimerHandle RefreshLayoutTimerHandle;
	};
}
