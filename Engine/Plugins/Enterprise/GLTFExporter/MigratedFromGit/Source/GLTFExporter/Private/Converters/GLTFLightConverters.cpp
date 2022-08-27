// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLightConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"

FGLTFJsonLightIndex FGLTFLightConverter::Convert(const ULightComponent* LightComponent)
{
	FGLTFJsonLight Light;

	Light.Name = FGLTFNameUtility::GetName(LightComponent);
	Light.Type = FGLTFConverterUtility::ConvertLightType(LightComponent->GetLightType());

	if (Light.Type == EGLTFJsonLightType::None)
	{
		// TODO: report error (unsupported light component type)
		return FGLTFJsonLightIndex(INDEX_NONE);
	}

	Light.Intensity = LightComponent->Intensity;

	const FLinearColor LightColor = LightComponent->bUseTemperature ? FLinearColor::MakeFromColorTemperature(LightComponent->Temperature) : LightComponent->GetLightColor();
	Light.Color = FGLTFConverterUtility::ConvertColor(LightColor);

	if (const UPointLightComponent* PointLightComponent = Cast<UPointLightComponent>(LightComponent))
	{
		Light.Range = FGLTFConverterUtility::ConvertLength(PointLightComponent->AttenuationRadius, Builder.ExportOptions->ExportUniformScale);
	}

	if (const USpotLightComponent* SpotLightComponent = Cast<USpotLightComponent>(LightComponent))
	{
		Light.Spot.InnerConeAngle = FGLTFConverterUtility::ConvertLightAngle(SpotLightComponent->InnerConeAngle);
		Light.Spot.OuterConeAngle = FGLTFConverterUtility::ConvertLightAngle(SpotLightComponent->OuterConeAngle);
	}

	return Builder.AddLight(Light);
}
