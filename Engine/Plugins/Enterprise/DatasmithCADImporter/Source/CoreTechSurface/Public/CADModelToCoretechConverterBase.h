// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADModelConverter.h"
#include "CTSession.h"
#include "CoreTechSurfaceHelper.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMeshHelper.h"

class FCADModelToCoretechConverterBase : public CADLibrary::FCTSession, public CADLibrary::ICADModelConverter
{
public:

	FCADModelToCoretechConverterBase(const TCHAR* InOwner)
		: CADLibrary::FCTSession(InOwner)
	{
		// Unit for CoreTech session is set to cm, 0.01, because Wire's unit is cm. Consequently, Scale factor is set to 1.
		ImportParams.MetricUnit = 0.01;
		ImportParams.ScaleFactor = 1;
		ImportParams.bEnableKernelIOTessellation = true;
	}

	virtual bool Tessellate(const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription) override
	{
		// Apply stitching if applicable
		TopoFixes(1.);

		// Perform tessellation
		DatasmithMeshHelper::PrepareAttributeForStaticMesh(OutMeshDescription);

		return CoreTechSurface::Tessellate(MainObjectId, ImportParams, InMeshParameters, OutMeshDescription);
	}

	virtual bool RepairTopology() override
	{
		// Apply stitching if applicable
		return TopoFixes(1.);
	}

	virtual void InitializeProcess(double NewSceneunit) override
	{
		ClearData();
		SetSceneUnit(NewSceneunit);
	}

	virtual bool SaveBRep(const TCHAR* InFolderPath, TSharedRef<IDatasmithMeshElement>& MeshElement) override
	{
		FString FilePath = FPaths::Combine(InFolderPath, MeshElement->GetName()) + TEXT(".ct");
		if (SaveBrep(FilePath))
		{
			MeshElement->SetFile(*FilePath);
			return true;
		}
		return false;
	}

	virtual void SetImportParameters(float ChordTolerance, float MaxEdgeLength, float NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique, bool bScaleUVMap) override
	{
		FCTSession::SetImportParameters(ChordTolerance, MaxEdgeLength, NormalTolerance, StitchingTechnique, bScaleUVMap);
		ImportParams.bEnableKernelIOTessellation = true;
	}

	virtual bool IsSessionValid() override
	{
		return FCTSession::IsSessionValid();
	}

	void AddSurfaceDataForMesh(const TCHAR* InFilePath, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload) const
	{
		const CADLibrary::FImportParameters& InImportParameters = GetImportParameters();
		CoreTechSurface::AddSurfaceDataForMesh(InFilePath, InImportParameters, InMeshParameters, InTessellationOptions, OutMeshPayload);
	}

};

