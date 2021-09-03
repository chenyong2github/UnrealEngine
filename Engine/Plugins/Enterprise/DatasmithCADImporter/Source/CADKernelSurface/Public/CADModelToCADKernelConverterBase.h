// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADKernelSurfaceExtension.h"
#include "CADKernelTools.h"
#include "CADModelConverter.h"
#include "CADOptions.h"
#include "IDatasmithSceneElements.h"

#include "CADKernel/Core/Session.h"
#include "CADKernel/Topo/Model.h"

struct FDatasmithMeshElementPayload;
struct FDatasmithTessellationOptions;

class FCADModelToCADKernelConverterBase : public CADLibrary::ICADModelConverter
{
public:

	FCADModelToCADKernelConverterBase(CADLibrary::FImportParameters InImportParameters)
		: CADKernelSession(0.00001 / InImportParameters.MetricUnit)
		, ImportParameters(InImportParameters)
		, GeometricTolerance(0.00001 / InImportParameters.MetricUnit)
		, SquareTolerance(GeometricTolerance* GeometricTolerance)
	{
		ImportParameters.bDisableCADKernelTessellation = false;
	}

	virtual void InitializeProcess(double InMetricUnit) override
	{
		CADKernelSession.Clear();
	}

	virtual bool RepairTopology() override
	{
		// Apply stitching if applicable
		return true;
	}

	virtual bool SaveBRep(const TCHAR* InFolderPath, TSharedRef<IDatasmithMeshElement>& MeshElement) override
	{
		FString FilePath = FPaths::Combine(InFolderPath, MeshElement->GetName()) + TEXT(".ugeom");
		CADKernelSession.SaveDatabase(*FilePath);
		MeshElement->SetFile(*FilePath);
		return true;
	}

	virtual bool Tessellate(const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription) override
	{
		TSharedRef<CADKernel::FTopologicalEntity> CADKernelEntity = CADKernelSession.GetModel();
		return CADLibrary::FCADKernelTools::Tessellate(CADKernelEntity, ImportParameters, InMeshParameters, OutMeshDescription);
	}

	virtual void SetImportParameters(double ChordTolerance, double MaxEdgeLength, double NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique, bool bScaleUVMap) override
	{
		ImportParameters.ChordTolerance = ChordTolerance;
		ImportParameters.MaxEdgeLength = MaxEdgeLength;
		ImportParameters.MaxNormalAngle = NormalTolerance;
		ImportParameters.StitchingTechnique = StitchingTechnique;
		ImportParameters.bScaleUVMap = bScaleUVMap;
	}

	virtual void SetMetricUnit(double NewMetricUnit) override
	{
		ImportParameters.SetMetricUnit(NewMetricUnit);
	}

	virtual double GetScaleFactor() const override
	{
		return ImportParameters.ScaleFactor;
	}

	virtual double GetMetricUnit() const override
	{
		return ImportParameters.MetricUnit;
	}

	virtual bool IsSessionValid() override
	{
		return true;
	}

	virtual void AddSurfaceDataForMesh(const TCHAR* InFilePath, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload) const override
	{
		CADKernelSurface::AddSurfaceDataForMesh(InFilePath, ImportParameters, InMeshParameters, InTessellationOptions, OutMeshPayload);
	}

protected:

	CADKernel::FSession CADKernelSession;

	CADLibrary::FImportParameters ImportParameters;
	double GeometricTolerance;
	double SquareTolerance;

};

