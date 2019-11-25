// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#ifdef CAD_INTERFACE // #ueent_wip if WITH_CORETECH

#include "CTSession.h"
#include "CADData.h"


#include <vector>

// If attached with a debugger, errors from CT assigned to a Checked_IO_ERROR will break.
#define BREAK_ON_CT_USAGE_ERROR 0

namespace CADLibrary
{
	TWeakPtr<CTSession> CTSession::SharedSession;

	void CTSession::ClearData()
	{
		CheckedCTError Result = CTKIO_UnloadModel();

		// recreate the Main Object
		CT_OBJECT_ID NullParent = 0;
		Result = CT_COMPONENT_IO::Create(MainObjectId, NullParent);
	}

	CheckedCTError CTSession::SaveBrep(const FString& FilePath)
	{
		CT_LIST_IO ObjectList;
		ObjectList.PushBack(MainObjectId);
		return CTKIO_SaveFile(ObjectList, *FilePath, L"Ct");
	}

	CheckedCTError CTSession::TopoFixes(double SewingToleranceFactor)
	{
		return CADLibrary::Repair(MainObjectId, ImportParams.StitchingTechnique, SewingToleranceFactor);
	}

	void CheckedCTError::Validate()
	{
		static bool breakOnError = BREAK_ON_CT_USAGE_ERROR;
		ensure(!breakOnError || bool(*this));
	}

	void CTSession::SetImportParameters(float ChordTolerance, float MaxEdgeLength, float NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique, bool bScaleUVMap)
	{
		ImportParams.ChordTolerance = ChordTolerance;
		ImportParams.MaxEdgeLength = MaxEdgeLength;
		ImportParams.MaxNormalAngle = NormalTolerance;
		ImportParams.StitchingTechnique = StitchingTechnique;
		ImportParams.bScaleUVMap = bScaleUVMap;

		CT_TESS_DATA_TYPE VertexType = CT_TESS_DOUBLE;
		CT_TESS_DATA_TYPE NormalType = CT_TESS_FLOAT;
		CT_TESS_DATA_TYPE UVType = CT_TESS_DOUBLE;
		CTKIO_ChangeTesselationParameters(ImportParams.ChordTolerance, ImportParams.MaxEdgeLength, ImportParams.MaxNormalAngle, CT_FALSE, VertexType, NormalType, UVType);
	}

} // namespace CADLibrary

#endif // CAD_INTERFACE

