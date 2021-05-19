// Copyright Epic Games, Inc. All Rights Reserved.

#include "CTSession.h"
#include "CADData.h"

namespace CADLibrary
{
	TWeakPtr<FCTSession> FCTSession::SharedSession;

	void FCTSession::ClearData()
	{
		CTKIO_UnloadModel();

		// recreate the Main Object
		CTKIO_CreateModel(MainObjectId);
	}

	bool FCTSession::SaveBrep(const FString& FilePath)
	{
		return CTKIO_SaveFile({ MainObjectId }, *FilePath, L"Ct");
	}

	bool FCTSession::TopoFixes(double SewingToleranceFactor)
	{
		return CADLibrary::CTKIO_Repair(MainObjectId, ImportParams.StitchingTechnique, SewingToleranceFactor);
	}

	void FCTSession::SetSceneUnit(double InMetricUnit)
	{
		ImportParams.MetricUnit = InMetricUnit;
		CTKIO_ChangeUnit(InMetricUnit);
	}


	void FCTSession::SetImportParameters(float ChordTolerance, float MaxEdgeLength, float NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique, bool bScaleUVMap)
	{
		ImportParams.ChordTolerance = ChordTolerance;
		ImportParams.MaxEdgeLength = MaxEdgeLength;
		ImportParams.MaxNormalAngle = NormalTolerance;
		ImportParams.StitchingTechnique = StitchingTechnique;
		ImportParams.bScaleUVMap = bScaleUVMap;

		CTKIO_ChangeTesselationParameters(ImportParams.ChordTolerance, ImportParams.MaxEdgeLength, ImportParams.MaxNormalAngle);
	}

} // namespace CADLibrary

