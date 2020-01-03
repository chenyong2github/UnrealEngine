// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/material.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class IDatasmithScene;
class IDatasmithMasterMaterialElement;


class FDatasmithSketchUpMaterial
{
public:

	static int32 const DEFAULT_MATERIAL_ID   = 0;
	static int32 const INHERITED_MATERIAL_ID = 0;

public:

	// Initialize the dictionary of material definitions.
	static void InitMaterialDefinitionMap(
		SUModelRef InSModelRef // model containing SketchUp material definitions
	);

	// Clear the dictionary of material definitions.
	static void ClearMaterialDefinitionMap();

	// Get the material of a SketckUp component instance.
	static SUMaterialRef GetMaterial(
		SUComponentInstanceRef InSComponentInstanceRef // valid SketckUp component instance
	);

	// Get the material ID of a SketckUp material.
	static int32 GetMaterialID(
		SUMaterialRef InSMaterialRef // valid SketckUp material
	);

	// Return a material name sanitized for Datasmith,
	// while noting that the material definition is used locally (non-inherited) by some meshes.
	static FString const& GetLocalizedMaterialName(
		int32 InSMaterialID // SketchUp material ID
	);

	// Return a material name sanitized for Datasmith,
	// while noting that the material definition is inherited by some meshes from a parent component.
	static FString const& GetInheritedMaterialName(
		int32 InSMaterialID // SketchUp material ID
	);

	// Export the material definitions into the Datasmith scene.
	static void ExportDefinitions(
		TSharedRef<IDatasmithScene> IODSceneRef,        // Datasmith scene to populate
		TCHAR const*                InTextureFileFolder // Datasmith texture file folder path
	);

private:

	FDatasmithSketchUpMaterial();

	FDatasmithSketchUpMaterial(
		SUMaterialRef InSMaterialDefinitionRef // source SketchUp material definition
	);

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpMaterial(FDatasmithSketchUpMaterial const&) = delete;
	FDatasmithSketchUpMaterial& operator=(FDatasmithSketchUpMaterial const&) = delete;

	// Make the material names sanitized for Datasmith.
	void InitMaterialNames();

	// Export the material definition into Datasmith material elements.
	void ExportMaterial(
		TSharedRef<IDatasmithScene> IODSceneRef,        // Datasmith scene to populate
		TCHAR const*                InTextureFileFolder // Datasmith texture file folder path
	) const;

	// Create a Datasmith material element for the material definition.
	TSharedPtr<IDatasmithMasterMaterialElement> CreateMaterialElement(
		TCHAR const* InMaterialName,      // material name sanitized for Datasmith
		TCHAR const* InTextureFileFolder, // Datasmith texture file folder path
		bool         bInWriteTextureFile, // whether or not to write the SketchUp texture into a file
		bool         bInScaleTexture      // whether or not to scale the SketchUp texture
	) const;

private:

	// Dictionary of material definitions indexed by the SketchUp material IDs.
	static TMap<int32, TSharedPtr<FDatasmithSketchUpMaterial>> MaterialDefinitionMap;

	// Source SketchUp material.
	SUMaterialRef SSourceMaterialRef;

	// Source SketchUp material ID.
	int32 SSourceID;

	// Source SketchUp material name.
	FString SSourceName;

	// Source SketchUp material type: SUMaterialType_Colored, SUMaterialType_Textured or SUMaterialType_ColorizedTexture.
	SUMaterialType SSourceType;

	// Source SketchUp material color.
	SUColor SSourceColor;

	// Whether or not the source SketchUp color alpha values are used.
	bool bSSourceColorAlphaUsed;

	// Whether or not the source SketchUp material has a valid texture.
	bool bMaterialHasTexture;

	// Source SketchUp material texture.
	SUTextureRef SSourceTextureRef;

	// Source SketchUp texture file name (without any path).
	FString SSourceTextureFileName;

	// Whether or not the source SketchUp texture alpha channel is used.
	bool bSSourceTextureAlphaUsed;

	// Pixel scale factors of the source SketchUp texture.
	double SSourceTextureSScale;
	double SSourceTextureTScale;

	// Texture file name (with extension) sanitized for Datasmith.
	FString TextureFileName;

	// Generic material name sanitized for Datasmith used without SketchUp material texture.
	FString GenericMaterialName;

	// Used locally (non-inherited) material name sanitized for Datasmith used with unscaled SketchUp material texture.
	FString LocalizedMaterialName;

	// Inherited material name sanitized for Datasmith used with scaled SketchUp material texture.
	FString InheritedMaterialName;

	// Whether or not this material definition is used locally (non-inherited) by some meshes.
	bool bLocalizedByMeshes;

	// Whether or not this material definition is inherited by some meshes from a parent component.
	bool bInheritedByMeshes;
};
