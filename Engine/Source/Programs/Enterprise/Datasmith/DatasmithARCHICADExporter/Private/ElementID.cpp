// Copyright Epic Games, Inc. All Rights Reserved.

#include "ElementID.h"

#include <stdexcept>

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

// Contructor
FElementID::FElementID(const FSyncContext& InSyncContext)
	: SyncContext(InSyncContext)
	, Index3D(0)
	, SyncData(nullptr)
	, LibPartInfo(nullptr)
	, bFullElementFetched(false)
	, bLibPartInfoFetched(false)
{
}

// Initialize with 3D element
void FElementID::InitElement(GS::Int32 InIndex3d)
{
	Index3D = InIndex3d;
	SyncContext.GetModel().GetElement(Index3D, &Element3D);
	APIElement.header.guid = APINULLGuid;
	bFullElementFetched = false;
	LibPartInfo = nullptr;
	bLibPartInfoFetched = false;
}

// Initialize with sync data
void FElementID::InitElement(FSyncData* IOSyncdata)
{
	UE_AC_TestPtr(IOSyncdata);
	SyncData = IOSyncdata;
	Index3D = IOSyncdata->GetIndex3D();
	if (Index3D > 0)
	{
		SyncContext.GetModel().GetElement(Index3D, &Element3D);
	}
	bFullElementFetched = false;
	LibPartInfo = nullptr;
	bLibPartInfoFetched = false;
}

// Initialize element header from 3D element
bool FElementID::InitHeader()
{
	if (IsInvalid())
	{
		throw std::runtime_error(
			Utf8StringFormat("FElementID::InitHeader - Invalid element for index=%d\n", Index3D).c_str());
	}
	bFullElementFetched = false;
	Zap(&APIElement.header);
	APIElement.header.guid = GSGuid2APIGuid(Element3D.GetElemGuid());
	GSErrCode GSErr = ACAPI_Element_GetHeader(&APIElement.header, 0);
	if (GSErr != NoError)
	{
		utf8_string ErrorName(GetErrorName(GSErr));
		utf8_string TypeName(GetTypeName());
		UE_AC_DebugF("Error \"%s\" with element %d {%s} Type=%s\n", ErrorName.c_str(), Index3D,
					 Element3D.GetElemGuid().ToUniString().ToUtf8(), TypeName.c_str());
		if (GSErr != APIERR_BADID)
		{
			UE_AC::ThrowGSError(GSErr, __FILE__, __LINE__);
		}
		return false;
	}
	return true;
}

const API_Element& FElementID::GetAPIElement()
{
	if (bFullElementFetched == false)
	{
		API_Guid guid = APIElement.header.guid;
		Zap(&APIElement);
		APIElement.header.guid = guid;
		UE_AC_TestGSError(ACAPI_Element_Get(&APIElement, 0));
		bFullElementFetched = true;
	}

	return APIElement;
}

// If this element is related to a lib part ?
const FLibPartInfo* FElementID::GetLibPartInfo()
{
	if (bLibPartInfoFetched == false)
	{
		// Get the lib part from it's UnId
		FGSUnID::Buffer lpfUnID = {0};
		GSErrCode		GSErr = ACAPI_Goodies(APIAny_GetElemLibPartUnIdID, &APIElement.header, lpfUnID);
		if (GSErr == NoError)
		{
			LibPartInfo = SyncContext.GetSyncDatabase().GetLibPartInfo(lpfUnID);
		}
		else if (GSErr != APIERR_BADID)
		{
			UE_AC_DebugF("FElementID::InitLibPartInfo - APIAny_GetElemLibPartUnIdID return error %s\n",
						 GetErrorName(GSErr));
		}
		bLibPartInfoFetched = true;
	}

	return LibPartInfo;
}

void FElementID::CollectDependantElementsType(API_ElemTypeID TypeID) const
{
	GS::Array< API_Guid > ConnectedElements;
	UE_AC_TestGSError(ACAPI_Element_GetConnectedElements(APIElement.header.guid, TypeID, &ConnectedElements));
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
	if (APIElement.header.typeID == API_WallID)
	{
		CollectDependantElementsType(API_WindowID);
		CollectDependantElementsType(API_DoorID);
	}
	else if (APIElement.header.typeID == API_RoofID || APIElement.header.typeID == API_ShellID)
	{
		CollectDependantElementsType(API_SkylightID);
	}
	else if (APIElement.header.typeID == API_WindowID || APIElement.header.typeID == API_DoorID ||
			 APIElement.header.typeID == API_SkylightID)
	{
		// Do nothing
	}
	else
	{
		GS::Guid				  OwnerElemGuid = APIGuid2GSGuid(APIElement.header.guid);
		API_Guid				  OwnerElemApiGuid = APIElement.header.guid;
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
