// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftBridge.h"

#ifdef USE_TECHSOFT_SDK

#include "TechSoftInterface.h"
#include "TUniqueTechSoftObj.h"
#include "CADFileReport.h"

#include "CADKernel/Core/Session.h"

#include "CADKernel/Geo/Curves/BezierCurve.h"
#include "CADKernel/Geo/Curves/BoundedCurve.h"
#include "CADKernel/Geo/Curves/CompositeCurve.h"
#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Curves/EllipseCurve.h"
#include "CADKernel/Geo/Curves/HyperbolaCurve.h"
#include "CADKernel/Geo/Curves/NURBSCurve.h"
#include "CADKernel/Geo/Curves/NURBSCurve.h"
#include "CADKernel/Geo/Curves/ParabolaCurve.h"
#include "CADKernel/Geo/Curves/PolylineCurve.h"
#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Geo/Curves/SegmentCurve.h"
#include "CADKernel/Geo/Curves/SurfacicCurve.h"

#include "CADKernel/Geo/Surfaces/BezierSurface.h"
#include "CADKernel/Geo/Surfaces/CompositeSurface.h"
#include "CADKernel/Geo/Surfaces/ConeSurface.h"
#include "CADKernel/Geo/Surfaces/CoonsSurface.h"
#include "CADKernel/Geo/Surfaces/CylinderSurface.h"
#include "CADKernel/Geo/Surfaces/NURBSSurface.h"
#include "CADKernel/Geo/Surfaces/OffsetSurface.h"
#include "CADKernel/Geo/Surfaces/PlaneSurface.h"
#include "CADKernel/Geo/Surfaces/RevolutionSurface.h"
#include "CADKernel/Geo/Surfaces/RuledSurface.h"
#include "CADKernel/Geo/Surfaces/SphericalSurface.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Geo/Surfaces/TabulatedCylinderSurface.h"
#include "CADKernel/Geo/Surfaces/TorusSurface.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalVertex.h"

#include "CADKernel/Utils/StringUtil.h"

#include "CADKernel/UI/Display.h"

namespace CADKernel
{
namespace TechSoftUtils
{

const FUVReparameterization FUVReparameterization::Identity(1., 0, 1., 0);

template<typename... InArgTypes>
A3DStatus GetCurveAsNurbs(const A3DCrvBase* A3DCurve, A3DCrvNurbsData* DataPtr, InArgTypes&&... Args)
{
	return CADLibrary::TechSoftInterface::GetCurveAsNurbs(A3DCurve, DataPtr, Forward<InArgTypes>(Args)...);
};

template<typename... InArgTypes>
A3DStatus GetSurfaceAsNurbs(const A3DSurfBase* A3DSurface, A3DSurfNurbsData* DataPtr, InArgTypes&&... Args)
{
	return CADLibrary::TechSoftInterface::GetSurfaceAsNurbs(A3DSurface, DataPtr, Forward<InArgTypes>(Args)...);
}; 

FString GetFileName(const FString& InFilePath)
{
	int32 Position = -1;
	if (!InFilePath.FindLastChar(TEXT('\\'), Position))
	{
		InFilePath.FindLastChar(TEXT('/'), Position);
	}

	if (Position >= 0)
	{
		return InFilePath.Right(Position + 1);
	}

	return InFilePath;
}

FString GetOriginalName(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('['), Position))
	{
		return Name.Left(Position);
	}
	return Name;
}

FString Get3dxmlOriginalName(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('('), Position))
	{
		FString NewName = Name.Right(Position + 1);
		if (NewName.FindLastChar(TEXT(')'), Position))
		{
			NewName[Position] = 0;
		}
		return NewName;
	}
	return Name;
}

FString GetSWOriginalName(const FString& Name)
{
	int32 Position;
	if (Name.FindLastChar(TEXT('-'), Position))
	{
		FString NewName = Name.Left(Position) + TEXT("<") + Name.Right(Position + 1) + TEXT(">");
		return NewName;
	}
	return Name;
}

int TraverseAttribute(const A3DMiscAttribute* Attribute, TMap<FString, FString>& OutMetaData)
{
	A3DInt32 Ret = A3D_SUCCESS;

	CADLibrary::TUniqueTSObj<A3DMiscAttributeData> AttributeData(Attribute);
	if (AttributeData.IsValid())
	{
		FString AttributeName;
		if (AttributeData->m_bTitleIsInt)
		{
			A3DUns32 uiVal = 0;
			memcpy(&uiVal, AttributeData->m_pcTitle, sizeof(A3DUns32));
			AttributeName = Utils::ToFString((uint32)uiVal);
		}
		else if (AttributeData->m_pcTitle && AttributeData->m_pcTitle[0] != '\0')
		{
			AttributeName = UTF8_TO_TCHAR(AttributeData->m_pcTitle);
		}
		else
		{
			return A3D_ERROR;
		}

		for (A3DUns32 Index = 0; Index < AttributeData->m_uiSize; ++Index)
		{
			//single->SetAttribute("m_bTitleIsInt", (int)AttributeData->m_asSingleAttributesData[Index].m_bTitleIsInt);
			if (AttributeData->m_asSingleAttributesData[Index].m_pcTitle == NULL)
			{
				//single->SetAttribute("m_pcTitle", "NULL");
			}
			else if (AttributeData->m_asSingleAttributesData[Index].m_bTitleIsInt)
			{
				A3DUns32 uiVal;
				memcpy(&uiVal, AttributeData->m_asSingleAttributesData[Index].m_pcTitle, sizeof(A3DUns32));
				//single->SetAttribute("m_pcTitle", (int)uiVal);
			}
			else
			{
				if (AttributeData->m_asSingleAttributesData[Index].m_pcTitle && AttributeData->m_asSingleAttributesData[Index].m_pcTitle[0] != '\0')
				{
					//single->SetAttribute("m_pcTitle", AttributeData->m_asSingleAttributesData[Index].m_pcTitle);
				}
			}

			FString AttributeValue;

			switch (AttributeData->m_asSingleAttributesData[Index].m_eType)
			{
			case kA3DModellerAttributeTypeTime:
			case kA3DModellerAttributeTypeInt:
			{
				A3DInt32 Value;
				memcpy(&Value, AttributeData->m_asSingleAttributesData[Index].m_pcData, sizeof(A3DInt32));
				AttributeValue = Utils::ToFString((int32) Value);
				break;
			}

			case kA3DModellerAttributeTypeReal:
			{
				A3DDouble Value;
				memcpy(&Value, AttributeData->m_asSingleAttributesData[Index].m_pcData, sizeof(A3DDouble));
				AttributeValue = Utils::ToFString((double) Value);
				break;
			}

			case kA3DModellerAttributeTypeString:
				if (AttributeData->m_asSingleAttributesData[Index].m_pcData && AttributeData->m_asSingleAttributesData[Index].m_pcData[0] != '\0')
				{
					AttributeValue = UTF8_TO_TCHAR(AttributeData->m_asSingleAttributesData[Index].m_pcData);
				}
				break;

			default:
				break;
			}

			if (AttributeName.Len())
			{
				if (Index)
				{
					FString Key = FString::Printf(TEXT("%s_%u"), *AttributeName, Index);
					OutMetaData.Emplace(Key, AttributeValue);
				}
				else
				{
					OutMetaData.Emplace(AttributeName, AttributeValue);
				}
			}
			else
			{
				Ret = A3D_ERROR;
			}
		}
	}

	return Ret;
}

int TraverseBase(const ECADType FileType, const A3DEntity* Entity, FEntityData& OutEntityData, bool bIsOccurrence)
{
	CADLibrary::TUniqueTSObj<A3DRootBaseData> RootBaseData(Entity);
	if (RootBaseData.IsValid())
	{
		if (RootBaseData->m_uiPersistentId > 0)
		{
			FString PersistentId = Utils::ToFString((uint32)RootBaseData->m_uiPersistentId);
			OutEntityData.MetaData.Emplace(TEXT("PersistentId"), PersistentId);
		}

		if (RootBaseData->m_pcPersistentId)
		{
			FString PersistentUuid = UTF8_TO_TCHAR(RootBaseData->m_pcPersistentId);
			Utils::RemoveUnwantedChar(PersistentUuid, L'-');
			OutEntityData.MetaData.Emplace(TEXT("UUID"), PersistentUuid);
		}

		for (A3DUns32 Index = 0; Index < RootBaseData->m_uiSize; ++Index)
		{
			TraverseAttribute(RootBaseData->m_ppAttributes[Index], OutEntityData.MetaData);
		}

		CADLibrary::TUniqueTSObj<A3DBoundingBoxData> sBoundingBox(Entity);
		if (sBoundingBox.IsValid())
		{
			if ((sBoundingBox->m_sMin.m_dX + sBoundingBox->m_sMin.m_dY + sBoundingBox->m_sMin.m_dZ + sBoundingBox->m_sMax.m_dX + sBoundingBox->m_sMax.m_dY + sBoundingBox->m_sMax.m_dZ) != 0.0)
			{
				FString BBox = FString::Printf(TEXT("Min(%f,%f,%f) Max(%f,%f,%f)"), sBoundingBox->m_sMin.m_dX, sBoundingBox->m_sMin.m_dY, sBoundingBox->m_sMin.m_dZ, sBoundingBox->m_sMax.m_dX, sBoundingBox->m_sMax.m_dY, sBoundingBox->m_sMax.m_dZ);
				OutEntityData.MetaData.Emplace(TEXT("BBox"), BBox);
			}
		}
	}

	if (bIsOccurrence)
	{
		FString* InstanceName = OutEntityData.MetaData.Find(TEXT("InstanceName"));
		FString* Name = OutEntityData.MetaData.Find(TEXT("Name"));
		FString* OriginalName = OutEntityData.MetaData.Find(TEXT("OriginalName"));
		FString* PartNumber = OutEntityData.MetaData.Find(TEXT("PartNumber"));

		if (!Name && !OriginalName &&
			RootBaseData->m_pcName && RootBaseData->m_pcName[0] != '\0')
		{
			FString NameStr = UTF8_TO_TCHAR(RootBaseData->m_pcName);
			FString PartNumberStr = GetOriginalName(NameStr);
			if (FileType == ECADType::CATIA_3DXML)
			{
				PartNumberStr = Get3dxmlOriginalName(PartNumberStr);
			}
			if (FileType == ECADType::SOLIDWORKS)
			{
				PartNumberStr = GetSWOriginalName(PartNumberStr);
			}
			OutEntityData.MetaData.Add(TEXT("PartNumber"), PartNumberStr);
			PartNumber = OutEntityData.MetaData.Find(TEXT("PartNumber"));
		}

		if (InstanceName)
		{
			OutEntityData.MetaData.Add(TEXT("Name"), *InstanceName);
		}
		else if (PartNumber)
		{
			OutEntityData.MetaData.Add(TEXT("Name"), *PartNumber);
		}
		else if (OriginalName)
		{
			OutEntityData.MetaData.Add(TEXT("Name"), *OriginalName);
		}
		OutEntityData.Name = OutEntityData.MetaData[L"Name"];
	}
	else
	{
		FString* Name = OutEntityData.MetaData.Find(TEXT("Name"));
		FString* OriginalName = OutEntityData.MetaData.Find(TEXT("OriginalName"));
		FString* PartNumber = OutEntityData.MetaData.Find(TEXT("PartNumber"));

		if (!OriginalName && !PartNumber &&
			RootBaseData->m_pcName && RootBaseData->m_pcName[0] != '\0')
		{
			FString NameStr = UTF8_TO_TCHAR(RootBaseData->m_pcName);
			FString OriginalNameStr = GetOriginalName(NameStr);
			OutEntityData.MetaData.Emplace(TEXT("OriginalName"), OriginalNameStr);
			OriginalName = OutEntityData.MetaData.Find(TEXT("OriginalName"));
		}

		if (PartNumber)
		{
			OutEntityData.MetaData.Add(TEXT("Name"), *PartNumber);
		}
		else if (OriginalName)
		{
			OutEntityData.MetaData.Add(TEXT("Name"), *OriginalName);
		}

		Name = OutEntityData.MetaData.Find(TEXT("Name"));
		if (Name)
		{
			OutEntityData.Name = *Name;
			if (!OriginalName)
			{
				OutEntityData.MetaData.Add(TEXT("OriginalName"), OutEntityData.Name);
			}
		}
	}

	return A3D_SUCCESS;
}

int TraverseGraphics(const A3DGraphics* Graphics)
{
	CADLibrary::TUniqueTSObj<A3DGraphicsData> GraphicsData(Graphics);
	if (GraphicsData.IsValid())
	{
		if (GraphicsData->m_uiLayerIndex != A3D_DEFAULT_LAYER)
		{
			//graphics->SetAttribute("m_uiLayerIndex", (int)GraphicsData.m_uiLayerIndex);
		}
		if (GraphicsData->m_uiStyleIndex != A3D_DEFAULT_STYLE_INDEX)
		{
			//graphics->SetAttribute("m_uiStyleIndex", (int)GraphicsData.m_uiStyleIndex);
		}

		//graphics->SetAttribute("m_usBehaviour", (int)GraphicsData.m_usBehaviour);
	}

	return A3D_SUCCESS;
}

int TraverseBaseWithGraphics(const ECADType FileType, const A3DEntity* Entity, FEntityData& OutEntityData, bool bIsOccurrence)
{
	CADLibrary::TUniqueTSObj<A3DRootBaseWithGraphicsData> BaseWithGraphicsData(Entity);
	if (BaseWithGraphicsData.IsValid())
	{
		TraverseBase(FileType, Entity, OutEntityData, bIsOccurrence);
		if (BaseWithGraphicsData->m_pGraphics != NULL)
		{
			TraverseGraphics(BaseWithGraphicsData->m_pGraphics);
		}
	}

	return A3D_SUCCESS;
}

int TraverseSource(const A3DEntity* Entity, FEntityData& OutEntityData, bool bIsOccurrence, const ECADType FileType)
{
	if (CADLibrary::TechSoftInterface::IsEntityBaseWithGraphicsType(Entity))
	{
		return TraverseBaseWithGraphics(FileType, Entity, OutEntityData, bIsOccurrence);
	}
	else if (CADLibrary::TechSoftInterface::IsEntityBaseType(Entity))
	{
		return TraverseBase(FileType, Entity, OutEntityData, bIsOccurrence);
	}
	return A3D_SUCCESS;
}

int TraverseSpecificMetaData(A3DEModellerType ModellerType, const A3DAsmProductOccurrence* Occurrence, FEntityData& MetaData)
{
	//----------- Export Specific information per CAD format -----------
	switch (ModellerType)
	{
	case kA3DModellerSlw:
	{
		CADLibrary::TUniqueTSObj<A3DAsmProductOccurrenceDataSLW> SolidWorksSpecificData(Occurrence);
		if (SolidWorksSpecificData.IsValid())
		{
			//A3DUns16 m_usStructSize;									// Reserved: will be initialized by \ref A3D_INITIALIZE_DATA.
			//A3DUTF8Char* SolidWorksSpecificData->m_psNodeSlwID;									/*!< ID comming from Solidworks, to use to resolve the "PathsInAssemblyTree" */

			//A3DInt32 SolidWorksSpecificData->m_iIndexCfg;										/*!< Cfg Index */
			//A3DUTF8Char* SolidWorksSpecificData->m_psCfgName;									/*!< Cfg Name in the file*/
			if (SolidWorksSpecificData->m_psCfgName)
			{
				FString ConfigurationName = UTF8_TO_TCHAR(SolidWorksSpecificData->m_psCfgName);
				MetaData.MetaData.Emplace(TEXT("ConfigurationName"), ConfigurationName);
				FString ConfigurationIndex = Utils::ToFString((int32) SolidWorksSpecificData->m_iIndexCfg);
				MetaData.MetaData.Emplace(TEXT("ConfigurationIndex"), ConfigurationIndex);
				FString Configuration = TEXT("Configuration_") + ConfigurationIndex;
				MetaData.MetaData.Emplace(Configuration, ConfigurationName);
			}

			//A3DEProductOccurrenceTypeSLW SolidWorksSpecificData->m_usType;						/*!< Product Occurrence Type */

			//A3DUns32 SolidWorksSpecificData->m_uiAttachementsSize;								/*!< The size of \ref m_psAttachements. */
			//A3DAsmAttachmentsInfosSLW* SolidWorksSpecificData->m_psAttachements;				/*!< Storage to put assembly attachments information on product occurrence in assembly tree. */

		}
		break;
	}
	case kA3DModellerUnigraphics:
	{
		CADLibrary::TUniqueTSObj<A3DAsmProductOccurrenceDataUg> UnigraphicsSpecificData(Occurrence);
		if (UnigraphicsSpecificData.IsValid())
		{
			if (UnigraphicsSpecificData->m_psPartUID)
			{
				FString PartUID = UTF8_TO_TCHAR(UnigraphicsSpecificData->m_psPartUID);
				MetaData.MetaData.Emplace(TEXT("UnigraphicsPartUID"), PartUID);
			}
			if (UnigraphicsSpecificData->m_psInstanceFileName)
			{
				FString InstanceFileName = UTF8_TO_TCHAR(UnigraphicsSpecificData->m_psInstanceFileName);
				MetaData.MetaData.Emplace(TEXT("UnigraphicsPartUID"), InstanceFileName);
			}

			if (UnigraphicsSpecificData->m_uiInstanceTag)
			{
				FString InstanceTag = Utils::ToFString((uint32)UnigraphicsSpecificData->m_uiInstanceTag);
				MetaData.MetaData.Emplace(TEXT("UnigraphicsInstanceTag"), InstanceTag);
			}

			//// Treat RefSets
			//if (UnigraphicsSpecificData.m_uiChildrenByRefsetsSize || UnigraphicsSpecificData.m_uiSolidsByRefsetsSize)
			//{
			//	// Children by RefSet
			//	for (A3DUns32 iRefSet = 0; iRefSet < UnigraphicsSpecificData.m_uiChildrenByRefsetsSize; ++iRefSet)
			//	{
			//		const A3DElementsByRefsetUg& pRefsetData = UnigraphicsSpecificData.m_asChildrenByRefsets[iRefSet];
			//		// Init Xml Element if first occurrence
			//		//refset->SetAttribute("Name", pRefsetData.m_psRefset);
			//		// Add children
			//		for (A3DUns32 i = 0; i < pRefsetData.m_uiElementsSize; ++i)
			//		{
			//			//refsetChild->SetAttribute("id", pRefsetData.m_auiElements[i]);
			//		}
			//	}
			//	// Solid by RefSet
			//	for (A3DUns32 iRefSet = 0; iRefSet < UnigraphicsSpecificData.m_uiSolidsByRefsetsSize; ++iRefSet)
			//	{
			//		const A3DElementsByRefsetUg& pRefsetData = UnigraphicsSpecificData.m_asSolidsByRefsets[iRefSet];
			//		//refset->SetAttribute("Name", pRefsetData.m_psRefset);
			//		// Add Solid
			//		for (A3DUns32 i = 0; i < pRefsetData.m_uiElementsSize; ++i)
			//		{
			//			//refsetChild->SetAttribute("id", pRefsetData.m_auiElements[i]);
			//		}
			//	}
			//}
		}
		break;
	}

	case kA3DModellerCatiaV5:
	{
		CADLibrary::TUniqueTSObj<A3DAsmProductOccurrenceDataCV5> CatiaV5SpecificData(Occurrence);
		if (CatiaV5SpecificData.IsValid())
		{
			if (CatiaV5SpecificData->m_iCurrentAsmProductIdentifier)
			{
				FString InstanceTag = Utils::ToFString((int32) CatiaV5SpecificData->m_iCurrentAsmProductIdentifier);
				MetaData.MetaData.Emplace(TEXT("CatiaAsmProductIdentifier"), InstanceTag);
			}

			if (CatiaV5SpecificData->m_iNotUpdatedAsmProductIdentifier)
			{
				FString InstanceTag = Utils::ToFString((int32) CatiaV5SpecificData->m_iNotUpdatedAsmProductIdentifier);
				MetaData.MetaData.Emplace(TEXT("CatiaNotUpdatedAsmProductIdentifier"), InstanceTag);
			}

			if (CatiaV5SpecificData->m_aiCLSID[0])
			{
				FString InstanceTag = FString::Printf(TEXT("%d %d %d %d"), CatiaV5SpecificData->m_aiCLSID[0], CatiaV5SpecificData->m_aiCLSID[1], CatiaV5SpecificData->m_aiCLSID[2], CatiaV5SpecificData->m_aiCLSID[3]);
				MetaData.MetaData.Emplace(TEXT("CatiaCLSID"), InstanceTag);
			}

			if (CatiaV5SpecificData->m_psVersion)
			{
				FString Version = UTF8_TO_TCHAR(CatiaV5SpecificData->m_psVersion);
				MetaData.MetaData.Emplace(TEXT("CatiaVersion"), Version);
			}

			if (CatiaV5SpecificData->m_psPartNumber)
			{
				FString PartNumber = UTF8_TO_TCHAR(CatiaV5SpecificData->m_psPartNumber);
				MetaData.MetaData.Emplace(TEXT("CatiaPartNumber"), PartNumber);
			}
		}
		break;
	}

	default:
		break;
	}
	return A3D_SUCCESS;
}

FMatrixH CreateCoordinateSystem(const A3DMiscCartesianTransformationData& Transformation, double UnitScale = 1.0)
{
	FPoint Origin(&Transformation.m_sOrigin.m_dX);
	FPoint Ox(&Transformation.m_sXVector.m_dX);
	FPoint Oy(&Transformation.m_sYVector.m_dX);

	Ox.Normalize();
	Oy.Normalize();

	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		Origin *= UnitScale;
	}
	FPoint Oz = Ox ^ Oy;

	FMatrixH Matrix = FMatrixH(Origin, Ox, Oy, Oz);

	if (!FMath::IsNearlyEqual(Transformation.m_sScale.m_dX, 1.) || !FMath::IsNearlyEqual(Transformation.m_sScale.m_dY, 1.) || !FMath::IsNearlyEqual(Transformation.m_sScale.m_dZ, 1.))
	{
		FMatrixH Scale = FMatrixH::MakeScaleMatrix(Transformation.m_sScale.m_dX, Transformation.m_sScale.m_dY, Transformation.m_sScale.m_dZ);
		Matrix *= Scale;
	}
	return Matrix;
}

FMatrixH TraverseTransformation3D(const A3DMiscCartesianTransformation* CartesianTransformation, double UnitScale)
{
	FMatrixH Matrix;
	if (CartesianTransformation)
	{
		CADLibrary::TUniqueTSObj<A3DMiscCartesianTransformationData> CartesianTransformationData(CartesianTransformation);
		if (CartesianTransformationData.IsValid())
		{
			Matrix = CreateCoordinateSystem(*CartesianTransformationData, UnitScale);
		}
	}
	return Matrix;
}

FMatrixH TraverseCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem, double UnitScale)
{

	FEntityData MetaData;
	TechSoftUtils::TraverseSource(CoordinateSystem, MetaData);

	FMatrixH Matrix;
	CADLibrary::TUniqueTSObj<A3DRiCoordinateSystemData> CoordinateSystemData(CoordinateSystem);
	if (CoordinateSystemData.IsValid())
	{
		Matrix = TraverseTransformation3D(CoordinateSystemData->m_pTransformation, UnitScale);
	}
	return Matrix;
}

FMatrixH  TraverseGeneralTransformation(const A3DMiscGeneralTransformation* GeneralTransformation, double UnitScale)
{

	FMatrixH Matrix;
	CADLibrary::TUniqueTSObj<A3DMiscGeneralTransformationData> GeneralTransformationData(GeneralTransformation);
	if (GeneralTransformationData.IsValid())
	{
		for (int32 Index = 0; Index < 16; ++Index)
		{
			Matrix[Index] = GeneralTransformationData->m_adCoeff[Index];
		}
		ensureCADKernel(!FMath::IsNearlyEqual(UnitScale, 1.));
	}
	return Matrix;
}

FMatrixH TraverseTransformation(const A3DMiscCartesianTransformation* Transformation3D, double UnitScale)
{
	if (Transformation3D == NULL)
	{
		return FMatrixH();
	}

	FEntityData MetaData;
	TraverseSource(Transformation3D, MetaData, false);

	A3DEEntityType Type = kA3DTypeUnknown;
	CADLibrary::TechSoftInterface::GetEntityType(Transformation3D, &Type);

	if (Type == kA3DTypeMiscCartesianTransformation)
	{
		return TraverseTransformation3D(Transformation3D, UnitScale);
	}
	else if (Type == kA3DTypeMiscGeneralTransformation)
	{
		return TraverseGeneralTransformation(Transformation3D, UnitScale);
	}
	return FMatrixH();
}

void FillInt32Array(const int32 Count, const A3DInt32* Values, TArray<int32>& OutInt32Array)
{
	OutInt32Array.Reserve(Count);
	for (int32 Index = 0; Index < Count; Index++)
	{
		OutInt32Array.Add(Values[Index]);
	}
};

void FillDoubleArray(const int32 Count, const double* Values, TArray<double>& OutDoubleArray)
{
	OutDoubleArray.Reserve(Count);
	for (int32 Index = 0; Index < Count; Index++)
	{
		OutDoubleArray.Add(Values[Index]);
	}
};

void FillDoubleArray(const int32 UCount, const int32 VCount, const double* Values, TArray<double>& OutDoubleArray)
{
	OutDoubleArray.SetNum(UCount * VCount);
	for (int32 Undex = 0, ValueIndex = 0; Undex < UCount; ++Undex)
	{
		int32 Index = Undex;
		for (int32 Vndex = 0; Vndex < VCount; ++Vndex, Index += UCount, ++ValueIndex)
		{
			OutDoubleArray[Index] = Values[ValueIndex];
		}
	}
}

void FillPointArray(const int32 Count, const A3DVector3dData* Points, TArray<FPoint>& OutPointsArray, double UnitScale = 1.0)
{
	OutPointsArray.Reserve(Count);
	for (int32 Index = 0; Index < Count; Index++)
	{
		OutPointsArray.Emplace(&Points[Index].m_dX);
	}

	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		for (FPoint& Point : OutPointsArray)
		{
			Point *= UnitScale;
		}
	}
};

void FillPointArray(const int32 UCount, const int32 VCount, const A3DVector3dData* Points, TArray<FPoint>& OutPointsArray, double UnitScale = 1.0)
{
	OutPointsArray.SetNum(UCount * VCount);
	for (int32 Undex = 0, PointIndex = 0; Undex < UCount; ++Undex)
	{
		int32 Index = Undex;
		for (int32 Vndex = 0; Vndex < VCount; ++Vndex, Index += UCount, ++PointIndex)
		{
			OutPointsArray[Index].Set((double*)&(Points[PointIndex].m_dX));
		}
	}

	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		for (FPoint& Point : OutPointsArray)
		{
			Point *= UnitScale;
		}
	}
};

FSurfacicBoundary GetSurfacicBoundary(A3DDomainData& Domain, double UnitScale = 1.0, bool bSwapUV = false)
{
	EIso UIndex = bSwapUV ? EIso::IsoV : EIso::IsoU;
	EIso VIndex = bSwapUV ? EIso::IsoU : EIso::IsoV;

	FSurfacicBoundary Boundary;
	Boundary[UIndex].Min = Domain.m_sMin.m_dX * UnitScale;
	Boundary[UIndex].Max = Domain.m_sMax.m_dX * UnitScale;
	Boundary[VIndex].Min = Domain.m_sMin.m_dY * UnitScale;
	Boundary[VIndex].Max = Domain.m_sMax.m_dY * UnitScale;
	return Boundary;
}

FLinearBoundary GetLinearBoundary(A3DIntervalData& A3DDomain)
{
	FLinearBoundary Domain(A3DDomain.m_dMin, A3DDomain.m_dMax);
	return Domain;
}

FLinearBoundary GetLinearBoundary(const A3DCrvBase* A3DCurve)
{
	CADLibrary::TUniqueTSObj<A3DIntervalData> A3DDomain(A3DCurve);
	FLinearBoundary Domain(A3DDomain->m_dMin, A3DDomain->m_dMax);
	return Domain;
}

} // ns TechSoftUtils

FTechSoftBridge::FTechSoftBridge(FSession& InSession, FCADFileReport& InReport)
	: Session(InSession)
	, Report(InReport)
	, GeometricTolerance(Session.GetGeometricTolerance())
	, SquareGeometricTolerance(FMath::Square(Session.GetGeometricTolerance()))
	, SquareJoiningVertexTolerance(SquareGeometricTolerance * 2)
{
}

TSharedRef<FBody> FTechSoftBridge::AddBody(A3DRiBrepModel* BrepModel, FEntityData& ParentData, double FileUnit)
{
	Report.BodyCount++;

	UnitScale = FileUnit;

	FEntityData BrepMetaData;
	TechSoftUtils::TraverseSource(BrepModel, BrepMetaData, false);

	TSharedPtr<FEntity>* EntityPtr = TechSoftToEntity.Find(BrepModel);
	if (EntityPtr)
	{
		TSharedPtr<FBody> Body = StaticCastSharedPtr<FBody>(*EntityPtr);
		if (!Body.IsValid())
		{
			return Body.ToSharedRef();
		}
	}

	TSharedRef<FBody> Body = FEntity::MakeShared<FBody>();
	TechSoftToEntity.Add(BrepModel, Body);

	AddMetadata(BrepMetaData, *Body);

	CADLibrary::TUniqueTSObj<A3DRiBrepModelData> BRepModelData(BrepModel);
	if (BRepModelData.IsValid())
	{
		TraverseBrepData(BRepModelData->m_pBrepData, Body);
	}

	return Body;
}

void FTechSoftBridge::TraverseBrepData(const A3DTopoBrepData* A3DBrepData, TSharedRef<FBody>& Body)
{
	FEntityData MetaData;
	TechSoftUtils::TraverseSource(A3DBrepData, MetaData, false);

	{
		CADLibrary::TUniqueTSObj<A3DTopoBodyData> TopoBodyData(A3DBrepData);
		if (TopoBodyData.IsValid())
		{
			if (TopoBodyData->m_pContext)
			{
				CADLibrary::TUniqueTSObj<A3DTopoContextData> TopoContextData(TopoBodyData->m_pContext);
				if (TopoContextData.IsValid())
				{
					if (TopoContextData->m_bHaveScale)
					{
						UnitScale *= TopoContextData->m_dScale;
					}
				}
			}
		}
	}

	CADLibrary::TUniqueTSObj<A3DTopoBrepDataData> TopoBrepData(A3DBrepData);
	if (TopoBrepData.IsValid())
	{
		for (A3DUns32 Index = 0; Index < TopoBrepData->m_uiConnexSize; ++Index)
		{
			TraverseConnex(TopoBrepData->m_ppConnexes[Index], Body);
		}
	}
}

void FTechSoftBridge::TraverseConnex(const A3DTopoConnex* A3DTopoConnex, TSharedRef<FBody>& Body)
{
	FEntityData MetaData;
	TechSoftUtils::TraverseSource(A3DTopoConnex, MetaData);

	CADLibrary::TUniqueTSObj<A3DTopoConnexData> TopoConnexData(A3DTopoConnex);
	if (TopoConnexData.IsValid())
	{
		for (A3DUns32 Index = 0; Index < TopoConnexData->m_uiShellSize; ++Index)
		{
			TraverseShell(TopoConnexData->m_ppShells[Index], Body);
		}
	}
}

void FTechSoftBridge::TraverseShell(const A3DTopoShell* A3DShell, TSharedRef<FBody>& Body)
{
	FEntityData MetaData;
	TechSoftUtils::TraverseSource(A3DShell, MetaData);

	TSharedRef<FShell> Shell = FEntity::MakeShared<FShell>();
	Body->AddShell(Shell);
	Report.ShellCount++;

	AddMetadata(MetaData, *Shell);

	CADLibrary::TUniqueTSObj<A3DTopoShellData> ShellData(A3DShell);
	if (ShellData.IsValid())
	{
		for (A3DUns32 ui = 0; ui < ShellData->m_uiFaceSize; ++ui)
		{
			AddFace(ShellData->m_ppFaces[ui], Shell);
		}
	}
}

static bool bUseCurveAsNurbs = true;
TSharedPtr<FCurve> FTechSoftBridge::AddCurve(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	TSharedPtr<FCurve> Curve = TSharedPtr<FCurve>();
	A3DEEntityType eType;
	A3DInt32 Ret = CADLibrary::TechSoftInterface::GetEntityType(A3DCurve, &eType);
	if (Ret == A3D_SUCCESS)
	{
		Report.CurveCount++;

		switch (eType)
		{
		case kA3DTypeCrvNurbs:
			Curve = AddCurveNurbs(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvLine:
			Curve = AddCurveLine(A3DCurve, UVReparameterization);
			Report.CurveNurbsCount++;
			break;
		case kA3DTypeCrvCircle:
			Curve = AddCurveCircle(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvEllipse:
			Curve = AddCurveEllipse(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvParabola:
			Curve = AddCurveParabola(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvHyperbola:
			Curve = AddCurveHyperbola(A3DCurve, UVReparameterization);
			Report.CurveNurbsCount++;
			break;
		case kA3DTypeCrvHelix:
			Curve = AddCurveHelix(A3DCurve, UVReparameterization);
			Report.CurveNurbsCount++;
			break;
		case kA3DTypeCrvPolyLine:
			Curve = AddCurvePolyLine(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvComposite:
			Curve = AddCurveComposite(A3DCurve, UVReparameterization);
			break;
		default:
			Curve = AddCurveAsNurbs(A3DCurve, UVReparameterization);
			break;
		}
	}

	FLinearBoundary Boundary = TechSoftUtils::GetLinearBoundary(A3DCurve);

	return Curve;
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveLine(const A3DCrvLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveLineCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}


	CADLibrary::TUniqueTSObj<A3DCrvLineData> CrvLineData(A3DCurve);
	if (!CrvLineData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvLineData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//traverseTransfo3d(sData.m_sTrsf, crv);
	//traverseParam(&sData.m_sParam, crv);
	//A3DCrvLineGet(NULL, &sData);

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveCircle(const A3DCrvCircle* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveCircleCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvCircleData> CrvCircleData(A3DCurve);
	if (!CrvCircleData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvCircleData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//traverseTransfo3d(sData.m_sTrsf, crv);
	//traverseParam(&sData.m_sParam, crv);
	//_SetDoubleAttribute(crv, "m_dRadius", sData.m_dRadius);

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveEllipse(const A3DCrvEllipse* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveEllipseCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvEllipseData> CrvEllipseData(A3DCurve);
	if (!CrvEllipseData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvEllipseData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//traverseTransfo3d(sData.m_sTrsf, crv);
	//traverseParam(&sData.m_sParam, crv);
	//_SetDoubleAttribute(crv, "m_dXRadius", sData.m_dXRadius);
	//_SetDoubleAttribute(crv, "m_dYRadius", sData.m_dYRadius);

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveParabola(const A3DCrvParabola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveParabolaCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvParabolaData> CrvParabolaData(A3DCurve);
	if (!CrvParabolaData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvParabolaData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//	traverseTransfo3d(sData.m_sTrsf, crv);
	//	traverseParam(&sData.m_sParam, crv);
	//	_SetDoubleAttribute(crv, "m_dFocal", sData.m_dFocal);
	//	crv->SetAttribute("m_cParamType", (int)sData.m_cParamType);

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveHyperbola(const A3DCrvHyperbola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveHyperbolaCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvHyperbolaData> CrvHyperbolaData(A3DCurve);
	if (!CrvHyperbolaData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvHyperbolaData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//traverseTransfo3d(sData.m_sTrsf, crv);
	//traverseParam(&sData.m_sParam, crv);
	//_SetDoubleAttribute(crv, "m_dSemiAxis", sData.m_dSemiAxis);
	//_SetDoubleAttribute(crv, "m_dSemiImageAxis", sData.m_dSemiImageAxis);
	//crv->SetAttribute("m_cParamType", (int)sData.m_cParamType);

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveHelix(const A3DCrvHelix* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveHelixCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvHelixData> CrvHelixData(A3DCurve);
	if (!CrvHelixData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvHelixData->m_bIs2D;

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurvePolyLine(const A3DCrvPolyLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurvePolyLineCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvPolyLineData> CrvPolyLineData(A3DCurve);
	if (!CrvPolyLineData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvPolyLineData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//traverseTransfo3d(sData.m_sTrsf, crv);
	//traverseParam(&sData.m_sParam, crv);
	//traversePoints("m_pPts", sData.m_uiSize, sData.m_pPts, crv);


	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveComposite(const A3DCrvComposite* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveCompositeCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DCrvCompositeData> CrvCompositeData(A3DCurve);
	if (!CrvCompositeData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	bool bIs2D = (bool)CrvCompositeData->m_bIs2D;

	//setting->SetAttribute("m_bIs2D", (int)sData.m_bIs2D);
	//traverseTransfo3d(sData.m_sTrsf, crv);
	//traverseParam(&sData.m_sParam, crv);
	//crv->SetAttribute("m_bClosed", (int)sData.m_bClosed);
	//traverseBools("m_pbSenses", sData.m_uiSize, sData.m_pbSenses, crv);

	//A3DUns32 ui, uiSize = sData.m_uiSize;
	//for (ui = 0; ui < uiSize; ++ui)
	//	traverseCurve(sData.m_ppCurves[ui], crv);

	return TSharedPtr<FCurve>();
}

TSharedPtr<FCurve> AddCurveNurbsFromData(A3DCrvNurbsData& A3DNurbs, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	FNurbsCurveData Nurbs;
	Nurbs.Dimension = A3DNurbs.m_bIs2D ? 2 : 3;
	Nurbs.bIsRational = (bool)A3DNurbs.m_bRational;
	Nurbs.Degree = A3DNurbs.m_uiDegree;

	TechSoftUtils::FillPointArray(A3DNurbs.m_uiCtrlSize, A3DNurbs.m_pCtrlPts, Nurbs.Poles);
	if (UVReparameterization.NeedApply())
	{
		for (FPoint& Point : Nurbs.Poles)
		{
			UVReparameterization.Apply(Point);
		}
	}

	TechSoftUtils::FillDoubleArray(A3DNurbs.m_uiKnotSize, A3DNurbs.m_pdKnots, Nurbs.NodalVector);
	if (Nurbs.bIsRational)
	{
		TechSoftUtils::FillDoubleArray(A3DNurbs.m_uiCtrlSize, A3DNurbs.m_pdWeights, Nurbs.Weights);
	}

	//A3DNurbs.m_eKnotType;
	//kA3DKnotTypeUniformKnots,			/*!< Uniform. */
	//kA3DKnotTypeUnspecified,			/*!< No particularity. */
	//kA3DKnotTypeQuasiUniformKnots,	/*!< Quasi-uniform. */
	//kA3DKnotTypePieceWiseBezierKnots	/*!< Extrema with multiplicities of degree + 1, internal is degree. */
	ensureCADKernel(A3DNurbs.m_eKnotType != kA3DKnotTypePieceWiseBezierKnots);

	//A3DNurbs.m_eCurveForm;
	//kA3DBSplineCurveFormUnspecified,	/*!< No particularity. */
	//kA3DBSplineCurveFormPolyline,		/*!< Polyline. */
	//kA3DBSplineCurveFormCircularArc,	/*!< Circle arc. */
	//kA3DBSplineCurveFormEllipticArc,	/*!< Elliptic arc. */
	//kA3DBSplineCurveFormParabolicArc,	/*!< Parabolic arc. */
	//kA3DBSplineCurveFormHyperbolicArc	/*!< Hyperbolic arc. */
	ensureCADKernel(A3DNurbs.m_eCurveForm != kA3DBSplineCurveFormPolyline);
	ensureCADKernel(A3DNurbs.m_eCurveForm != kA3DBSplineCurveFormCircularArc);
	ensureCADKernel(A3DNurbs.m_eCurveForm != kA3DBSplineCurveFormEllipticArc);
	ensureCADKernel(A3DNurbs.m_eCurveForm != kA3DBSplineCurveFormParabolicArc);
	ensureCADKernel(A3DNurbs.m_eCurveForm != kA3DBSplineCurveFormHyperbolicArc);

	A3DCrvNurbsGet(NULL, &A3DNurbs);

	return FEntity::MakeShared<FNURBSCurve>(Nurbs);
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveNurbs(const A3DCrvNurbs* A3DNurbs, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveLineCount++;

	CADLibrary::TUniqueTSObj<A3DCrvNurbsData> CrvNurbsData(A3DNurbs);
	if (!CrvNurbsData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	return AddCurveNurbsFromData(*CrvNurbsData, UVReparameterization);
}

TSharedPtr<FCurve> FTechSoftBridge::AddCurveAsNurbs(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveAsNurbsCount++;

	CADLibrary::TUniqueTSObj<A3DCrvNurbsData> NurbsData;

	A3DDouble Tolerance = 1e-3;
	A3DBool bUseSameParameterization = true;
	NurbsData.FillWith(&TechSoftUtils::GetCurveAsNurbs, A3DCurve, Tolerance, bUseSameParameterization);

	if (!NurbsData.IsValid())
	{
		return TSharedPtr<FCurve>();
	}

	return AddCurveNurbsFromData(*NurbsData, UVReparameterization);
}

TSharedPtr<FTopologicalEdge> FTechSoftBridge::AddEdge(const A3DTopoCoEdge* A3DCoedge, const TSharedRef<FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization, EOrientation& OutOrientation)
{
	Report.EdgeCount++;

	CADLibrary::TUniqueTSObj<A3DTopoCoEdgeData> CoEdgeData(A3DCoedge);
	if (!CoEdgeData.IsValid())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	ensureCADKernel(CoEdgeData->m_pUVCurve);

	TSharedPtr<FCurve> Curve = AddCurve(CoEdgeData->m_pUVCurve, UVReparameterization);
	if (!Curve.IsValid())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	TSharedRef<FRestrictionCurve> RestrictionCurve = FEntity::MakeShared<FRestrictionCurve>(Surface, Curve.ToSharedRef());

	TSharedPtr<FTopologicalEdge> Edge;
	if (CoEdgeData->m_pEdge)
	{
		CADLibrary::TUniqueTSObj<A3DTopoEdgeData> A3DEdgeData(CoEdgeData->m_pEdge);
		if (A3DEdgeData.IsValid())
		{
			double Tolerance = A3DEdgeData->m_dTolerance;
			bool bHasTrimDomain = (bool)A3DEdgeData->m_bHasTrimDomain;

			if (A3DEdgeData->m_p3dCurve)
			{
				//traverseCurve(A3DEdgeData.m_p3dCurve, edge);
			}

			if (A3DEdgeData->m_bHasTrimDomain)
			{
				FLinearBoundary Boundary = TechSoftUtils::GetLinearBoundary(A3DEdgeData->m_sInterval);
				Edge = FTopologicalEdge::Make(RestrictionCurve, Boundary);
			}
		}
	}

	if (!Edge.IsValid())
	{
		Edge = FTopologicalEdge::Make(RestrictionCurve);
	}

	if (!Edge.IsValid())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	A3DEdgeToEdge.Emplace(A3DCoedge, Edge);

	//CoEdgeData->m_ucOrientationUVWithLoop;
	//CoEdgeData->m_ucOrientationWithLoop;
	OutOrientation = CoEdgeData->m_ucOrientationUVWithLoop > 0 ? EOrientation::Front : EOrientation::Back;

	// Link edges
	if (CoEdgeData->m_pNeighbor)
	{
		const A3DTopoCoEdge* Neighbor = CoEdgeData->m_pNeighbor;
		while (Neighbor && Neighbor != A3DCoedge)
		{
			TSharedPtr<FTopologicalEdge>* TwinEdge = A3DEdgeToEdge.Find(Neighbor);
			if (TwinEdge != nullptr)
			{
				Edge->Link(*TwinEdge->Get(), SquareJoiningVertexTolerance);
			}

			CADLibrary::TUniqueTSObj<A3DTopoCoEdgeData> NeighborData(Neighbor);
			if (NeighborData.IsValid())
			{
				Neighbor = NeighborData->m_pNeighbor;
			}
			else
			{
				break;
			}
		}
	}

	return Edge;
}

TSharedPtr<FTopologicalLoop> FTechSoftBridge::AddLoop(const A3DTopoLoop* A3DLoop, const TSharedRef<FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.LoopCount++;

	TArray<TSharedPtr<FTopologicalEdge>> Edges;
	TArray<EOrientation> Directions;

	CADLibrary::TUniqueTSObj<A3DTopoLoopData> TopoLoopData(A3DLoop);
	if (!TopoLoopData.IsValid())
	{
		Report.DegeneratedLoopCount++;
		return TSharedPtr<FTopologicalLoop>();
	}

	bool bLoopOrientation = (bool)TopoLoopData->m_ucOrientationWithSurface;
	for (A3DUns32 Index = 0; Index < TopoLoopData->m_uiCoEdgeSize; ++Index)
	{
		EOrientation Orientation;
		TSharedPtr<FTopologicalEdge> Edge = AddEdge(TopoLoopData->m_ppCoEdges[Index], Surface, UVReparameterization, Orientation);
		if (!Edge.IsValid())
		{
			Report.DegeneratedEdgeCount++;
			continue;
		}

		Edges.Emplace(Edge);
		Directions.Emplace(Orientation);
	}

	//int32 CoedgeCount = CTCoedgeIds.Count();
	//Edges.Reserve(CoedgeCount);
	//Directions.Reserve(CoedgeCount);

	//CTCoedgeIds.IteratorInitialize();

	//CT_OBJECT_ID CoEdgeId;
	//while ((CoEdgeId = CTCoedgeIds.IteratorIter()) != 0)
	//{
	//	TSharedPtr<FTopologicalEdge> Edge = AddEdge(CoEdgeId, Surface);
	//	if (!Edge.IsValid())
	//	{
	//		continue;
	//	}

	//	Edges.Emplace(Edge);
	//	Directions.Emplace(EOrientation::Front);
	//}

	if (Edges.Num() == 0)
	{
		Report.DegeneratedLoopCount++;
		return TSharedPtr<FTopologicalLoop>();
	}

	return FTopologicalLoop::Make(Edges, Directions, GeometricTolerance);
}

void FTechSoftBridge::AddFace(const A3DTopoFace* A3DFace, TSharedRef<FShell>& Shell)
{
	Report.FaceCount++;

	FEntityData MetaData;
	TechSoftUtils::TraverseSource(A3DFace, MetaData);

	CADLibrary::TUniqueTSObj<A3DTopoFaceData> TopoFaceData(A3DFace);
	if (!TopoFaceData.IsValid())
	{
		Report.FailedFaceCount++;
		return;
	}

	const A3DSurfBase* A3DSurface = TopoFaceData->m_pSurface;
	TechSoftUtils::FUVReparameterization UVReparameterization = TechSoftUtils::FUVReparameterization::Identity;
	TSharedPtr<FSurface> SurfacePtr = AddSurface(A3DSurface, UVReparameterization);
	if (!SurfacePtr.IsValid())
	{
		Report.DegeneratedSurfaceCount++;
		Report.FailedFaceCount++;
		return;
	}
	TSharedRef<FSurface> Surface = SurfacePtr.ToSharedRef();

	TSharedRef<FTopologicalFace> Face = FEntity::MakeShared<FTopologicalFace>(Surface);

	if (TopoFaceData->m_bHasTrimDomain)
	{
		FSurfacicBoundary SurfaceBoundary = TechSoftUtils::GetSurfacicBoundary(TopoFaceData->m_sSurfaceDomain, UnitScale);
		Surface->TrimBoundaryTo(SurfaceBoundary);
	}

	if (!TopoFaceData->m_uiLoopSize)
	{
		Face->ApplyNaturalLoops();
	}
	else
	{
		TArray<TSharedPtr<FTopologicalLoop>> Loops;

		for (A3DUns32 Index = 0; Index < TopoFaceData->m_uiLoopSize; ++Index)
		{
			TSharedPtr<FTopologicalLoop> Loop = FTechSoftBridge::AddLoop(TopoFaceData->m_ppLoops[Index], Surface, UVReparameterization);
			if (!Loop.IsValid())
			{
				continue;
			}

			TArray<FPoint2D> LoopSampling;
			Loop->Get2DSampling(LoopSampling);
			FAABB2D Boundary;
			Boundary += LoopSampling;
			Loop->Boundary.Set(Boundary.GetMin(), Boundary.GetMax());

			// Check if the loop is not composed with only degenerated edge
			bool bDegeneratedLoop = true;
			for (const FOrientedEdge& Edge : Loop->GetEdges())
			{
				if (!Edge.Entity->IsDegenerated())
				{
					bDegeneratedLoop = false;
					break;
				}
			}
			if (bDegeneratedLoop)
			{
				continue;
			}

			Loops.Add(Loop);
		}

		if (Loops.Num() == 0)
		{
			FMessage::Printf(Log, TEXT("The Face has no loop, this face is bounded by its natural boundaries\n"));
			//Face->ApplyNaturalLoops();
			Report.FailedFaceCount++;
			return; //?
		}
		else
		{
			Face->AddLoops(Loops, Report.DoubtfulLoopOrientationCount);
		}
	}

	AddMetadata(MetaData, *Face);
	//Face->SetPatchId((int32)CTFaceId);

	EOrientation Orientation = EOrientation::Front; //CTOrientation == CT_FORWARD ? EOrientation::Front : EOrientation::Back;
	Shell->Add(Face, Orientation);
}

static bool bUseSurfaceAsNurbs = true;

TSharedPtr<FSurface> FTechSoftBridge::AddSurface(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SurfaceCount++;

	FEntityData MetaData;
	TechSoftUtils::TraverseSource(A3DSurface, MetaData);

	A3DEEntityType Type;
	int32 Ret = CADLibrary::TechSoftInterface::GetEntityType(A3DSurface, &Type);
	if (Ret == A3D_SUCCESS)
	{
		switch (Type)
		{
		case kA3DTypeSurfBlend01:
			return AddBlend01Surface(A3DSurface, OutUVReparameterization);
			
		case kA3DTypeSurfBlend02:
			return AddBlend02Surface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfBlend03:
			return AddBlend03Surface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfNurbs:
			return AddNurbsSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfCone:
			OutUVReparameterization.SetScale(1, UnitScale);
			return AddConeSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfCylinder:
			OutUVReparameterization.SetScale(1, UnitScale);
			return AddCylinderSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfCylindrical:
			return AddCylindricalSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfOffset:
			return AddOffsetSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfPipe:
			return AddPipeSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfPlane:
			OutUVReparameterization.SetScale(UnitScale, UnitScale);
			return AddPlaneSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfRuled:
			return AddRuledSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfSphere:
			return AddSphereSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfRevolution:
			return AddRevolutionSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfExtrusion:
			return AddExtrusionSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfFromCurves:
			return AddSurfaceFromCurves(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfTorus:
			return AddTorusSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfTransform:
			return AddTransformSurface(A3DSurface, OutUVReparameterization);

		default:
			return AddSurfaceAsNurbs(A3DSurface, OutUVReparameterization);
		}
	}
	else if (Ret == A3D_NOT_IMPLEMENTED)
	{
		return AddSurfaceAsNurbs(A3DSurface, OutUVReparameterization);
	}
	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddConeSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.ConeSurfaceCount++;

	CADLibrary::TUniqueTSObj<A3DSurfConeData> A3DConeData(Surface);
	if (!A3DConeData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DConeData->m_sParam);
	FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DConeData->m_sTrsf, UnitScale);
	FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DConeData->m_sParam.m_sUVDomain, UnitScale);
	TSharedPtr<FSurface> Cone = FEntity::MakeShared<FConeSurface>(GeometricTolerance, CoordinateSystem, A3DConeData->m_dRadius * UnitScale, A3DConeData->m_dSemiAngle, Boundary);

	return Cone;
}

TSharedPtr<FSurface> FTechSoftBridge::AddCylinderSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.CylinderSurfaceCount++;

	CADLibrary::TUniqueTSObj<A3DSurfCylinderData> A3DCylinderData(Surface);
	if (!A3DCylinderData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DCylinderData->m_sParam);
	FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DCylinderData->m_sTrsf, UnitScale);
	FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DCylinderData->m_sParam.m_sUVDomain, UnitScale);
	TSharedPtr<FSurface> Cylinder = FEntity::MakeShared<FCylinderSurface>(GeometricTolerance, CoordinateSystem, A3DCylinderData->m_dRadius * UnitScale, Boundary);

	return Cylinder;
}

TSharedPtr<FSurface> FTechSoftBridge::AddLinearTransfoSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.LinearTransfoSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	//TUniqueTSObj<A3DSurfLinearTransfoData A3DLinearTransfoData;
	//A3D_INITIALIZE_DATA(A3DSurfLinearTransfoData, A3DLinearTransfoData);
	//A3DInt32 Ret = A3DSurfLinearTransfoGet(Surface, &A3DLinearTransfoData);
	//if (Ret != A3D_SUCCESS)
	//{
	//	return TSharedPtr<FSurface>();
	//}

	//A3DSurfLinearTransfoGet(NULL, &A3DLinearTransfoData);
	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddNurbsSurface(const A3DSurfNurbs* Nurbs, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.NurbsSurfaceCount++;

	CADLibrary::TUniqueTSObj<A3DSurfNurbsData> A3DNurbsData(Nurbs);
	if (!A3DNurbsData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return AddSurfaceNurbs(*A3DNurbsData, OutUVReparameterization);
}

TSharedPtr<FSurface> FTechSoftBridge::AddOffsetSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.OffsetSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddPlaneSurface(const A3DSurfPlane* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.PlaneSurfaceCount++;

	CADLibrary::TUniqueTSObj<A3DSurfPlaneData> A3DPlaneData(Surface);
	if (!A3DPlaneData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DPlaneData->m_sParam);
	FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DPlaneData->m_sTrsf, UnitScale);
	FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DPlaneData->m_sParam.m_sUVDomain, UnitScale);
	TSharedPtr<FSurface> Plane = FEntity::MakeShared<FPlaneSurface>(GeometricTolerance, CoordinateSystem, Boundary);

	return Plane;
}

TSharedPtr<FSurface> FTechSoftBridge::AddRevolutionSurface(const A3DSurfRevolution* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.RevolutionSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfRevolutionData> A3DRevolutionData(Surface);
	if (!A3DRevolutionData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddRuledSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.RuledSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfRuledData> A3DRuledData(Surface);
	if (!A3DRuledData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddSphereSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SphereSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfSphereData> A3DSphereData(Surface);
	if (!A3DSphereData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DSphereData->m_sParam);
	FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DSphereData->m_sTrsf, UnitScale);
	FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DSphereData->m_sParam.m_sUVDomain, UnitScale);
	TSharedRef<FSphericalSurface> Sphere = FEntity::MakeShared<FSphericalSurface>(GeometricTolerance, CoordinateSystem, A3DSphereData->m_dRadius * UnitScale, Boundary);

	return Sphere;
}

TSharedPtr<FSurface> FTechSoftBridge::AddTorusSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.TorusSurfaceCount++;

	CADLibrary::TUniqueTSObj<A3DSurfTorusData> A3DTorusData(Surface);
	if (!A3DTorusData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DTorusData->m_sParam);
	FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DTorusData->m_sTrsf, UnitScale);
	FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DTorusData->m_sParam.m_sUVDomain, UnitScale);
	TSharedPtr<FSurface> Torus = FEntity::MakeShared<FTorusSurface>(GeometricTolerance, CoordinateSystem, A3DTorusData->m_dMajorRadius * UnitScale, A3DTorusData->m_dMinorRadius * UnitScale, Boundary);

	return Torus;
}

TSharedPtr<FSurface> FTechSoftBridge::AddBlend01Surface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.Blend01SurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfBlend01Data> A3DBlend01Data(Surface);
	if (!A3DBlend01Data.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddBlend02Surface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.Blend02SurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfBlend02Data> A3DBlend02Data(Surface);
	if (!A3DBlend02Data.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddBlend03Surface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.Blend03SurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

#ifdef WIP
	CADLibrary::TUniqueTSObj<A3DSurfBlend03Data> A3DBlend03Data(Surface);
	if (!A3DBlend03Data.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	FBlend03Data BlendData;

	F3DDebugSession _(TEXT("Blend03Surface"));
	{

		BlendData.CenterCurve.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.RailCurve1.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.RailCurve2.Reserve(A3DBlend03Data->m_uiNbOfElement);

		BlendData.Parameters.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.Multiplicities.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.RailToAnglesV.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.Tangents.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.RailToDerivativesV.Reserve(A3DBlend03Data->m_uiNbOfElement);
		BlendData.SecondDerivatives.Reserve(A3DBlend03Data->m_uiNbOfElement * 3);
		BlendData.RailToSecondDerivatives.Reserve(A3DBlend03Data->m_uiNbOfElement);

		BlendData.RailToParameterV = A3DBlend03Data->m_dRail2ParameterV;
		BlendData.TrimVMin= A3DBlend03Data->m_dTrimVMin;
		BlendData.TrimVMax= A3DBlend03Data->m_dTrimVMax;
		BlendData.InitialMaxV = A3DBlend03Data->m_dInitialMaxV;

		TArray<FPoint> Positions;
		A3DBlend03Data->m_pPositions;
		TechSoftUtils::FillPointArray(A3DBlend03Data->m_uiNbOfElement*3, A3DBlend03Data->m_pPositions, Positions, UnitScale);

		for (uint32 Index = 0; Index < (3 * A3DBlend03Data->m_uiNbOfElement);)
		{
			BlendData.RailCurve1.Add(Positions[Index++]);
			BlendData.RailCurve2.Add(Positions[Index++]);
			BlendData.CenterCurve.Add(Positions[Index++]);
		}

		TechSoftUtils::FillPointArray(A3DBlend03Data->m_uiNbOfElement * 3, A3DBlend03Data->m_pTangent, Positions, 1);
		TechSoftUtils::FillPointArray(A3DBlend03Data->m_uiNbOfElement * 3, A3DBlend03Data->m_pSecondDerivatives, Positions, 1);

		TechSoftUtils::FillDoubleArray(A3DBlend03Data->m_uiNbOfElement, A3DBlend03Data->m_pdParameters, BlendData.Parameters);
		TechSoftUtils::FillInt32Array(A3DBlend03Data->m_uiNbOfElement, A3DBlend03Data->m_piMultiplicities, BlendData.Multiplicities);
		TechSoftUtils::FillDoubleArray(A3DBlend03Data->m_uiNbOfElement, A3DBlend03Data->m_pdRail2AnglesV, BlendData.RailToAnglesV);
		TechSoftUtils::FillDoubleArray(A3DBlend03Data->m_uiNbOfElement, A3DBlend03Data->m_pdRail2DerivativesV, BlendData.RailToDerivativesV);
		TechSoftUtils::FillDoubleArray(A3DBlend03Data->m_uiNbOfElement, A3DBlend03Data->m_pdRail2SecondDerivatives, BlendData.RailToSecondDerivatives);

		DisplayPoints(TEXT("Center"), BlendData.CenterCurve);
		DisplayPoints(TEXT("RailCurve1"), BlendData.RailCurve1, EVisuProperty::YellowPoint);
		DisplayPoints(TEXT("RailCurve2"), BlendData.RailCurve2, EVisuProperty::GreenPoint);

		TechSoftUtils::FillPointArray(A3DBlend03Data->m_uiNbOfElement * 3, A3DBlend03Data->m_pPositions, Positions, UnitScale);

		FNurbsCurveData NurbsCurve;
		NurbsCurve.Dimension = 3;
		NurbsCurve.Degree = 5;
		for (uint32 Index = 0; Index < A3DBlend03Data->m_uiNbOfElement; ++Index)
		{
			for (int32 Mndex = 0; Mndex < BlendData.Multiplicities[Index]; ++Mndex)
			{
				NurbsCurve.NodalVector.Add(BlendData.Parameters[Index]);
			}
		}
		NurbsCurve.bIsRational = false;
		NurbsCurve.Poles = BlendData.CenterCurve;

		TSharedPtr<FNURBSCurve> CenterNurbs = FEntity::MakeShared<FNURBSCurve>(NurbsCurve);
		{
			F3DDebugSession _(TEXT("Center"));
			Display(*CenterNurbs);
		}

		NurbsCurve.Poles = BlendData.RailCurve1;
		TSharedPtr<FNURBSCurve> Rail1Nurbs = FEntity::MakeShared<FNURBSCurve>(NurbsCurve);
		{
			F3DDebugSession _(TEXT("Rail1Nurbs"));
			Display(*Rail1Nurbs);
		}

		NurbsCurve.Poles = BlendData.RailCurve2;
		TSharedPtr<FNURBSCurve> Rail2Nurbs = FEntity::MakeShared<FNURBSCurve>(NurbsCurve);
		{
			F3DDebugSession _(TEXT("Rail2Nurbs"));
			Display(*Rail2Nurbs);
		}

	}
#endif
	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddCylindricalSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.CylindricalSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfCylindricalData> A3DCylindricalData(Surface);
	if (!A3DCylindricalData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddPipeSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.PipeSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfPipeData> A3DPipeData(Surface);
	if (!A3DPipeData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddExtrusionSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.ExtrusionSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfExtrusionData> A3DExtrusionData(Surface);
	if (!A3DExtrusionData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddSurfaceFromCurves(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SurfaceFromCurvesCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfFromCurvesData> A3DFromCurvesData(Surface);
	if (!A3DFromCurvesData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddTransformSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.TransformSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	CADLibrary::TUniqueTSObj<A3DSurfFromCurvesData> A3DTransformData(Surface);
	if (!A3DTransformData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return TSharedPtr<FSurface>();
}

TSharedPtr<FSurface> FTechSoftBridge::AddSurfaceNurbs(const A3DSurfNurbsData& A3DNurbsData, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	FNurbsSurfaceData NurbsData;

	A3DNurbsData.m_eKnotType;
	//kA3DKnotTypeUniformKnots,			/*!< Uniform. */
	//kA3DKnotTypeUnspecified,			/*!< No particularity. */
	//kA3DKnotTypeQuasiUniformKnots,	/*!< Quasi-uniform. */
	//kA3DKnotTypePieceWiseBezierKnots	/*!< Extrema with multiplicities of degree + 1, internal is degree. */

	A3DNurbsData.m_eSurfaceForm;
	//kA3DBSplineSurfaceFormPlane,				/*!< Planar surface. */
	//kA3DBSplineSurfaceFormCylindrical,		/*!< Cylindrical surface. */
	//kA3DBSplineSurfaceFormConical,			/*!< Conical surface. */
	//kA3DBSplineSurfaceFormSpherical,			/*!< Spherical surface. */
	//kA3DBSplineSurfaceFormRevolution,			/*!< Surface of revolution. */
	//kA3DBSplineSurfaceFormRuled,				/*!< Ruled surface. */
	//kA3DBSplineSurfaceFormGeneralizedCone,	/*!< Cone. */
	//kA3DBSplineSurfaceFormQuadric,			/*!< Quadric surface. */
	//kA3DBSplineSurfaceFormLinearExtrusion,	/*!< Surface of extrusion. */
	//kA3DBSplineSurfaceFormUnspecified,		/*!< Unspecified. */
	//kA3DBSplineSurfaceFormPolynomial			/*!< Polynomial surface. */

	NurbsData.PoleUCount = A3DNurbsData.m_uiUCtrlSize;
	NurbsData.PoleVCount = A3DNurbsData.m_uiVCtrlSize;
	int32 PoleCount = A3DNurbsData.m_uiUCtrlSize * A3DNurbsData.m_uiVCtrlSize;

	NurbsData.UDegree = A3DNurbsData.m_uiUDegree;
	NurbsData.VDegree = A3DNurbsData.m_uiVDegree;

	TechSoftUtils::FillDoubleArray(A3DNurbsData.m_uiUKnotSize, A3DNurbsData.m_pdUKnots, NurbsData.UNodalVector);
	TechSoftUtils::FillDoubleArray(A3DNurbsData.m_uiVKnotSize, A3DNurbsData.m_pdVKnots, NurbsData.VNodalVector);

	TArray<FPoint> Poles;
	TechSoftUtils::FillPointArray(NurbsData.PoleUCount, NurbsData.PoleVCount, A3DNurbsData.m_pCtrlPts, NurbsData.Poles);
	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		for (FPoint& Point : NurbsData.Poles)
		{
			Point *= UnitScale;
		}
	}

	bool bIsRational = false;
	if (A3DNurbsData.m_pdWeights)
	{
		bIsRational = true;
		TechSoftUtils::FillDoubleArray(NurbsData.PoleUCount, NurbsData.PoleVCount, A3DNurbsData.m_pdWeights, NurbsData.Weights);
	}

	return FEntity::MakeShared<FNURBSSurface>(GeometricTolerance, NurbsData);
}

TSharedPtr<FSurface> FTechSoftBridge::AddSurfaceAsNurbs(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SurfaceAsNurbsCount++;

	CADLibrary::TUniqueTSObj<A3DSurfNurbsData> A3DNurbsData;

	A3DDouble Tolerance = 1e-3;
	A3DBool bUseSameParameterization = true;
	A3DNurbsData.FillWith(&TechSoftUtils::GetSurfaceAsNurbs, Surface, Tolerance, bUseSameParameterization);

	if (!A3DNurbsData.IsValid())
	{
		return TSharedPtr<FSurface>();
	}

	return AddSurfaceNurbs(*A3DNurbsData, OutUVReparameterization);

}

void FTechSoftBridge::AddMetadata(FEntityData& MetaData, FTopologicalShapeEntity& Entity)
{
	//Entity->SetHostId(CTNodeId);
	Entity.SetName(*MetaData.Name);
	//Entity.SetLayer(LayerId);
	//uint32 ColorHId = CADLibrary::BuildColorId(ColorId, Alpha);
	//Entity.SetColorId(ColorHId);
	//Entity.SetPatchId();
}

}


#endif // USE_TECHSOFT_SDK