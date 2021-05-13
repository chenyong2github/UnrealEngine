// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

#include "SyncContext.h"
#include "SyncData.h"
#include "Utils/TAssValueName.h"

#include "ModelElement.hpp"

BEGIN_NAMESPACE_UE_AC

// Class that contain element id and related
class FElementID
{
  public:
	// Contructor
	FElementID(const FSyncContext& InSyncContext);

	// Initialize with 3D element
	void InitElement(GS::Int32 InIndex3D);

	// Initialize with sync data
	void InitElement(FSyncData* IOSyncdata);

	// Return true if object is valid (i.e. not recently deleted)
	bool IsInvalid() const { return Element3D.IsInvalid(); }

	// Return the element index (in 3D list)
	GS::Int32 GetIndex3D() const { return Index3D; }

	// Return the 3D element
	const ModelerAPI::Element& GetElement3D() const { return Element3D; }

	// Return the 3D type name
	static const utf8_t* GetTypeName(ModelerAPI::Element::Type InType)
	{
		return TAssEnumName< ModelerAPI::Element::Type >::GetName(InType);
	}

	// Return element 3D type name
	const utf8_t* GetTypeName() const { return GetTypeName(Element3D.GetType()); }

	// Initialize element header from 3D element
	bool InitHeader();

	// Initialize element header with element guid
	void InitHeader(const API_Guid& InGuid)
	{
		bFullElementFetched = false;
		Zap(&APIElement.header);
		APIElement.header.guid = InGuid;
		UE_AC_TestGSError(ACAPI_Element_GetHeader(&APIElement.header, 0));
	}

	// Set the Sync Data associated to the current element
	void SetSyncData(FSyncData* InSyncData) { SyncData = InSyncData; }

	// Return the Sync Data associated to the current element
	FSyncData* GetSyncData() const { return SyncData; }

	// Connect to parent or childs
	void HandleDepedencies() const;

	// Return true if element is a morph type body (will need double side)
	bool IsSurface() const;

	// Return the element's header
	const API_Elem_Head& GetHeader() const { return APIElement.header; }

	// Return the complete element
	const API_Element& GetAPIElement();

	// Return the lib part info if this element come from it
	const FLibPartInfo* GetLibPartInfo();

	// Current synchronisation context
	const FSyncContext& SyncContext;

  private:
	void CollectDependantElementsType(API_ElemTypeID TypeID) const;

	// 3D element index
	GS::Int32 Index3D;
	// 3D element
	ModelerAPI::Element Element3D;

	// APIElement contain all values (by opposition to header only)
	bool bFullElementFetched;
	// AC API Element
	API_Element APIElement;

	// Sync data associated to the current element
	FSyncData* SyncData;

	// Lib part info have been fetched
	bool bLibPartInfoFetched;
	// LibPartInfo != nullptr if element come from it
	const FLibPartInfo* LibPartInfo;
};

class FSyncData::FProcessInfo
{
  public:
	FProcessInfo(const FSyncContext& InSyncContext)
		: SyncContext(InSyncContext)
		, ProgessValue(0)
		, ElementID(InSyncContext)
	{
	}

	const FSyncContext& SyncContext;
	int					ProgessValue = 0;
	FElementID			ElementID;
	size_t				Index = 0;
};

END_NAMESPACE_UE_AC
