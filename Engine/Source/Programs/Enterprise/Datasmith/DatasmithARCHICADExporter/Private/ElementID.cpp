// Copyright Epic Games, Inc. All Rights Reserved.

#include "ElementID.h"

BEGIN_NAMESPACE_UE_AC

template <>
FAssValueName::SAssValueName TAssEnumName< ModelerAPI::Element::Type >::AssEnumName[] = {
	EnumName(ModelerAPI::Element, UndefinedElement),
	EnumName(ModelerAPI::Element, WallElement),
	EnumName(ModelerAPI::Element, SlabElement),
	EnumName(ModelerAPI::Element, RoofElement),
	EnumName(ModelerAPI::Element, CurtainWallElement),
	EnumName(ModelerAPI::Element, CWFrameElement),
	EnumName(ModelerAPI::Element, CWPanelElement),
	EnumName(ModelerAPI::Element, CWJunctionElement),
	EnumName(ModelerAPI::Element, CWAccessoryElement),
	EnumName(ModelerAPI::Element, CWSegmentElement),
	EnumName(ModelerAPI::Element, ShellElement),
	EnumName(ModelerAPI::Element, SkylightElement),
	EnumName(ModelerAPI::Element, FreeshapeElement),
	EnumName(ModelerAPI::Element, DoorElement),
	EnumName(ModelerAPI::Element, WindowElement),
	EnumName(ModelerAPI::Element, ObjectElement),
	EnumName(ModelerAPI::Element, LightElement),
	EnumName(ModelerAPI::Element, ColumnElement),
	EnumName(ModelerAPI::Element, MeshElement),
	EnumName(ModelerAPI::Element, BeamElement),
	EnumName(ModelerAPI::Element, RoomElement),
#if AC_VERSION >= 21
	EnumName(ModelerAPI::Element, StairElement),
	EnumName(ModelerAPI::Element, RiserElement),
	EnumName(ModelerAPI::Element, TreadElement),
	EnumName(ModelerAPI::Element, StairStructureElement),
	EnumName(ModelerAPI::Element, RailingElement),
	EnumName(ModelerAPI::Element, ToprailElement),
	EnumName(ModelerAPI::Element, HandrailElement),
	EnumName(ModelerAPI::Element, RailElement),
	EnumName(ModelerAPI::Element, RailingPostElement),
	EnumName(ModelerAPI::Element, InnerPostElement),
	EnumName(ModelerAPI::Element, BalusterElement),
	EnumName(ModelerAPI::Element, RailingPanelElement),
	EnumName(ModelerAPI::Element, RailingSegmentElement),
	EnumName(ModelerAPI::Element, RailingNodeElement),
	EnumName(ModelerAPI::Element, RailPatternElement),
	EnumName(ModelerAPI::Element, InnerTopRailEndElement),
	EnumName(ModelerAPI::Element, InnerHandRailEndElement),
	EnumName(ModelerAPI::Element, RailFinishingObjectElement),
	EnumName(ModelerAPI::Element, TopRailConnectionElement),
	EnumName(ModelerAPI::Element, HandRailConnectionElement),
	EnumName(ModelerAPI::Element, RailConnectionElement),
	EnumName(ModelerAPI::Element, RailEndElement),
	EnumName(ModelerAPI::Element, BalusterSetElement),
#endif
#if AC_VERSION >= 23
	EnumName(ModelerAPI::Element, AnalyticalSupportElement),
	EnumName(ModelerAPI::Element, AnalyticalLinkElement),
	EnumName(ModelerAPI::Element, Opening),
	EnumName(ModelerAPI::Element, Openingframeinfill),
	EnumName(ModelerAPI::Element, Openingpatchinfill),
	EnumName(ModelerAPI::Element, ColumnSegmentElement),
	EnumName(ModelerAPI::Element, BeamSegmentElement),
#endif
	EnumName(ModelerAPI::Element, OtherElement),
	EnumEnd(-1)};

void FElementID::CollectDependantElementsType(API_ElemTypeID TypeID) const
{
	GS::Array< API_Guid > ConnectedElements;
	UE_AC_TestGSError(ACAPI_Element_GetConnectedElements(ElementHeader.guid, TypeID, &ConnectedElements));
	for (USize i = 0; i < ConnectedElements.GetSize(); ++i)
	{
		FSyncData*& ChildSyncData = SyncContext.GetSyncDatabase().GetSyncData(APIGuid2GSGuid(ConnectedElements[i]));
		if (ChildSyncData == nullptr)
		{
			ChildSyncData = new FSyncData::FElement(APIGuid2GSGuid(ConnectedElements[i]), SyncContext);
		}
		ChildSyncData->SetParent(SyncData);
		UE_AC_VerboseF("FElementID::ConnectedElements %u %s -> %s\n", i, APIGuidToString(ConnectedElements[i]).ToUtf8(),
					   SyncData->ElementId.ToUniString().ToUtf8());
	}
}

void FElementID::HandleDepedencies() const
{
	if (ElementHeader.typeID == API_WallID)
	{
		CollectDependantElementsType(API_WindowID);
		CollectDependantElementsType(API_DoorID);
	}
	else if (ElementHeader.typeID == API_RoofID || ElementHeader.typeID == API_ShellID)
	{
		CollectDependantElementsType(API_SkylightID);
	}
	else if (ElementHeader.typeID == API_WindowID || ElementHeader.typeID == API_DoorID ||
			 ElementHeader.typeID == API_SkylightID)
	{
		// Do nothing
	}
	else
	{
		GS::Guid				  OwnerElemGuid = APIGuid2GSGuid(ElementHeader.guid);
		API_Guid				  OwnerElemApiGuid = ElementHeader.guid;
		API_HierarchicalElemType  HierarchicalElemType = API_SingleElem;
		API_HierarchicalOwnerType HierarchicalOwnerType = API_RootHierarchicalOwner;
		GSErrCode GSErr = ACAPI_Goodies(APIAny_GetHierarchicalElementOwnerID, &OwnerElemGuid, &HierarchicalOwnerType,
										&HierarchicalElemType, &OwnerElemApiGuid);
		if (GSErr != NoError || OwnerElemApiGuid == APINULLGuid)
		{
			return;
		}

		if (HierarchicalElemType == API_ChildElemInMultipleElem)
		{
			FSyncData*& Parent = SyncContext.GetSyncDatabase().GetSyncData(APIGuid2GSGuid(OwnerElemApiGuid));
			if (Parent == nullptr)
			{
				Parent = new FSyncData::FElement(APIGuid2GSGuid(OwnerElemApiGuid), SyncContext);
			}
			SyncData->SetParent(Parent);
			SyncData->SetIsAComponent();
			Parent->SetDefaultParent(*this);
			UE_AC_VerboseF("FElementID::MakeConnections Child %s -> Parent %s\n",
						   SyncData->GetId().ToUniString().ToUtf8(), APIGuidToString(OwnerElemApiGuid).ToUtf8());
		}
		else
		{
		}
	}
}

END_NAMESPACE_UE_AC
