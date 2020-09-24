// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith SDK.
#include "DatasmithSceneFactory.h"

class FDatasmithFacadeScene;

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

	/** 
	 * Set the scale factor from world units to Datasmith centimeters.
	 * If the given value is too close to 0, it is clamped to SMALL_NUMBER value.
	 */
	static void SetWorldUnitScale(
		float InWorldUnitScale // scale factor from world units to Datasmith centimeters
	);

	virtual ~FDatasmithFacadeElement() {}

	// Hash the given InString and fills the OutBuffer up to the BufferSize. A string hash has 32 character (+ null character).
	static void GetStringHash( const TCHAR* InString, TCHAR OutBuffer[33], size_t BufferSize );

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

	// Build and export the Datasmith scene element asset when required.
	// This must be done before building a Datasmith scene element.
	virtual void ExportAsset(
		FString const& InAssetFolder // Datasmith asset folder path
	);

	// Build a Datasmith scene element and add it to the Datasmith scene.
	virtual void BuildScene(
		FDatasmithFacadeScene& SceneRef // Datasmith scene
	) = 0;

	TSharedRef<IDatasmithElement>& GetDatasmithElement() { return InternalDatasmithElement;	}

	const TSharedRef<IDatasmithElement>& GetDatasmithElement() const { return InternalDatasmithElement;	}

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

	FDatasmithFacadeElement(
		const TSharedRef<IDatasmithElement>& InElement
	);

	// Coordinate system type of the world geometries and transforms.
	static ECoordinateSystemType WorldCoordinateSystemType;

	// Scale factor from world units to Datasmith centimeters.
	static float WorldUnitScale;

	TSharedRef<IDatasmithElement> InternalDatasmithElement;
};
