// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeActorLight.h"

// Datasmith SDK.
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


FDatasmithFacadeActorLight::FDatasmithFacadeActorLight(
	const TCHAR* InElementName,
	const TCHAR* InElementLabel
) :
	FDatasmithFacadeActor(InElementName, InElementLabel),
	LightType(ELightType::DirectionalLight),
	// Default light property values from Datasmith implementation.
	bIsEnabled(true),
	Intensity(1.0),
	LinearColor(1.0, 1.0, 1.0, 1.0), // opaque white
	bUseTemperature(false),
	Temperature(6500.0),
	bUseIESBrightnessScale(false),
	IESBrightnessScale(1.0),
	PointIntensityUnit(EPointLightIntensityUnit::Unitless),
	PointSourceRadius(-1.0),
	PointSourceLength(-1.0),
	PointAttenuationRadius(-1.0),
	SpotInnerConeAngle(45.0),
	SpotOuterConeAngle(60.0),
	AreaShape(EAreaLightShape::None),
	AreaType(EAreaLightType::Point),
	AreaWidth(0.0),
	AreaLength(0.0)
{
	// Prevent the Datasmith light actor from being removed by optimization.
	KeepActor();
}

void FDatasmithFacadeActorLight::SetLightType(
	ELightType InLightType
)
{
	LightType = InLightType;
}

void FDatasmithFacadeActorLight::SetEnabled(
	bool bInIsEnabled
)
{
	bIsEnabled = bInIsEnabled;
}

void FDatasmithFacadeActorLight::SetIntensity(
	double InIntensity
)
{
	Intensity = InIntensity;
}

void FDatasmithFacadeActorLight::SetColor(
	unsigned char InR,
	unsigned char InG,
	unsigned char InB,
	unsigned char InA
)
{
	// Convert the Datasmith light sRGBA color to linear color.
	LinearColor = FLinearColor(FColor(InR, InG, InB, InA));
}

void FDatasmithFacadeActorLight::SetColor(
	float InR,
	float InG,
	float InB,
	float InA
)
{
	LinearColor.R = InR;
	LinearColor.G = InG;
	LinearColor.B = InB;
	LinearColor.A = InA;
}

void FDatasmithFacadeActorLight::SetTemperature(
	double InTemperature
)
{
	Temperature     = InTemperature;
	bUseTemperature = true;
}

void FDatasmithFacadeActorLight::WriteIESFile(
	const TCHAR* InIESFileFolder,
	const TCHAR* InIESFileName,
	const TCHAR* InIESData
)
{
	// Set the file path of the IES definition file.
	IESFilePath = FPaths::Combine(FString(InIESFileFolder), FString(InIESFileName));

	// Write the IES definition data in the IES definition file.
	FFileHelper::SaveStringToFile(FString(InIESData), *IESFilePath);
}

void FDatasmithFacadeActorLight::SetIESFilePath(
	const TCHAR* InIESFilePath
)
{
	IESFilePath = InIESFilePath;
}

void FDatasmithFacadeActorLight::SetIESBrightnessScale(
	double InIESBrightnessScale
)
{
	IESBrightnessScale     = InIESBrightnessScale;
	bUseIESBrightnessScale = true;
}

void FDatasmithFacadeActorLight::SetPointIntensityUnit(
	EPointLightIntensityUnit InPointIntensityUnit
)
{
	PointIntensityUnit = InPointIntensityUnit;
}

void FDatasmithFacadeActorLight::SetPointSourceRadius(
	float InPointSourceRadius
)
{
	PointSourceRadius = InPointSourceRadius * WorldUnitScale;
}

void FDatasmithFacadeActorLight::SetPointSourceLength(
	float InPointSourceLength
)
{
	PointSourceLength = InPointSourceLength * WorldUnitScale;
}

void FDatasmithFacadeActorLight::SetPointAttenuationRadius(
	float InPointAttenuationRadius
)
{
	PointAttenuationRadius = InPointAttenuationRadius * WorldUnitScale;
}

void FDatasmithFacadeActorLight::SetSpotInnerConeAngle(
	float InSpotInnerConeAngle
)
{
	SpotInnerConeAngle = InSpotInnerConeAngle;
}

void FDatasmithFacadeActorLight::SetSpotOuterConeAngle(
	float InSpotOuterConeAngle
)
{
	SpotOuterConeAngle = InSpotOuterConeAngle;
}

void FDatasmithFacadeActorLight::SetAreaShape(
	EAreaLightShape InAreaShape
)
{
	AreaShape = InAreaShape;
}

void FDatasmithFacadeActorLight::SetAreaType(
	EAreaLightType InAreaType
)
{
	AreaType = InAreaType;
}

void FDatasmithFacadeActorLight::SetAreaWidth(
	float InAreaWidth
)
{
	AreaWidth = InAreaWidth * WorldUnitScale;
}

void FDatasmithFacadeActorLight::SetAreaLength(
	float InAreaLength
)
{
	AreaLength = InAreaLength * WorldUnitScale;
}

void FDatasmithFacadeActorLight::SetPortalDimensions
(
	float InDimensionX,
	float InDimensionY,
	float InDimensionZ
)
{
	// Set the Datasmith actor world scale to drive the Datasmith lightmass portal dimensions.
	WorldTransform.SetScale3D(FVector(InDimensionX * WorldUnitScale, InDimensionY * WorldUnitScale, InDimensionZ * WorldUnitScale));
}

TSharedPtr<IDatasmithActorElement> FDatasmithFacadeActorLight::CreateActorHierarchy(
	TSharedRef<IDatasmithScene> IOSceneRef
) const
{
	// Create a Datasmith light actor element.
	TSharedPtr<IDatasmithLightActorElement> LightActorPtr;
	switch (LightType)
	{
	    case ELightType::DirectionalLight:
		{
			LightActorPtr = FDatasmithSceneFactory::CreateDirectionalLight(*ElementName);
			break;
		}
		case ELightType::PointLight:
		{
			LightActorPtr = FDatasmithSceneFactory::CreatePointLight(*ElementName);
			break;
		}
		case ELightType::LightmassPortal:
		{
			LightActorPtr = FDatasmithSceneFactory::CreateLightmassPortal(*ElementName);
			break;
		}
		case ELightType::SpotLight:
		{
			LightActorPtr = FDatasmithSceneFactory::CreateSpotLight(*ElementName);
			break;
		}
		case ELightType::AreaLight:
		{
			LightActorPtr = FDatasmithSceneFactory::CreateAreaLight(*ElementName);
			break;
		}
	}

	// Set the Datasmith light actor base properties.
	SetActorProperties(IOSceneRef, LightActorPtr);

	// Set the Datasmith light actor properties.
	switch (LightType)
	{
		case ELightType::AreaLight:
		{
			TSharedPtr<IDatasmithAreaLightElement> AreaLightActorPtr = StaticCastSharedPtr<IDatasmithAreaLightElement>(LightActorPtr);

			// Set the Datasmith area light shape.
			AreaLightActorPtr->SetLightShape(EDatasmithLightShape(AreaShape));

			// Set the Datasmith area light type.
			AreaLightActorPtr->SetLightType(EDatasmithAreaLightType(AreaType));

			// Set the Datasmith area light shape size on the Y axis.
			AreaLightActorPtr->SetWidth(AreaWidth);

			// Set the Datasmith area light shape size on the X axis.
			AreaLightActorPtr->SetLength(AreaLength);

			// Fall back to next case since IDatasmithAreaLightElement is a IDatasmithSpotLightElement.
		}

		case ELightType::SpotLight:
		{
			TSharedPtr<IDatasmithSpotLightElement> SpotLightActorPtr = StaticCastSharedPtr<IDatasmithSpotLightElement>(LightActorPtr);

			// Set the inner cone angle of the Datasmith spot light and derived types.
			SpotLightActorPtr->SetInnerConeAngle(SpotInnerConeAngle);

			// Set the outer cone angle of the Datasmith spot light and derived types.
			SpotLightActorPtr->SetOuterConeAngle(SpotOuterConeAngle);

			// Fall back to next case since IDatasmithSpotLightElement is a IDatasmithPointLightElement.
		}

		case ELightType::PointLight:
		case ELightType::LightmassPortal: // is a IDatasmithPointLightElement with no specific properties
		{
			TSharedPtr<IDatasmithPointLightElement> PointLightActorPtr = StaticCastSharedPtr<IDatasmithPointLightElement>(LightActorPtr);

			// Set the intensity unit of the Datasmith point light and derived types.
			PointLightActorPtr->SetIntensityUnits(EDatasmithLightUnits(PointIntensityUnit));

			// Set the source radius, or 2D source width, of the Datasmith point light and derived types.
			PointLightActorPtr->SetSourceRadius(PointSourceRadius);

			// Set the 2D source length of the Datasmith point light and derived types.
			PointLightActorPtr->SetSourceLength(PointSourceLength);

			// Set the attenuation radius of the Datasmith point light and derived types.
			PointLightActorPtr->SetAttenuationRadius(PointAttenuationRadius);

			// Fall back to next case since IDatasmithPointLightElement is a IDatasmithLightActorElement.
		}

		case ELightType::DirectionalLight: // is a IDatasmithLightActorElement with no specific properties
		{
			TSharedPtr<IDatasmithDirectionalLightElement> DirectionalLightActorPtr = StaticCastSharedPtr<IDatasmithDirectionalLightElement>(LightActorPtr);

			// Set whether or not the Datasmith light is enabled.
			DirectionalLightActorPtr->SetEnabled(bIsEnabled);

			// Set the Datasmith light intensity.
			DirectionalLightActorPtr->SetIntensity(Intensity);

			// Set the Datasmith light linear color.
			DirectionalLightActorPtr->SetColor(LinearColor);

			// Set Whether or not to use the Datasmith light temperature.
			DirectionalLightActorPtr->SetUseTemperature(bUseTemperature);

			if (bUseTemperature)
			{
				// Set the Datasmith light temperature.
				DirectionalLightActorPtr->SetTemperature(Temperature);
			}

			if (!IESFilePath.IsEmpty())
			{
				// The Datasmith light is controlled by a IES definition file.
				DirectionalLightActorPtr->SetUseIes(true);

				// Set the IES definition file path of the Datasmith light.
				DirectionalLightActorPtr->SetIesFile(*IESFilePath);

				// Set Whether or not to use the Datasmith light IES brightness scale.
				DirectionalLightActorPtr->SetUseIesBrightness(bUseIESBrightnessScale);

				if (bUseIESBrightnessScale)
				{
					// Set the Datasmith light IES brightness scale.
					DirectionalLightActorPtr->SetIesBrightnessScale(IESBrightnessScale);
				}
			}

			break;
		}
	}

	// Add the hierarchy of children to the Datasmith actor.
	AddActorChildren(IOSceneRef, LightActorPtr);

	return LightActorPtr;
}
