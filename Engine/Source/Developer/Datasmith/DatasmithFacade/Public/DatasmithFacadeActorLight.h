// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeActor.h"


class DATASMITHFACADE_API FDatasmithFacadeActorLight :
	public FDatasmithFacadeActor
{
public:

	// Possible Datasmith light types.
	enum class ELightType
	{
		DirectionalLight, // IDatasmithDirectionalLightElement is a IDatasmithLightActorElement with no specific properties
		PointLight,       // IDatasmithPointLightElement       is a IDatasmithLightActorElement
		LightmassPortal,  // IDatasmithLightmassPortalElement  is a IDatasmithPointLightElement with no specific properties
		SpotLight,        // IDatasmithSpotLightElement        is a IDatasmithPointLightElement
		AreaLight         // IDatasmithAreaLightElement        is a IDatasmithSpotLightElement
	};

	// Possible Datasmith point light intensity units.
	// Copy of EDatasmithLightUnits from DatasmithCore DatasmithDefinitions.h.
	enum class EPointLightIntensityUnit
	{
		Unitless,
		Candelas,
		Lumens
	};

	// Possible Datasmith area light shapes.
	// Copy of EDatasmithLightShape from DatasmithCore DatasmithDefinitions.h.
	enum class EAreaLightShape : uint8
	{
		Rectangle,
		Disc,
		Sphere,
		Cylinder,
		None
	};

	// Possible Datasmith area light types.
	// Copy of EDatasmithAreaLightType from DatasmithCore DatasmithDefinitions.h.
	enum class EAreaLightType
	{
		Point,
		Spot,
		IES_DEPRECATED,
		Rect
	};

public:

	FDatasmithFacadeActorLight(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeActorLight() {}

	// Set the Datasmith light type.
	void SetLightType(
		ELightType InLightType // light type
	);

	// Set whether or not the Datasmith light is enabled.
	void SetEnabled(
		bool bInIsEnabled
	);

	// Set the Datasmith light intensity.
	void SetIntensity(
		double InIntensity // light intensity
	);

	// Set the Datasmith light sRGBA color.
	void SetColor(
		unsigned char InR, // red
		unsigned char InG, // green
		unsigned char InB, // blue
		unsigned char InA  // alpha
	);

	// Set the Datasmith light linear color.
	void SetColor(
		float InR, // red
		float InG, // green
		float InB, // blue
		float InA  // alpha
	);

	// Set the Datasmith light temperature.
	void SetTemperature(
		double InTemperature // light temperature (in Kelvin degrees)
	);

	// Write a IES definition file and set its file path for the Datasmith light.
	void WriteIESFile(
		const TCHAR* InIESFileFolder, // IES definition file folder path
		const TCHAR* InIESFileName,   // IES definition file name
		const TCHAR* InIESData        // IES definition data
	);

	// Set the file path of the IES definition file of the Datasmith light.
	void SetIESFilePath(
		const TCHAR* InIESFilePath // IES definition file path
	);

	// Set the Datasmith light IES brightness scale.
	void SetIESBrightnessScale(
		double InIESBrightnessScale // IES brightness scale
	);

	// Set the intensity unit of the Datasmith point light and derived types.
	void SetPointIntensityUnit(
		EPointLightIntensityUnit InPointIntensityUnit // point light intensity unit
	);

	// Set the source radius, or 2D source width, of the Datasmith point light and derived types.
	void SetPointSourceRadius(
		float InPointSourceRadius // point light source radius or width (in world units)
	);

	// Set the 2D source length of the Datasmith point light and derived types.
	void SetPointSourceLength(
		float InPointSourceLength // point light source length (in world units)
	);

	// Set the attenuation radius of the Datasmith point light and derived types.
	void SetPointAttenuationRadius(
		float InPointAttenuationRadius // point light attenuation radius (in world units)
	);

	// Set the inner cone angle of the Datasmith spot light and derived types.
	void SetSpotInnerConeAngle(
		float InSpotInnerConeAngle // spot light inner cone angle (in degrees)
	);

	// Set the outer cone angle of the Datasmith spot light and derived types.
	void SetSpotOuterConeAngle(
		float InSpotOuterConeAngle // spot light outer cone angle (in degrees)
	);

	// Set the Datasmith area light shape.
	void SetAreaShape(
		EAreaLightShape InAreaShape // area light shape
	);

	// Set the Datasmith area light type.
	void SetAreaType(
		EAreaLightType InAreaType // area light type
	);

	// Set the Datasmith area light shape size on the Y axis.
	void SetAreaWidth(
		float InAreaWidth // area light shape size on the Y axis (in world units)
	);

	// Set the Datasmith area light shape size on the X axis.
	void SetAreaLength(
		float InAreaLength // area light shape size on the X axis (in world units)
	);

	// Set the Datasmith lightmass portal dimensions.
	void SetPortalDimensions
	(
		float InDimensionX, // lightmass portal dimension on the X axis (in world units)
		float InDimensionY, // lightmass portal dimension on the Y axis (in world units)
		float InDimensionZ  // lightmass portal dimension on the Z axis (in world units)
	);

#ifdef SWIG_FACADE
protected:
#endif

	// Create and initialize a Datasmith light actor hierarchy.
	virtual TSharedPtr<IDatasmithActorElement> CreateActorHierarchy(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) const override;

private:

	// Datasmith light type.
	ELightType LightType;

	// Whether or not the Datasmith light is enabled.
	bool bIsEnabled;

	// Datasmith light intensity.
	double Intensity;

	// Datasmith light linear color.
	FLinearColor LinearColor;

	// Whether or not to use the Datasmith light temperature.
	bool bUseTemperature;

	// Datasmith light temperature (in Kelvin degrees).
	double Temperature;

	// File path of the IES definition file of the Datasmith light.
	FString IESFilePath;

	// Whether or not to use the Datasmith light IES brightness scale.
	bool bUseIESBrightnessScale;

	// Datasmith light IES brightness scale.
	double IESBrightnessScale;

	// Intensity unit of the Datasmith point light and derived types.
	EPointLightIntensityUnit PointIntensityUnit;

	// Source radius (in centimeters), or 2D source width, of the Datasmith point light and derived types.
	float PointSourceRadius;

	// 2D source length (in centimeters) of the Datasmith point light and derived types.
	float PointSourceLength;

	// Attenuation radius (in centimeters) of the Datasmith point light and derived types.
	float PointAttenuationRadius;

	// Inner cone angle (in degrees) of the Datasmith spot light and derived types.
	float SpotInnerConeAngle;

	// Outer cone angle (in degrees) of the Datasmith spot light and derived types.
	float SpotOuterConeAngle;

	// Datasmith area light shape.
	EAreaLightShape AreaShape;

	// Datasmith area light type.
	EAreaLightType AreaType;

	// Datasmith area light shape size on the Y axis (in world units).
	float AreaWidth;

	// Datasmith area light shape size on the X axis (in world units).
	float AreaLength;
};
