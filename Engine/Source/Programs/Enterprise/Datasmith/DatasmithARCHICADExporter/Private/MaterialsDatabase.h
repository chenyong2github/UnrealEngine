// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

#include <map>

class IDatasmithBaseMaterialElement;

BEGIN_NAMESPACE_UE_AC

class FSyncContext;

enum ESided : bool
{
	kSingleSide = false,
	kDoubleSide = true
};

enum : int32
{
	kInvalidMaterialIndex = 0
};

class FMaterialKey
{
  public:
	FMaterialKey(GS::Int32 InACMaterialIndex, GS::Int32 InACTextureIndex, ESided InSided)
		: ACMaterialIndex(InACMaterialIndex)
		, ACTextureIndex(InACTextureIndex)
		, Sided(InSided)
	{
	}

	GS::Int32 ACMaterialIndex;
	GS::Int32 ACTextureIndex;
	ESided	  Sided;

	// less operator needed for use as map key
	bool operator<(const FMaterialKey& InOther) const
	{
		return ACMaterialIndex < InOther.ACMaterialIndex ||
			   (ACMaterialIndex == InOther.ACMaterialIndex &&
				(ACTextureIndex < InOther.ACTextureIndex ||
				 (ACTextureIndex < InOther.ACTextureIndex && Sided < InOther.Sided)));
	}
};

// Materials database
class FMaterialsDatabase
{
  public:
	// Class to permit sync of material
	class FMaterialSyncData
	{
	  public:
		// Constructor
		FMaterialSyncData() {}

		void Reset()
		{
			bUsed = false;
			bMaterialChanged = false;
			bReallyUsed = false;
		}

		// Return the Datasmith Id (Name) {Material GUID + Texture GUID + "_S"}
		const FString& GetDatasmithId() const { return DatasmithId; }

		// Return the Datasmith Label (Displayable name) {Material name + Texture name + "_S"}
		const FString& GetDatasmithLabel() const { return DatasmithLabel; }

		bool	 bIsInitialized = false;
		GS::Guid MaterialId; // Guid (real or simulated)
		GS::Guid TextureId; // Guid (MD5 content computed)
		ESided	 Side = kSingleSide; // If this material must be double sided
		FString	 DatasmithId;
		FString	 DatasmithLabel;

		int32 MaterialIndex = kInvalidMaterialIndex; // AC Material Index
		int32 TextureIndex = kInvalidMaterialIndex; // AC Texture Index

		bool										bUsed = true; // True if this material is used
		bool										bReallyUsed = false;
		bool										bHasTexture = false; // True if this material has texture
		double										CosAngle = 1.0; // Texture's angle cosinus
		double										SinAngle = 0.0; // Texture's angle sinus
		double										InvXSize = 1.0; // Used to compute uv
		double										InvYSize = 1.0; // Used to compute uv
		bool										bMaterialChanged = false;
		TSharedPtr< IDatasmithBaseMaterialElement > Element;
	};

	// Constructor
	FMaterialsDatabase();

	// Destructor
	~FMaterialsDatabase();

	// Reset
	void Clear();

	const FMaterialSyncData& GetMaterial(const FSyncContext& SyncContext, GS::Int32 inACMaterialIndex,
										 GS::Int32 inACTextureIndex, ESided InSided);

  private:
	typedef std::map< FMaterialKey, FMaterialSyncData > MapSyncData; // Map Material key to Material sync data

	void InitMaterial(const FSyncContext& SyncContext, const FMaterialKey& MaterialKey, FMaterialSyncData* Material);

	MapSyncData MapMaterials;
};

END_NAMESPACE_UE_AC
