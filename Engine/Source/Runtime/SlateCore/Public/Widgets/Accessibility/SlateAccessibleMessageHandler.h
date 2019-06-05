// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_ACCESSIBILITY

#include "GenericPlatform/GenericAccessibleInterfaces.h"
#include "Misc/Variant.h"

class SWidget;

/**
 * Message handling system for Slate Accessibility API, dealing with both receiving events and pushing them back to the platform layer.
 */
class SLATECORE_API FSlateAccessibleMessageHandler : public FGenericAccessibleMessageHandler
{
public:
	// FGenericAccessibleMessageHandler
	virtual void OnActivate() override;
	virtual void OnDeactivate() override;
	virtual TSharedPtr<IAccessibleWidget> GetAccessibleWindow(const TSharedRef<FGenericWindow>& InWindow) const override;
	virtual AccessibleWidgetId GetAccessibleWindowId(const TSharedRef<FGenericWindow>& InWindow) const override;
	virtual TSharedPtr<IAccessibleWidget> GetAccessibleWidgetFromId(AccessibleWidgetId Id) const override;
	virtual bool ApplicationIsAccessible() const override { return true; }
	//~

	/**
	 * Callback for SWidget destructor. Removes the corresponding accessible widget for the Slate widget.
	 *
	 * @param Widget The widget that is being deleted.
	 */
	void OnWidgetRemoved(SWidget* Widget);
	/**
	 * Callback for a Slate widget's parent changing.
	 *
	 * @param Widget The widget whose parent changed.
	 */
	void OnWidgetParentChanged(TSharedRef<SWidget> Widget);
	/**
	 * Callback for a Slate widget's children changing. Although somewhat rare, widgets are not required
	 * to return a child even if the child was parented to it (for instance, SWidgetSwitcher).
	 *
	 * Note: This should not be called if the children are already calling OnWidgetParentChanged().
	 *
	 * @param Widget The widget whose children changed.
	 */
	void OnWidgetChildrenChanged(TSharedRef<SWidget> Widget);
	/**
	 * Callback for a Slate widget's main accessible behavior changing. 
	 *
	 * @param Widget The widget whose behavior changed.
	 */
	void OnWidgetAccessibleBehaviorChanged(TSharedRef<SWidget> Widget);
	/**
	 * Callback for a Slate widget indicating that some event happened to it.
	 *
	 * @param Widget The widget raising the event.
	 * @param Event The type of event being raised.
	 */
	void OnWidgetEventRaised(TSharedRef<SWidget> Widget, EAccessibleEvent Event);
	/**
	 * Callback for a Slate widget indicating that a property change occurred. This may also be used by certain events
	 * such as Notification which don't have an 'OldValue'. Only NewValue should be set for those types of events.
	 *
	 * @param Widget The widget raising the event.
	 * @param Event The type of event being raised.
	 * @param OldValue The value of the property being changed before the change occurred.
	 * @param NewValue The value of the property being changed after the change occurred, or any miscellaneous data for the event.
	 */
	void OnWidgetEventRaised(TSharedRef<SWidget> Widget, EAccessibleEvent Event, FVariant OldValue, FVariant NewValue);
};

#endif
