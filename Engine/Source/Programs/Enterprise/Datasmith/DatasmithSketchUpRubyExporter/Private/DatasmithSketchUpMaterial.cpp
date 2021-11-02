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
#include "SketchUpAPI/model/typed_value.h"
#include "SketchUpAPI/model/rendering_options.h"
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
		FTexture* Texture,
		bool bInScaleTexture
	)
	{
		// Create a Datasmith material element for the material definition.
		TSharedRef<IDatasmithUEPbrMaterialElement> DatasmithMaterialElementPtr = FDatasmithSceneFactory::CreateUEPbrMaterial(InMaterialName);

		// Set the material element label used in the Unreal UI.
		FString MaterialLabel = FDatasmithUtils::SanitizeObjectName(InMaterial.SketchupSourceName);
		DatasmithMaterialElementPtr->SetLabel(*MaterialLabel);

		
		FLinearColor LinearColor = FMaterial::ConvertColor(InMaterial.SourceColor, InMaterial.bSourceColorAlphaUsed);

		DatasmithMaterialElementPtr->SetTwoSided(false);// todo: consider this

		bool bTranslucent = InMaterial.bSourceColorAlphaUsed;
		if (Texture)
		{
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

			// Set the Datasmith material element opacity.
			if (Texture->GetTextureUseAlphaChannel())
			{
				// Invert texture translarency to get Unreal opacity
				IDatasmithMaterialExpressionGeneric* ExpressionOpacity = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
				ExpressionOpacity->SetExpressionName(TEXT("OneMinus"));


				ExpressionTexture->ConnectExpression(*ExpressionOpacity->GetInput(0), 3);

				ExpressionOpacity->ConnectExpression(DatasmithMaterialElementPtr->GetOpacity());
			}
		}
		else
		{
			IDatasmithMaterialExpressionColor* ExpressionColor = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
			ExpressionColor->SetName(TEXT("Base Color"));
			ExpressionColor->GetColor() = LinearColor;
			ExpressionColor->ConnectExpression(DatasmithMaterialElementPtr->GetBaseColor());

			// Set the Datasmith material element opacity.
			if (InMaterial.bSourceColorAlphaUsed)
			{
				IDatasmithMaterialExpressionScalar* ExpressionOpacity = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
				ExpressionOpacity->SetName(TEXT("Opacity"));
				ExpressionOpacity->GetScalar() = float(InMaterial.SourceColor.alpha) / float(255);
				ExpressionOpacity->ConnectExpression(DatasmithMaterialElementPtr->GetOpacity());
			}
		}


		if (bTranslucent)
		{
			DatasmithMaterialElementPtr->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
		}

		Context.DatasmithScene->AddMaterial(DatasmithMaterialElementPtr);

		// Return the Datasmith material element.
		return DatasmithMaterialElementPtr;
	}
}

FMaterial::FMaterial(SUMaterialRef InMaterialRef)
	: MaterialRef(InMaterialRef)
	, bInvalidated(true)
{}

TSharedPtr<FMaterial> FMaterial::Create(FExportContext& Context, SUMaterialRef InMaterialRef)
{
	TSharedPtr<FMaterial> Material = MakeShared<FMaterial>(InMaterialRef);
	return Material;
}

void FMaterial::Invalidate()
{
	bInvalidated = true;
}

FLinearColor FMaterial::ConvertColor(const SUColor& C, bool bAlphaUsed)
{
	FColor SRGBColor(C.red, C.green, C.blue, bAlphaUsed ? C.alpha : 255);
	return FLinearColor(SRGBColor);
}

TSharedRef<IDatasmithBaseMaterialElement> FMaterial::CreateDefaultMaterialElement(FExportContext& Context)
{
	TSharedRef<IDatasmithUEPbrMaterialElement> DatasmithMaterialElementPtr = FDatasmithSceneFactory::CreateUEPbrMaterial(TEXT("Default"));

	FLinearColor LinearColor(0.5, 0.5, 0.5, 1);

	// Retrieve Front Face color current Style - Styles api doesn't have a way to access it, using RenderingOptions instead
	SURenderingOptionsRef RenderingOptionsRef = SU_INVALID;
	if (SUModelGetRenderingOptions(Context.ModelRef, &RenderingOptionsRef) == SU_ERROR_NONE)
	{
		
		SUTypedValueRef ColorTypedValue = SU_INVALID;
		SUTypedValueCreate(&ColorTypedValue);
		if (SURenderingOptionsGetValue(RenderingOptionsRef, "FaceFrontColor", &ColorTypedValue) == SU_ERROR_NONE)
		{
			SUColor Color;
			if (SUTypedValueGetColor(ColorTypedValue, &Color) == SU_ERROR_NONE)
			{
				LinearColor = ConvertColor(Color);
			}
		}
		SUTypedValueRelease(&ColorTypedValue);
	}

	DatasmithMaterialElementPtr->SetTwoSided(false);// todo: consider this

	IDatasmithMaterialExpressionColor* ExpressionColor = DatasmithMaterialElementPtr->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ExpressionColor->SetName(TEXT("Base Color"));
	ExpressionColor->GetColor() = LinearColor;
	ExpressionColor->ConnectExpression(DatasmithMaterialElementPtr->GetBaseColor());

	Context.DatasmithScene->AddMaterial(DatasmithMaterialElementPtr);

	return DatasmithMaterialElementPtr;
}

void FMaterial::Remove(FExportContext& Context)
{

	MaterialDirectlyAppliedToMeshes.RemoveDatasmithElement(Context);
	MaterialInheritedByNodes.RemoveDatasmithElement(Context);

	Context.Textures.UnregisterMaterial(this);
}

void FMaterial::UpdateTexturesUsage(FExportContext& Context)
{
	if (!bInvalidated)
	{
		return;
	}

	if (Texture)
	{
		Context.Textures.UnregisterMaterial(this);
		Texture = nullptr;
	}

	// todo: extract once(another usage in Update)
	FExtractedMaterial ExtractedMaterial(Context, MaterialRef);

	if (SUIsValid(ExtractedMaterial.TextureRef))
	{
		Texture = (ExtractedMaterial.SourceType == SUMaterialType::SUMaterialType_ColorizedTexture)
			? Context.Textures.AddColorizedTexture(ExtractedMaterial.TextureRef, ExtractedMaterial.SketchupSourceName)
			: Context.Textures.AddTexture(ExtractedMaterial.TextureRef, ExtractedMaterial.SketchupSourceName);
		Context.Textures.RegisterMaterial(this);
	}
}

void FMaterial::Update(FExportContext& Context)
{
	if(!bInvalidated)
	{
		return;
	}

	FExtractedMaterial ExtractedMaterial(Context, MaterialRef);
	EntityId = ExtractedMaterial.SketchupSourceID.EntityID;

	MaterialDirectlyAppliedToMeshes.RemoveDatasmithElement(Context);
	if (MaterialDirectlyAppliedToMeshes.HasUsers())
	{
		MaterialDirectlyAppliedToMeshes.DatasmithElement = CreateMaterialElement(Context, ExtractedMaterial, *ExtractedMaterial.LocalizedMaterialName, Texture, false);
	}

	MaterialInheritedByNodes.RemoveDatasmithElement(Context);
	if (MaterialInheritedByNodes.HasUsers())
	{
		MaterialInheritedByNodes.DatasmithElement = CreateMaterialElement(Context, ExtractedMaterial, *ExtractedMaterial.InheritedMaterialName, Texture, true);
	}

	FMaterialIDType MaterialId = DatasmithSketchUpUtils::GetMaterialID(MaterialRef);
	MaterialDirectlyAppliedToMeshes.Apply(MaterialId);
	MaterialInheritedByNodes.Apply(MaterialId);

	bInvalidated = false;
}

FMaterialOccurrence& FMaterial::RegisterGeometry(FEntitiesGeometry* Geom)
{
	MaterialDirectlyAppliedToMeshes.RegisterGeometry(Geom);

	if (MaterialDirectlyAppliedToMeshes.IsInvalidated())
	{
		Invalidate(); // Invalidate material if occurrence is not built
	}

	return MaterialDirectlyAppliedToMeshes;
}

void FMaterial::UnregisterGeometry(FExportContext& Context, FEntitiesGeometry* Geom)
{
	MaterialDirectlyAppliedToMeshes.UnregisterGeometry(Context, Geom);
}

FMaterialOccurrence& FMaterial::RegisterInstance(FNodeOccurence* NodeOccurrence)
{
	MaterialInheritedByNodes.RegisterInstance(NodeOccurrence);

	if (MaterialInheritedByNodes.IsInvalidated())
	{
		Invalidate(); // Invalidate material if occurrence is not built
	}

	NodeOccurrence->MaterialOverride = this;
	return MaterialInheritedByNodes;
}

void FMaterial::UnregisterInstance(FExportContext& Context, FNodeOccurence* NodeOccurrence)
{
	MaterialInheritedByNodes.UnregisterInstance(Context, NodeOccurrence);

	NodeOccurrence->MaterialOverride = nullptr;
}

const TCHAR* FMaterialOccurrence::GetName()
{
	if (!DatasmithElement)
	{
		return nullptr;
	}

	return DatasmithElement->GetName();
}

void FMaterialOccurrence::Apply(FMaterialIDType MaterialId)
{
	// Apply material to meshes
	for (FEntitiesGeometry* Geometry : MeshesMaterialDirectlyAppliedTo)
	{
		//Geometry->SetMaterialElementName(DatasmithSketchUpUtils::GetMaterialID(MaterialRef), MaterialDirectlyAppliedToMeshes->GetName());
		for (const TSharedPtr<FDatasmithInstantiatedMesh>& Mesh : Geometry->Meshes)
		{
			if (int32* SlotIdPtr = Mesh->SlotIdForMaterialID.Find(MaterialId))
			{
				Mesh->DatasmithMesh->SetMaterial(GetName(), *SlotIdPtr);
			}
		}
	}

	// Apply material to mesh actors
	for (FNodeOccurence* NodePtr : NodesMaterialInheritedBy)
	{
		FNodeOccurence& Node = *NodePtr;
		FDefinition* EntityDefinition = Node.Entity.GetDefinition();
		DatasmithSketchUp::FEntitiesGeometry& EntitiesGeometry = *EntityDefinition->GetEntities().EntitiesGeometry;

		for (int32 MeshIndex = 0; MeshIndex < Node.MeshActors.Num(); ++MeshIndex)
		{
			// Update Override(Inherited)  Material
			// todo: set inherited material only on mesh actors that have faces with default material, right now setting on every mesh, hot harmful but excessive
			if (EntitiesGeometry.IsMeshUsingInheritedMaterial(MeshIndex))
			{
				const TSharedPtr<IDatasmithMeshActorElement>& MeshActor = Node.MeshActors[MeshIndex];

				// SketchUp has 'material override' only for single('Default') material. 
				// So we reset overrides on the actor to remove this single override(if it was set) and re-add new override
				MeshActor->ResetMaterialOverrides(); // Clear previous override if was set
				MeshActor->AddMaterialOverride(GetName(), EntitiesGeometry.GetInheritedMaterialOverrideSlotId());
			}
		}
	}
}

void FMaterialOccurrence::RemoveDatasmithElement(FExportContext& Context)
{
	if (!DatasmithElement)
	{
		return;
	}
	Context.DatasmithScene->RemoveMaterial(DatasmithElement);
	DatasmithElement.Reset();
}

bool FMaterialOccurrence::RemoveUser(FExportContext& Context)
{
	if(!ensure(UserCount != 0))
	{
		return true;
	}

	UserCount--;

	if (UserCount != 0)
	{
		return false;
	}

	RemoveDatasmithElement(Context);
	return true;
}


void FMaterialOccurrence::RegisterGeometry(FEntitiesGeometry* Geom)
{
	if (!MeshesMaterialDirectlyAppliedTo.Contains(Geom))
	{
		MeshesMaterialDirectlyAppliedTo.Add(Geom);
		AddUser();
	}
}

void FMaterialOccurrence::UnregisterGeometry(FExportContext& Context, FEntitiesGeometry* Geom)
{
	if (!MeshesMaterialDirectlyAppliedTo.Contains(Geom))
	{
		return;
	}

	MeshesMaterialDirectlyAppliedTo.Remove(Geom);
	RemoveUser(Context);
}

void FMaterialOccurrence::RegisterInstance(FNodeOccurence* NodeOccurrence)
{
	NodesMaterialInheritedBy.Add(NodeOccurrence);
	AddUser();
}

void FMaterialOccurrence::UnregisterInstance(FExportContext& Context, FNodeOccurence* NodeOccurrence)
{
	NodesMaterialInheritedBy.Remove(NodeOccurrence);
	RemoveUser(Context);
}

void FMaterialCollection::Update()
{
	// Update usage of textures by materials before updating textures(to only update used textures)
	for (TPair<FMaterialIDType, TSharedPtr<DatasmithSketchUp::FMaterial>> IdAndMaterial : MaterialDefinitionMap)
	{
		FMaterial& Material = *IdAndMaterial.Value;
		if (Material.IsUsed())
		{
			Material.UpdateTexturesUsage(Context);
		}
	}

	Context.Textures.Update();

	// Update materials after textures are updated - some materials might end up using shared texture(in case it has same contents, which is determined in Textures Update)
	for (TPair<FMaterialIDType, TSharedPtr<FMaterial>> IdAndMaterial : MaterialDefinitionMap)
	{
		FMaterial& Material = *IdAndMaterial.Value;
		if (Material.IsUsed())
		{
			Material.Update(Context);
		}
	}

	if (DefaultMaterial.IsInvalidated() && DefaultMaterial.HasUsers())
	{
		DefaultMaterial.DatasmithElement = FMaterial::CreateDefaultMaterialElement(Context);
		DefaultMaterial.Apply(FMaterial::INHERITED_MATERIAL_ID);
	}
}

const TCHAR* FMaterialCollection::GetDefaultMaterialName()
{
	return DefaultMaterial.GetName();
}

