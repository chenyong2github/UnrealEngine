// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLightConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Utilities/GLTFCoreUtilities.h"
#include "Converters/GLTFNameUtility.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"

FGLTFJsonLight* FGLTFLightConverter::Convert(const ULightComponent* LightComponent)
{
	const EGLTFJsonLightType Type = FGLTFCoreUtilities::ConvertLightType(LightComponent->GetLightType());
	if (Type == EGLTFJsonLightType::None)
	{
		// TODO: report error (unsupported light component type)
		return nullptr;
	}

	FGLTFJsonLight* Light = Builder.AddLight();
	Light->Name = FGLTFNameUtility::GetName(LightComponent);
	Light->Type = Type;

	const float ConversionScale = Type == EGLTFJsonLightType::Directional ? 1.0f : (0.01f * 0.01f); // Conversion from cm2 to m2
	const FLinearColor ColorBrightness = LightComponent->GetColoredLightBrightness() * ConversionScale;
	const float Brightness = FMath::Max(ColorBrightness.GetMax(), 1.0f);
	const FLinearColor Color = ColorBrightness / Brightness;

	Light->Intensity = Brightness;
	Light->Color = FGLTFCoreUtilities::ConvertColor3(Color, false);

	if (const UPointLightComponent* PointLightComponent = Cast<UPointLightComponent>(LightComponent))
	{
		Light->Range = FGLTFCoreUtilities::ConvertLength(PointLightComponent->AttenuationRadius, Builder.ExportOptions->ExportUniformScale);
	}

	if (const USpotLightComponent* SpotLightComponent = Cast<USpotLightComponent>(LightComponent))
	{
		Light->Spot.InnerConeAngle = FGLTFCoreUtilities::ConvertLightAngle(SpotLightComponent->InnerConeAngle);
		Light->Spot.OuterConeAngle = FGLTFCoreUtilities::ConvertLightAngle(SpotLightComponent->OuterConeAngle);

		if (Builder.ExportOptions->bStrictCompliance)
		{
			Light->Spot.InnerConeAngle = FMath::Clamp(Light->Spot.InnerConeAngle, 0.0f, nextafterf(Light->Spot.OuterConeAngle, 0.0f));
			Light->Spot.OuterConeAngle = FMath::Clamp(Light->Spot.OuterConeAngle, nextafterf(Light->Spot.InnerConeAngle, HALF_PI), HALF_PI);
		}
	}

	return Light;
}
