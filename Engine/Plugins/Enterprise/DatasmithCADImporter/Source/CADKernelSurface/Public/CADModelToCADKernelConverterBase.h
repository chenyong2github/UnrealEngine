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
#include "CADKernel/Topo/Topomaker.h"

struct FDatasmithMeshElementPayload;
struct FDatasmithTessellationOptions;

class FCADModelToCADKernelConverterBase : public CADLibrary::ICADModelConverter
{
public:

	FCADModelToCADKernelConverterBase(CADLibrary::FImportParameters InImportParameters)
		: CADKernelSession(0.01)
		, ImportParameters(InImportParameters)
		, GeometricTolerance(0.01)
		, SquareTolerance(GeometricTolerance* GeometricTolerance)
	{
	}

	virtual void InitializeProcess() override
	{
		CADKernelSession.Clear();
	}

	virtual bool RepairTopology() override
	{
		// Apply stitching if applicable
		if(ImportParameters.GetStitchingTechnique() != CADLibrary::StitchingNone)
		{
			// the joining tolerance is set to 0.1 mm until the user can specify it
			const double JoiningTolerance = 0.1;
			CADKernel::FTopomaker Topomaker(CADKernelSession, JoiningTolerance);
			Topomaker.Sew();
			Topomaker.OrientShells();
		}

		return true;
	}

	virtual bool SaveModel(const TCHAR* InFolderPath, TSharedRef<IDatasmithMeshElement>& MeshElement) override
	{
		FString FilePath = FPaths::Combine(InFolderPath, MeshElement->GetName()) + TEXT(".ugeom");
		CADKernelSession.SaveDatabase(*FilePath);
		MeshElement->SetFile(*FilePath);
		return true;
	}

	virtual bool Tessellate(const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription) override
	{
		CADKernel::FModel& Model = CADKernelSession.GetModel();
		return CADLibrary::FCADKernelTools::Tessellate(Model, ImportParameters, InMeshParameters, OutMeshDescription);
	}

	virtual void SetImportParameters(double ChordTolerance, double MaxEdgeLength, double NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique) override
	{
		ImportParameters.SetTesselationParameters(ChordTolerance, MaxEdgeLength, NormalTolerance, StitchingTechnique);
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

