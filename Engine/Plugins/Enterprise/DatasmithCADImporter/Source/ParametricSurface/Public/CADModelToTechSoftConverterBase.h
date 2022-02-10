// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADModelConverter.h"
#include "CADOptions.h"
#include "IDatasmithSceneElements.h"

#include "TechSoftInterface.h"

struct FDatasmithMeshElementPayload;
struct FDatasmithTessellationOptions;

typedef void A3DAsmPartDefinition;
typedef void A3DAsmProductOccurrence;
typedef void A3DEntity;
typedef void A3DRiRepresentationItem;
typedef void A3DTopoBrepData;


class PARAMETRICSURFACE_API FCADModelToTechSoftConverterBase : public CADLibrary::ICADModelConverter
{
public:

	FCADModelToTechSoftConverterBase(CADLibrary::FImportParameters InImportParameters)
		: ImportParameters(InImportParameters)
	{
	}

	virtual void InitializeProcess(double InMetricUnit) override;

	virtual bool RepairTopology() override;

	virtual bool SaveModel(const TCHAR* InFolderPath, TSharedRef<IDatasmithMeshElement>& MeshElement) override;

	virtual bool Tessellate(const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription) override;

	virtual void SetImportParameters(double ChordTolerance, double MaxEdgeLength, double NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique) override
	{
		ImportParameters.SetTesselationParameters(ChordTolerance, MaxEdgeLength, NormalTolerance, StitchingTechnique);
	}

	virtual void SetMetricUnit(double NewMetricUnit) override
	{
		ImportParameters.SetMetricUnit(NewMetricUnit);
	}

	virtual double GetScaleFactor() const override
	{
		return ImportParameters.GetScaleFactor();
	}

	virtual double GetMetricUnit() const override
	{
		return ImportParameters.GetMetricUnit();
	}

	virtual bool IsSessionValid() override
	{
		return true;
	}

	virtual void AddSurfaceDataForMesh(const TCHAR* InFilePath, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload) const override;

protected:
	CADLibrary::FImportParameters ImportParameters;
	TArray<A3DRiRepresentationItem*> RiRepresentationItems;
	CADLibrary::FUniqueTechSoftModelFile ModelFile;
};
