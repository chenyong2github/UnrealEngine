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
		return CTKIO_SaveFile({ MainObjectId }, *FilePath, TEXT("Ct"));
	}

	bool FCTSession::TopoFixes(double SewingToleranceFactor)
	{
		return CADLibrary::CTKIO_Repair(MainObjectId, ImportParams.GetStitchingTechnique(), SewingToleranceFactor);
	}

	void FCTSession::SetSceneUnit(double InMetricUnit)
	{
		ImportParams.SetMetricUnit(InMetricUnit);
		CTKIO_ChangeUnit(InMetricUnit);
	}

	void FCTSession::SetImportParameters(double ChordTolerance, double MaxEdgeLength, double NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique)
	{
		ImportParams.SetTesselationParameters(ChordTolerance, MaxEdgeLength, NormalTolerance, StitchingTechnique);
		CTKIO_ChangeTesselationParameters(ImportParams.GetChordTolerance(), ImportParams.GetMaxEdgeLength(), ImportParams.GetMaxNormalAngle());
	}

} // namespace CADLibrary

