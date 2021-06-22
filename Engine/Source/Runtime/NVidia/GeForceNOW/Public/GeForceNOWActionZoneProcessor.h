// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if NV_GEFORCENOW

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"

class SWidget;
class SWindow;

enum class ETrackedSlateWidgetOperations : uint8;

class FWidgetGFNActionZone
{
public:

	FWidgetGFNActionZone(const SWidget* InWidget);

	void UpdateActionZone(TArray<TSharedRef<SWindow>>& SlateWindows);
	void ClearActionZone();

	unsigned int GetID() const;

private:

	const SWidget* Widget;
	FSlateRect ActionZoneRect;

	bool bWasInteractable = false;

public:

	inline bool operator==(const FWidgetGFNActionZone& OtherWidgetGFNActionZone) const
	{
		return Widget == OtherWidgetGFNActionZone.Widget;
	}

	inline bool operator==(const SWidget* OtherWidget) const
	{
		return Widget == OtherWidget;
	}

};

/**
 * Singleton that manages the Action Zones for GeForceNow.
 * Action Zones are rects that overlay the game stream on the user's end that when pressed trigger the Native Virtual Keyboard
 */
class GEFORCENOWWRAPPER_API GeForceNOWActionZoneProcessor : public TSharedFromThis<GeForceNOWActionZoneProcessor>
{
public:

	void Initialize();
	void Terminate();

private:

	void HandleTrackedWidgetChanges(const SWidget* Widget, const FName& MetaDataTypeId, ETrackedSlateWidgetOperations Operation);

	void HandleEditableTextWidgetRegistered(const SWidget* Widget);
	void HandleEditableTextWidgetUnregistered(const SWidget* Widget);

	bool ProcessGFNWidgetActionZones(float DeltaTime);
	void StartProcess();
	void StopProcess();

	TArray<FWidgetGFNActionZone> GFNWidgetActionZones;

	FDelegateHandle ProcessDelegateHandle;
};

#endif // NV_GEFORCENOW
