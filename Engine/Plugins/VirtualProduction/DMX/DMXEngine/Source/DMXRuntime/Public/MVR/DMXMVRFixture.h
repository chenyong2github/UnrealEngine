// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/Optional.h"
#include "Serialization/Archive.h"

class FXmlNode;
class FXmlFile;


/** MVR Fixture Addresses as per Standard */
struct DMXRUNTIME_API FDMXMVRFixtureAddresses
{
	FDMXMVRFixtureAddresses()
	{}

	FDMXMVRFixtureAddresses(int32 InUniverse, int32 InAddress)
		: Universe(InUniverse)
		, Address(InAddress)
	{}

	int32 Universe;
	int32 Address;

	/** Serializes an MVR Fixture Address from or into an archive */
	friend FArchive& operator<<(FArchive& Ar, FDMXMVRFixtureAddresses& Addresses)
	{
		Ar << Addresses.Universe;
		Ar << Addresses.Address;

		return Ar;
	}
};

/** MVR Fixture Color CIE as per Standard */
struct DMXRUNTIME_API FDMXMVRColorCIE
{
	float X;
	float Y;
	uint8 YY;

	/** Serializes an MVR Color CIE from or into an archive */
	friend FArchive& operator<<(FArchive& Ar, FDMXMVRColorCIE& ColorCIE)
	{
		Ar << ColorCIE.X;
		Ar << ColorCIE.Y;
		Ar << ColorCIE.YY;

		return Ar;
	}
};

/** MVR Fixture Fixture Mapping as per Standard */
struct DMXRUNTIME_API FDMXMVRFixtureMapping
{
	/** The unique identifier of the MappingDefinition node that will be the source of the mapping. */
	FGuid LinkDef;

	/** The offset in pixels in x direction from top left corner of the source that will be used for the mapped object. */
	TOptional<int32> UX;

	/** The offset in pixels in y direction from top left corner of the source that will be used for the mapped object. */
	TOptional<int32> UY;

	/** The size in pixels in x direction from top left of the starting point. */
	TOptional<int32> OX;

	/** The size in pixels in y direction from top left of the starting point. */
	TOptional<int32> OY;

	/** The rotation around the middle point of the defined rectangle in degree. Positive direction is counter cock wise. */
	TOptional<int32> RZ;

	/** Serializes an MVR Fixture Mapping from or into an archive */
	friend FArchive& operator<<(FArchive& Ar, FDMXMVRFixtureMapping& Mapping)
	{


		Ar << Mapping.LinkDef;
		Ar << Mapping.UX;
		Ar << Mapping.UY;
		Ar << Mapping.OX;
		Ar << Mapping.OY;
		Ar << Mapping.RZ;

		return Ar;
	}
};

/** MVR Fixture Fixture Gobo as per Standard */
struct DMXRUNTIME_API FDMXMVRFixtureGobo
{
	/** The node value is the Gobo used for the fixture. The image ressource must apply to the GDTF standard. Use a FileName to specify. */
	FString Value;

	/** The roation of the Gobo in degree. */
	float Rotation = 0.f;

	/** Serializes an MVR Fixture Gobo from or into an archive */
	friend FArchive& operator<<(FArchive& Ar, FDMXMVRFixtureGobo& Gobo)
	{
		Ar << Gobo.Value;
		Ar << Gobo.Rotation;

		return Ar;
	}
};

/** MVR Fixture Fixture as per Standard */
struct DMXRUNTIME_API FDMXMVRFixture
{
public:
	FDMXMVRFixture();
	FDMXMVRFixture(const FXmlNode* const FixtureNode);

	/** Returns true if this is a valid MVR Fixture */
	bool IsValid() const;

	/** The unique identifier of the object. */
	FGuid UUID;

	/** The name of the object. */
	FString Name;

	/** The location of the object inside the parent coordinate system. */
	TOptional<FTransform> Transform;
	
	/** The name of the file containing the GDTF information for this light fixture. */
	FString GDTFSpec;

	/** The name of the used DMX mode. This has to match the name of a DMXMode in the GDTF file. */
	FString GDTFMode;

	/** A focus point reference that this lighting fixture aims at if this reference exists. */
	TOptional<FGuid> Focus;

	/** Defines if a Object cast Shadows. */
	TOptional<bool> bCastShadows;

	/** A position reference that this lighting fixture belongs to if this reference exists. */
	TOptional<FGuid> Position;

	/** The Fixture Id of the lighting fixture. This is the short name of the fixture. */
	FString FixtureId;

	/** The unit number of the lighting fixture in a position. */
	int32 UnitNumber = 0;
	
	/** Sets the Universe ID, in UE form */
	FORCEINLINE void SetUniverseID(int32 UniverseID) { Addresses.Universe = UniverseID - 1; }

	/** Returns the Universe ID, in UE form */
	FORCEINLINE int32 GetUniverseID() const { return Addresses.Universe + 1; }

	/** Sets the Address of the MVR Fixture */
	FORCEINLINE void SetStartingChannel(int32 Address) { Addresses.Address = Address; }

	/** Returns the Address of the MVR Fixture */
	FORCEINLINE int32 GetStartingChannel() const { return Addresses.Address; }

private:
	/** The container for DMX Addresses for this fixture. */
	FDMXMVRFixtureAddresses Addresses;

public:
	/** A color assigned to a fixture. If it is not defined, there is no color for the fixture. */
	TOptional<FDMXMVRColorCIE> CIEColor;

	/** The Fixture Type ID is a value that can be used as a short name of the Fixture Type. This does not have to be unique. The default value is 0. */
	TOptional<int32> FixtureTypeId;

	/** The Custom ID is a value that can be used as a short name of the Fixture Instance. This does not have to be unique. The default value is 0. */
	TOptional<int32> CustomId;

	/** The container for Mappings for this fixture. */
	TOptional<FDMXMVRFixtureMapping> Mapping;

	/** The Gobo used for the fixture. The image ressource must apply to the GDTF standard. */
	TOptional<FDMXMVRFixtureGobo> Gobo;

	/** Serializes an MVR Fixture from or into an archive */
	friend FArchive& operator<<(FArchive& Ar, FDMXMVRFixture& MVRFixture)
	{
		Ar << MVRFixture.UUID;
		Ar << MVRFixture.Name;
		Ar << MVRFixture.Transform;
		Ar << MVRFixture.GDTFSpec;
		Ar << MVRFixture.GDTFMode;
		Ar << MVRFixture.Focus;
		Ar << MVRFixture.bCastShadows;
		Ar << MVRFixture.Position;
		Ar << MVRFixture.FixtureId;
		Ar << MVRFixture.UnitNumber;	
		Ar << MVRFixture.Addresses;
		Ar << MVRFixture.CIEColor;
		Ar << MVRFixture.FixtureTypeId;
		Ar << MVRFixture.CustomId;
		Ar << MVRFixture.Mapping;
		Ar << MVRFixture.Gobo;
		
		return Ar;		 
	}
};
