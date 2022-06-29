// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegionCustomization.h"
#include "ColorCorrectRegion.h"
#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FColorCorrectRegionDetails::MakeInstance()
{
	return MakeShareable(new FColorCorrectRegionDetails);
}

void FColorCorrectRegionDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	bool bPriorityHidden = false;
	for (const TWeakObjectPtr<UObject>& SelectedObject : DetailBuilder.GetSelectedObjects())
	{
		if (AColorCorrectRegion* CCR = Cast<AColorCorrectRegion>(SelectedObject.Get()))
		{
			if (CCR->Type == EColorCorrectRegionsType::Plane)
			{
				bPriorityHidden = true;
				break;
			}
		}
	}
	if (bPriorityHidden)
	{
		TSharedRef<IPropertyHandle> TileLayersProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AColorCorrectRegion, Priority));
		DetailBuilder.HideProperty(TileLayersProperty);
	}
}
