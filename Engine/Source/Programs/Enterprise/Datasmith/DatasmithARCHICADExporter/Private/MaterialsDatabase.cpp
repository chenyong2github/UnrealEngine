// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialsDatabase.h"

#include "ModelMaterial.hpp"
#include "Texture.hpp"
#include "AttributeIndex.hpp"

#include "Synchronizer.h"
#include "TexturesCache.h"

#include "DatasmithSceneFactory.h"

BEGIN_NAMESPACE_UE_AC

// Constructor
FMaterialsDatabase::FMaterialsDatabase() {}

// Destructor
FMaterialsDatabase::~FMaterialsDatabase() {}

// Reset
void FMaterialsDatabase::Clear()
{
	MapMaterials.clear();
}

const FMaterialsDatabase::FMaterialSyncData& FMaterialsDatabase::GetMaterial(const FSyncContext& SyncContext,
																			 GS::Int32			 inACMaterialIndex,
																			 GS::Int32 inACTextureIndex, ESided InSided)
{
	// Test invariant
	if (inACMaterialIndex <= kInvalidMaterialIndex)
	{
		UE_AC_DebugF("FMaterialsDatabase::GetMaterial - Invalid material index (%d)\n", inACMaterialIndex);
		inACMaterialIndex = 1;
	}
	// Test invariant
	if (inACTextureIndex < kInvalidMaterialIndex)
	{
		UE_AC_DebugF("FMaterialsDatabase::GetMaterial - Invalid texture index (%d)\n", inACTextureIndex);
		inACTextureIndex = kInvalidMaterialIndex;
	}
	FMaterialKey MaterialKey(inACMaterialIndex, inACTextureIndex, InSided);

	FMaterialsDatabase::FMaterialSyncData& material = MapMaterials[MaterialKey];
	if (!material.bIsInitialized)
	{
		InitMaterial(SyncContext, MaterialKey, &material);
	}

	return material;
}

void FMaterialsDatabase::InitMaterial(const FSyncContext& SyncContext, const FMaterialKey& MaterialKey,
									  FMaterialSyncData* Material)
{
	if (Material->bIsInitialized)
	{
		return;
	}
	Material->bIsInitialized = true;

	// Get modeler material
	ModelerAPI::AttributeIndex IndexMaterial(ModelerAPI::AttributeIndex::MaterialIndex, MaterialKey.ACMaterialIndex);
	ModelerAPI::Material	   AcMaterial;
	SyncContext.GetModel().GetMaterial(IndexMaterial, &AcMaterial);
	ModelerAPI::AttributeIndex IndexTexture;
	AcMaterial.GetTextureIndex(IndexTexture);
	GS::Int32 textureIndex = IndexTexture.GetOriginalModelerIndex();
	if (MaterialKey.ACTextureIndex != kInvalidMaterialIndex)
	{
		textureIndex = MaterialKey.ACTextureIndex;
	}

	// Get 3D DB material (for guid and ModiTime)
	GS::UniString	DisplayName;
	API_Component3D CUmat;
	BNZeroMemory(&CUmat, sizeof(CUmat));
	CUmat.header.typeID = API_UmatID;
	CUmat.header.index = MaterialKey.ACMaterialIndex;
	CUmat.umat.mater.head.uniStringNamePtr = &DisplayName;
	UE_AC_TestGSError(ACAPI_3D_GetComponent(&CUmat));
	API_Guid MatGuid = CUmat.umat.mater.head.guid;
	if (MatGuid == APINULLGuid)
	{
		// Simulate a Guid from material name and properties
		MD5::Generator g;
		std::string	   name(DisplayName.ToUtf8());
		g.Update(name.c_str(), (unsigned int)name.size());
		const char* p1 = (const char*)&CUmat.umat.mater.mtype;
		g.Update(p1, (unsigned int)((const char*)&CUmat.umat.mater.texture - p1));
		MD5::FingerPrint fp;
		g.Finish(fp);
		MatGuid = Fingerprint2API_Guid(fp);
		if (textureIndex > 0)
		{
			// Add the texture finderprint
			MatGuid =
				CombineGuid(MatGuid, SyncContext.GetTexturesCache().GetTexture(SyncContext, textureIndex).Fingerprint);
		}
		UE_AC_VerboseF("Simulate Guid for material %d, %s Guid=%s\n", MaterialKey.ACMaterialIndex, DisplayName.ToUtf8(),
					   APIGuidToString(MatGuid).ToUtf8());
	}
	Material->MaterialId = APIGuid2GSGuid(MatGuid);
	Material->DatasmithId = GSStringToUE(APIGuidToString(MatGuid));

	Material->bHasTexture = false;
	Material->CosAngle = 1;
	Material->SinAngle = 0;
	Material->InvXSize = 1;
	Material->InvYSize = 1;

	// If the material use a texture
	const FTexturesCache::FTexturesCacheElem* texture = nullptr;
	if (textureIndex > 0)
	{
		// Add the texture info to SyncDatabase
		texture = &SyncContext.GetTexturesCache().GetTexture(SyncContext, textureIndex);

		Material->bHasTexture = true;

		Material->CosAngle = cos(AcMaterial.GetTextureRotationAngle());
		Material->SinAngle = sin(AcMaterial.GetTextureRotationAngle());
		Material->InvXSize = texture->InvXSize;
		Material->InvYSize = texture->InvYSize;

		if (MaterialKey.ACTextureIndex != kInvalidMaterialIndex)
		{
			GS::UniString fingerprint = APIGuidToString(texture->Fingerprint);

			Material->DatasmithId += TEXT("_");
			Material->DatasmithId += GSStringToUE(fingerprint);

			DisplayName += fingerprint;
			DisplayName += "_";
			DisplayName += texture->TextureLabel;
		}
	}
	Material->DatasmithLabel = GSStringToUE(DisplayName);

	if (MaterialKey.Sided == kDoubleSide)
	{
		Material->DatasmithId += TEXT("_DS");
		Material->DatasmithLabel += TEXT("_DS");
	}

	if (Material->Element.IsValid())
	{
		return;
	}

	TSharedRef< IDatasmithUEPbrMaterialElement > DSMaterial =
		FDatasmithSceneFactory::CreateUEPbrMaterial(*Material->DatasmithId);
	Material->Element = DSMaterial;
	DSMaterial->SetLabel(*Material->DatasmithLabel);

	const float opacity = 1.0f - (float)AcMaterial.GetTransparency();
	const bool	bIsTransparent = opacity != 1.0;

	bool bHasAphaMask = false;

	if (texture != nullptr)
	{
		IDatasmithMaterialExpressionTexture* BaseTextureExpression =
			DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionTexture >();
		BaseTextureExpression->SetTexturePathName(GSStringToUE(APIGuidToString(texture->Fingerprint)));
		BaseTextureExpression->SetName(GSStringToUE(texture->TextureLabel));
		BaseTextureExpression->ConnectExpression(DSMaterial->GetBaseColor());

		if (texture->bHasAlpha && texture->bAlphaIsTransparence && bIsTransparent)
		{
			IDatasmithMaterialExpressionGeneric* MultiplyExpression =
				DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
			MultiplyExpression->SetExpressionName(TEXT("Multiply"));
			MultiplyExpression->SetName(TEXT("Multiply Expression"));

			IDatasmithMaterialExpressionScalar* OpacityExpression =
				DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
			OpacityExpression->GetScalar() = opacity;
			OpacityExpression->SetName(TEXT("Opacity"));

			IDatasmithExpressionInput* MultiplyInputA = MultiplyExpression->GetInput(0);
			IDatasmithExpressionInput* MultiplyInputB = MultiplyExpression->GetInput(1);

			MultiplyExpression->ConnectExpression(DSMaterial->GetOpacity());

			BaseTextureExpression->ConnectExpression(*MultiplyInputA, 4);
			OpacityExpression->ConnectExpression(*MultiplyInputB);
		}
		else if (texture->bHasAlpha && texture->bAlphaIsTransparence)
		{
			BaseTextureExpression->ConnectExpression(DSMaterial->GetOpacity(), 4);
		}
	}
	else
	{
		// Diffuse color
		const ModelerAPI::Color			   surfaceColor = AcMaterial.GetSurfaceColor();
		IDatasmithMaterialExpressionColor* DiffuseExpression =
			DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionColor >();
		if (DiffuseExpression != nullptr)
		{
			DiffuseExpression->GetColor() = FColor((uint8)(surfaceColor.red * 255), (uint8)(surfaceColor.green * 255),
												   (uint8)(surfaceColor.blue * 255));
			DiffuseExpression->SetName(TEXT("Base Color"));
			DiffuseExpression->ConnectExpression(DSMaterial->GetBaseColor());
		}
	}

	// Specular color
	IDatasmithMaterialExpressionScalar* specularExpression =
		DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
	specularExpression->GetScalar() = (float)AcMaterial.GetSpecularReflection();
	specularExpression->SetName(TEXT("Specular"));
	specularExpression->ConnectExpression(DSMaterial->GetSpecular());

	// Emissive color
	const ModelerAPI::Color			   EmissiveColor = AcMaterial.GetEmissionColor();
	IDatasmithMaterialExpressionColor* EmissiveExpression =
		DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionColor >();
	EmissiveExpression->GetColor() =
		FColor((uint8)(EmissiveColor.red * 255), (uint8)(EmissiveColor.green * 255), (uint8)(EmissiveColor.blue * 255));
	EmissiveExpression->SetName(TEXT("Emissive Color"));
	EmissiveExpression->ConnectExpression(DSMaterial->GetEmissiveColor());

	// Opacity
	if (!bHasAphaMask && bIsTransparent)
	{
		IDatasmithMaterialExpressionScalar* OpacityExpression =
			DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		OpacityExpression->GetScalar() = opacity;
		OpacityExpression->SetName(TEXT("Opacity"));
		OpacityExpression->ConnectExpression(DSMaterial->GetOpacity());
	}

	if (MaterialKey.Sided == kDoubleSide)
	{
		DSMaterial->SetTwoSided(true);
	}

	// Metallic
	float metallic = 0.0;
	if (DisplayName.Contains("Metal"))
	{
		metallic = 1.0;
	}
	IDatasmithMaterialExpressionScalar* MetallicExpression =
		DSMaterial->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
	MetallicExpression->GetScalar() = metallic;
	MetallicExpression->SetName(TEXT("Metallic"));
	MetallicExpression->ConnectExpression(DSMaterial->GetMetallic());

	SyncContext.GetScene().AddMaterial(DSMaterial);
}

END_NAMESPACE_UE_AC
