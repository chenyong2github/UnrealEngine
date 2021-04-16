// Copyright Epic Games, Inc. All Rights Reserved.

#include "ElementTools.h"
#include "TAssValueName.h"

BEGIN_NAMESPACE_UE_AC

// clang-format off
template <>
FAssValueName::SAssValueName TAssEnumName< API_ElemVariationID >::AssEnumName[] = {
	ValueName(APIVarId_Generic),

#if AC_VERSION < 25
	ValueName(APIVarId_LabelVirtSy),
	ValueName(APIVarId_LabelCeil),
	ValueName(APIVarId_LabelRoof),
	ValueName(APIVarId_LabelShell),
	ValueName(APIVarId_LabelMesh),
	ValueName(APIVarId_LabelHatch),
	ValueName(APIVarId_LabelCurtainWall),
	ValueName(APIVarId_LabelCWPanel),
	ValueName(APIVarId_LabelCWFrame),
	ValueName(APIVarId_LabelWall2),
	ValueName(APIVarId_LabelColumn),
	ValueName(APIVarId_LabelBeam),
	ValueName(APIVarId_LabelWind),
	ValueName(APIVarId_LabelDoor),
	ValueName(APIVarId_LabelSkylight),
	ValueName(APIVarId_LabelSymb),
	ValueName(APIVarId_LabelLight),
	ValueName(APIVarId_LabelMorph),
	ValueName(APIVarId_LabelCWAccessory),
	ValueName(APIVarId_LabelCWJunction),
#endif

	ValueName(APIVarId_SymbStair),
	ValueName(APIVarId_WallEnd),

	ValueName(APIVarId_Door),
	ValueName(APIVarId_Skylight),
	ValueName(APIVarId_Object),
	ValueName(APIVarId_GridElement),
	ValueName(APIVarId_Light),
	ValueName(APIVarId_CornerWindow),

	EnumEnd(-1)
};
// clang-format on

// Tool: return the info string (≈ name)
bool FElementTools::GetInfoString(const API_Guid& InGUID, GS::UniString* OutString)
{
	GSErrCode GSErr = ACAPI_Database(APIDb_GetElementInfoStringID, (void*)&InGUID, OutString);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("CElement::GetInfoString - Get info string error=%s\n", GetErrorName(GSErr));
		return false;
	}
	return true;
}

// Tool: Return the localize name for element type id
const GS::UniString& FElementTools::TypeName(API_ElemTypeID InElementType)
{
	UE_AC_Assert(API_FirstElemType <= InElementType && InElementType <= API_LastElemType);

	static std::unique_ptr< GS::UniString > TypeNames[API_LastElemType + 1] = {};

	if (TypeNames[InElementType] == nullptr)
	{
		TypeNames[InElementType] = std::make_unique< GS::UniString >();
		GSErrCode GSErr =
			ACAPI_Goodies(APIAny_GetElemTypeNameID, (void*)(size_t)InElementType, TypeNames[InElementType].get());
		if (GSErr != NoError)
		{
			UE_AC_DebugF("CElement::TypeName - Error %d for type%d\n", GSErr, InElementType);
		}
	}

	return *TypeNames[InElementType];
}

// Tool: Return the localize name for element's type
const GS::UniString& FElementTools::TypeName(const API_Guid& InElementGuid)
{
	API_Elem_Head ElementHead;
	Zap(&ElementHead);
	ElementHead.guid = InElementGuid;
	GSErrCode GSErr = ACAPI_Element_GetHeader(&ElementHead);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FElementTools::TypeName - Can't get element header {%s} Error=%d\n",
					 APIGuidToString(InElementGuid).ToUtf8(), GSErr);
		ElementHead.typeID = API_ZombieElemID;
	}
	return TypeName(ElementHead.typeID);
}

// Tool: Return the variation as string
utf8_string FElementTools::GetVariationAsString(API_ElemVariationID InVariation)
{
	utf8_string VariationString;
	if (InVariation != APIVarId_Generic)
	{
		const utf8_t* VarName = TAssEnumName< API_ElemVariationID >::GetName(InVariation, FAssValueName::kDontThrow);
		if (VarName[0] != 'U')
		{
			VariationString = VarName;
		}
		else
		{
			VariationString += '\'';
			const unsigned char* sv = (const unsigned char*)&InVariation;
			size_t				 IndexVariation = 0;
			for (; IndexVariation < 4 && sv[IndexVariation] >= 32 && sv[IndexVariation] <= 126; IndexVariation++)
			{
				VariationString += (char)sv[IndexVariation];
			}
			if (IndexVariation != 4)
			{
				VariationString = Utf8StringFormat("0x%08X", InVariation);
			}
			else
			{
				VariationString += '\'';
			}
		}
	}
	return VariationString;
}

// Tool: return libpart index (or 0 if no libpart)
GS::Int32 FElementTools::GetLibPartIndex(const API_Element& InElement)
{
	switch (InElement.header.typeID)
	{
		case API_WindowID:
		case API_DoorID:
			return InElement.door.openingBase.libInd;
		case API_ObjectID:
		case API_LampID:
			return InElement.lamp.libInd;
		case API_ZoneID:
			return InElement.zone.libInd;
		default:
			return 0;
	}
}

// Tool: return element's owner guid
API_Guid FElementTools::GetOwner(const API_Element& InElement)
{
	size_t Offset = GetOwnerOffset(InElement.header.typeID);
	if (Offset)
	{
		return *reinterpret_cast< const API_Guid* >(reinterpret_cast< const char* >(&InElement) + Offset);
	}
	return APINULLGuid;
}

// Tool: return element's owner guid
API_Guid FElementTools::GetOwner(const API_Guid& InElementGuid)
{
	API_Element ApiElement;
	Zap(&ApiElement);
	ApiElement.header.guid = InElementGuid;
	auto GSErr = ACAPI_Element_Get(&ApiElement);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("CSyncContext::IsSelected - ACAPI_Element_Get error=%s ObjectId=%s\n", GetErrorName(GSErr),
					 APIGuidToString(InElementGuid).ToUtf8());
		return APINULLGuid;
	}
	return GetOwner(ApiElement);
}

// Tool: return owner offset for specified element type
size_t FElementTools::GetOwnerOffset(API_ElemTypeID InTypeID)
{
	static size_t TableString[API_LastElemType + 1] = {0};
	static bool	  bInitialized = false;

	if (bInitialized == false)
	{
		bInitialized = true;

		TableString[API_WallID] = offsetof(API_Element, wall.head);
		TableString[API_ColumnID] = offsetof(API_Element, column.head);
		TableString[API_BeamID] = offsetof(API_Element, beam.head);
		TableString[API_WindowID] = offsetof(API_Element, window.owner);
		TableString[API_DoorID] = offsetof(API_Element, door.owner);
		TableString[API_ObjectID] = offsetof(API_Element, object.owner);
		TableString[API_LampID] = offsetof(API_Element, lamp.owner);
		TableString[API_SlabID] = offsetof(API_Element, slab.head);
		TableString[API_RoofID] = offsetof(API_Element, roof.head);
		TableString[API_MeshID] = offsetof(API_Element, mesh.head);

		TableString[API_DimensionID] = offsetof(API_Element, dimension.head);
		TableString[API_RadialDimensionID] = offsetof(API_Element, radialDimension.head);
		TableString[API_LevelDimensionID] = offsetof(API_Element, levelDimension.head);
		TableString[API_AngleDimensionID] = offsetof(API_Element, angleDimension.head);

		TableString[API_TextID] = offsetof(API_Element, text.owner);
		TableString[API_LabelID] = offsetof(API_Element, label.parent);
		TableString[API_ZoneID] = offsetof(API_Element, zone.head);

		TableString[API_HatchID] = offsetof(API_Element, hatch.head);
		TableString[API_LineID] = offsetof(API_Element, line.head);
		TableString[API_PolyLineID] = offsetof(API_Element, polyLine.head);
		TableString[API_ArcID] = offsetof(API_Element, arc.head);
		TableString[API_CircleID] = offsetof(API_Element, circle.head);
		TableString[API_SplineID] = offsetof(API_Element, spline.head);
		TableString[API_HotspotID] = offsetof(API_Element, hotspot.head);

		TableString[API_CutPlaneID] = offsetof(API_Element, cutPlane.head);
		TableString[API_CameraID] = offsetof(API_Element, camera.head);
		TableString[API_CamSetID] = offsetof(API_Element, camset.head);

		TableString[API_GroupID] = 0;
		TableString[API_SectElemID] = offsetof(API_Element, sectElem.head);

		TableString[API_DrawingID] = offsetof(API_Element, drawing.head);
		TableString[API_PictureID] = offsetof(API_Element, picture.head);
		TableString[API_DetailID] = offsetof(API_Element, detail.head);
		TableString[API_ElevationID] = offsetof(API_Element, elevation.head);
		TableString[API_InteriorElevationID] = offsetof(API_Element, interiorElevation.head);
		TableString[API_WorksheetID] = offsetof(API_Element, worksheet.head);

		TableString[API_HotlinkID] = offsetof(API_Element, hotlink.head);

		TableString[API_CurtainWallID] = offsetof(API_Element, curtainWall.head);
		TableString[API_CurtainWallSegmentID] = offsetof(API_Element, cwSegment.owner);
		TableString[API_CurtainWallFrameID] = offsetof(API_Element, cwFrame.owner);
		TableString[API_CurtainWallPanelID] = offsetof(API_Element, cwPanel.owner);
		TableString[API_CurtainWallJunctionID] = offsetof(API_Element, cwJunction.owner);
		TableString[API_CurtainWallAccessoryID] = offsetof(API_Element, cwAccessory.owner);
		TableString[API_ShellID] = offsetof(API_Element, shell.head);
		TableString[API_SkylightID] = offsetof(API_Element, skylight.owner);
		TableString[API_MorphID] = offsetof(API_Element, morph.head);
		TableString[API_ChangeMarkerID] = offsetof(API_Element, changeMarker.head);

		TableString[API_StairID] = offsetof(API_Element, stair.head);
		TableString[API_RiserID] = offsetof(API_Element, stairRiser.owner);
		TableString[API_TreadID] = offsetof(API_Element, stairTread.owner);
		TableString[API_StairStructureID] = offsetof(API_Element, stairStructure.owner);

		TableString[API_RailingID] = offsetof(API_Element, railing.head);
		TableString[API_RailingToprailID] = offsetof(API_Element, railingToprail.owner);
		TableString[API_RailingHandrailID] = offsetof(API_Element, railingHandrail.owner);
		TableString[API_RailingRailID] = offsetof(API_Element, railingRail.owner);
		TableString[API_RailingPostID] = offsetof(API_Element, railingPost.owner);
		TableString[API_RailingInnerPostID] = offsetof(API_Element, railingInnerPost.owner);
		TableString[API_RailingBalusterID] = offsetof(API_Element, railingBaluster.owner);
		TableString[API_RailingPanelID] = offsetof(API_Element, railingPanel.owner);
		TableString[API_RailingSegmentID] = offsetof(API_Element, railingSegment.owner);
		TableString[API_RailingNodeID] = offsetof(API_Element, railingNode.owner);
		TableString[API_RailingBalusterSetID] = offsetof(API_Element, railingBalusterSet.owner);
		TableString[API_RailingPatternID] = offsetof(API_Element, railingPattern.owner);
		TableString[API_RailingToprailEndID] = offsetof(API_Element, railingToprailEnd.owner);
		TableString[API_RailingHandrailEndID] = offsetof(API_Element, railingToprailEnd.owner);
		TableString[API_RailingRailEndID] = offsetof(API_Element, railingRailEnd.owner);
		TableString[API_RailingToprailConnectionID] = offsetof(API_Element, railingToprailConnection.owner);
		TableString[API_RailingHandrailConnectionID] = offsetof(API_Element, railingHandrailConnection.owner);
		TableString[API_RailingRailConnectionID] = offsetof(API_Element, railingRailConnection.owner);
		TableString[API_RailingEndFinishID] = offsetof(API_Element, railingEndFinish.owner);

		TableString[API_AnalyticalSupportID] = offsetof(API_Element, analyticalSupport.head);
		TableString[API_AnalyticalLinkID] = offsetof(API_Element, analyticalLink.head);
		TableString[API_ColumnSegmentID] = offsetof(API_Element, columnSegment.owner);
		TableString[API_BeamSegmentID] = offsetof(API_Element, beamSegment.owner);
		TableString[API_OpeningID] = offsetof(API_Element, opening.owner);
	}

	if (InTypeID < 0 && InTypeID > API_LastElemType)
	{
		UE_AC_DebugF("FElementTools::GetOwnerOffset - Invalid API_ElemTypeID=%d\n", InTypeID);
		InTypeID = API_ZombieElemID;
	}
	return TableString[InTypeID];
}

// Tool: return classifications of the element
GSErrCode FElementTools::GetElementClassifications(
	GS::Array< GS::Pair< API_ClassificationSystem, API_ClassificationItem > >& OutClassifications,
	const API_Guid&															   InElementGuid)
{
	GS::Array< GS::Pair< API_Guid, API_Guid > > SystemItemPairs;
	UE_AC_ReturnOnGSError(ACAPI_Element_GetClassificationItems(InElementGuid, SystemItemPairs));

	for (GS::Array< GS::Pair< API_Guid, API_Guid > >::FastIterator IterSystemItemPair = SystemItemPairs.BeginFast();
		 IterSystemItemPair != SystemItemPairs.EndFast(); ++IterSystemItemPair)
	{
		API_ClassificationSystem System;
		System.guid = IterSystemItemPair->first;
		UE_AC_ReturnOnGSError(ACAPI_Classification_GetClassificationSystem(System));

		API_ClassificationItem Classification;
		UE_AC_ReturnOnGSError(
			ACAPI_Element_GetClassificationInSystem(InElementGuid, IterSystemItemPair->first, Classification));

		GS::Pair< API_ClassificationSystem, API_ClassificationItem > ApiClassification(System, Classification);
		OutClassifications.Push(ApiClassification);
	}

	return NoError;
}

GSErrCode FElementTools::GetElementProperties(GS::Array< API_Property >& OutProperties, const API_Guid& InElementGuid)
{
	GS::Array< API_PropertyDefinition > PropertyDefinitions;
	UE_AC_ReturnOnGSError(ACAPI_Element_GetPropertyDefinitions(InElementGuid, API_PropertyDefinitionFilter_UserDefined,
															   PropertyDefinitions));

	GS::Array< API_PropertyDefinition > PropertyDefinitionsFiltered;
	for (const API_PropertyDefinition& PropertyDefinition : PropertyDefinitions)
	{
		if (PropertyDefinition.measureType == API_PropertyDefaultMeasureType ||
			PropertyDefinition.measureType == API_PropertyUndefinedMeasureType)
		{
			API_Property Property;
			Property.definition = PropertyDefinition;
			OutProperties.Push(Property);
			PropertyDefinitionsFiltered.Push(PropertyDefinition);
		}
	}

	UE_AC_ReturnOnGSError(ACAPI_Element_GetPropertyValues(InElementGuid, PropertyDefinitionsFiltered, OutProperties));

	return NoError;
}

END_NAMESPACE_UE_AC
