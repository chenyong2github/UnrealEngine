// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpMaterial.h"

#include "DatasmithSketchUpCommon.h"
#include "DatasmithSketchUpComponent.h"
#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpMetadata.h"
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
				: Context.Textures.AddTexture(InMaterial.TextureRef, InMaterial.SketchupSourceName);

			IDatasmithMaterialExpressionTexture* ExpressionTexture = DatasmithMaterialElementPtr->AddMaterialExpression< IDatasmithMaterialExpressionTexture >();
			ExpressionTexture->SetName(TEXT("Texture"));
			ExpressionTexture->SetTexturePathName(Texture->GetDatasmithElementName());

			// Apply texture scaling
			if (bInScaleTexture && !Texture->TextureScale.Equals(FVector2D::UnitVector))
			{
				IDatasmithMaterialExpressionFunctionCall* UVEditExpression = DatasmithMaterialElementPtr->AddMaterialExpression< IDatasmithMaterialExpressionFunctionCall >();
				UVEditExpression->SetFunctionPathName(TEXT("/DatasmithContent/Materials/UVEdit.UVEdit"));

				UVEditExpression->ConnectExpression(ExpressionTexture->GetInputCoordinate());

				// Tiling
				IDatasmithMaterialExpressionColor* TilingValue = DatasmithMaterialElementPtr->AddMaterialExpression< IDatasmithMaterialExpressionColor >();
				TilingValue->SetName(TEXT("UV Tiling"));
				TilingValue->GetColor() = FLinearColor(Texture->TextureScale.X, Texture->TextureScale.Y, 0.f);

				TilingValue->ConnectExpression(*UVEditExpression->GetInput(2));

				//IDatasmithMaterialExpressionColor* OffsetValue = MaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionColor >();
				//OffsetValue->SetName(TEXT("UV Offset"));
				//OffsetValue->GetColor() = FLinearColor(0.f, 0.f, 0.f);
				//OffsetValue->ConnectExpression(*UVEditExpression->GetInput(7));

				IDatasmithMaterialExpressionTextureCoordinate* TextureCoordinateExpression = DatasmithMaterialElementPtr->AddMaterialExpression< IDatasmithMaterialExpressionTextureCoordinate >();
				TextureCoordinateExpression->SetCoordinateIndex(0);
				TextureCoordinateExpression->ConnectExpression(*UVEditExpression->GetInput(0));
			}

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

void FMaterial::Remove(FExportContext& Context)
{

	if (MaterialDirectlyAppliedToMeshes)

	{
		Context.DatasmithScene->RemoveMaterial(MaterialDirectlyAppliedToMeshes->DatasmithElement);
	}

	if (MaterialInheritedByNodes)
	{
		Context.DatasmithScene->RemoveMaterial(MaterialInheritedByNodes->DatasmithElement);
	}

}

void FMaterial::Update(FExportContext& Context)
{
	FExtractedMaterial ExtractedMaterial(Context, MaterialRef);
	EntityId = ExtractedMaterial.SketchupSourceID.EntityID;


	{
		TSharedPtr<IDatasmithBaseMaterialElement> Element = CreateMaterialElement(Context, ExtractedMaterial, *ExtractedMaterial.LocalizedMaterialName, false);

		if (MaterialDirectlyAppliedToMeshes)
		{
			Context.DatasmithScene->RemoveMaterial(MaterialDirectlyAppliedToMeshes->DatasmithElement);
			MaterialDirectlyAppliedToMeshes->DatasmithElement = Element;
		}
		else
		{
			MaterialDirectlyAppliedToMeshes = MakeShared<FMaterialOccurrence>(Element);
		}
	}

	{
		TSharedPtr<IDatasmithBaseMaterialElement> Element = CreateMaterialElement(Context, ExtractedMaterial, *ExtractedMaterial.InheritedMaterialName, true);

		if (MaterialInheritedByNodes)
		{
			Context.DatasmithScene->RemoveMaterial(MaterialInheritedByNodes->DatasmithElement);
			MaterialInheritedByNodes->DatasmithElement = Element;
		}
		else
		{
			MaterialInheritedByNodes = MakeShared<FMaterialOccurrence>(Element);
		}
	}
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

