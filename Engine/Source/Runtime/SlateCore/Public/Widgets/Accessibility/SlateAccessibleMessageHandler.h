// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_ACCESSIBILITY

#include "GenericPlatform/GenericAccessibleInterfaces.h"
#include "Misc/Variant.h"

class FSlateAccessibleWidget;
class SWidget;
class SWindow;

/**
 * Message handling system for Slate Accessibility API, dealing with both receiving events and pushing them back to the platform layer.
 * The message handler is also responsible for processing the Slate widget tree and 
 */
class SLATECORE_API FSlateAccessibleMessageHandler : public FGenericAccessibleMessageHandler
{
public:
	FSlateAccessibleMessageHandler();

	// FGenericAccessibleMessageHandler
	virtual void OnActivate() override;
	virtual void OnDeactivate() override;
	virtual TSharedPtr<IAccessibleWidget> GetAccessibleWindow(const TSharedRef<FGenericWindow>& InWindow) const override;
	virtual AccessibleWidgetId GetAccessibleWindowId(const TSharedRef<FGenericWindow>& InWindow) const override;
	virtual TSharedPtr<IAccessibleWidget> GetAccessibleWidgetFromId(AccessibleWidgetId Id) const override;
	//~

	/**
	 * Callback for SWidget destructor. Removes the corresponding accessible widget for the Slate widget.
	 *
	 * @param Widget The widget that is being deleted.
	 */
	void OnWidgetRemoved(SWidget* Widget);

	/**
	 * Callback for a Slate widget indicating that a property change occurred. This may also be used by certain events
	 * such as Notification which don't have an 'OldValue'. Only NewValue should be set for those types of events.
	 *
	 * @param Widget The widget raising the event.
	 * @param Event The type of event being raised.
	 * @param OldValue The value of the property being changed before the change occurred.
	 * @param NewValue The value of the property being changed after the change occurred, or any miscellaneous data for the event.
	 */
	void OnWidgetEventRaised(TSharedRef<SWidget> Widget, EAccessibleEvent Event, FVariant OldValue = FVariant(), FVariant NewValue = FVariant());

	/**
	 * Refresh the accessible widget tree next available tick. This should be called any time the Slate tree changes.
	 */
	void MarkDirty() { bDirty = true; }

	/**
	 * Process any pending Slate widgets and update the accessible widget tree.
	 */
	void Tick();

private:
	struct FWidgetAndParent {
		FWidgetAndParent(TWeakPtr<SWidget> InWidget, TSharedRef<FSlateAccessibleWidget> InParent)
			: Widget(InWidget), Parent(InParent)
		{
		}

		TWeakPtr<SWidget> Widget;
		TSharedRef<FSlateAccessibleWidget> Parent;
	};

	/** A list of widgets waiting to be processed in order to keep the accessible widget tree up to date. */
	TArray<FWidgetAndParent> ToProcess;
	/** If true, Tick() will begin the update process to the accessible widget tree. Use MarkDirty() to set. */
	bool bDirty;
};

#endif
