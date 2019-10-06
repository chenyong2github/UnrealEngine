// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith SDK.
#include "DatasmithSceneFactory.h"


class DATASMITHFACADE_API FDatasmithFacadeElement
{
	friend class FDatasmithFacadeScene;

public:

	// Possible coordinate system types.
	enum class ECoordinateSystemType
	{
		LeftHandedYup, // left-handed Y-up coordinate system: Cinema 4D
		LeftHandedZup, // left-handed Z-up coordinate system: Unreal native
		RightHandedZup // right-handed Z-up coordinate system: Revit
	};

	// Type of a method that converts a vertex from world coordinates to Unreal coordinates.
	typedef FVector (* ConvertVertexMethod)(
		float InX, // vertex X in world coordinates
		float InY, // vertex Y in world coordinates
		float InZ  // vertex Z in world coordinates
	);

public:

	// Set the coordinate system type of the world geometries and transforms.
	static void SetCoordinateSystemType(
		ECoordinateSystemType InWorldCoordinateSystemType // world coordinate system type
	);

	// Set the scale factor from world units to Datasmith centimeters.
	static void SetWorldUnitScale(
		float InWorldUnitScale // scale factor from world units to Datasmith centimeters
	);

	FDatasmithFacadeElement(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeElement() {}

	// Hash the Datasmith element name.
	void HashName();

	// Set the Datasmith element name.
	void SetName(
		const TCHAR* InElementName // Datasmith element name
	);

	// Return the Datasmith element name.
	const TCHAR* GetName() const;

	// Set the Datasmith element label.
	void SetLabel(
		const TCHAR* InElementLabel // Datasmith element label
	);

	// Return the Datasmith element label.
	const TCHAR* GetLabel() const;

	// Add a metadata string property to the Datasmith element.
	virtual void AddMetadataString(
		const TCHAR* InPropertyName, // property name
		const TCHAR* InPropertyValue // property value
	);

#ifdef SWIG_FACADE
protected:
#endif

	// Convert a position from world unit coordinates to Unreal centimeter coordinates.
	static ConvertVertexMethod ConvertPosition;

	// Convert a direction from the world coordinate system to the Unreal coordinate system.
	static ConvertVertexMethod ConvertDirection;

	// Convert a translation from world unit coordinates to Unreal centimeter coordinates.
	static FVector ConvertTranslation(
		FVector const& InVertex // translation in world unit coordinates
	);

	// Return the optimized Datasmith scene element.
	virtual TSharedPtr<FDatasmithFacadeElement> Optimize(
		TSharedPtr<FDatasmithFacadeElement> InElementPtr,           // this Datasmith scene element
		bool                                bInNoSingleChild = true // remove intermediate Datasmith actors having a single child
	);

	// Build the Datasmith scene element asset when required.
	virtual void BuildAsset();

	// Build and export the Datasmith scene element asset when required.
	// This must be done before building a Datasmith scene element.
	virtual void ExportAsset(
		FString const& InAssetFolder // Datasmith asset folder path
	);

	// Build a Datasmith scene element and add it to the Datasmith scene.
	virtual void BuildScene(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) = 0;

private:

	// Scale and convert a position from left-handed Y-up coordinates to Unreal left-handed Z-up centimeter coordinates.
	static FVector ConvertPositionLeftHandedYup(
		float InX, // position X in world unit coordinates
		float InY, // position Y in world unit coordinates
		float InZ  // position Z in world unit coordinates
	);

	// Scale a position to Unreal left-handed Z-up centimeter coordinates.
	static FVector ConvertPositionLeftHandedZup(
		float InX, // position X in world unit coordinates
		float InY, // position Y in world unit coordinates
		float InZ  // position Z in world unit coordinates
	);

	// Scale and convert a position from right-handed Z-up coordinates to Unreal left-handed Z-up centimeter coordinates.
	static FVector ConvertPositionRightHandedZup(
		float InX, // position X in world unit coordinates
		float InY, // position Y in world unit coordinates
		float InZ  // position Z in world unit coordinates
	);

	// Convert a direction from a left-handed Y-up coordinate system to Unreal left-handed Z-up coordinate system.
	static FVector ConvertDirectionLeftHandedYup(
		float InX, // direction X in world coordinates
		float InY, // direction Y in world coordinates
		float InZ  // direction Z in world coordinates
	);

	// Convert a direction to Unreal left-handed Z-up coordinate system.
	static FVector ConvertDirectionLeftHandedZup(
		float InX, // direction X in world coordinates
		float InY, // direction Y in world coordinates
		float InZ  // direction Z in world coordinates
	);

	// Convert a direction from a right-handed Z-up coordinate system to Unreal left-handed Z-up coordinate system.
	static FVector ConvertDirectionRightHandedZup(
		float InX, // direction X in world coordinates
		float InY, // direction Y in world coordinates
		float InZ  // direction Z in world coordinates
	);

protected:

	// Coordinate system type of the world geometries and transforms.
	static ECoordinateSystemType WorldCoordinateSystemType;

	// Scale factor from world units to Datasmith centimeters.
	static float WorldUnitScale;

	// Datasmith scene element name (i.e. ID).
	FString ElementName;

	// Datasmith scene element label (i.e. UI name).
	FString ElementLabel;
};
