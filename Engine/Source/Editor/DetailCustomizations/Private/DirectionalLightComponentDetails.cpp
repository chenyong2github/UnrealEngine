// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectionalLightComponentDetails.h"

#include "Components/LightComponentBase.h"
#include "Components/SceneComponent.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Engine/EngineTypes.h"
#include "HAL/Platform.h"
#include "IDetailPropertyRow.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Misc/Attribute.h"
#include "PropertyHandle.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "DirectionalLightComponentDetails"


TSharedRef<IDetailCustomization> FDirectionalLightComponentDetails::MakeInstance()
{
	return MakeShareable( new FDirectionalLightComponentDetails );
}

void FDirectionalLightComponentDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	// Get cascaded shadow map category
	IDetailCategoryBuilder& ShadowMapCategory = DetailBuilder.EditCategory("CascadedShadowMaps", FText::GetEmpty(), ECategoryPriority::Default );

	TSharedPtr<IPropertyHandle> MovableShadowRadiusPropertyHandle = DetailBuilder.GetProperty("DynamicShadowDistanceMovableLight");
	TSharedPtr<IPropertyHandle> StationaryShadowRadiusPropertyHandle = DetailBuilder.GetProperty("DynamicShadowDistanceStationaryLight");

	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnGameThread() != 0);

	if(!bAllowStaticLighting)
	{
		// If static lighting is not allowed, hide DynamicShadowDistanceStationaryLight and rename DynamicShadowDistanceMovableLight to "Dynamic Shadow Distance"

		FProperty* MovableShadowRadiusProperty = MovableShadowRadiusPropertyHandle->GetProperty();
		MovableShadowRadiusProperty->SetMetaData(TEXT("DisplayName"), TEXT("Dynamic Shadow Distance"));

		ShadowMapCategory.AddProperty(StationaryShadowRadiusPropertyHandle)
			.Visibility(EVisibility::Hidden);
	}

	TSharedPtr<IPropertyHandle> LightIntensityProperty = DetailBuilder.GetProperty("Intensity", ULightComponentBase::StaticClass());
	// Point lights need to override the ui min and max for units of lumens, so we have to undo that
	LightIntensityProperty->SetInstanceMetaData("UIMin",TEXT("0.0f"));
	LightIntensityProperty->SetInstanceMetaData("UIMax",TEXT("150.0f"));
	LightIntensityProperty->SetInstanceMetaData("SliderExponent", TEXT("2.0f"));
	LightIntensityProperty->SetInstanceMetaData("Units", TEXT("lux"));
	LightIntensityProperty->SetToolTipText(LOCTEXT("DirectionalLightIntensityToolTipText", "Maximum illumination from the light in lux"));

}

#undef LOCTEXT_NAMESPACE
