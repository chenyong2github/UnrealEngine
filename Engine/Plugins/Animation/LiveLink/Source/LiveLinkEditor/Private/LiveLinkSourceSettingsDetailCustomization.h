// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"

#include "LiveLinkTypes.h"
#include "LiveLinkSourceSettings.h"

/**
* Customizes a FLiveLinkSourceSettingsDetailCustomization
*/
class FLiveLinkSourceSettingsDetailCustomization : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance() 
	{
		return MakeShared<FLiveLinkSourceSettingsDetailCustomization>();
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
	// End IDetailCustomization interface

private:
	void ForceRefresh();

	IDetailLayoutBuilder* DetailBuilder = nullptr;
};
