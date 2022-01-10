// Copyright Epic Games, Inc. All Rights Reserved.

#define INITIALIZE_A3D_API

#include "TechSoftInterface.h"

#include "CADInterfacesModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "TUniqueTechSoftObj.h"


namespace CADLibrary
{
FTechSoftInterface& GetTechSoftInterface()
{
	static FTechSoftInterface TechSoftInterface;
	return TechSoftInterface;
}

bool TECHSOFT_InitializeKernel(const TCHAR* InEnginePluginsPath)
{
	return GetTechSoftInterface().InitializeKernel(InEnginePluginsPath);
}

bool FTechSoftInterface::InitializeKernel(const TCHAR* InEnginePluginsPath)
{
#ifdef USE_TECHSOFT_SDK
	if (bIsInitialize)
	{
		return true;
	}

	FString EnginePluginsPath(InEnginePluginsPath);
	if (EnginePluginsPath.IsEmpty())
	{
		EnginePluginsPath = FPaths::EnginePluginsDir();
	}

	FString TechSoftDllPath = FPaths::Combine(EnginePluginsPath, TEXT("Enterprise/DatasmithCADImporter"), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory(), "TechSoft");
	TechSoftDllPath = FPaths::ConvertRelativePathToFull(TechSoftDllPath);
	ExchangeLoader = MakeUnique<A3DSDKHOOPSExchangeLoader>(*TechSoftDllPath);

	const A3DStatus IRet = ExchangeLoader->m_eSDKStatus;
	if (IRet != A3D_SUCCESS)
	{
		UE_LOG(LogCADInterfaces, Warning, TEXT("Failed to load required library in %s. Plug-in will not be functional."), *TechSoftDllPath);
	}
	else
	{
		bIsInitialize = true;
	}
	return bIsInitialize;
#else
	return false;
#endif
}

#ifdef USE_TECHSOFT_SDK

A3DStatus FTechSoftInterface::Import(const A3DImport& Import)
{
	return ExchangeLoader->Import(Import);
}

A3DAsmModelFile* FTechSoftInterface::GetModelFile()
{
	return ExchangeLoader->m_psModelFile;
}


void TUniqueTSObj<A3DAsmModelFileData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DAsmModelFileData, Data);
}

void TUniqueTSObj<A3DAsmPartDefinitionData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DAsmPartDefinitionData, Data);
}

void TUniqueTSObj<A3DAsmProductOccurrenceData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DAsmProductOccurrenceData, Data);
}

void TUniqueTSObj<A3DAsmProductOccurrenceDataCV5>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DAsmProductOccurrenceDataCV5, Data);
}

void TUniqueTSObj<A3DAsmProductOccurrenceDataSLW>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DAsmProductOccurrenceDataSLW, Data);
}

void TUniqueTSObj<A3DAsmProductOccurrenceDataUg>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DAsmProductOccurrenceDataUg, Data);
}

void TUniqueTSObj<A3DGraphicsData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DGraphicsData, Data);
}

void TUniqueTSObjFromIndex<A3DGraphMaterialData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DGraphMaterialData, Data);
}

void TUniqueTSObjFromIndex<A3DGraphRgbColorData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DGraphRgbColorData, Data);
}

void TUniqueTSObjFromIndex<A3DGraphStyleData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DGraphStyleData, Data);
}

void TUniqueTSObj<A3DMiscAttributeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscAttributeData, Data);
}

void TUniqueTSObj<A3DMiscCartesianTransformationData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscCartesianTransformationData, Data);
}

void TUniqueTSObj<A3DMiscGeneralTransformationData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscGeneralTransformationData, Data);
}

void TUniqueTSObj<A3DMiscMaterialPropertiesData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscMaterialPropertiesData, Data);
}

void TUniqueTSObj<A3DRiBrepModelData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRiBrepModelData, Data);
}

void TUniqueTSObj<A3DRiCoordinateSystemData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRiCoordinateSystemData, Data);
}

void TUniqueTSObj<A3DRiDirectionData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRiDirectionData, Data);
}

void TUniqueTSObj<A3DRiPolyBrepModelData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRiPolyBrepModelData, Data);
}

void TUniqueTSObj<A3DRiRepresentationItemData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRiRepresentationItemData, Data);
}

void TUniqueTSObj<A3DRiSetData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRiSetData, Data);
}

void TUniqueTSObj<A3DRootBaseData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRootBaseData, Data);
}

void TUniqueTSObj<A3DRootBaseWithGraphicsData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DRootBaseWithGraphicsData, Data);
}

void TUniqueTSObj<A3DTess3DData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTess3DData, Data);
}

void TUniqueTSObj<A3DTessBaseData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTessBaseData, Data);
}

void TUniqueTSObj<A3DTopoBrepDataData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoBrepDataData, Data);
}

void TUniqueTSObj<A3DTopoEdgeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoEdgeData, Data);
}

void TUniqueTSObj<A3DTopoFaceData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoFaceData, Data);
}

void TUniqueTSObj<A3DTopoUniqueVertexData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoUniqueVertexData, Data);
}

void TUniqueTSObj<A3DTopoWireEdgeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoWireEdgeData, Data);
}

A3DStatus TUniqueTSObj<A3DAsmModelFileData>::GetData(const A3DAsmModelFile* InAsmModelFilePtr)
{
	return A3DAsmModelFileGet(InAsmModelFilePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DAsmPartDefinitionData>::GetData(const A3DAsmPartDefinition* InAsmPartDefinitionPtr)
{
	return A3DAsmPartDefinitionGet(InAsmPartDefinitionPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DAsmProductOccurrenceData>::GetData(const A3DAsmProductOccurrence* InAsmProductOccurrencePtr)
{
	return A3DAsmProductOccurrenceGet(InAsmProductOccurrencePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DAsmProductOccurrenceDataCV5>::GetData(const A3DAsmProductOccurrence* InAsmProductOccurrencePtr)
{
	return A3DAsmProductOccurrenceGetCV5(InAsmProductOccurrencePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DAsmProductOccurrenceDataSLW>::GetData(const A3DAsmProductOccurrence* InAsmProductOccurrencePtr)
{
	return A3DAsmProductOccurrenceGetSLW(InAsmProductOccurrencePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DAsmProductOccurrenceDataUg>::GetData(const A3DAsmProductOccurrence* InAsmProductOccurrencePtr)
{
	return A3DAsmProductOccurrenceGetUg(InAsmProductOccurrencePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DGraphicsData>::GetData(const A3DGraphics* InGraphicsPtr)
{
	return A3DGraphicsGet(InGraphicsPtr, &Data);
}

A3DStatus TUniqueTSObjFromIndex<A3DGraphMaterialData>::GetData(uint32 InGraphicsIndex)
{
	return A3DGlobalGetGraphMaterialData(InGraphicsIndex, &Data);
}

A3DStatus TUniqueTSObjFromIndex<A3DGraphRgbColorData>::GetData(uint32 ObjectIndex)
{
	return A3DGlobalGetGraphRgbColorData(ObjectIndex, &Data);
}

A3DStatus TUniqueTSObjFromIndex<A3DGraphStyleData>::GetData(uint32 ObjectIndex)
{
	return A3DGlobalGetGraphStyleData(ObjectIndex, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscAttributeData>::GetData(const A3DMiscAttribute* InMiscAttributePtr)
{
	return A3DMiscAttributeGet(InMiscAttributePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscCartesianTransformationData>::GetData(const A3DMiscCartesianTransformation* InMiscCartesianTransformationPtr)
{
	return A3DMiscCartesianTransformationGet(InMiscCartesianTransformationPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscGeneralTransformationData>::GetData(const A3DMiscGeneralTransformation* InMiscGeneralTransformationPtr)
{
	return A3DMiscGeneralTransformationGet(InMiscGeneralTransformationPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscMaterialPropertiesData>::GetData(const A3DEntity* InMiscMaterialPropertiesPtr)
{
	return A3DMiscGetMaterialProperties(InMiscMaterialPropertiesPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRiBrepModelData>::GetData(const A3DRiBrepModel* InRiBrepModelPtr)
{
	return A3DRiBrepModelGet(InRiBrepModelPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRiCoordinateSystemData>::GetData(const A3DRiCoordinateSystem* InRiCoordinateSystemPtr)
{
	return A3DRiCoordinateSystemGet(InRiCoordinateSystemPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRiDirectionData>::GetData(const A3DRiDirection* InRiDirectionPtr)
{
	return A3DRiDirectionGet(InRiDirectionPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRiPolyBrepModelData>::GetData(const A3DRiPolyBrepModel* InRiPolyBrepModelPtr)
{
	return A3DRiPolyBrepModelGet(InRiPolyBrepModelPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRiRepresentationItemData>::GetData(const A3DRiRepresentationItem* InRiRepresentationItemPtr)
{
	return A3DRiRepresentationItemGet(InRiRepresentationItemPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRiSetData>::GetData(const A3DRiSet* InRiSetPtr)
{
	return A3DRiSetGet(InRiSetPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRootBaseData>::GetData(const A3DRootBase* InRootBasePtr)
{
	return A3DRootBaseGet(InRootBasePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DRootBaseWithGraphicsData>::GetData(const A3DRootBaseWithGraphics* InRootBaseWithGraphicsPtr)
{
	return A3DRootBaseWithGraphicsGet(InRootBaseWithGraphicsPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTess3DData>::GetData(const A3DTess3D* InTess3DPtr)
{
	return A3DTess3DGet(InTess3DPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTessBaseData>::GetData(const A3DTessBase* InTessBasePtr)
{
	return A3DTessBaseGet(InTessBasePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoBrepDataData>::GetData(const A3DTopoBrepData* InTopoBrepDataPtr)
{
	return A3DTopoBrepDataGet(InTopoBrepDataPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoEdgeData>::GetData(const A3DTopoEdge* InTopoEdgePtr)
{
	return A3DTopoEdgeGet(InTopoEdgePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoFaceData>::GetData(const A3DTopoFace* InTopoFacePtr)
{
	return A3DTopoFaceGet(InTopoFacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoUniqueVertexData>::GetData(const A3DTopoUniqueVertex* InTopoUniqueVertexPtr)
{
	return A3DTopoUniqueVertexGet(InTopoUniqueVertexPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoWireEdgeData>::GetData(const A3DTopoWireEdge* InTopoWireEdgePtr)
{
	return A3DTopoWireEdgeGet(InTopoWireEdgePtr, &Data);
}


uint32 TUniqueTSObjFromIndex<A3DGraphMaterialData>::DefaultValue = A3D_DEFAULT_MATERIAL_INDEX;
uint32 TUniqueTSObjFromIndex<A3DGraphPictureData>::DefaultValue = A3D_DEFAULT_MATERIAL_INDEX;
uint32 TUniqueTSObjFromIndex<A3DGraphRgbColorData>::DefaultValue = A3D_DEFAULT_COLOR_INDEX;
uint32 TUniqueTSObjFromIndex<A3DGraphStyleData>::DefaultValue = A3D_DEFAULT_STYLE_INDEX;

#endif

}
