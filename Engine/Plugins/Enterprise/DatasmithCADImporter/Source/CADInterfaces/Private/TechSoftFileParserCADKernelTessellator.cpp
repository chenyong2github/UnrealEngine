// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftFileParserCADKernelTessellator.h"


#include "CADFileData.h"
#include "CADKernelTools.h"
#include "TechSoftUtils.h"
#include "TechSoftBridge.h"
#include "TUniqueTechSoftObj.h"

#include "CADKernel/Core/Entity.h"
#include "CADKernel/Core/Session.h"
#include "CADKernel/Core/Types.h"

#include "CADKernel/Mesh/Meshers/ParametricMesher.h"
#include "CADKernel/Mesh/Structure/ModelMesh.h"

#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/TopologicalShapeEntity.h"
#include "CADKernel/Topo/Topomaker.h"

namespace CADLibrary
{

FTechSoftFileParserCADKernelTessellator::FTechSoftFileParserCADKernelTessellator(FCADFileData& InCADData, const FString& EnginePluginsPath)
	: FTechSoftFileParser(InCADData, EnginePluginsPath)
	, LastHostIdUsed(1 << 30)
{
}

#ifdef USE_TECHSOFT_SDK

void FTechSoftFileParserCADKernelTessellator::GenerateBodyMeshes()
{
	if (bForceSew || CADFileData.GetImportParameters().GetStitchingTechnique() == EStitchingTechnique::StitchingSew)
	{
		SewAndGenerateBodyMeshes();
	}
	else
	{
		// If no sew is required, FTechSoftFileParser::GenerateBodyMeshes is called to process all the bodies one by one with the override GenerateBodyMesh
		FTechSoftFileParser::GenerateBodyMeshes();
	}
}

void FTechSoftFileParserCADKernelTessellator::SewAndGenerateBodyMeshes()
{
	int32 RepresentationCount = RepresentationItemsCache.Num();
	TMap<FCadId, TArray<A3DRiRepresentationItem*>> OccurenceIdToRepresentations;
	for (TPair<A3DRiRepresentationItem*, int32>& Entry : RepresentationItemsCache)
	{
		A3DRiRepresentationItem* RepresentationItemPtr = Entry.Key;
		FArchiveBody& Body = CADFileData.GetBodyAt(Entry.Value);

		TArray<A3DRiRepresentationItem*>& Representations = OccurenceIdToRepresentations.FindOrAdd(Body.ParentId);
		if (Representations.Max() == 0)
		{
			Representations.Reserve(RepresentationCount);
		}

		Representations.Add(RepresentationItemPtr);
	}

	for (TPair<FCadId, TArray<A3DRiRepresentationItem*>>& Representations : OccurenceIdToRepresentations)
	{
		SewAndMesh(Representations.Value);
	}

}

void FTechSoftFileParserCADKernelTessellator::SewAndMesh(TArray<A3DRiRepresentationItem*>& Representations)
{
	double GeometricTolerance = CADFileData.GetImportParameters().ConvertMMToImportUnit(0.01); // mm

	CADKernel::FSession CADKernelSession(GeometricTolerance);
	CADKernelSession.SetFirstNewHostId(LastHostIdUsed);
	CADKernel::FModel& CADKernelModel = CADKernelSession.GetModel();

	CADKernel::FCADFileReport Report;
	FTechSoftBridge TechSoftBridge(*this, CADKernelSession, Report);

	for (A3DRiRepresentationItem* Representation : Representations)
	{
		int32* BodyIndex = RepresentationItemsCache.Find(Representation);
		if (BodyIndex == nullptr)
		{
			continue;
		}
		FArchiveBody& Body = CADFileData.GetBodyAt(*BodyIndex);

		CADKernel::FBody* CADKernelBody = TechSoftBridge.AddBody(Representation, Body.MetaData, Body.BodyUnit);
	}

	// Sew if needed
	CADKernel::FTopomaker Topomaker(CADKernelSession, GeometricTolerance);
	Topomaker.Sew();
	Topomaker.SplitIntoConnectedShells();
	Topomaker.OrientShells();

	// The Sew + SplitIntoConnectedShells change the bodies: some are deleted some are create
	// but at the end the count of body is always <= than the initial count
	// We need to found the unchanged bodies to link them to their FArchiveBody
	// The new bodies will be linked to FArchiveBody of deleted bodies but the metadata of these FArchiveBody havbe to be cleaned

	int32 BodyCount = CADKernelModel.GetBodies().Num();
	int32 DeletedBodyCount = Representations.Num() - BodyCount;

	TArray<CADKernel::FBody*> NewBodies;
	TArray<CADKernel::FBody*> ExistingBodies;
	NewBodies.Reserve(BodyCount);
	ExistingBodies.Reserve(BodyCount);

	// find new and existing bodies
	for (const TSharedPtr<CADKernel::FBody>& CADKernelBody : CADKernelModel.GetBodies())
	{
		if (!CADKernelBody.IsValid())
		{
			continue;
		}

		const A3DRiRepresentationItem* Representation = TechSoftBridge.GetA3DBody(CADKernelBody.Get());
		if (Representation == nullptr)
		{
			NewBodies.Add(CADKernelBody.Get());
		}
		else
		{
			ExistingBodies.Add(CADKernelBody.Get());
		}
	}

	// find Representation of deleted bodies to find unused FArchiveBody
	//int32 DeletedBodyCount = Representations.Num() - BodyCount;
	TArray<int32> ArchiveBodyIndexOfDeletedRepresentation;
	ArchiveBodyIndexOfDeletedRepresentation.Reserve(DeletedBodyCount);

	for (A3DRiRepresentationItem* Representation : Representations)
	{
		CADKernel::FBody* Body = TechSoftBridge.GetBody(Representation);
		if (Body == nullptr)
		{
			int32* ArchiveBodyIndex = RepresentationItemsCache.Find(Representation);
			if (ArchiveBodyIndex == nullptr)
			{
				continue; // should not append
			}
			ArchiveBodyIndexOfDeletedRepresentation.Add(*ArchiveBodyIndex);
		}
	}

	// Process existing bodies
	for (CADKernel::FBody* Body : ExistingBodies)
	{
		const A3DRiRepresentationItem* Representation = TechSoftBridge.GetA3DBody(Body);

		int32* ArchiveBodyIndex = RepresentationItemsCache.Find(Representation);
		if (ArchiveBodyIndex == nullptr)
		{
			continue; // should not append
		}

		FArchiveBody& ArchiveBody = CADFileData.GetBodyAt(*ArchiveBodyIndex);
		MeshAndGetTessellation(CADKernelSession, ArchiveBody, *Body);
	}

	// Process new bodies
	int32 Index = 0;
	for (CADKernel::FBody* Body : NewBodies)
	{
		int32 ArchiveBodyIndex = ArchiveBodyIndexOfDeletedRepresentation[Index++];
		FArchiveBody& ArchiveBody = CADFileData.GetBodyAt(ArchiveBodyIndex);
		MeshAndGetTessellation(CADKernelSession, ArchiveBody, *Body);
	}

	// Delete unused FArchiveBody
	for (; Index < ArchiveBodyIndexOfDeletedRepresentation.Num(); ++Index)
	{
		int32 ArchiveBodyIndex = ArchiveBodyIndexOfDeletedRepresentation[Index++];
		FArchiveBody& ArchiveBody = CADFileData.GetBodyAt(ArchiveBodyIndex);
		ArchiveBody.MetaData.Empty();
		ArchiveBody.ParentId = 0;
		ArchiveBody.MeshActorName = 0;
	}
}

void FTechSoftFileParserCADKernelTessellator::GenerateBodyMesh(A3DRiRepresentationItem* Representation, FArchiveBody& ArchiveBody)
{
	double GeometricTolerance = CADFileData.GetImportParameters().ConvertMMToImportUnit(0.01); // mm

	CADKernel::FSession CADKernelSession(GeometricTolerance);
	CADKernelSession.SetFirstNewHostId(LastHostIdUsed);
	CADKernel::FModel& CADKernelModel = CADKernelSession.GetModel();

	CADKernel::FCADFileReport Report;
	FTechSoftBridge TechSoftBridge(*this, CADKernelSession, Report);

	CADKernel::FBody* CADKernelBody = TechSoftBridge.AddBody(Representation, ArchiveBody.MetaData, ArchiveBody.BodyUnit);

	if (CADFileData.GetImportParameters().GetStitchingTechnique() == StitchingHeal)
	{
		double Tolerance = CADFileData.GetImportParameters().ConvertMMToImportUnit(0.1); // mm
		CADKernel::FTopomaker Topomaker(CADKernelSession, Tolerance);
		Topomaker.Sew();
		Topomaker.OrientShells();
	}

	MeshAndGetTessellation(CADKernelSession, ArchiveBody, *CADKernelBody);
}

void FTechSoftFileParserCADKernelTessellator::MeshAndGetTessellation(CADKernel::FSession& CADKernelSession, FArchiveBody& ArchiveBody, CADKernel::FBody& CADKernelBody)
{
	FBodyMesh& BodyMesh = CADFileData.AddBodyMesh(ArchiveBody.ObjectId, ArchiveBody);
	ArchiveBody.ColorFaceSet = BodyMesh.ColorSet;
	ArchiveBody.MaterialFaceSet = BodyMesh.MaterialSet;

	// Save Body in CADKernelArchive file for re-tessellation
	if (CADFileData.IsCacheDefined())
	{
		FString BodyFilePath = CADFileData.GetBodyCachePath(ArchiveBody.MeshActorName);
		CADKernelSession.SaveDatabase(*BodyFilePath);
	}

	// Tessellate the body
	TSharedRef<CADKernel::FModelMesh> CADKernelModelMesh = CADKernel::FEntity::MakeShared<CADKernel::FModelMesh>();

	FCADKernelTools::DefineMeshCriteria(CADKernelModelMesh.Get(), CADFileData.GetImportParameters(), CADKernelSession.GetGeometricTolerance());

	CADKernel::FParametricMesher Mesher(*CADKernelModelMesh);
	Mesher.MeshEntity(CADKernelBody);

	FCADKernelTools::GetBodyTessellation(*CADKernelModelMesh, CADKernelBody, BodyMesh);

	ArchiveBody.ColorFaceSet = BodyMesh.ColorSet;
	ArchiveBody.MaterialFaceSet = BodyMesh.MaterialSet;
}

A3DStatus FTechSoftFileParserCADKernelTessellator::AdaptBRepModel()
{
	const A3DUns32 ValidSurfaceCount = 9;
	A3DUns32 AcceptedSurfaces[ValidSurfaceCount] = {
		//kA3DTypeSurfBlend01,
		//kA3DTypeSurfBlend02,
		//kA3DTypeSurfBlend03,
		kA3DTypeSurfNurbs,
		kA3DTypeSurfCone,
		kA3DTypeSurfCylinder,
		kA3DTypeSurfCylindrical,
		//kA3DTypeSurfOffset,
		//kA3DTypeSurfPipe,
		kA3DTypeSurfPlane,
		kA3DTypeSurfRuled,
		kA3DTypeSurfSphere,
		kA3DTypeSurfRevolution,
		//kA3DTypeSurfExtrusion,
		//kA3DTypeSurfFromCurves,
		kA3DTypeSurfTorus,
		//kA3DTypeSurfTransform,
	};

	const A3DUns32 ValidCurveCount = 7;
	A3DUns32 AcceptedCurves[ValidCurveCount] = {
		//kA3DTypeCrvBase,
		//kA3DTypeCrvBlend02Boundary,
		kA3DTypeCrvNurbs,
		kA3DTypeCrvCircle,
		//kA3DTypeCrvComposite,
		//kA3DTypeCrvOnSurf,
		kA3DTypeCrvEllipse,
		//kA3DTypeCrvEquation,
		//kA3DTypeCrvHelix,
		kA3DTypeCrvHyperbola,
		//kA3DTypeCrvIntersection,
		kA3DTypeCrvLine,
		//kA3DTypeCrvOffset,
		kA3DTypeCrvParabola,
		kA3DTypeCrvPolyLine,
		//kA3DTypeCrvTransform,
	};

	TUniqueTSObj<A3DCopyAndAdaptBrepModelData> CopyAndAdaptBrepModelData;
	CopyAndAdaptBrepModelData->m_bUseSameParam = false;                        // If `A3D_TRUE`, surfaces will keep their parametrization when converted to NURBS.       
	CopyAndAdaptBrepModelData->m_dTol = 1e-3;                                  // Tolerance value of resulting B-rep. The value is relative to the scale of the model.
	CopyAndAdaptBrepModelData->m_bDeleteCrossingUV = false;                    // If `A3D_TRUE`, UV curves that cross seams of periodic surfaces are replaced by 3D curves 
	CopyAndAdaptBrepModelData->m_bSplitFaces = true;                           // If `A3D_TRUE`, the faces with a periodic basis surface are split on parametric seams
	CopyAndAdaptBrepModelData->m_bSplitClosedFaces = false;                    // If `A3D_TRUE`, the faces with a closed basis surface are split into faces at the parametric seam and mid-parameter
	CopyAndAdaptBrepModelData->m_bForceComputeUV = true;                       // If `A3D_TRUE`, UV curves are computed from the B-rep data
	CopyAndAdaptBrepModelData->m_bAllowUVCrossingSeams = true;                 // If `A3D_TRUE` and m_bForceComputeUV is set to `A3D_TRUE`, computed UV curves can cross seams.
	CopyAndAdaptBrepModelData->m_bForceCompute3D = false;                      // If `A3D_TRUE`, 3D curves are computed from the B-rep data
	CopyAndAdaptBrepModelData->m_bContinueOnError = true;                      // Continue processing even if an error occurs. Use \ref A3DCopyAndAdaptBrepModelAdvanced to get the error status.
	CopyAndAdaptBrepModelData->m_bClampTolerantUVCurvesInsideUVDomain = false; // If `A3D_FALSE`, UV curves may stray outside the UV domain as long as the 3D edge tolerance is respected. If set to `A3D_TRUE`, the UV curves will be clamped to the UV domain (if the clamp still leaves them within the edge tolerance). */
	CopyAndAdaptBrepModelData->m_bForceDuplicateGeometries = false;            // If `A3D_TRUE`, break the sharing of surfaces and curves into topologies.*/

	CopyAndAdaptBrepModelData->m_uiAcceptableSurfacesSize = ValidSurfaceCount;
	CopyAndAdaptBrepModelData->m_puiAcceptableSurfaces = &AcceptedSurfaces[0];
	CopyAndAdaptBrepModelData->m_uiAcceptableCurvesSize = ValidCurveCount;
	CopyAndAdaptBrepModelData->m_puiAcceptableCurves = &AcceptedCurves[0];

	int32 ErrorCount = 0;
	A3DCopyAndAdaptBrepModelErrorData* Errors = nullptr;
	A3DStatus Ret = TechSoftInterface::AdaptBRepInModelFile(ModelFile.Get(), *CopyAndAdaptBrepModelData, ErrorCount, &Errors);
	if ((Ret == A3D_SUCCESS || Ret == A3D_TOOLS_CONTINUE_ON_ERROR) && ErrorCount > 0)
	{
		// Add warning about error during the adaptation
		CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s had %d error during BRep adaptation step."), *CADFileData.GetCADFileDescription().GetFileName(), ErrorCount));
	}
	else if (Ret != A3D_SUCCESS)
	{
		CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s failed during BRep adaptation step."), *CADFileData.GetCADFileDescription().GetFileName(), ErrorCount));
		return A3D_ERROR;
	}
	return A3D_SUCCESS;
}
#endif  

} // ns CADLibrary
