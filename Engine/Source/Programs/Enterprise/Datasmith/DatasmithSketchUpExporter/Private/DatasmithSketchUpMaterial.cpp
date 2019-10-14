// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpMaterial.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/component_instance.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/texture.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Array.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"


TMap<int32, TSharedPtr<FDatasmithSketchUpMaterial>> FDatasmithSketchUpMaterial::MaterialDefinitionMap;

void FDatasmithSketchUpMaterial::InitMaterialDefinitionMap(
	SUModelRef InSModelRef
)
{
	// Add a default material into our dictionary of material definitions.
	MaterialDefinitionMap.Add(DEFAULT_MATERIAL_ID, TSharedPtr<FDatasmithSketchUpMaterial>(new FDatasmithSketchUpMaterial()));

	// Get the number of material definitions in the SketchUp model.
	size_t SMaterialDefinitionCount = 0;
	SUModelGetNumMaterials(InSModelRef, &SMaterialDefinitionCount); // we can ignore the returned SU_RESULT

	if (SMaterialDefinitionCount > 0)
	{
		// Retrieve the material definitions in the SketchUp model.
		TArray<SUMaterialRef> SMaterialDefinitions;
		SMaterialDefinitions.Init(SU_INVALID, SMaterialDefinitionCount);
		SUModelGetMaterials(InSModelRef, SMaterialDefinitionCount, SMaterialDefinitions.GetData(), &SMaterialDefinitionCount); // we can ignore the returned SU_RESULT
		SMaterialDefinitions.SetNum(SMaterialDefinitionCount);

		// Add the material definitions to our dictionary.
		for (SUMaterialRef SMaterialDefinitionRef : SMaterialDefinitions)
		{
			TSharedPtr<FDatasmithSketchUpMaterial> MaterialPtr = TSharedPtr<FDatasmithSketchUpMaterial>(new FDatasmithSketchUpMaterial(SMaterialDefinitionRef));
			MaterialDefinitionMap.Add(MaterialPtr->SSourceID, MaterialPtr);
		}
	}
}

void FDatasmithSketchUpMaterial::ClearMaterialDefinitionMap()
{
	// Remove all entries from our dictionary of material definitions.
	MaterialDefinitionMap.Empty();
}

SUMaterialRef FDatasmithSketchUpMaterial::GetMaterial(
	SUComponentInstanceRef InSComponentInstanceRef
)
{
	// Retrieve the SketckUp drawing element material.
	SUMaterialRef SMaterialRef = SU_INVALID;
	SUDrawingElementGetMaterial(SUComponentInstanceToDrawingElement(InSComponentInstanceRef), &SMaterialRef); // we can ignore the returned SU_RESULT

	return SMaterialRef;
}

int32 FDatasmithSketchUpMaterial::GetMaterialID(
	SUMaterialRef InSMaterialRef
)
{
	// Get the SketckUp material ID.
	int32 SMaterialID = DEFAULT_MATERIAL_ID;
	SUEntityGetID(SUMaterialToEntity(InSMaterialRef), &SMaterialID); // we can ignore the returned SU_RESULT

	return SMaterialID;
}

FString const& FDatasmithSketchUpMaterial::GetLocalizedMaterialName(
	int32 InSMaterialID
)
{
	// Make sure the SketchUp material ID exists in our dictionary of material definitions.
	int32 SMaterialID = MaterialDefinitionMap.Contains(InSMaterialID) ? InSMaterialID : DEFAULT_MATERIAL_ID;

	TSharedPtr<FDatasmithSketchUpMaterial> MaterialDefinition = MaterialDefinitionMap[SMaterialID];

	// Note that the material definition is used by some meshes.
	MaterialDefinition->bLocalizedByMeshes = true;

	// Return the material definition name sanitized for Datasmith.
	return MaterialDefinition->bMaterialHasTexture ? MaterialDefinition->LocalizedMaterialName : MaterialDefinition->GenericMaterialName;
}

FString const& FDatasmithSketchUpMaterial::GetInheritedMaterialName(
	int32 InSMaterialID
)
{
	// Make sure the SketchUp material ID exists in our dictionary of material definitions.
	int32 SMaterialID = MaterialDefinitionMap.Contains(InSMaterialID) ? InSMaterialID : DEFAULT_MATERIAL_ID;

	TSharedPtr<FDatasmithSketchUpMaterial> MaterialDefinition = MaterialDefinitionMap[SMaterialID];

	// Note that the material definition is used by some meshes.
	MaterialDefinition->bInheritedByMeshes = true;

	// Return the material definition name sanitized for Datasmith.
	return MaterialDefinition->bMaterialHasTexture ? MaterialDefinition->InheritedMaterialName : MaterialDefinition->GenericMaterialName;
}

void FDatasmithSketchUpMaterial::ExportDefinitions(
	TSharedRef<IDatasmithScene> IODSceneRef,
	TCHAR const*                InTextureFileFolder
)
{
	// Export the material definitions used by some meshes.
	for (auto const& MaterialDefinitionEntry : MaterialDefinitionMap)
	{
		// Export the material definition into Datasmith material elements.
		MaterialDefinitionEntry.Value->ExportMaterial(IODSceneRef, InTextureFileFolder);
	}
}

FDatasmithSketchUpMaterial::FDatasmithSketchUpMaterial():
	SSourceMaterialRef(SU_INVALID),
	SSourceID(DEFAULT_MATERIAL_ID),
	SSourceName(TEXT("Default")),
	SSourceType(SUMaterialType_Colored),
	SSourceColor({ 128, 128, 128, 255 }), // default RGBA: sRGB opaque middle gray
	bSSourceColorAlphaUsed(false),
	bMaterialHasTexture(false),
	bSSourceTextureAlphaUsed(false),
	bLocalizedByMeshes(false),
	bInheritedByMeshes(false)
{
	// Make the material names sanitized for Datasmith.
	InitMaterialNames();
}

FDatasmithSketchUpMaterial::FDatasmithSketchUpMaterial(
	SUMaterialRef InSMaterialDefinitionRef
):
	SSourceMaterialRef(InSMaterialDefinitionRef),
	SSourceColor({ 128, 128, 128, 255 }), // default RGBA: sRGB opaque middle gray
	bMaterialHasTexture(false),
	SSourceTextureRef(SU_INVALID),
	bSSourceTextureAlphaUsed(false),
	bLocalizedByMeshes(false),
	bInheritedByMeshes(false)
{
	SUResult SResult = SU_ERROR_NONE;

	// Get the material ID of the SketckUp material.
	SSourceID = GetMaterialID(SSourceMaterialRef);

	// Retrieve the SketchUp material name.
	SU_GET_STRING(SUMaterialGetName, SSourceMaterialRef, SSourceName);
	// Remove any name encasing "[]".
	SSourceName.RemoveFromStart(TEXT("["));
	SSourceName.RemoveFromEnd(TEXT("]"));

	// Get the SketchUp material type.
	SUMaterialGetType(SSourceMaterialRef, &SSourceType); // we can ignore the returned SU_RESULT

	// Get the SketchUp material color.
	SUColor SMaterialColor;
	SResult = SUMaterialGetColor(SSourceMaterialRef, &SMaterialColor);
	// Keep the default opaque middle gray when the material does not have a color value (SU_ERROR_NO_DATA).
	if (SResult == SU_ERROR_NONE)
	{
		SSourceColor = SMaterialColor;
	}

	// Get the flag indicating whether or not the SketchUp color alpha values are used.
	SUMaterialGetUseOpacity(SSourceMaterialRef, &bSSourceColorAlphaUsed); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp material texture.
	SUTextureRef STextureRef = SU_INVALID;
	SResult = SUMaterialGetTexture(SSourceMaterialRef, &STextureRef);
	// Make sure the SketchUp material has a texture (no SU_ERROR_NO_DATA).
	if (SResult == SU_ERROR_NONE)
	{
		SSourceTextureRef = STextureRef;
	}

	if (SUIsValid(SSourceTextureRef))
	{
		bMaterialHasTexture = true;

		// Retrieve the SketchUp texture file name.
		SU_GET_STRING(SUTextureGetFileName, SSourceTextureRef, SSourceTextureFileName);

		// Get the flag indicating whether or not the SketchUp texture alpha channel is used.
		bool bSTextureAlphaUsed = false;
		SResult = SUTextureGetUseAlphaChannel(SSourceTextureRef, &bSTextureAlphaUsed);
		// Make sure the flag was retrieved properly (no SU_ERROR_NO_DATA).
		if (SResult == SU_ERROR_NONE)
		{
			bSSourceTextureAlphaUsed = bSTextureAlphaUsed;
		}

		// Get the pixel scale factors of the source SketchUp texture.
		size_t STextureWidth  = 0;
		size_t STextureHeight = 0;
		SUTextureGetDimensions(SSourceTextureRef, &STextureWidth, &STextureHeight, &SSourceTextureSScale, &SSourceTextureTScale); // we can ignore the returned SU_RESULT

		// Make the texture file name (with extension) sanitized for Datasmith.
		FString TextureBaseName = FPaths::GetBaseFilename(SSourceTextureFileName);
		if (SSourceType == SUMaterialType::SUMaterialType_ColorizedTexture)
		{
			// Set a material-specific texture file name since the saved SketchUp texture will be colorized with the material color.
			TextureBaseName = TextureBaseName + TEXT('-') + SSourceName;
		}
		TextureFileName = FDatasmithUtils::SanitizeFileName(TextureBaseName) + FPaths::GetExtension(SSourceTextureFileName, /*bIncludeDot*/ true);
	}

	// Make the material names sanitized for Datasmith.
	InitMaterialNames();
}

void FDatasmithSketchUpMaterial::InitMaterialNames()
{
	FString SanitizedName = FDatasmithUtils::SanitizeObjectName(SSourceName);
	FString HashedName    = FMD5::HashAnsiString(*SSourceName);

	GenericMaterialName   = FString::Printf(TEXT("%ls-G%ls"), *SanitizedName, *HashedName);
	LocalizedMaterialName = FString::Printf(TEXT("%ls-L%ls"), *SanitizedName, *HashedName);
	InheritedMaterialName = FString::Printf(TEXT("%ls-I%ls"), *SanitizedName, *HashedName);
}

void FDatasmithSketchUpMaterial::ExportMaterial(
	TSharedRef<IDatasmithScene> IODSceneRef,
	TCHAR const*                InTextureFileFolder
) const
{
	if (bMaterialHasTexture)
	{
		if (bLocalizedByMeshes)
		{
			// Create a Datasmith material element for the used locally (non-inherited) material definition.
			TSharedPtr<IDatasmithMasterMaterialElement> DMaterialElementPtr = CreateMaterialElement(*LocalizedMaterialName, InTextureFileFolder, true, false);

			// Add the material element to the Datasmith scene.
			IODSceneRef->AddMaterial(DMaterialElementPtr);
		}

		if (bInheritedByMeshes)
		{
			// Create a Datasmith material element for the inherited material definition.
			TSharedPtr<IDatasmithMasterMaterialElement> DMaterialElementPtr = CreateMaterialElement(*InheritedMaterialName, InTextureFileFolder, !bLocalizedByMeshes, true);

			// Add the material element to the Datasmith scene.
			IODSceneRef->AddMaterial(DMaterialElementPtr);
		}
	}
	else
	{
		if (bLocalizedByMeshes || bInheritedByMeshes)
		{
			// Create a Datasmith material element for the generic material definition.
			TSharedPtr<IDatasmithMasterMaterialElement> DMaterialElementPtr = CreateMaterialElement(*GenericMaterialName, InTextureFileFolder, false, false);

			// Add the material element to the Datasmith scene.
			IODSceneRef->AddMaterial(DMaterialElementPtr);
		}
	}
}

TSharedPtr<IDatasmithMasterMaterialElement> FDatasmithSketchUpMaterial::CreateMaterialElement(
	TCHAR const* InMaterialName,
	TCHAR const* InTextureFileFolder,
	bool         bInWriteTextureFile,
	bool         bInScaleTexture

) const
{
	// Create a Datasmith material element for the material definition.
	TSharedPtr<IDatasmithMasterMaterialElement> DMaterialElementPtr = FDatasmithSceneFactory::CreateMasterMaterial(InMaterialName);

	// Set the material element label used in the Unreal UI.
	FString MaterialLabel = FDatasmithUtils::SanitizeObjectName(SSourceName);
	DMaterialElementPtr->SetLabel(*MaterialLabel);

	TSharedPtr<IDatasmithKeyValueProperty> DMaterialPropertyPtr;

	// Convert the SketchUp sRGB color to a Datasmith linear color.
	FColor       SRGBColor(SSourceColor.red, SSourceColor.green, SSourceColor.blue, bSSourceColorAlphaUsed ? SSourceColor.alpha : 255);
	FLinearColor LinearColor(SRGBColor);

	// Set the Datasmith material element color.
	DMaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Color"));
	DMaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Color);
	DMaterialPropertyPtr->SetValue(*LinearColor.ToString());
	DMaterialElementPtr->AddProperty(DMaterialPropertyPtr);

	if (bMaterialHasTexture)
	{
		FString TextureFilePath = FPaths::Combine(InTextureFileFolder, TextureFileName);

		if (bInWriteTextureFile)
		{
			// Write the SketchUp texture into a file when required.
			SUResult SResult = SUTextureWriteToFile(SSourceTextureRef, TCHAR_TO_UTF8(*TextureFilePath));
			if (SResult == SU_ERROR_SERIALIZATION)
			{
				// TODO: Append an error message to the export summary.
			}
		}

		// Set the Datasmith material element texture usage.
		DMaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("UseTextureImage"));
		DMaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Bool);
		DMaterialPropertyPtr->SetValue(TEXT("true"));
		DMaterialElementPtr->AddProperty(DMaterialPropertyPtr);

		// Set the Datasmith material element texture file path.
		DMaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Texture"));
		DMaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
		DMaterialPropertyPtr->SetValue(*TextureFilePath);
		DMaterialElementPtr->AddProperty(DMaterialPropertyPtr);

		// Scale the Datasmith material element texture.
		DMaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("TextureScale"));
		DMaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Color);
		FString TextureScale;
		if (bInScaleTexture)
		{
			TextureScale = FString::Printf(TEXT("(R=%lf,G=%lf,B=0.0,A=0.0)"), SSourceTextureSScale, SSourceTextureTScale);
		}
		else
		{
			TextureScale = TEXT("(R=1.0,G=1.0,B=0.0,A=0.0)");
		}
		DMaterialPropertyPtr->SetValue(*TextureScale);
		DMaterialElementPtr->AddProperty(DMaterialPropertyPtr);
	}

	// Set whether or not the material or texture alpha values should be used by Datasmith.
	if (bSSourceColorAlphaUsed || bSSourceTextureAlphaUsed)
	{
		DMaterialElementPtr->SetMaterialType(EDatasmithMasterMaterialType::Transparent);
	}
	else
	{
		DMaterialElementPtr->SetMaterialType(EDatasmithMasterMaterialType::Opaque);
	}

	// Set the Datasmith material element opacity.
	if (bSSourceColorAlphaUsed)
	{
		DMaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Opacity"));
		DMaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
		DMaterialPropertyPtr->SetValue(*FString::Printf(TEXT("%f"), float(SSourceColor.alpha) / float(255)));
		DMaterialElementPtr->AddProperty(DMaterialPropertyPtr);
	}

	// Return the Datasmith material element.
	return DMaterialElementPtr;
}
