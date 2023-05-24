// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"


/** Details customization for the xyY Color Space */
class FDMXPixelMappingColorSpaceDetails_xyY
	: public IDetailCustomization
{
public:
	/** Creates an instance of this customization */
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin IDetailCustomization interface 
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization interface 
};
