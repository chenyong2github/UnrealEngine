// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpCommon.h"

#include "DatasmithSketchUpComponent.h"
#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMesh.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"
#include "DatasmithSketchUpTexture.h"
#include "DatasmithSketchUpUtils.h"

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
#include "DatasmithMaterialsUtils.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"


using namespace DatasmithSketchUp;


// Structure that holds extracted data from SketchUp material during export process
class FExtractedMaterial
{
public:

	FExtractedMaterial(
		FExportContext& Context,
		SUMaterialRef InSMaterialDefinitionRef // source SketchUp material definition
	);

	// Source SketchUp material referecene
	SUMaterialRef SourceMaterialRef;

	// Extracted from SketchUp
	// Source SketchUp material ID.
	FEntityIDType SketchupSourceID;

	// Source SketchUp material name.
	FString SketchupSourceName;

	// Source SketchUp material type: SUMaterialType_Colored, SUMaterialType_Textured or SUMaterialType_ColorizedTexture.
	SUMaterialType SourceType;

	// Source SketchUp material color.
	SUColor SourceColor;

	// Whether or not the source SketchUp color alpha values are used.
	bool bSourceColorAlphaUsed;

	SUTextureRef TextureRef = SU_INVALID;

	FString LocalizedMaterialName;
	FString InheritedMaterialName;


private:

	// Make the material names sanitized for Datasmith.
	void InitMaterialNames();
};

FExtractedMaterial::FExtractedMaterial(
	FExportContext& Context,
	SUMaterialRef InSMaterialDefinitionRef)
	: SourceMaterialRef(InSMaterialDefinitionRef)
	, SourceColor({ 128, 128, 128, 255 }) // default RGBA: sRGB opaque middle gray
{
	SUResult SResult = SU_ERROR_NONE;

	// Get the material ID of the SketckUp material.
	SketchupSourceID = DatasmithSketchUpUtils::GetMaterialID(SourceMaterialRef);

	// Retrieve the SketchUp material name.
	SketchupSourceName = SuGetString(SUMaterialGetName, SourceMaterialRef);
	// Remove any name encasing "[]".
	SketchupSourceName.RemoveFromStart(TEXT("["));
	SketchupSourceName.RemoveFromEnd(TEXT("]"));

	// Get the SketchUp material type.
	SUMaterialGetType(SourceMaterialRef, &SourceType); // we can ignore the returned SU_RESULT


	// Get the SketchUp material color.
	SUColor SMaterialColor;
	SResult = SUMaterialGetColor(SourceMaterialRef, &SMaterialColor);
	// Keep the default opaque middle gray when the material does not have a color value (SU_ERROR_NO_DATA).
	if (SResult == SU_ERROR_NONE)
	{
		SourceColor = SMaterialColor;
	}

	// Get the flag indicating whether or not the SketchUp color alpha values are used.
	SUMaterialGetUseOpacity(SourceMaterialRef, &bSourceColorAlphaUsed); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp material texture.
	SResult = SUMaterialGetTexture(SourceMaterialRef, &TextureRef);

	// Make the material names sanitized for Datasmith.
	InitMaterialNames();
}

void FExtractedMaterial::InitMaterialNames()
{
	FString SanitizedName = FDatasmithUtils::SanitizeObjectName(SketchupSourceName);
	FString HashedName    = FMD5::HashAnsiString(*SketchupSourceName);

	LocalizedMaterialName = FString::Printf(TEXT("%ls-L%ls"), *SanitizedName, *HashedName);
	InheritedMaterialName = FString::Printf(TEXT("%ls-I%ls"), *SanitizedName, *HashedName);
}

FMaterialIDType const FMaterial::DEFAULT_MATERIAL_ID;
FMaterialIDType const FMaterial::INHERITED_MATERIAL_ID;


namespace DatasmithSketchUp
{
	TSharedPtr<IDatasmithBaseMaterialElement> CreateMaterialElement(
		FExportContext& Context,
		FExtractedMaterial& InMaterial,
		TCHAR const* InMaterialName,
		bool         bInScaleTexture
	)
	{
		// Create a Datasmith material element for the material definition.
		TSharedRef<IDatasmithUEPbrMaterialElement> DatasmithMaterialElementPtr = FDatasmithSceneFactory::CreateUEPbrMaterial(InMaterialName);

		// Set the material element label used in the Unreal UI.
		FString MaterialLabel = FDatasmithUtils::SanitizeObjectName(InMaterial.SketchupSourceName);
		DatasmithMaterialElementPtr->SetLabel(*MaterialLabel);

		// Convert the SketchUp sRGB color to a Datasmith linear color.
		FColor       SRGBColor(InMaterial.SourceColor.red, InMaterial.SourceColor.green, InMaterial.SourceColor.blue, InMaterial.bSourceColorAlphaUsed ? InMaterial.SourceColor.alpha : 255);
		FLinearColor LinearColor(SRGBColor);

		DatasmithMaterialElementPtr->SetTwoSided(false);// todo: consider this

		IDatasmithMaterialExpressionColor* ExpressionColor = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		ExpressionColor->SetName(TEXT("Base Color"));
		ExpressionColor->GetColor() = LinearColor;
		ExpressionColor->ConnectExpression(DatasmithMaterialElementPtr->GetBaseColor());

		bool bTranslucent = InMaterial.bSourceColorAlphaUsed;
		if (SUIsValid(InMaterial.TextureRef))
		{
			FTexture* Texture = (InMaterial.SourceType == SUMaterialType::SUMaterialType_ColorizedTexture)
				? Context.Textures.AddColorizedTexture(InMaterial.TextureRef, InMaterial.SketchupSourceName)
				: Context.Textures.AddTexture(InMaterial.TextureRef);

			DatasmithMaterialsUtils::FUVEditParameters UVParameters;
			if (bInScaleTexture)
			{
				UVParameters.UVTiling = Texture->TextureScale;
			}

			IDatasmithMaterialExpressionTexture* ExpressionTexture = DatasmithMaterialsUtils::CreateTextureExpression(DatasmithMaterialElementPtr, TEXT("Texture"), Texture->GetDatasmithElementName(), UVParameters);

			// todo: multiply or replace base color with texture?
			ExpressionTexture->ConnectExpression(DatasmithMaterialElementPtr->GetBaseColor());

			bTranslucent = bTranslucent || Texture->GetTextureUseAlphaChannel();
		}

		if (bTranslucent)
		{
			DatasmithMaterialElementPtr->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
		}

		// Set the Datasmith material element opacity.
		if (InMaterial.bSourceColorAlphaUsed)
		{
			IDatasmithMaterialExpressionScalar* ExpressionOpacity = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
			ExpressionOpacity->SetName(TEXT("Opacity"));
			ExpressionOpacity->GetScalar() = float(InMaterial.SourceColor.alpha) / float(255);
			ExpressionOpacity->ConnectExpression(DatasmithMaterialElementPtr->GetOpacity());
		}

		Context.DatasmithScene->AddMaterial(DatasmithMaterialElementPtr);

		// Return the Datasmith material element.
		return DatasmithMaterialElementPtr;
	}

}

FMaterial::FMaterial(SUMaterialRef InMaterialRef)
	: MaterialRef(InMaterialRef)

{}

TSharedPtr<FMaterial> FMaterial::Create(FExportContext& Context, SUMaterialRef InMaterialRef)
{
	TSharedPtr<FMaterial> Material = MakeShared<FMaterial>(InMaterialRef);
	Material->Update(Context);
	return Material;
}

TSharedPtr<FMaterialOccurrence> FMaterial::CreateDefaultMaterial(FExportContext& Context)
{
	TSharedRef<IDatasmithUEPbrMaterialElement> DatasmithMaterialElementPtr = FDatasmithSceneFactory::CreateUEPbrMaterial(TEXT("Default"));

	FLinearColor LinearColor(0.5, 0.5, 0.5, 1);

	DatasmithMaterialElementPtr->SetTwoSided(false);// todo: consider this

	IDatasmithMaterialExpressionColor* ExpressionColor = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ExpressionColor->SetName(TEXT("Base Color"));
	ExpressionColor->GetColor() = LinearColor;
	ExpressionColor->ConnectExpression(DatasmithMaterialElementPtr->GetBaseColor());

	Context.DatasmithScene->AddMaterial(DatasmithMaterialElementPtr);

	return MakeShared<FMaterialOccurrence>(DatasmithMaterialElementPtr);
}


void FMaterial::Update(FExportContext& Context)
{
	FExtractedMaterial ExtractedMaterial(Context, MaterialRef);
	EntityId = ExtractedMaterial.SketchupSourceID.EntityID;
	MaterialDirectlyAppliedToMeshes = MakeShared<FMaterialOccurrence>(CreateMaterialElement(Context, ExtractedMaterial, *ExtractedMaterial.LocalizedMaterialName, false));
	MaterialInheritedByNodes = MakeShared<FMaterialOccurrence>(CreateMaterialElement(Context, ExtractedMaterial, *ExtractedMaterial.InheritedMaterialName, true));
}

FMaterialOccurrence& FMaterial::RegisterGeometry(FEntitiesGeometry* Geom)
{
	MeshesMaterialDirectlyAppliedTo.Add(Geom);
	return *MaterialDirectlyAppliedToMeshes;
}

void FMaterial::UnregisterGeometry(FEntitiesGeometry* Geom)
{
	MeshesMaterialDirectlyAppliedTo.Remove(Geom);
}

FMaterialOccurrence& FMaterial::RegisterInstance(FNodeOccurence* NodeOccurrence)
{
	NodesMaterialInheritedBy.Add(NodeOccurrence);
	return *MaterialInheritedByNodes;
}

const TCHAR* FMaterialOccurrence::GetName()
{
	return DatasmithElement->GetName();
}

