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

	FCADModelToCADKernelConverterBase()
		// Unit is set to cm, 0.01, because Wire's unit is cm.
		: CADKernelSession(0.00001 / 0.01)
		, GeometricTolerance(0.00001 / 0.01)
		, SquareTolerance(GeometricTolerance* GeometricTolerance)
	{
		ImportParameters.MetricUnit = 0.01;
		ImportParameters.ScaleFactor = 1;
		ImportParameters.bEnableKernelIOTessellation = false;
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

	virtual void SetImportParameters(float ChordTolerance, float MaxEdgeLength, float NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique, bool bScaleUVMap) override
	{
		ImportParameters.ChordTolerance = ChordTolerance;
		ImportParameters.MaxEdgeLength = MaxEdgeLength;
		ImportParameters.MaxNormalAngle = NormalTolerance;
		ImportParameters.StitchingTechnique = StitchingTechnique;
		ImportParameters.bScaleUVMap = bScaleUVMap;
		ImportParameters.bEnableKernelIOTessellation = false;
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

