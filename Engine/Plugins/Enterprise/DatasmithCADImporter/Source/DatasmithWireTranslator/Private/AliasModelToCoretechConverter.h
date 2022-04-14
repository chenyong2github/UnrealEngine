// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AliasBRepConverter.h"
#include "CADModelToCoretechConverterBase.h"
#include "CTSession.h"

class AlDagNode;
class AlShell;
class AlSurface;
class AlTrimBoundary;
class AlTrimCurve;
class AlTrimRegion;
class AlCurve;

namespace UE_DATASMITHWIRETRANSLATOR_NAMESPACE
{

typedef double AlMatrix4x4[4][4];

class FAliasModelToCoretechConverter : public FCADModelToCoretechConverterBase, public IAliasBRepConverter
{
public:

	FAliasModelToCoretechConverter(const TCHAR* InOwner, CADLibrary::FImportParameters InImportParameters)
		: FCADModelToCoretechConverterBase(InOwner, InImportParameters)
	{
	}

	virtual bool AddBRep(AlDagNode& DagNode, const FColor& Color, EAliasObjectReference ObjectReference) override;

protected:

	/**
	 * Create a CT coedge (represent the use of an edge by a face).
	 * @param TrimCurve: A curve in parametric surface space, part of a trim boundary.
	 */
	uint64 AddTrimCurve(const AlTrimCurve& TrimCurve);
	uint64 AddTrimBoundary(const AlTrimBoundary& TrimBoundary);

	uint64 AddTrimRegion(const AlTrimRegion& InTrimRegion, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation);
	void AddFace(const AlSurface& InSurface, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceLis);
	void AddShell(const AlShell& InShell, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceLis);

protected:
	TMap<void*, uint64>  AlEdge2CTEdge;
};

}