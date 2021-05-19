// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef USE_OPENNURBS
#include "CoreMinimal.h"

#include "CTSession.h"

class ON_Brep;
class ON_NurbsSurface;
class ON_BrepFace;
class ON_BoundingBox;
class ON_3dVector;

class BRepToKernelIOBodyTranslator
{
public:
	BRepToKernelIOBodyTranslator(ON_Brep& InBRep)
		: BRep(InBRep)
	{}

	uint64 CreateBody(const ON_3dVector& ON_Offset);
private:
	uint64 CreateCTSurface(ON_NurbsSurface& Surface);
	void CreateCTFace(const ON_BrepFace& Face, TArray<uint64>& dest);
	void CreateCTFace_internal(const ON_BrepFace& Face, TArray<uint64>& dest, ON_BoundingBox& outerBBox, ON_NurbsSurface& Surface, bool ignoreInner);

private:
	ON_Brep& BRep;

	// BRep.m_T is an array that store all Trims, so we can also use an array to make the map between Trim index to Coedge Id
	TArray<uint64> BrepTrimToCoedge;
};



class FRhinoCoretechWrapper : public CADLibrary::FCTSession
{
public:
	/**
	 * Make sure CT is initialized, and a main object is ready.
	 * Handle input file unit and an output unit
	 * @param InOwner
	 * @param FileMetricUnit number of meters per file unit.
	 * @param ScaleFactor scale factor to apply to the mesh to be in UE4 unit (cm).
	 * eg. For a file in inches, arg should be 0.0254
	 */
	FRhinoCoretechWrapper(const TCHAR* InOwner)
		: FCTSession(InOwner)
	{
	}

	/**
	 * Set BRep to tessellate, offsetting it prior to tessellation(used to set mesh pivot at the center of the surface bounding box)
	 *
	 * @param  Brep	a brep to tessellate
	 * @param  Offset translate brep by this value before tessellating 
	 */
	bool AddBRep(ON_Brep& Brep, const ON_3dVector& Offset);
	
	static TSharedPtr<FRhinoCoretechWrapper> GetSharedSession();

	bool Tessellate(FMeshDescription& Mesh, CADLibrary::FMeshParameters& MeshParameters);

protected:
	static TWeakPtr<FRhinoCoretechWrapper> SharedSession;
};

#endif // USE_OPENNURBS