// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithDataHelper.h"

#include "CADData.h"
#include "DatasmithSceneFactory.h"
#include "Math/Color.h"

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColor(FColor& Color)
{
	FString Name = FString::FromInt(CADLibrary::BuildColorHash(Color));
	FString Label = FString::Printf(TEXT("%02x%02x%02x%02x"), Color.R, Color.G, Color.B, Color.A);

	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*Name);
	MaterialElement->SetLabel(*Label);

	FLinearColor LinearColor = FLinearColor::FromPow22Color(Color);

	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Diffuse Color"));
	ColorExpression->GetColor() = LinearColor;

	MaterialElement->GetBaseColor().SetExpression(ColorExpression);
	MaterialElement->SetParentLabel(TEXT("CAD Color"));

	if (LinearColor.A < 1.0f)
	{
		MaterialElement->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);

		IDatasmithMaterialExpressionScalar* Scalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		Scalar->GetScalar() = LinearColor.A;
		Scalar->SetName(TEXT("Opacity Level"));

		MaterialElement->GetOpacity().SetExpression(Scalar);
		MaterialElement->SetParentLabel(TEXT("CAD Transparent Color"));
	}
	return MaterialElement;
}

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterial(CADLibrary::FCADMaterial& Material, TSharedRef<IDatasmithScene> Scene)
{
	FString Name = FString::FromInt(CADLibrary::BuildMaterialHash(Material));

	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*Name);
	FString MaterialLabel = Material.MaterialName;
	if (MaterialLabel.IsEmpty())
	{
		MaterialLabel = TEXT("Material");
	}
	MaterialElement->SetLabel(*MaterialLabel);

	// Set a diffuse color if there's nothing in the BaseColor
	if (MaterialElement->GetBaseColor().GetExpression() == nullptr)
	{
		FLinearColor LinearColor = FLinearColor::FromPow22Color(Material.Diffuse);

		IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		ColorExpression->SetName(TEXT("Diffuse Color"));
		ColorExpression->GetColor() = LinearColor;

		MaterialElement->GetBaseColor().SetExpression(ColorExpression);
	}

	if (Material.Transparency > 0.0)
	{
		MaterialElement->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
		IDatasmithMaterialExpressionScalar* Scalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		Scalar->GetScalar() = Material.Transparency;
		MaterialElement->GetOpacity().SetExpression(Scalar);
	}

	// Set a Emissive color 
	if (MaterialElement->GetEmissiveColor().GetExpression() == nullptr)
	{
		// Doc CT => TODO
		//GLfloat Specular[4] = { specular.rgb[0] / 255., specular.rgb[1] / 255., specular.rgb[2] / 255., 1. - transparency };
		//GLfloat Shininess[1] = { (float)(128 * shininess) };
		//glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, Specular);
		//glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, Shininess);

		FLinearColor LinearColor = FLinearColor::FromPow22Color(Material.Specular);

		IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		ColorExpression->SetName(TEXT("Specular Color"));
		ColorExpression->GetColor() = LinearColor;

		MaterialElement->GetEmissiveColor().SetExpression(ColorExpression);
	}

	// Simple conversion of shininess and reflectivity to PBR roughness and metallic values; model could be improved to properly blend the values
	if (!FMath::IsNearlyZero(Material.Shininess))
	{
		IDatasmithMaterialExpressionScalar* Scalar = static_cast<IDatasmithMaterialExpressionScalar*>(MaterialElement->AddMaterialExpression(EDatasmithMaterialExpressionType::ConstantScalar));
		Scalar->GetScalar() = 1.f - Material.Shininess;
		Scalar->SetName(TEXT("Shininess"));
		MaterialElement->GetRoughness().SetExpression(Scalar);
	}

	if (!FMath::IsNearlyZero(Material.Reflexion))
	{
		IDatasmithMaterialExpressionScalar* Scalar = static_cast<IDatasmithMaterialExpressionScalar*>(MaterialElement->AddMaterialExpression(EDatasmithMaterialExpressionType::ConstantScalar));
		Scalar->GetScalar() = Material.Reflexion;
		Scalar->SetName(TEXT("Reflexion"));
		MaterialElement->GetMetallic().SetExpression(Scalar);
	}

	return MaterialElement;
}


