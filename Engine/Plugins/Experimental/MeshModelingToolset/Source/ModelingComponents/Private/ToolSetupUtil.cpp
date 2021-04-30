// Copyright Epic Games, Inc. All Rights Reserved.


#include "ToolSetupUtil.h"

#include "Curves/CurveFloat.h"
#include "InteractiveTool.h"
#include "InteractiveToolManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"


UMaterialInterface* ToolSetupUtil::GetDefaultMaterial()
{
	return UMaterial::GetDefaultMaterial(MD_Surface);
}


UMaterialInterface* ToolSetupUtil::GetDefaultMaterial(UInteractiveToolManager* ToolManager, UMaterialInterface* SourceMaterial)
{
	if (SourceMaterial == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return SourceMaterial;
}


UMaterialInstanceDynamic* ToolSetupUtil::GetVertexColorMaterial(UInteractiveToolManager* ToolManager)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/MeshVertexColorMaterial"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		return MatInstance;
	}
	return nullptr;
}


UMaterialInterface* ToolSetupUtil::GetDefaultWorkingMaterial(UInteractiveToolManager* ToolManager)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/InProgressMaterial"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}


UMaterialInstanceDynamic* ToolSetupUtil::GetUVCheckerboardMaterial(double CheckerDensity)
{
	UMaterial* CheckerMaterialBase = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/CheckerMaterial"));
	if (CheckerMaterialBase != nullptr)
	{
		UMaterialInstanceDynamic* CheckerMaterial = UMaterialInstanceDynamic::Create(CheckerMaterialBase, NULL);
		if (CheckerMaterial != nullptr)
		{
			CheckerMaterial->SetScalarParameterValue("Density", CheckerDensity);
			return CheckerMaterial;
		}
	}
	return UMaterialInstanceDynamic::Create(GetDefaultMaterial(), NULL);
}



UMaterialInstanceDynamic* ToolSetupUtil::GetDefaultBrushVolumeMaterial(UInteractiveToolManager* ToolManager)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/BrushIndicatorMaterial"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		return MatInstance;
	}
	return nullptr;
}



UMaterialInterface* ToolSetupUtil::GetDefaultSculptMaterial(UInteractiveToolManager* ToolManager)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SculptMaterial"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}



UMaterialInstanceDynamic* ToolSetupUtil::GetTransparentSculptMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, double Opacity, bool bTwoSided)
{
	// Unfortunately the two-sided flag is not something that we can give as a runtime 
	// parameter, so we need separate versions of the material.
	UMaterial* Material = bTwoSided ? LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SculptMaterial_TransparentTwoSided"))
		: LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SculptMaterial_Transparent"));

	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		MatInstance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return MatInstance;
	}
	return nullptr;
}



UMaterialInterface* ToolSetupUtil::GetImageBasedSculptMaterial(UInteractiveToolManager* ToolManager, ImageMaterialType Type)
{
	UMaterialInterface* Material = nullptr;
	if (Type == ImageMaterialType::DefaultBasic)
	{
		Material = LoadObject<UMaterialInstance>(nullptr, TEXT("/MeshModelingToolset/Materials/SculptMaterial_Basic"));
	}
	else if (Type == ImageMaterialType::DefaultSoft)
	{
		Material = LoadObject<UMaterialInstance>(nullptr, TEXT("/MeshModelingToolset/Materials/SculptMaterial_Soft"));
	}
	else if (Type == ImageMaterialType::TangentNormalFromView)
	{
		Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SculptMaterial_TangentNormalFromView"));
	}

	if (Material == nullptr && ToolManager != nullptr)
	{
		Material = GetDefaultSculptMaterial(ToolManager);
	}

	return Material;
}



UMaterialInstanceDynamic* ToolSetupUtil::GetCustomImageBasedSculptMaterial(UInteractiveToolManager* ToolManager, UTexture* SetImage)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/ImageBasedMaterial_Master"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		if (SetImage != nullptr)
		{
			MatInstance->SetTextureParameterValue(TEXT("ImageTexture"), SetImage);
		}
		return MatInstance;
	}
	return nullptr;
}



UMaterialInterface* ToolSetupUtil::GetSelectionMaterial(UInteractiveToolManager* ToolManager)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SelectionMaterial"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}


UMaterialInterface* ToolSetupUtil::GetSelectionMaterial(const FLinearColor& UseColor, UInteractiveToolManager* ToolManager, float PercentDepthOffset)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SelectionMaterial"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("ConstantColor"), UseColor);
		if (PercentDepthOffset != 0)
		{
			MatInstance->SetScalarParameterValue(TEXT("PercentDepthOffset"), PercentDepthOffset);
		}
		return MatInstance;
	}
	return Material;
}


UMaterialInstanceDynamic* ToolSetupUtil::GetSimpleCustomMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, float Opacity)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SimpleBaseMaterial_Transparent"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		MatInstance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return MatInstance;
	}
	return nullptr;
}

MODELINGCOMPONENTS_API UMaterialInstanceDynamic* ToolSetupUtil::GetSimpleCustomMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SimpleBaseMaterial"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		return MatInstance;
	}
	return nullptr;
}

UMaterialInstanceDynamic* ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, float PercentDepthOffset, float Opacity)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SimpleTwoSidedOffsetMaterial_Transparent"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		MatInstance->SetScalarParameterValue(TEXT("PercentDepthOffset"), PercentDepthOffset);
		MatInstance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return MatInstance;
	}
	return nullptr;
}

MODELINGCOMPONENTS_API UMaterialInstanceDynamic* ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, float PercentDepthOffset)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SimpleTwoSidedOffsetMaterial"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		MatInstance->SetScalarParameterValue(TEXT("PercentDepthOffset"), PercentDepthOffset);
		return MatInstance;
	}
	return nullptr;
}

UMaterialInstanceDynamic* ToolSetupUtil::GetCustomDepthOffsetMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, float PercentDepthOffset, float Opacity)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SimpleOffsetMaterial_Transparent"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		MatInstance->SetScalarParameterValue(TEXT("PercentDepthOffset"), PercentDepthOffset);
		MatInstance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return MatInstance;
	}
	return nullptr;
}

MODELINGCOMPONENTS_API UMaterialInstanceDynamic* ToolSetupUtil::GetCustomDepthOffsetMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, float PercentDepthOffset)
{
	UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/SimpleOffsetMaterial"));
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, ToolManager);
		MatInstance->SetVectorParameterValue(TEXT("Color"), Color);
		MatInstance->SetScalarParameterValue(TEXT("PercentDepthOffset"), PercentDepthOffset);
		return MatInstance;
	}
	return nullptr;
}

UMaterialInterface* ToolSetupUtil::GetDefaultEditVolumeMaterial()
{
	return LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/VolumeEditMaterial"));
}


UMaterialInterface* ToolSetupUtil::GetDefaultPointComponentMaterial(UInteractiveToolManager* ToolManager, bool bDepthTested)
{
	UMaterialInterface* Material = (bDepthTested) ?
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/PointSetComponentMaterial"))
		: LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/PointSetOverlaidComponentMaterial"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}

UMaterialInterface* ToolSetupUtil::GetRoundPointComponentMaterial(UInteractiveToolManager* ToolManager, bool bDepthTested)
{
	UMaterialInterface* Material = (bDepthTested) ?
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/PointSetComponentMaterial_Round"))
		: LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/PointSetOverlaidComponentMaterial_Round"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}

UMaterialInterface* ToolSetupUtil::GetDefaultLineComponentMaterial(UInteractiveToolManager* ToolManager, bool bDepthTested)
{
	UMaterialInterface* Material = (bDepthTested) ?
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/LineSetComponentMaterial"))
		: LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/LineSetOverlaidComponentMaterial"));
	if (Material == nullptr && ToolManager != nullptr)
	{
		// We don't seem to have a default line material to use here.
		return ToolManager->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	}
	return Material;
}

UCurveFloat* ToolSetupUtil::GetContrastAdjustmentCurve(UInteractiveToolManager* ToolManager)
{
	// This curve would currently be shared across any tools that need such a curve. We'll probably want to revisit this
	// once it is used in multiple tools.
	UCurveFloat* Curve = LoadObject<UCurveFloat>(nullptr, TEXT("/MeshModelingToolset/Curves/ContrastAdjustmentCurve"));

	// Create a transient duplicate of the curve, as we are going to expose this curve to the user, and we
	// do not want them editing the default Asset.
	UCurveFloat* CurveCopy = DuplicateObject<UCurveFloat>(Curve, GetTransientPackage());

	return CurveCopy;
}