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
#ifndef CADKERNEL_DEV
	if (EnginePluginsPath.IsEmpty())
	{
		EnginePluginsPath = FPaths::EnginePluginsDir();
	}
#endif

#ifdef CADKERNEL_DEV
	FString TechSoftDllPath = EnginePluginsPath;
#else
	FString TechSoftDllPath = FPaths::Combine(EnginePluginsPath, TEXT("Enterprise/DatasmithCADImporter"), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory(), "TechSoft");
#endif

	TechSoftDllPath = FPaths::ConvertRelativePathToFull(TechSoftDllPath);
	ExchangeLoader = MakeUnique<A3DSDKHOOPSExchangeLoader>(*TechSoftDllPath);

	const A3DStatus IRet = ExchangeLoader->m_eSDKStatus;
	if (IRet != A3D_SUCCESS)
	{
#ifndef CADKERNEL_DEV
		UE_LOG(LogCADInterfaces, Warning, TEXT("Failed to load required library in %s. Plug-in will not be functional."), *TechSoftDllPath);
#endif
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

A3DStatus FTechSoftInterface::UnloadModel()
{
	A3DStatus Ret = A3DStatus::A3D_ERROR;
	if (ExchangeLoader)
	{
		Ret = A3DAsmModelFileDelete(ExchangeLoader->m_psModelFile);
	}
	ExchangeLoader->m_psModelFile = nullptr;
	return Ret;
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

void TUniqueTSObj<A3DBoundingBoxData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DBoundingBoxData, Data);
}

void TUniqueTSObj<A3DCrvCircleData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvCircleData, Data);
}

void TUniqueTSObj<A3DCrvCompositeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvCompositeData, Data);
}

void TUniqueTSObj<A3DCrvEllipseData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvEllipseData, Data);
}

void TUniqueTSObj<A3DCrvHelixData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvHelixData, Data);
}

void TUniqueTSObj<A3DCrvHyperbolaData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvHyperbolaData, Data);
}

void TUniqueTSObj<A3DCrvLineData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvLineData, Data);
}

void TUniqueTSObj<A3DCrvNurbsData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvNurbsData, Data);
}

void TUniqueTSObj<A3DCrvParabolaData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvParabolaData, Data);
}

void TUniqueTSObj<A3DCrvPolyLineData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DCrvPolyLineData, Data);
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

void TUniqueTSObj<A3DIntervalData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DIntervalData, Data);
}

void TUniqueTSObj<A3DMiscAttributeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscAttributeData, Data);
}

void TUniqueTSObj<A3DMiscCartesianTransformationData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscCartesianTransformationData, Data);
}

void TUniqueTSObj<A3DMiscEntityReferenceData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscEntityReferenceData, Data);
}

void TUniqueTSObj<A3DMiscGeneralTransformationData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscGeneralTransformationData, Data);
}

void TUniqueTSObj<A3DMiscMaterialPropertiesData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscMaterialPropertiesData, Data);
}

void TUniqueTSObj<A3DMiscReferenceOnCsysItemData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscReferenceOnCsysItemData, Data);
}

void TUniqueTSObj<A3DMiscReferenceOnTopologyData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscReferenceOnTopologyData, Data);
}

void TUniqueTSObj<A3DMiscReferenceOnTessData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DMiscReferenceOnTessData, Data);
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

void TUniqueTSObj<A3DSurfBlend01Data>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfBlend01Data, Data);
}

void TUniqueTSObj<A3DSurfBlend02Data>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfBlend02Data, Data);
}

void TUniqueTSObj<A3DSurfBlend03Data>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfBlend03Data, Data);
}

void TUniqueTSObj<A3DSurfConeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfConeData, Data);
}

void TUniqueTSObj<A3DSurfCylinderData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfCylinderData, Data);
}

void TUniqueTSObj<A3DSurfCylindricalData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfCylindricalData, Data);
}

void TUniqueTSObj<A3DSurfExtrusionData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfExtrusionData, Data);
}

void TUniqueTSObj<A3DSurfFromCurvesData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfFromCurvesData, Data);
}

void TUniqueTSObj<A3DSurfNurbsData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfNurbsData, Data);
}

void TUniqueTSObj<A3DSurfPipeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfPipeData, Data);
}

void TUniqueTSObj<A3DSurfPlaneData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfPlaneData, Data);
}

void TUniqueTSObj<A3DSurfRevolutionData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfRevolutionData, Data);
}

void TUniqueTSObj<A3DSurfRuledData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfRuledData, Data);
}

void TUniqueTSObj<A3DSurfSphereData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfSphereData, Data);
}

void TUniqueTSObj<A3DSurfTorusData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DSurfTorusData, Data);
}

void TUniqueTSObj<A3DTess3DData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTess3DData, Data);
}

void TUniqueTSObj<A3DTessBaseData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTessBaseData, Data);
}

void TUniqueTSObj<A3DTopoBodyData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoBodyData, Data);
}

void TUniqueTSObj<A3DTopoBrepDataData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoBrepDataData, Data);
}

void TUniqueTSObj<A3DTopoCoEdgeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoCoEdgeData, Data);
}

void TUniqueTSObj<A3DTopoConnexData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoConnexData, Data);
}

void TUniqueTSObj<A3DTopoContextData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoContextData, Data);
}

void TUniqueTSObj<A3DTopoEdgeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoEdgeData, Data);
}

void TUniqueTSObj<A3DTopoFaceData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoFaceData, Data);
}

void TUniqueTSObj<A3DTopoLoopData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoLoopData, Data);
}

void TUniqueTSObj<A3DTopoShellData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoShellData, Data);
}

void TUniqueTSObj<A3DTopoUniqueVertexData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoUniqueVertexData, Data);
}

void TUniqueTSObj<A3DTopoWireEdgeData>::InitializeData()
{
	A3D_INITIALIZE_DATA(A3DTopoWireEdgeData, Data);
}

void TUniqueTSObj<A3DUTF8Char*>::InitializeData()
{
	Data = nullptr;
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

A3DStatus TUniqueTSObj<A3DBoundingBoxData>::GetData(const A3DEntity* InGraphicsPtr)
{
	return A3DMiscGetBoundingBox(InGraphicsPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvCircleData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvCircleGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvCompositeData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvCompositeGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvEllipseData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvEllipseGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvHelixData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvHelixGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvHyperbolaData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvHyperbolaGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvLineData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvLineGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvNurbsData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvNurbsGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvParabolaData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvParabolaGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DCrvPolyLineData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvPolyLineGet(InCurvePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DIntervalData>::GetData(const A3DEntity* InCurvePtr)
{
	return A3DCrvGetInterval(InCurvePtr, &Data);
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

A3DStatus TUniqueTSObj<A3DMiscEntityReferenceData>::GetData(const A3DMiscEntityReference* InMiscEntityReferencePtr)
{
	return A3DMiscEntityReferenceGet(InMiscEntityReferencePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscGeneralTransformationData>::GetData(const A3DMiscGeneralTransformation* InMiscGeneralTransformationPtr)
{
	return A3DMiscGeneralTransformationGet(InMiscGeneralTransformationPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscMaterialPropertiesData>::GetData(const A3DEntity* InMiscMaterialPropertiesPtr)
{
	return A3DMiscGetMaterialProperties(InMiscMaterialPropertiesPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscReferenceOnTopologyData>::GetData(const A3DEntity* InMiscReferenceOnTopologyPtr)
{
	return A3DMiscReferenceOnTopologyGet(InMiscReferenceOnTopologyPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscReferenceOnCsysItemData>::GetData(const A3DEntity* InMiscReferenceOnCsysItem)
{
	return A3DMiscReferenceOnCsysItemGet(InMiscReferenceOnCsysItem, &Data);
}

A3DStatus TUniqueTSObj<A3DMiscReferenceOnTessData>::GetData(const A3DEntity* InMiscReferenceOnTess)
{
	return A3DMiscReferenceOnTessGet(InMiscReferenceOnTess, &Data);
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

A3DStatus TUniqueTSObj<A3DSurfBlend01Data>::GetData(const A3DEntity* InSurfPtr)
{
	return A3DSurfBlend01Get(InSurfPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfBlend02Data>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfBlend02Get(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfBlend03Data>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfBlend03Get(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfConeData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfConeGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfCylinderData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfCylinderGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfCylindricalData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfCylindricalGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfExtrusionData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfExtrusionGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfFromCurvesData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfFromCurvesGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfNurbsData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfNurbsGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfPipeData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfPipeGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfPlaneData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfPlaneGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfRevolutionData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfRevolutionGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfRuledData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfRuledGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfSphereData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfSphereGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DSurfTorusData>::GetData(const A3DEntity* InSurfacePtr)
{
	return A3DSurfTorusGet(InSurfacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTess3DData>::GetData(const A3DTess3D* InTess3DPtr)
{
	return A3DTess3DGet(InTess3DPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTessBaseData>::GetData(const A3DTessBase* InTessBasePtr)
{
	return A3DTessBaseGet(InTessBasePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoBodyData>::GetData(const A3DEntity* InBodyPtr)
{
	return A3DTopoBodyGet(InBodyPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoBrepDataData>::GetData(const A3DTopoBrepData* InTopoBrepDataPtr)
{
	return A3DTopoBrepDataGet(InTopoBrepDataPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoCoEdgeData>::GetData(const A3DEntity* InGraphicsPtr)
{
	return A3DTopoCoEdgeGet(InGraphicsPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoConnexData>::GetData(const A3DEntity* InGraphicsPtr)
{
	return A3DTopoConnexGet(InGraphicsPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoContextData>::GetData(const A3DEntity* InGraphicsPtr)
{
	return A3DTopoContextGet(InGraphicsPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoEdgeData>::GetData(const A3DTopoEdge* InTopoEdgePtr)
{
	return A3DTopoEdgeGet(InTopoEdgePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoFaceData>::GetData(const A3DTopoFace* InTopoFacePtr)
{
	return A3DTopoFaceGet(InTopoFacePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoLoopData>::GetData(const A3DEntity* InTopoLoopPtr)
{
	return A3DTopoLoopGet(InTopoLoopPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoShellData>::GetData(const A3DEntity* InTopoPtr)
{
	return A3DTopoShellGet(InTopoPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoUniqueVertexData>::GetData(const A3DTopoUniqueVertex* InTopoUniqueVertexPtr)
{
	return A3DTopoUniqueVertexGet(InTopoUniqueVertexPtr, &Data);
}

A3DStatus TUniqueTSObj<A3DTopoWireEdgeData>::GetData(const A3DTopoWireEdge* InTopoWireEdgePtr)
{
	return A3DTopoWireEdgeGet(InTopoWireEdgePtr, &Data);
}

A3DStatus TUniqueTSObj<A3DUTF8Char*>::GetData(const A3DAsmProductOccurrence* InOccurrencePtr)
{
	return A3DAsmProductOccurrenceGetFilePathName(InOccurrencePtr, &Data);
}

uint32 TUniqueTSObjFromIndex<A3DGraphMaterialData>::DefaultValue = A3D_DEFAULT_MATERIAL_INDEX;
uint32 TUniqueTSObjFromIndex<A3DGraphPictureData>::DefaultValue = A3D_DEFAULT_MATERIAL_INDEX;
uint32 TUniqueTSObjFromIndex<A3DGraphRgbColorData>::DefaultValue = A3D_DEFAULT_COLOR_INDEX;
uint32 TUniqueTSObjFromIndex<A3DGraphStyleData>::DefaultValue = A3D_DEFAULT_STYLE_INDEX;
#endif

namespace TechSoftUtils
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

#ifdef USE_TECHSOFT_SDK

A3DStatus GetSurfaceAsNurbs(const A3DSurfBase* SurfacePtr, A3DSurfNurbsData* DataPtr, A3DDouble Tolerance, A3DBool bUseSameParameterization)
{
	return A3DSurfBaseGetAsNurbs(SurfacePtr, Tolerance, bUseSameParameterization, DataPtr);
}

A3DStatus GetCurveAsNurbs(const A3DCrvBase* CurvePtr, A3DCrvNurbsData* DataPtr, A3DDouble Tolerance, A3DBool bUseSameParameterization)
{
	return A3DCrvBaseGetAsNurbs(CurvePtr, Tolerance, bUseSameParameterization, DataPtr);
}

A3DStatus GetOriginalFilePathName(const A3DAsmProductOccurrence* A3DOccurrencePtr, A3DUTF8Char** FilePathUTF8Ptr)
{
	return A3DAsmProductOccurrenceGetOriginalFilePathName(A3DOccurrencePtr, FilePathUTF8Ptr);
}

A3DStatus GetFilePathName(const A3DAsmProductOccurrence* A3DOccurrencePtr, A3DUTF8Char** FilePathUTF8Ptr)
{
	return A3DAsmProductOccurrenceGetFilePathName(A3DOccurrencePtr, FilePathUTF8Ptr);
}

A3DStatus GetEntityType(const A3DEntity* EntityPtr, A3DEEntityType* EntityTypePtr)
{
	return A3DEntityGetType(EntityPtr, EntityTypePtr);
}

bool IsEntityBaseWithGraphicsType(const A3DEntity* EntityPtr)
{
	return (bool)A3DEntityIsBaseWithGraphicsType(EntityPtr);
}

bool IsEntityBaseType(const A3DEntity* EntityPtr)
{
	return (bool)A3DEntityIsBaseType(EntityPtr);
}

#endif

}


}
