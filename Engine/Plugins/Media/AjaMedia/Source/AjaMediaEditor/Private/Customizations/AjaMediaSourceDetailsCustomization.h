// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AjaDeviceProvider.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "MediaIOCoreDefinitions.h"

class FAjaDeviceProvider;
class IDetailLayoutBuilder;
struct EVisibility;

/**
 * 
 */
class FAjaMediaSourceDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin IDetailCustomization Interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	//~ End IDetailCustomization Interface

private:
	FReply OnAutoDetectClicked();
	bool IsAutoDetectEnabled() const;
	EVisibility GetThrobberVisibility() const;

	void OnAutoDetected(TArray<FAjaDeviceProvider::FMediaIOConfigurationWithTimecodeFormat>);

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	TUniquePtr<FAjaDeviceProvider> DeviceProvider;
	bool bAutoDetectRequested = false;
};
