// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADModelConverter.h"
#include "CTSession.h"
#include "CoreTechSurfaceHelper.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMeshHelper.h"
#include "ParametricSurfaceTranslator.h"

class FCADModelToCoretechConverterBase : public CADLibrary::FCTSession, public CADLibrary::ICADModelConverter
{
public:

	FCADModelToCoretechConverterBase(const TCHAR* InOwner, const CADLibrary::FImportParameters& InImportParameters)
		: CADLibrary::FCTSession(InOwner, InImportParameters)
	{
	}

	virtual bool Tessellate(const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription) override
	{
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

	virtual bool SaveModel(const TCHAR* InFolderPath, TSharedRef<IDatasmithMeshElement>& MeshElement) override
	{
		FString FilePath = FPaths::Combine(InFolderPath, MeshElement->GetName()) + TEXT(".ct");
		if (SaveBrep(FilePath))
		{
			MeshElement->SetFile(*FilePath);
			return true;
		}
		return false;
	}

	virtual void SetImportParameters(double ChordTolerance, double MaxEdgeLength, double NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique) override
	{
		FCTSession::SetImportParameters(ChordTolerance, MaxEdgeLength, NormalTolerance, StitchingTechnique);
	}

	virtual void SetMetricUnit(double NewMetricUnit) override
	{
		ImportParams.SetMetricUnit(NewMetricUnit);
	}

	virtual double GetScaleFactor() const override
	{
		return ImportParams.GetScaleFactor();
	}

	virtual double GetMetricUnit() const override
	{
		return ImportParams.GetMetricUnit();
	}

	virtual bool IsSessionValid() override
	{
		return FCTSession::IsCoreTechSessionValid();
	}

	void AddSurfaceDataForMesh(const TCHAR* InFilePath, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload) const
	{
		const CADLibrary::FImportParameters& InImportParameters = GetImportParameters();
		ParametricSurfaceUtils::AddSurfaceData(InFilePath, InImportParameters, InMeshParameters, InTessellationOptions, OutMeshPayload);
	}

};

