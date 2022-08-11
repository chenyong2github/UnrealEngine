// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADOptions.h"

namespace CADLibrary
{

int32 GMaxImportThreads = 0;
FAutoConsoleVariableRef GCADTranslatorMaxImportThreads(
	TEXT("ds.CADTranslator.MaxImportThreads"),
	GMaxImportThreads,
	TEXT("\
CAD file parallel processing\n\
Default is MaxImportThreads = 0\n\
0: multi-processing, n : multi-processing limited to n process. EnableCADCache is mandatory.\n\
1: -if EnableCADCache is true, the scene is read in a sequential mode with cache i.e. cache is used for sub-files already read,\n\
   -if EnableCADCache is false, the scene is read all at once\n"),
	ECVF_Default);

bool FImportParameters::bGDisableCADKernelTessellation = true;
FAutoConsoleVariableRef GCADTranslatorDisableCADKernelTessellation(
	TEXT("ds.CADTranslator.DisableCADKernelTessellation"),
	FImportParameters::bGDisableCADKernelTessellation,
	TEXT("Disable to use CAD import library tessellator.\n"),
	ECVF_Default);

bool FImportParameters::bGEnableCADCache = true;
FAutoConsoleVariableRef GCADTranslatorEnableCADCache(
	TEXT("ds.CADTranslator.EnableCADCache"),
	FImportParameters::bGEnableCADCache,
	TEXT("\
Enable/disable temporary CAD processing file cache. These file will be use in a next import to avoid CAD file processing.\n\
If MaxImportThreads != 1, EnableCADCache value is ignored\n\
Default is enable\n"),
	ECVF_Default);

bool FImportParameters::bGOverwriteCache = false;
FAutoConsoleVariableRef GCADTranslatorOverwriteCache(
	TEXT("ds.CADTranslator.OverwriteCache"),
	FImportParameters::bGOverwriteCache,
	TEXT("Overwrite any existing cache associated with the file being imported.\n"),
	ECVF_Default);

bool FImportParameters::bGEnableTimeControl = true;
FAutoConsoleVariableRef GCADTranslatorEnableTimeControl(
	TEXT("ds.CADTranslator.EnableTimeControl"),
	FImportParameters::bGEnableTimeControl,
	TEXT("Enable the timer that kill the worker if the import time is unusually long. With this time control, the load of the corrupted file is canceled but the rest of the scene is imported.\n"),
	ECVF_Default);

bool FImportParameters::bGPreferJtFileEmbeddedTessellation = false;
FAutoConsoleVariableRef GCADTranslatorJtFileEmbeddedTessellation(
	TEXT("ds.CADTranslator.PreferJtFileEmbeddedTessellation"),
	FImportParameters::bGPreferJtFileEmbeddedTessellation,
	TEXT("If both (tessellation and BRep) exist in the file, import embedded tessellation instead of meshing BRep.\n"),
	ECVF_Default);

float FImportParameters::GStitchingTolerance = 0.001f;
FAutoConsoleVariableRef GCADTranslatorStitchingTolerance(
	TEXT("ds.CADTranslator.StitchingTolerance"),
	FImportParameters::GStitchingTolerance,
	TEXT("Welding threshold for Heal/Sew stitching methods in cm\n\
Default value of StitchingTolerance is 0.001 cm\n"),
ECVF_Default);

float FImportParameters::GUnitScale = 1.f;
FAutoConsoleVariableRef GCADTranslatorUnitScale(
	TEXT("ds.CADTranslator.UnitScale"),
	FImportParameters::GUnitScale,
	TEXT("Scale factor to change the unit of the DMU (Only applies to TechSoft import.)\n\
Default value of UnitScale is 1 i.e. unit = cm\n"),
ECVF_Default);

bool FImportParameters::bGSewMeshIfNeeded = true;
FAutoConsoleVariableRef GCADTranslatorSewMeshIfNeeded(
	TEXT("ds.CADTranslator.SewMeshIfNeeded"),
	FImportParameters::bGSewMeshIfNeeded,
	TEXT("Prefrom a welding of the mesh to try to stitch mesh cracks\n\
This welding is performed respecting the ds.CADTranslator.StitchingTolerance\n\
Default value is true\n"),
ECVF_Default);

float FImportParameters::GMeshingParameterFactor = 1;
FAutoConsoleVariableRef GCADTranslatorMeshingParameterFactor(
	TEXT("ds.CADTranslator.MeshingParameterFactor"),
	FImportParameters::GMeshingParameterFactor,
	TEXT("Factor to allow to use smaller value than the defined minimal value of metric meshing parameters (i.e. Chord error > 0.005 cm, Max Edge Length > 1. cm) \n\
The used value of the meshing parameter is value * MeshingParameterFactor \n\
Default value is 1.\n"),
ECVF_Default);


static bool bGAliasSewByColor = false;
FAutoConsoleVariableRef GAliasSewByColor(
	TEXT("ds.CADTranslator.Alias.SewByColor"),
	bGAliasSewByColor,
	TEXT("Enable Sew action merges BReps according to their material i.e. only BReps associated with same material can be merged together.\
Default is disable\n"),
ECVF_Default);

uint32 GetTypeHash(const FImportParameters& ImportParameters)
{
	uint32 ParametersHash = ::GetTypeHash(ImportParameters.bGDisableCADKernelTessellation);
	ParametersHash = HashCombine(ParametersHash, ::GetTypeHash(ImportParameters.ChordTolerance));
	ParametersHash = HashCombine(ParametersHash, ::GetTypeHash(ImportParameters.MaxEdgeLength));
	ParametersHash = HashCombine(ParametersHash, ::GetTypeHash(ImportParameters.MaxNormalAngle));
	ParametersHash = HashCombine(ParametersHash, ::GetTypeHash(ImportParameters.StitchingTechnique));
	return ParametersHash;
}

}
