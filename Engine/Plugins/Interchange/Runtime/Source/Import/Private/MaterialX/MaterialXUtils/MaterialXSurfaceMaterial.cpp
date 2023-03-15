// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "MaterialXSurfaceMaterial.h"
#include "InterchangeImportLog.h"
#include "MaterialX/MaterialXUtils/MaterialXManager.h"
#include "MaterialX/MaterialXUtils/MaterialXStandardSurfaceShader.h"
#include "MaterialX/MaterialXUtils/MaterialXSurfaceUnlitShader.h"

namespace mx = MaterialX;

FMaterialXSurfaceMaterial::FMaterialXSurfaceMaterial(UInterchangeBaseNodeContainer& BaseNodeContainer)
	: FMaterialXBase(BaseNodeContainer)
{}

TSharedRef<FMaterialXBase> FMaterialXSurfaceMaterial::MakeInstance(UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	return MakeShared<FMaterialXSurfaceMaterial>(BaseNodeContainer);
}

void FMaterialXSurfaceMaterial::Translate(MaterialX::NodePtr SurfaceMaterialNode)
{
	bool bHasSupportedSurfaceShader = false;

	for(mx::InputPtr Input : SurfaceMaterialNode->getInputs())
	{
		mx::NodePtr ConnectedNode = Input->getConnectedNode();

		if(ConnectedNode)
		{
			TSharedPtr<FMaterialXBase> ShaderTranslator = FMaterialXManager::GetInstance().GetShaderTranslator(ConnectedNode->getCategory().c_str(), NodeContainer);
			bHasSupportedSurfaceShader = ShaderTranslator != nullptr;
			if(bHasSupportedSurfaceShader)
			{
				ShaderTranslator->Translate(ConnectedNode);
			}
		}
	}

	if(!bHasSupportedSurfaceShader)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("the surfaceshader of <%s> is not supported"), ANSI_TO_TCHAR(SurfaceMaterialNode->getName().c_str()));
	}
}

#endif