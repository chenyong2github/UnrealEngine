// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshAttributeSet.h"
#include "IndexTypes.h"
#include "Async/Async.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


void FDynamicMeshAttributeSet::Copy(const FDynamicMeshAttributeSet& Copy)
{
	SetNumUVLayers(Copy.NumUVLayers());
	for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
	{
		UVLayers[UVIdx].Copy(Copy.UVLayers[UVIdx]);
	}
	SetNumNormalLayers(Copy.NumNormalLayers());
	for (int NormalLayerIndex = 0; NormalLayerIndex < NumNormalLayers(); NormalLayerIndex++)
	{
		NormalLayers[NormalLayerIndex].Copy(Copy.NormalLayers[NormalLayerIndex]);
	}
	if (Copy.ColorLayer)
	{
		EnablePrimaryColors();
		ColorLayer->Copy(*(Copy.ColorLayer));
	}
	else
	{
		DisablePrimaryColors();
	}
	if (Copy.MaterialIDAttrib)
	{
		EnableMaterialID();
		MaterialIDAttrib->Copy(*(Copy.MaterialIDAttrib));
	}
	else
	{
		DisableMaterialID();
	}

	SetNumPolygroupLayers(Copy.NumPolygroupLayers());
	for (int GroupIdx = 0; GroupIdx < NumPolygroupLayers(); ++GroupIdx)
	{
		PolygroupLayers[GroupIdx].Copy(Copy.PolygroupLayers[GroupIdx]);
	}

	GenericAttributes.Reset();
	ResetRegisteredAttributes();
	for (const TPair<FName, TUniquePtr<FDynamicMeshAttributeBase>>& AttribPair : Copy.GenericAttributes)
	{
		AttachAttribute(AttribPair.Key, AttribPair.Value->MakeCopy(ParentMesh));
	}

	// parent mesh is *not* copied!
}


bool FDynamicMeshAttributeSet::IsCompact()
{
	for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
	{
		if (!UVLayers[UVIdx].IsCompact())
		{
			return false;
		}
	}
	for (int NormalLayerIndex = 0; NormalLayerIndex < NumNormalLayers(); NormalLayerIndex++)
	{
		if (!NormalLayers[NormalLayerIndex].IsCompact())
		{
			return false;
		}
	}
	if (HasPrimaryColors())
	{
		if (!ColorLayer->IsCompact())
		{
			return false;
		}
	}
	
	// material ID and generic per-element attributes currently cannot be non-compact
	return true;
}



void FDynamicMeshAttributeSet::CompactCopy(const FCompactMaps& CompactMaps, const FDynamicMeshAttributeSet& Copy)
{
	SetNumUVLayers(Copy.NumUVLayers());
	for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
	{
		UVLayers[UVIdx].CompactCopy(CompactMaps, Copy.UVLayers[UVIdx]);
	}
	SetNumNormalLayers(Copy.NumNormalLayers());
	for (int NormalLayerIndex = 0; NormalLayerIndex < NumNormalLayers(); NormalLayerIndex++)
	{
		NormalLayers[NormalLayerIndex].CompactCopy(CompactMaps, Copy.NormalLayers[NormalLayerIndex]);
	}
	if (Copy.ColorLayer)
	{
		EnablePrimaryColors();
		ColorLayer->CompactCopy(CompactMaps, *(Copy.ColorLayer));
	}
	else
	{
		DisablePrimaryColors();
	}

	if (Copy.MaterialIDAttrib)
	{
		EnableMaterialID();
		MaterialIDAttrib->CompactCopy(CompactMaps, *(Copy.MaterialIDAttrib));
	}
	else
	{
		DisableMaterialID();
	}

	SetNumPolygroupLayers(Copy.NumPolygroupLayers());
	for (int GroupIdx = 0; GroupIdx < NumPolygroupLayers(); ++GroupIdx)
	{
		PolygroupLayers[GroupIdx].CompactCopy(CompactMaps, Copy.PolygroupLayers[GroupIdx]);
	}

	GenericAttributes.Reset();
	ResetRegisteredAttributes();
	for (const TPair<FName, TUniquePtr<FDynamicMeshAttributeBase>>& AttribPair : Copy.GenericAttributes)
	{
		AttachAttribute(AttribPair.Key, AttribPair.Value->MakeCompactCopy(CompactMaps, ParentMesh));
	}

	// parent mesh is *not* copied!
}




void FDynamicMeshAttributeSet::CompactInPlace(const FCompactMaps& CompactMaps)
{
	for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
	{
		UVLayers[UVIdx].CompactInPlace(CompactMaps);
	}
	for (int NormalLayerIndex = 0; NormalLayerIndex < NumNormalLayers(); NormalLayerIndex++)
	{
		NormalLayers[NormalLayerIndex].CompactInPlace(CompactMaps);
	}
	if (ColorLayer.IsValid())
	{
		ColorLayer->CompactInPlace(CompactMaps);
	}
	if (MaterialIDAttrib.IsValid())
	{
		MaterialIDAttrib->CompactInPlace(CompactMaps);
	}

	for (int GroupIdx = 0; GroupIdx < NumPolygroupLayers(); ++GroupIdx)
	{
		PolygroupLayers[GroupIdx].CompactInPlace(CompactMaps);
	}

	for (const TPair<FName, TUniquePtr<FDynamicMeshAttributeBase>>& AttribPair : GenericAttributes)
	{
		AttribPair.Value->CompactInPlace(CompactMaps);
	}
}



void FDynamicMeshAttributeSet::SplitAllBowties(bool bParallel)
{
	int32 UVLayerCount = NumUVLayers();
	int32 NormalLayerCount = NumNormalLayers();
	
	TArray<TFuture<void>> Pending;
	auto ASyncOrRunSplit = [&Pending, bParallel](auto Overlay)->void
	{
		if (bParallel)
		{	
			auto AsyncTask = Async(EAsyncExecution::ThreadPool, [Overlay]() 
			{
				Overlay->SplitBowties();
			});
			Pending.Add(MoveTemp(AsyncTask));
		}
		else
		{
			Overlay->SplitBowties();
		}
	};

	for (int32 i = 0; i < UVLayerCount; ++i)
	{	
		FDynamicMeshUVOverlay* UVLayer = GetUVLayer(i);
		ASyncOrRunSplit(UVLayer);
	}
	for (int32 i = 0; i < NormalLayerCount; ++i)
	{
		FDynamicMeshNormalOverlay* NormalLayer = GetNormalLayer(i);
		ASyncOrRunSplit(NormalLayer);
	}
	if (HasPrimaryColors())
	{
		FDynamicMeshColorOverlay* Colors = PrimaryColors();
		ASyncOrRunSplit(Colors);
	}

	// this array will be empty if bParallel == false
	for (TFuture<void>& Future : Pending)
	{
		Future.Wait();
	}
}



void FDynamicMeshAttributeSet::EnableMatchingAttributes(const FDynamicMeshAttributeSet& ToMatch)
{
	SetNumUVLayers(ToMatch.NumUVLayers());
	for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
	{
		UVLayers[UVIdx].ClearElements();
	}
	SetNumNormalLayers(ToMatch.NumNormalLayers());
	for (int NormalLayerIndex = 0; NormalLayerIndex < NumNormalLayers(); NormalLayerIndex++)
	{
		NormalLayers[NormalLayerIndex].ClearElements();
	}
	if (ToMatch.ColorLayer)
	{
		EnablePrimaryColors();
	}
	else
	{
		DisablePrimaryColors();
	}

	if (ToMatch.MaterialIDAttrib)
	{
		EnableMaterialID();
	}
	else
	{
		DisableMaterialID();
	}

	SetNumPolygroupLayers(ToMatch.NumPolygroupLayers());
	for (int GroupIdx = 0; GroupIdx < NumPolygroupLayers(); ++GroupIdx)
	{
		PolygroupLayers[GroupIdx].Initialize((int32)0);
	}

	GenericAttributes.Reset();
	ResetRegisteredAttributes();
	for (const TPair<FName, TUniquePtr<FDynamicMeshAttributeBase>>& AttribPair : ToMatch.GenericAttributes)
	{
		AttachAttribute(AttribPair.Key, AttribPair.Value->MakeNew(ParentMesh));
	}
}



void FDynamicMeshAttributeSet::Reparent(FDynamicMesh3* NewParent)
{
	ParentMesh = NewParent;

	for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
	{
		UVLayers[UVIdx].Reparent(NewParent);
	}
	for (int NormalLayerIndex = 0; NormalLayerIndex < NumNormalLayers(); NormalLayerIndex++)
	{
		NormalLayers[NormalLayerIndex].Reparent(NewParent);
	}
	if (ColorLayer)
	{
		ColorLayer->Reparent(NewParent);
	}

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->Reparent(NewParent);
	}

	for (int GroupIdx = 0; GroupIdx < NumPolygroupLayers(); ++GroupIdx)
	{
		PolygroupLayers[GroupIdx].Reparent(NewParent);
	}

	for (const TPair<FName, TUniquePtr<FDynamicMeshAttributeBase>>& AttribPair : GenericAttributes)
	{
		AttribPair.Value->Reparent(NewParent);
	}
}



void FDynamicMeshAttributeSet::SetNumUVLayers(int Num)
{
	if (UVLayers.Num() == Num)
	{
		return;
	}
	if (Num >= UVLayers.Num())
	{
		for (int i = (int)UVLayers.Num(); i < Num; ++i)
		{
			UVLayers.Add(new FDynamicMeshUVOverlay(ParentMesh));
		}
	}
	else
	{
		UVLayers.RemoveAt(Num, UVLayers.Num() - Num);
	}
	check(UVLayers.Num() == Num);
}



void FDynamicMeshAttributeSet::EnableTangents()
{
	SetNumNormalLayers(3);
}

void FDynamicMeshAttributeSet::DisableTangents()
{
	SetNumNormalLayers(1);
}


void FDynamicMeshAttributeSet::SetNumNormalLayers(int Num)
{
	if (NormalLayers.Num() == Num)
	{
		return;
	}
	if (Num >= NormalLayers.Num())
	{
		for (int32 i = NormalLayers.Num(); i < Num; ++i)
		{
			NormalLayers.Add(new FDynamicMeshNormalOverlay(ParentMesh));
		}
	}
	else
	{
		NormalLayers.RemoveAt(Num, UVLayers.Num() - Num);
	}
	check(NormalLayers.Num() == Num);
}

void FDynamicMeshAttributeSet::EnablePrimaryColors()
{
	if (HasPrimaryColors() == false)
	{ 
		ColorLayer = MakeUnique<FDynamicMeshColorOverlay>(ParentMesh);
	}
}

void FDynamicMeshAttributeSet::DisablePrimaryColors()
{
	ColorLayer.Reset();
}

int32 FDynamicMeshAttributeSet::NumPolygroupLayers() const
{
	return PolygroupLayers.Num();
}


void FDynamicMeshAttributeSet::SetNumPolygroupLayers(int32 Num)
{
	if (PolygroupLayers.Num() == Num)
	{
		return;
	}
	if (Num >= PolygroupLayers.Num())
	{
		for (int i = (int)PolygroupLayers.Num(); i < Num; ++i)
		{
			PolygroupLayers.Add(new FDynamicMeshPolygroupAttribute(ParentMesh));
		}
	}
	else
	{
		PolygroupLayers.RemoveAt(Num, PolygroupLayers.Num() - Num);
	}
	check(PolygroupLayers.Num() == Num);
}

FDynamicMeshPolygroupAttribute* FDynamicMeshAttributeSet::GetPolygroupLayer(int Index)
{
	return &PolygroupLayers[Index];
}

const FDynamicMeshPolygroupAttribute* FDynamicMeshAttributeSet::GetPolygroupLayer(int Index) const
{
	return &PolygroupLayers[Index];
}



void FDynamicMeshAttributeSet::EnableMaterialID()
{
	if (HasMaterialID() == false)
	{
		MaterialIDAttrib = MakeUnique<FDynamicMeshMaterialAttribute>(ParentMesh);
		MaterialIDAttrib->Initialize((int32)0);
	}
}

void FDynamicMeshAttributeSet::DisableMaterialID()
{
	MaterialIDAttrib.Reset();
}



bool FDynamicMeshAttributeSet::IsSeamEdge(int eid) const
{
	for (const FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		if (UVLayer.IsSeamEdge(eid))
		{
			return true;
		}
	}

	for (const FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		if (NormalLayer.IsSeamEdge(eid))
		{
			return true;
		}
	}

	if (ColorLayer && ColorLayer->IsSeamEdge(eid))
	{
		return true;
	}
	
	return false;
}

bool FDynamicMeshAttributeSet::IsSeamEndEdge(int eid) const
{
	for (const FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		if (UVLayer.IsSeamEndEdge(eid))
		{
			return true;
		}
	}

	for (const FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		if (NormalLayer.IsSeamEndEdge(eid))
		{
			return true;
		}
	}

	if (ColorLayer && ColorLayer->IsSeamEndEdge(eid))
	{
		return true;
	}
	return false;
}

bool FDynamicMeshAttributeSet::IsSeamEdge(int EdgeID, bool& bIsUVSeamOut, bool& bIsNormalSeamOut, bool& bIsColorSeamOut) const
{
	bIsUVSeamOut = false;
	for (const FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		if (UVLayer.IsSeamEdge(EdgeID))
		{
			bIsUVSeamOut = true;
		}
	}

	bIsNormalSeamOut = false;
	for (const FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		if (NormalLayer.IsSeamEdge(EdgeID))
		{
			bIsNormalSeamOut = true;
		}
	}

	bIsColorSeamOut = false;
	if (ColorLayer && ColorLayer->IsSeamEdge(EdgeID))
	{
		bIsColorSeamOut = true;
	}
	return (bIsUVSeamOut || bIsNormalSeamOut || bIsColorSeamOut);
}


bool FDynamicMeshAttributeSet::IsSeamVertex(int VID, bool bBoundaryIsSeam) const
{
	for (const FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		if (UVLayer.IsSeamVertex(VID, bBoundaryIsSeam))
		{
			return true;
		}
	}
	for (const FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		if (NormalLayer.IsSeamVertex(VID, bBoundaryIsSeam))
		{
			return true;
		}
	}
	if (ColorLayer && ColorLayer->IsSeamVertex(VID, bBoundaryIsSeam))
	{
		return true;
	}
	return false;
}

bool FDynamicMeshAttributeSet::IsMaterialBoundaryEdge(int EdgeID) const
{
	if ( MaterialIDAttrib == nullptr )
	{
		return false;
	}
	check(ParentMesh->IsEdge(EdgeID));
	const FDynamicMesh3::FEdge Edge = ParentMesh->GetEdge(EdgeID);
	const int Tri0 = Edge.Tri[0];
	const int Tri1 = Edge.Tri[1];
	if (( Tri0 == IndexConstants::InvalidID ) || (Tri1 == IndexConstants::InvalidID))
	{
		return false;
	}
	const int MatID0 = MaterialIDAttrib->GetValue(Tri0);
	const int MatID1 = MaterialIDAttrib->GetValue(Tri1);
	return MatID0 != MatID1;
}

void FDynamicMeshAttributeSet::OnNewVertex(int VertexID, bool bInserted)
{
	FDynamicMeshAttributeSetBase::OnNewVertex(VertexID, bInserted);
}


void FDynamicMeshAttributeSet::OnRemoveVertex(int VertexID)
{
	FDynamicMeshAttributeSetBase::OnRemoveVertex(VertexID);
}


void FDynamicMeshAttributeSet::OnNewTriangle(int TriangleID, bool bInserted)
{
	FDynamicMeshAttributeSetBase::OnNewTriangle(TriangleID, bInserted);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.InitializeNewTriangle(TriangleID);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.InitializeNewTriangle(TriangleID);
	}
	if (ColorLayer)
	{
		ColorLayer->InitializeNewTriangle(TriangleID);
	}
	if (MaterialIDAttrib)
	{
		int NewValue = 0;
		MaterialIDAttrib->SetNewValue(TriangleID, &NewValue);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		int32 NewGroup = 0;
		PolygroupLayer.SetNewValue(TriangleID, &NewGroup);
	}
}


void FDynamicMeshAttributeSet::OnRemoveTriangle(int TriangleID)
{
	FDynamicMeshAttributeSetBase::OnRemoveTriangle(TriangleID);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnRemoveTriangle(TriangleID);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnRemoveTriangle(TriangleID);
	}
	if (ColorLayer)
	{
		ColorLayer->OnRemoveTriangle(TriangleID);
	}

	// has no effect on MaterialIDAttrib
}

void FDynamicMeshAttributeSet::OnReverseTriOrientation(int TriangleID)
{
	FDynamicMeshAttributeSetBase::OnReverseTriOrientation(TriangleID);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnReverseTriOrientation(TriangleID);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnReverseTriOrientation(TriangleID);
	}
	if (ColorLayer)
	{
		ColorLayer->OnReverseTriOrientation(TriangleID);
	}
	// has no effect on MaterialIDAttrib
}

void FDynamicMeshAttributeSet::OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo)
{
	FDynamicMeshAttributeSetBase::OnSplitEdge(SplitInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnSplitEdge(SplitInfo);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnSplitEdge(SplitInfo);
	}
	if (ColorLayer)
	{
		ColorLayer->OnSplitEdge(SplitInfo);
	}
	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnSplitEdge(SplitInfo);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		PolygroupLayer.OnSplitEdge(SplitInfo);
	}
}

void FDynamicMeshAttributeSet::OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo & flipInfo)
{
	FDynamicMeshAttributeSetBase::OnFlipEdge(flipInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnFlipEdge(flipInfo);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnFlipEdge(flipInfo);
	}
	if (ColorLayer)
	{
		ColorLayer->OnFlipEdge(flipInfo);
	}
	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnFlipEdge(flipInfo);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		PolygroupLayer.OnFlipEdge(flipInfo);
	}
}


void FDynamicMeshAttributeSet::OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo & collapseInfo)
{
	FDynamicMeshAttributeSetBase::OnCollapseEdge(collapseInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnCollapseEdge(collapseInfo);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnCollapseEdge(collapseInfo);
	}
	if (ColorLayer)
	{
		ColorLayer->OnCollapseEdge(collapseInfo);
	}
	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnCollapseEdge(collapseInfo);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		PolygroupLayer.OnCollapseEdge(collapseInfo);
	}
}

void FDynamicMeshAttributeSet::OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo & pokeInfo)
{
	FDynamicMeshAttributeSetBase::OnPokeTriangle(pokeInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnPokeTriangle(pokeInfo);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnPokeTriangle(pokeInfo);
	}
	if (ColorLayer)
	{
		ColorLayer->OnPokeTriangle(pokeInfo);
	}
	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnPokeTriangle(pokeInfo);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		PolygroupLayer.OnPokeTriangle(pokeInfo);
	}
}

void FDynamicMeshAttributeSet::OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo & mergeInfo)
{
	FDynamicMeshAttributeSetBase::OnMergeEdges(mergeInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnMergeEdges(mergeInfo);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnMergeEdges(mergeInfo);
	}
	if (ColorLayer)
	{
		ColorLayer->OnMergeEdges(mergeInfo);
	}
	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnMergeEdges(mergeInfo);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		PolygroupLayer.OnMergeEdges(mergeInfo);
	}
}

void FDynamicMeshAttributeSet::OnSplitVertex(const DynamicMeshInfo::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate)
{
	FDynamicMeshAttributeSetBase::OnSplitVertex(SplitInfo, TrianglesToUpdate);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}
	for (FDynamicMeshNormalOverlay& NormalLayer : NormalLayers)
	{
		NormalLayer.OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}
	if (ColorLayer)
	{
		ColorLayer->OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}
	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}

	for (FDynamicMeshPolygroupAttribute& PolygroupLayer : PolygroupLayers)
	{
		PolygroupLayer.OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}
}

bool FDynamicMeshAttributeSet::IsSameAs(const FDynamicMeshAttributeSet& Other) const
{
	if (UVLayers.Num() != Other.UVLayers.Num() ||
		NormalLayers.Num() != Other.NormalLayers.Num() ||
		PolygroupLayers.Num() != Other.PolygroupLayers.Num())
	{
		return false;
	}

	for (int Idx = 0; Idx < UVLayers.Num(); Idx++)
	{
		if (!UVLayers[Idx].IsSameAs(Other.UVLayers[Idx]))
		{
			return false;
		}
	}

	for (int Idx = 0; Idx < NormalLayers.Num(); Idx++)
	{
		if (!NormalLayers[Idx].IsSameAs(Other.NormalLayers[Idx]))
		{
			return false;
		}
	}

	for (int Idx = 0; Idx < PolygroupLayers.Num(); Idx++)
	{
		if (!PolygroupLayers[Idx].IsSameAs(Other.PolygroupLayers[Idx]))
		{
			return false;
		}
	}

	if (HasPrimaryColors() != Other.HasPrimaryColors())
	{
		return false;
	}
	if (HasPrimaryColors())
	{
		if (!ColorLayer->IsSameAs(*Other.ColorLayer))
		{
			return false;
		}
	}

	if (HasMaterialID() != Other.HasMaterialID())
	{
		return false;
	}
	if (HasMaterialID())
	{
		if (!MaterialIDAttrib->IsSameAs(*Other.MaterialIDAttrib))
		{
			return false;
		}
	}

	// TODO: Test GenericAttributes

	return true;
}

void FDynamicMeshAttributeSet::Serialize(FArchive& Ar)
{
	Ar << UVLayers;
	Ar << NormalLayers;
	Ar << PolygroupLayers;

	if (Ar.IsLoading())
	{
		// Manually populate ParentMesh since deserialization of the individual
		// layers cannot populate the pointer.
		for (FDynamicMeshUVOverlay& Overlay : UVLayers)
		{
			Overlay.ParentMesh = ParentMesh;
		}
		for (FDynamicMeshNormalOverlay& Overlay : NormalLayers)
		{
			Overlay.ParentMesh = ParentMesh;
		}
		for (FDynamicMeshPolygroupAttribute& Attr : PolygroupLayers)
		{
			Attr.ParentMesh = ParentMesh;
		}
	}

	// Use int32 here to futureproof for multiple color layers.
	int32 bHasColorLayer = HasPrimaryColors() ? 1 : 0;
	Ar << bHasColorLayer;
	if (bHasColorLayer)
	{
		if (Ar.IsLoading())
		{
			EnablePrimaryColors();
		}
		Ar << *ColorLayer;
	}

	bool bHasMaterialID = HasMaterialID();
	Ar << bHasMaterialID;
	if (bHasMaterialID)
	{
		if (Ar.IsLoading())
		{
			EnableMaterialID();
		}
		Ar << *MaterialIDAttrib;
	}

	//Ar << GenericAttributes; // TODO
}

