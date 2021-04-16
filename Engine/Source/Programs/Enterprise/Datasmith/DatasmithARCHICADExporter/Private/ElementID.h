// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

#include "SyncContext.h"
#include "SyncData.h"
#include "Utils/TAssValueName.h"

#include "ModelElement.hpp"

#include <stdexcept>

BEGIN_NAMESPACE_UE_AC

// Class that contain element id and related
class FElementID
{
  public:
	// Contructor
	FElementID(const FSyncContext& InSyncContext)
		: SyncContext(InSyncContext)
		, Index3D(0)
		, SyncData(nullptr)
	{
	}

	// Initialize with 3D element
	void InitElement(GS::Int32 InIndex3d)
	{
		Index3D = InIndex3d;
		SyncContext.GetModel().GetElement(Index3D, &Element3D);
		ElementHeader.guid = APINULLGuid;
	}

	// Initialize with 3D element
	void InitElement(FSyncData* IOSyncdata)
	{
		UE_AC_TestPtr(IOSyncdata);
		SyncData = IOSyncdata;
		Index3D = IOSyncdata->GetIndex3D();
		if (Index3D > 0)
		{
			SyncContext.GetModel().GetElement(Index3D, &Element3D);
		}
	}

	// Return true if object is valid (i.e. not recently deleted)
	bool IsInvalid() const { return Element3D.IsInvalid(); }

	static const utf8_t* GetTypeName(ModelerAPI::Element::Type InType)
	{
		return TAssEnumName< ModelerAPI::Element::Type >::GetName(InType);
	}

	const utf8_t* GetTypeName() const { return GetTypeName(Element3D.GetType()); }

	// Initialize element header from 3D element
	bool InitHeader()
	{
		if (IsInvalid())
		{
			throw std::runtime_error(
				Utf8StringFormat("FElementID::InitHeader - Invalid element for index=%d\n", Index3D).c_str());
		}
		Zap(&ElementHeader);
		ElementHeader.guid = GSGuid2APIGuid(Element3D.GetElemGuid());
		GSErrCode GSErr = ACAPI_Element_GetHeader(&ElementHeader, 0);
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

	// Initialize element header with element guid
	void InitHeader(const API_Guid& InGuid)
	{
		Zap(&ElementHeader);
		ElementHeader.guid = InGuid;
		UE_AC_TestGSError(ACAPI_Element_GetHeader(&ElementHeader, 0));
	}

	void HandleDepedencies() const;

	// Return true if element is a morph type body (will need double side)
	bool IsSurface() const;

	const FSyncContext& SyncContext; // Current synchronisation context
	FSyncData*			SyncData;
	GS::Int32			Index3D; // 3D element index
	ModelerAPI::Element Element3D; // 3D element
	API_Elem_Head		ElementHeader; // AC element header
  private:
	void CollectDependantElementsType(API_ElemTypeID TypeID) const;
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
