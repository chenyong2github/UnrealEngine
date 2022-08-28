// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLightConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"

FGLTFJsonLight* FGLTFLightConverter::Convert(const ULightComponent* LightComponent)
{
	FGLTFJsonLight* Light = Builder.AddLight();

	Light->Name = FGLTFNameUtility::GetName(LightComponent);
	Light->Type = FGLTFConverterUtility::ConvertLightType(LightComponent->GetLightType());

	if (Light->Type == EGLTFJsonLightType::None)
	{
		// TODO: report error (unsupported light component type)
		return nullptr;
	}

	Light->Intensity = LightComponent->Intensity;

	const FLinearColor TemperatureColor = LightComponent->bUseTemperature ? FLinearColor::MakeFromColorTemperature(LightComponent->Temperature) : FLinearColor::White;
	Light->Color = FGLTFConverterUtility::ConvertColor3(TemperatureColor * LightComponent->GetLightColor(), Builder.ExportOptions->bStrictCompliance);

	if (const UPointLightComponent* PointLightComponent = Cast<UPointLightComponent>(LightComponent))
	{
		Light->Range = FGLTFConverterUtility::ConvertLength(PointLightComponent->AttenuationRadius, Builder.ExportOptions->ExportUniformScale);
	}

	if (const USpotLightComponent* SpotLightComponent = Cast<USpotLightComponent>(LightComponent))
	{
		Light->Spot.InnerConeAngle = FGLTFConverterUtility::ConvertLightAngle(SpotLightComponent->InnerConeAngle);
		Light->Spot.OuterConeAngle = FGLTFConverterUtility::ConvertLightAngle(SpotLightComponent->OuterConeAngle);

		if (Builder.ExportOptions->bStrictCompliance)
		{
			Light->Spot.InnerConeAngle = FMath::Clamp(Light->Spot.InnerConeAngle, 0.0f, nextafterf(Light->Spot.OuterConeAngle, 0.0f));
			Light->Spot.OuterConeAngle = FMath::Clamp(Light->Spot.OuterConeAngle, nextafterf(Light->Spot.InnerConeAngle, HALF_PI), HALF_PI);
		}
	}

	return Light;
}
