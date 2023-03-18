// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDParticleActorCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"


TSharedRef<IDetailCustomization> FChaosVDParticleActorCustomization::MakeInstance()
{
	return MakeShareable( new FChaosVDParticleActorCustomization );
}

void FChaosVDParticleActorCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Hide everything as the only thing we want to show in these actors is the Recorded debug data
	TArray<FName> CurrentCategoryNames;
	DetailBuilder.GetCategoryNames(CurrentCategoryNames);
	for (const FName& CategoryToHide : CurrentCategoryNames)
	{
		if (CategoryToHide != ChaosVDCategoryName)
		{
			DetailBuilder.HideCategory(CategoryToHide);
		}
	}

	DetailBuilder.EditCategory(ChaosVDCategoryName).InitiallyCollapsed(false);
}
