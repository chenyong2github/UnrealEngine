// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/GeometryCollection.h"

#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"

#include <iostream>
#include <fstream>
#include "Chaos/ChaosArchive.h"
#include "Voronoi/Voronoi.h"

DEFINE_LOG_CATEGORY_STATIC(FGeometryCollectionLogging, Log, All);

// @todo: update names 
const FName FGeometryCollection::FacesGroup = "Faces";
const FName FGeometryCollection::GeometryGroup = "Geometry";
const FName FGeometryCollection::VerticesGroup = "Vertices";
const FName FGeometryCollection::BreakingGroup = "Breaking";
const FName FGeometryCollection::MaterialGroup = "Material";

// Attributes
const FName FGeometryCollection::SimulatableParticlesAttribute("SimulatableParticlesAttribute");
const FName FGeometryCollection::SimulationTypeAttribute("SimulationType");
const FName FGeometryCollection::StatusFlagsAttribute("StatusFlags");

FGeometryCollection::FGeometryCollection()
	: FTransformCollection()
{
	Construct();
}


void FGeometryCollection::Construct()
{
	FManagedArrayCollection::FConstructionParameters TransformDependency(FTransformCollection::TransformGroup);
	FManagedArrayCollection::FConstructionParameters VerticesDependency(FGeometryCollection::VerticesGroup);
	FManagedArrayCollection::FConstructionParameters FacesDependency(FGeometryCollection::FacesGroup);

	// Transform Group
	AddExternalAttribute<int32>("TransformToGeometryIndex", FTransformCollection::TransformGroup, TransformToGeometryIndex);
	AddExternalAttribute<int32>("SimulationType", FTransformCollection::TransformGroup, SimulationType);
	AddExternalAttribute<int32>("StatusFlags", FTransformCollection::TransformGroup, StatusFlags);
	AddExternalAttribute<int32>("InitialDynamicState", FTransformCollection::TransformGroup, InitialDynamicState);

	// Vertices Group
	AddExternalAttribute<FVector>("Vertex", FGeometryCollection::VerticesGroup, Vertex);
	AddExternalAttribute<FVector>("Normal", FGeometryCollection::VerticesGroup, Normal);
	AddExternalAttribute<FVector2D>("UV", FGeometryCollection::VerticesGroup, UV);
	AddExternalAttribute<FLinearColor>("Color", FGeometryCollection::VerticesGroup, Color);
	AddExternalAttribute<FVector>("TangentU", FGeometryCollection::VerticesGroup, TangentU);
	AddExternalAttribute<FVector>("TangentV", FGeometryCollection::VerticesGroup, TangentV);
	AddExternalAttribute<int32>("BoneMap", FGeometryCollection::VerticesGroup, BoneMap, TransformDependency);

	// Faces Group
	AddExternalAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup, Indices, VerticesDependency);
	AddExternalAttribute<bool>("Visible", FGeometryCollection::FacesGroup, Visible);
	AddExternalAttribute<int32>("MaterialIndex", FGeometryCollection::FacesGroup, MaterialIndex);
	AddExternalAttribute<int32>("MaterialID", FGeometryCollection::FacesGroup, MaterialID);

	// Geometry Group
	AddExternalAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup, TransformIndex, TransformDependency);
	AddExternalAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup, BoundingBox);
	AddExternalAttribute<float>("InnerRadius", FGeometryCollection::GeometryGroup, InnerRadius);
	AddExternalAttribute<float>("OuterRadius", FGeometryCollection::GeometryGroup, OuterRadius);
	AddExternalAttribute<int32>("VertexStart", FGeometryCollection::GeometryGroup, VertexStart, VerticesDependency);
	AddExternalAttribute<int32>("VertexCount", FGeometryCollection::GeometryGroup, VertexCount);
	AddExternalAttribute<int32>("FaceStart", FGeometryCollection::GeometryGroup, FaceStart, FacesDependency);
	AddExternalAttribute<int32>("FaceCount", FGeometryCollection::GeometryGroup, FaceCount);

	// Material Group
	AddExternalAttribute<FGeometryCollectionSection>("Sections", FGeometryCollection::MaterialGroup, Sections, FacesDependency);
}


void FGeometryCollection::SetDefaults(FName Group, uint32 StartSize, uint32 NumElements)
{
	if (Group == FTransformCollection::TransformGroup)
	{
		for (uint32 Idx = StartSize; Idx < StartSize + NumElements; ++Idx)
		{
			TransformToGeometryIndex[Idx] = FGeometryCollection::Invalid;
			Parent[Idx] = FGeometryCollection::Invalid;
			SimulationType[Idx] = FGeometryCollection::ESimulationTypes::FST_None;
			StatusFlags[Idx] = 0;
			InitialDynamicState[Idx] = static_cast<int32>(Chaos::EObjectStateType::Uninitialized);
		}
	}
}

// MaterialIDOffset is based on the number of materials added by this append geometry call
int32 FGeometryCollection::AppendGeometry(const FGeometryCollection & Element, int32 MaterialIDOffset, bool ReindexAllMaterials, const FTransform& TransformRoot)
{
	// until we support a transform hierarchy this is just one.
	check(Element.NumElements(FGeometryCollection::TransformGroup) > 0);

	int NumTransforms = NumElements(FTransformCollection::TransformGroup);
	int NumNewTransforms = Element.NumElements(FTransformCollection::TransformGroup);

	int32 StartTransformIndex = Super::AppendTransform(Element, TransformRoot);
	check(NumTransforms == StartTransformIndex);

	check(Element.NumElements(FGeometryCollection::FacesGroup) > 0);
	check(Element.NumElements(FGeometryCollection::VerticesGroup) > 0);

	int NumNewVertices = Element.NumElements(FGeometryCollection::VerticesGroup);
	const TManagedArray<FVector>& ElementVertices = Element.Vertex;
	const TManagedArray<FVector>& ElementNormals = Element.Normal;
	const TManagedArray<FVector2D>& ElementUVs = Element.UV;
	const TManagedArray<FLinearColor>& ElementColors = Element.Color;
	const TManagedArray<FVector>& ElementTangentUs = Element.TangentU;
	const TManagedArray<FVector>& ElementTangentVs = Element.TangentV;
	const TManagedArray<int32>& ElementBoneMap = Element.BoneMap;

	const TManagedArray<FIntVector>& ElementIndices = Element.Indices;
	const TManagedArray<bool>& ElementVisible = Element.Visible;
	const TManagedArray<int32>& ElementMaterialIndex = Element.MaterialIndex;
	const TManagedArray<int32>& ElementMaterialID = Element.MaterialID;

	const TManagedArray<int32>& ElementTransformIndex = Element.TransformIndex;
	const TManagedArray<FBox>& ElementBoundingBox = Element.BoundingBox;
	const TManagedArray<float>& ElementInnerRadius = Element.InnerRadius;
	const TManagedArray<float>& ElementOuterRadius = Element.OuterRadius;
	const TManagedArray<int32>& ElementVertexStart = Element.VertexStart;
	const TManagedArray<int32>& ElementVertexCount = Element.VertexCount;
	const TManagedArray<int32>& ElementFaceStart = Element.FaceStart;
	const TManagedArray<int32>& ElementFaceCount = Element.FaceCount;

	const TManagedArray<FTransform>& ElementTransform = Element.Transform;
	const TManagedArray<FString>& ElementBoneName = Element.BoneName;
	const TManagedArray<FGeometryCollectionSection>& ElementSections = Element.Sections;

	const TManagedArray<int32>& ElementSimulationType = Element.SimulationType;
	const TManagedArray<int32>& ElementStatusFlags = Element.StatusFlags;
	const TManagedArray<int32>& ElementInitialDynamicState = Element.InitialDynamicState;

	// --- TRANSFORM ---
	for (int TransformIdx = 0; TransformIdx < NumNewTransforms; TransformIdx++)
	{
		SimulationType[TransformIdx + StartTransformIndex] = ElementSimulationType[TransformIdx];
		StatusFlags[TransformIdx + StartTransformIndex] = ElementStatusFlags[TransformIdx];
		InitialDynamicState[TransformIdx + StartTransformIndex] = ElementInitialDynamicState[TransformIdx];
	}

	// --- VERTICES GROUP ---

	int NumVertices = NumElements(FGeometryCollection::VerticesGroup);
	int VerticesIndex = AddElements(NumNewVertices, FGeometryCollection::VerticesGroup);
	TManagedArray<FVector>& Vertices = Vertex;
	TManagedArray<FVector>& Normals = Normal;
	TManagedArray<FVector2D>& UVs = UV;
	TManagedArray<FLinearColor>& Colors = Color;
	TManagedArray<FVector>& TangentUs = TangentU;
	TManagedArray<FVector>& TangentVs = TangentV;
	TManagedArray<int32>& BoneMaps = BoneMap;
	TManagedArray<FIntVector>& FaceIndices = Indices;

	for (int vdx = 0; vdx < NumNewVertices; vdx++)
	{
		Vertices[VerticesIndex + vdx] = ElementVertices[vdx];
		Normals[VerticesIndex + vdx] = ElementNormals[vdx];
		UVs[VerticesIndex + vdx] = ElementUVs[vdx];
		Colors[VerticesIndex + vdx] = ElementColors[vdx];
		TangentUs[VerticesIndex + vdx] = ElementTangentUs[vdx];
		TangentVs[VerticesIndex + vdx] = ElementTangentVs[vdx];
		BoneMaps[VerticesIndex + vdx] = ElementBoneMap[vdx] + StartTransformIndex;
	}

	// --- FACES GROUP ---


	int NumIndices = NumElements(FGeometryCollection::FacesGroup);
	int NumNewIndices = ElementIndices.Num();
	int IndicesIndex = AddElements(NumNewIndices, FGeometryCollection::FacesGroup);
	for (int32 tdx = 0; tdx < NumNewIndices; tdx++)
	{
		Indices[IndicesIndex + tdx] = FIntVector(VerticesIndex, VerticesIndex, VerticesIndex) + ElementIndices[tdx];
		Visible[IndicesIndex + tdx] = ElementVisible[tdx];
		MaterialIndex[IndicesIndex + tdx] = ElementMaterialIndex[tdx];
		// MaterialIDs need to be incremented
		MaterialID[IndicesIndex + tdx] = MaterialIDOffset + ElementMaterialID[tdx];	
	}

	// --- GEOMETRY GROUP ---

	int NumNewGeometryGroups = Element.NumElements(FGeometryCollection::GeometryGroup);
	NumNewGeometryGroups = (NumNewGeometryGroups == 0) ? 1 : NumNewGeometryGroups; // add one if Element input failed to create a geometry group
	int GeometryIndex = AddElements(NumNewGeometryGroups, FGeometryCollection::GeometryGroup);
	if (ElementTransformIndex.Num() > 0)
	{
		for (int32 tdx = 0; tdx < NumNewGeometryGroups; tdx++)
		{
			BoundingBox[GeometryIndex + tdx] = ElementBoundingBox[tdx];
			InnerRadius[GeometryIndex + tdx] = ElementInnerRadius[tdx];
			OuterRadius[GeometryIndex + tdx] = ElementOuterRadius[tdx];
			FaceStart[GeometryIndex + tdx] = NumIndices + ElementFaceStart[tdx];
			FaceCount[GeometryIndex + tdx] = ElementFaceCount[tdx];
			VertexStart[GeometryIndex + tdx] = NumVertices + ElementVertexStart[tdx];
			VertexCount[GeometryIndex + tdx] = ElementVertexCount[tdx];
			TransformIndex[GeometryIndex + tdx] = BoneMaps[VertexStart[GeometryIndex + tdx]];
			TransformToGeometryIndex[TransformIndex[GeometryIndex + tdx]] = GeometryIndex + tdx;

		}
	}
	else // Element input failed to create a geometry group
	{
		// Compute BoundingBox 
		BoundingBox[GeometryIndex] = FBox(ForceInitToZero);
		TransformIndex[GeometryIndex] = BoneMaps[VerticesIndex];
		VertexStart[GeometryIndex] = VerticesIndex;
		VertexCount[GeometryIndex] = NumNewVertices;
		FaceStart[GeometryIndex] = IndicesIndex;
		FaceCount[GeometryIndex] = NumNewIndices;

		TransformToGeometryIndex[TransformIndex[GeometryIndex]] = GeometryIndex;

		// Bounding Box
		for (int vdx = VerticesIndex; vdx < VerticesIndex+NumNewVertices; vdx++)
		{
			BoundingBox[GeometryIndex] += Vertices[vdx];
		}

		// Find average particle
		// @todo (CenterOfMass) : This need to be the center of mass instead
		FVector Center(0);
		for (int vdx = VerticesIndex; vdx <  VerticesIndex + NumNewVertices; vdx++)
		{
			Center += Vertices[vdx];
		}
		if (NumNewVertices) Center /= NumNewVertices;

		//
		//  Inner/Outer Radius
		//
		{
			TManagedArray<float>& InnerR = InnerRadius;
			TManagedArray<float>& OuterR = OuterRadius;

			// init the radius arrays
			InnerR[GeometryIndex] = FLT_MAX;
			OuterR[GeometryIndex] = -FLT_MAX;

			// Vertices
			for (int vdx = VerticesIndex; vdx < VerticesIndex + NumNewVertices; vdx++)
			{
				float Delta = (Center - Vertices[vdx]).Size();
				InnerR[GeometryIndex] = FMath::Min(InnerR[GeometryIndex], Delta);
				OuterR[GeometryIndex] = FMath::Max(OuterR[GeometryIndex], Delta);
			}


			// Inner/Outer centroid
			for (int fdx = IndicesIndex; fdx < IndicesIndex+NumNewIndices; fdx++)
			{
				FVector Centroid(0);
				for (int e = 0; e < 3; e++)
				{
					Centroid += Vertices[FaceIndices[fdx][e]];
				}
				Centroid /= 3;

				float Delta = (Center - Centroid).Size();
				InnerR[GeometryIndex] = FMath::Min(InnerR[GeometryIndex], Delta);
				OuterR[GeometryIndex] = FMath::Max(OuterR[GeometryIndex], Delta);
			}

			// Inner/Outer edges
			for (int fdx = IndicesIndex; fdx < IndicesIndex + NumNewIndices; fdx++)
			{
				for (int e = 0; e < 3; e++)
				{
					int i = e, j = (e + 1) % 3;
					FVector Edge = Vertices[FaceIndices[fdx][i]] + 0.5*(Vertices[FaceIndices[fdx][j]] - Vertices[FaceIndices[fdx][i]]);
					float Delta = (Center - Edge).Size();
					InnerR[GeometryIndex] = FMath::Min(InnerR[GeometryIndex], Delta);
					OuterR[GeometryIndex] = FMath::Max(OuterR[GeometryIndex], Delta);
				}
			}
		}
	}

	// --- MATERIAL GROUP ---
	// note for now, we rely on rebuilding mesh sections rather than passing them through.  We know
	// that MaterialID is set correctly to correspond with the material index that will be rendered
	if (ReindexAllMaterials)
	{
		ReindexMaterials();
	}	

	return StartTransformIndex;
}

// Input assumes that each face has a materialID that corresponds with a render material
// This will rebuild all mesh sections
void FGeometryCollection::ReindexMaterials()
{
	// clear all sections	
	TArray<int32> DelSections;
	GeometryCollectionAlgo::ContiguousArray(DelSections, NumElements(FGeometryCollection::MaterialGroup));
	Super::RemoveElements(FGeometryCollection::MaterialGroup, DelSections);
	DelSections.Reset(0);


	// rebuild sections		

	// count the number of triangles for each material section, adding a new section if the material ID is higher than the current number of sections
	for (int FaceElement = 0, nf = NumElements(FGeometryCollection::FacesGroup) ; FaceElement < nf ; ++FaceElement)
	{
		int32 Section = MaterialID[FaceElement];

		while (Section + 1 > NumElements(FGeometryCollection::MaterialGroup))
		{
			// add a new material section
			int32 Element = AddElements(1, FGeometryCollection::MaterialGroup);

			Sections[Element].MaterialID = Element;
			Sections[Element].FirstIndex = -1;
			Sections[Element].NumTriangles = 0;
			Sections[Element].MinVertexIndex = 0;
			Sections[Element].MaxVertexIndex = 0;
		}

		Sections[Section].NumTriangles++;
	}

	// fixup the section FirstIndex and MaxVertexIndex
	for (int SectionElement = 0; SectionElement < NumElements(FGeometryCollection::MaterialGroup); SectionElement++)
	{
		if (SectionElement == 0)
		{
			Sections[SectionElement].FirstIndex = 0;
		}
		else
		{
			// Each subsequent section has an index that starts after the last one
			// note the NumTriangles*3 - this is because indices are sent to the renderer in a flat array
			Sections[SectionElement].FirstIndex = Sections[SectionElement - 1].FirstIndex + Sections[SectionElement - 1].NumTriangles * 3;
		}

		Sections[SectionElement].MaxVertexIndex = NumElements(FGeometryCollection::VerticesGroup) - 1;

		// if a material group no longer has any triangles in it then add material section for removal
		if (Sections[SectionElement].NumTriangles == 0)
		{
			DelSections.Push(SectionElement);
		}
	}

	// remap indices so the materials appear to be grouped
	int Idx = 0;
	for (int Section=0; Section < NumElements(FGeometryCollection::MaterialGroup); Section++)
	{
		for (int FaceElement = 0; FaceElement < NumElements(FGeometryCollection::FacesGroup); FaceElement++)
		{
			int32 ID = (MaterialID)[FaceElement];
	
			if (Section == ID)
			{
				(MaterialIndex)[Idx++] = FaceElement;
			}
		}
	}

	// delete unused material sections
	if (DelSections.Num())
	{
		Super::RemoveElements(FGeometryCollection::MaterialGroup, DelSections);
	}
}

TArray<FGeometryCollectionSection> FGeometryCollection::BuildMeshSections(const TArray<FIntVector> &InputIndices, TArray<int32> BaseMeshOriginalIndicesIndex, TArray<FIntVector> &RetIndices) const
{	

	TArray<FGeometryCollectionSection> TmpSections;
	TArray<FGeometryCollectionSection> RetSections;		

	// count the number of triangles for each material section, adding a new section if the material ID is higher than the current number of sections
	for (int FaceElement = 0; FaceElement < InputIndices.Num(); ++FaceElement)
	{
		int32 Section = MaterialID[BaseMeshOriginalIndicesIndex[FaceElement]];		

		while (Section + 1 > TmpSections.Num())
		{
			// add a new material section
			int32 Element = TmpSections.AddZeroed();

			TmpSections[Element].MaterialID = Element;
			TmpSections[Element].FirstIndex = -1;
			TmpSections[Element].NumTriangles = 0;
			TmpSections[Element].MinVertexIndex = 0;
			TmpSections[Element].MaxVertexIndex = 0;
		}

		TmpSections[Section].NumTriangles++;
	}

	// fixup the section FirstIndex and MaxVertexIndex
	for (int SectionElement = 0; SectionElement < TmpSections.Num(); SectionElement++)
	{
		if (SectionElement == 0)
		{
			TmpSections[SectionElement].FirstIndex = 0;
		}
		else
		{
			// Each subsequent section has an index that starts after the last one
			// note the NumTriangles*3 - this is because indices are sent to the renderer in a flat array
			TmpSections[SectionElement].FirstIndex = TmpSections[SectionElement - 1].FirstIndex + TmpSections[SectionElement - 1].NumTriangles * 3;
		}

		TmpSections[SectionElement].MaxVertexIndex = NumElements(FGeometryCollection::VerticesGroup) - 1;
	}

	// remap indices so the materials appear to be grouped
	RetIndices.AddUninitialized(InputIndices.Num());
	int Idx = 0;
	for (int Section = 0; Section < TmpSections.Num(); Section++)
	{
		for (int FaceElement = 0; FaceElement < InputIndices.Num(); FaceElement++)
		{
			int32 ID = (MaterialID)[BaseMeshOriginalIndicesIndex[FaceElement]];

			if (Section == ID)
			{
				RetIndices[Idx++] = InputIndices[FaceElement];				
			}
		}
	}

	// if a material group no longer has any triangles in it then add material section for removal
	RetSections.Reserve(TmpSections.Num());
	for (int SectionElement = 0; SectionElement < TmpSections.Num(); SectionElement++)
	{
		if (TmpSections[SectionElement].NumTriangles > 0)
		{
			RetSections.Push(TmpSections[SectionElement]);
		}
	}

	return MoveTemp(RetSections);
}


void FGeometryCollection::RemoveElements(const FName & Group, const TArray<int32>& SortedDeletionList, FProcessingParameters Params)
{
	if (SortedDeletionList.Num())
	{
		GeometryCollectionAlgo::ValidateSortedList(SortedDeletionList, NumElements(Group));

		if (Group == FTransformCollection::TransformGroup)
		{
			// Find geometry connected to the transform
			TArray<int32> GeometryIndices;
			const TManagedArray<int32>& GeometryToTransformIndex = TransformIndex;
			for (int i = 0; i < GeometryToTransformIndex.Num(); i++)
			{
				if (SortedDeletionList.Contains(GeometryToTransformIndex[i]))
				{
					GeometryIndices.Add(i);
				}
			}

			RemoveGeometryElements(GeometryIndices);

			Super::RemoveElements(Group, SortedDeletionList);
		}
		else if (Group == FGeometryCollection::GeometryGroup)
		{
			RemoveGeometryElements(SortedDeletionList);
		}
		else if( Group == FGeometryCollection::FacesGroup)
		{
			Super::RemoveElements(Group, SortedDeletionList);
			UpdateFaceGroupElements();
		}
		else if (Group == FGeometryCollection::VerticesGroup)
		{
			Super::RemoveElements(Group, SortedDeletionList);
			UpdateVerticesGroupElements();
		}
		else
		{
			Super::RemoveElements(Group, SortedDeletionList);
		}


#if WITH_EDITOR
		if (Params.bDoValidation)
		{
			ensure(HasContiguousFaces());
			ensure(HasContiguousVertices());
			ensure(GeometryCollectionAlgo::HasValidGeometryReferences(this));
		}
#endif
	}
}

void FGeometryCollection::RemoveGeometryElements(const TArray<int32>& SortedGeometryIndicesToDelete)
{
	if (SortedGeometryIndicesToDelete.Num())
	{
		GeometryCollectionAlgo::ValidateSortedList(SortedGeometryIndicesToDelete, NumElements(FGeometryCollection::GeometryGroup));

		//
		// Find transform connected to the geometry [But don't delete them]
		//
		TArray<int32> TransformIndices;
		for (int i = 0; i < SortedGeometryIndicesToDelete.Num(); i++)
		{
			int32 GeometryIndex = SortedGeometryIndicesToDelete[i];
			if (0 <= GeometryIndex && GeometryIndex < TransformIndex.Num() && TransformIndex[GeometryIndex] != INDEX_NONE)
			{
				TransformIndices.Add(TransformIndex[GeometryIndex]);
			}
		}

		TArray<bool> Mask;

		//
		// Delete Vertices
		//
		GeometryCollectionAlgo::BuildLookupMask(TransformIndices, NumElements(FGeometryCollection::TransformGroup), Mask);

		TArray<int32> DelVertices;
		for (int32 Index = 0; Index < BoneMap.Num(); Index++)
		{
			if (BoneMap[Index] != Invalid && BoneMap[Index] < Mask.Num() && Mask[BoneMap[Index]])
			{
				DelVertices.Add(Index);
			}
		}
		DelVertices.Sort();


		//
		// Delete Faces
		//
		GeometryCollectionAlgo::BuildLookupMask(DelVertices, NumElements(FGeometryCollection::VerticesGroup), Mask);
		TManagedArray<FIntVector>& Tris = Indices;

		TArray<int32> DelFaces;
		for (int32 Index = 0; Index < Tris.Num(); Index++)
		{
			const FIntVector & Face = Tris[Index];
			for (int i = 0; i < 3; i++)
			{
				ensure(Face[i] < Mask.Num());
				if (Mask[Face[i]])
				{
					DelFaces.Add(Index);
					break;
				}
			}
		}
		DelFaces.Sort();

		Super::RemoveElements(FGeometryCollection::GeometryGroup, SortedGeometryIndicesToDelete);
		Super::RemoveElements(FGeometryCollection::VerticesGroup, DelVertices);
		Super::RemoveElements(FGeometryCollection::FacesGroup, DelFaces);

		for (int32 DeleteIdx = SortedGeometryIndicesToDelete.Num()-1; DeleteIdx >=0; DeleteIdx--)
		{
			int32 GeoIndex = SortedGeometryIndicesToDelete[DeleteIdx];
			for (int Idx = 0; Idx < TransformToGeometryIndex.Num(); Idx++)
			{
				if (TransformToGeometryIndex[Idx] >= GeoIndex)
					TransformToGeometryIndex[Idx]--;
			}

		}

		ReindexMaterials();
	}
}

void FGeometryCollection::Empty()
{
	for (const FName& GroupName : GroupNames())
	{
		EmptyGroup(GroupName);
	}
}

void FGeometryCollection::ReorderElements(FName Group, const TArray<int32>& NewOrder)
{
	if (Group == FTransformCollection::TransformGroup)
	{
		ReorderTransformElements(NewOrder);
	}
	else if (Group == FGeometryCollection::GeometryGroup)
	{
		ReorderGeometryElements(NewOrder);
	}
	else
	{
		Super::ReorderElements(Group, NewOrder);
	}
}

void FGeometryCollection::ReorderTransformElements(const TArray<int32>& NewOrder)
{
	struct FTransformGeomPair
	{
		FTransformGeomPair(int32 InTransformIdx, int32 InGeomIdx) : TransformIdx(InTransformIdx), GeomIdx(InGeomIdx) {}

		int32 TransformIdx;
		int32 GeomIdx;

		bool operator<(const FTransformGeomPair& Other) const { return TransformIdx < Other.TransformIdx; }
	};

	const int32 NumGeometries = TransformIndex.Num();
	TArray<FTransformGeomPair> Pairs;
	Pairs.Reserve(NumGeometries);
	for (int32 GeomIdx = 0; GeomIdx < NumGeometries; ++GeomIdx)
	{
		Pairs.Emplace(NewOrder[TransformIndex[GeomIdx]], GeomIdx);
	}
	Pairs.Sort();

	TArray<int32> NewGeomOrder;
	NewGeomOrder.Reserve(NumGeometries);
	for (const FTransformGeomPair& Pair : Pairs)
	{
		NewGeomOrder.Add(Pair.GeomIdx);
	}
	ReorderGeometryElements(NewGeomOrder);

	for (int32 Index = 0; Index < Parent.Num(); Index++)
	{
		// remap the parents (-1 === Invalid )
		if (Parent[Index] != -1)
		{
			Parent[Index] -= NewOrder[Parent[Index]];
		}

		// remap children
		TSet<int32> ChildrenCopy = Children[Index];
		Children[Index].Empty();
		for (int32 ChildID : ChildrenCopy)
		{
			if (ChildID >= 0)
			{
				Children[Index].Add(NewOrder[ChildID]);
			}
			else
			{
				Children[Index].Add(ChildID);	//not remap so just leave as was
			}
		}
	}

	Super::ReorderElements(FTransformCollection::TransformGroup, NewOrder);

}

void FGeometryCollection::ReorderGeometryElements(const TArray<int32>& NewOrder)
{
	const int32 NumGeometry = NumElements(GeometryGroup);
	check(NumGeometry == NewOrder.Num());

	//Compute new order for vertices group and faces group
	TArray<int32> NewVertOrder;
	NewVertOrder.Reserve(NumElements(VerticesGroup));
	int32 TotalVertOffset = 0;

	TArray<int32> NewFaceOrder;
	NewFaceOrder.Reserve(NumElements(FacesGroup));
	int32 TotalFaceOffset = 0;

	for (int32 OldGeomIdx = 0; OldGeomIdx < NumGeometry; ++OldGeomIdx)
	{
		const int32 NewGeomIdx = NewOrder[OldGeomIdx];

		//verts
		const int32 VertStartIdx = VertexStart[NewGeomIdx];
		const int32 NumVerts = VertexCount[NewGeomIdx];
		for (int32 VertIdx = VertStartIdx; VertIdx < VertStartIdx + NumVerts; ++VertIdx)
		{
			NewVertOrder.Add(VertIdx);
		}
		TotalVertOffset += NumVerts;

		//faces
		const int32 FaceStartIdx = FaceStart[NewGeomIdx];
		const int32 NumFaces = FaceCount[NewGeomIdx];
		for (int32 FaceIdx = FaceStartIdx; FaceIdx < FaceStartIdx + NumFaces; ++FaceIdx)
		{
			NewFaceOrder.Add(FaceIdx);
		}
		TotalFaceOffset += NumFaces;
	}

	//we must now reorder according to dependencies
	Super::ReorderElements(VerticesGroup, NewVertOrder);
	Super::ReorderElements(FacesGroup, NewFaceOrder);
	Super::ReorderElements(GeometryGroup, NewOrder);
}

void FGeometryCollection::UpdateVerticesGroupElements()
{
	//
	//  Reset the VertexCount array
	//
	int32 NumberOfVertices = Vertex.Num();
	for (int32 GeometryIndex = 0, ng = TransformIndex.Num(); GeometryIndex < ng; ++GeometryIndex)
	{
		int32 VertexIndex = VertexStart[GeometryIndex];
		if (VertexIndex != INDEX_NONE)
		{
			int32 StartBoneMapTransformValue = BoneMap[VertexIndex];
			int32 CurrentBoneMapTransformValue = StartBoneMapTransformValue;
			while ((CurrentBoneMapTransformValue == StartBoneMapTransformValue) && (++VertexIndex < NumberOfVertices))
			{
				CurrentBoneMapTransformValue = BoneMap[VertexIndex];
			}
			VertexCount[GeometryIndex] = VertexIndex - VertexStart[GeometryIndex];
		}
		else
		{
			VertexCount[GeometryIndex] = 0;
		}
	}
}

void FGeometryCollection::UpdateFaceGroupElements()
{
	//
	//  Reset the FaceCount array
	//
	int32 NumberOfFaces = Indices.Num();
	for (int32 GeometryIndex = 0, ng = TransformIndex.Num(); GeometryIndex < ng; ++GeometryIndex)
	{
		int32 FaceIndex = FaceStart[GeometryIndex];
		if (FaceIndex != INDEX_NONE)
		{
			int32 StartBoneMapTransformValue = BoneMap[Indices[FaceIndex][0]];
			int32 CurrentBoneMapTransformValue = StartBoneMapTransformValue;
			while ((CurrentBoneMapTransformValue == StartBoneMapTransformValue) && (++FaceIndex < NumberOfFaces))
			{
				CurrentBoneMapTransformValue = BoneMap[Indices[FaceIndex][0]];
			}
			FaceCount[GeometryIndex] = FaceIndex - FaceStart[GeometryIndex];
		}
		else
		{
			FaceCount[GeometryIndex] = 0;
		}
	}
}


void FGeometryCollection::UpdateGeometryVisibility(const TArray<int32>& NodeList, bool VisibilityState)
{

	for (int32 Idx = 0; Idx < Visible.Num(); Idx++)
	{
		for (int32 Node : NodeList)
		{
			if (BoneMap[Indices[Idx][0]] == Node)
			{
				Visible[Idx] = VisibilityState;
			}
		}	
	}
}
bool FGeometryCollection::HasVisibleGeometry() const
{
	bool bHasVisibleGeometry = false;
	const TManagedArray<bool>& VisibleIndices = Visible;

	for (int32 fdx = 0; fdx < VisibleIndices.Num(); fdx++)
	{
		if (VisibleIndices[fdx])
		{
			bHasVisibleGeometry = true;
			break;
		}
	}
	return bHasVisibleGeometry;
}

void FGeometryCollection::UpdateBoundingBox()
{
	if (BoundingBox.Num())
	{
		// Initialize BoundingBox
		for (int32 Idx = 0; Idx < BoundingBox.Num(); ++Idx)
		{
			BoundingBox[Idx].Init();
		}

		// Build reverse map between TransformIdx and index in the GeometryGroup
		TMap<int32, int32> GeometryGroupIndexMap;
		for (int32 Idx = 0; Idx < NumElements(FGeometryCollection::GeometryGroup); ++Idx)
		{
			GeometryGroupIndexMap.Add(TransformIndex[Idx], Idx);
		}
		// Compute BoundingBox
		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			int32 TransformIndexValue = BoneMap[Idx];
			BoundingBox[GeometryGroupIndexMap[TransformIndexValue]] += Vertex[Idx];
		}
	}
}

void FGeometryCollection::Serialize(Chaos::FChaosArchive& Ar)
{
	Super::Serialize(Ar);



	if (Ar.IsLoading())
	{
		// Versioning - correct assets that were saved before material sections were introduced
		if (NumElements(FGeometryCollection::MaterialGroup) == 0)
		{
			int SectionIndex = AddElements(1, FGeometryCollection::MaterialGroup);
			Sections[SectionIndex].MaterialID = 0;
			Sections[SectionIndex].FirstIndex = 0;
			Sections[SectionIndex].NumTriangles = Indices.Num();
			Sections[SectionIndex].MinVertexIndex = 0;
			Sections[SectionIndex].MaxVertexIndex = Vertex.Num();
		}

		// Recompute TransformToGroupIndex map
		int NumGeoms = NumElements(FGeometryCollection::GeometryGroup);
		int NumTransforms = NumElements(FGeometryCollection::TransformGroup);
		for (int32 Idx = 0; Idx < NumGeoms; ++Idx)
		{
			if (0<=TransformIndex[Idx]&&TransformIndex[Idx] < NumTransforms)
			{
				TransformToGeometryIndex[TransformIndex[Idx]] = Idx;
			}
		}

		// Add SimulationType attribute
		if (!(this->HasAttribute(FGeometryCollection::SimulationTypeAttribute, FTransformCollection::TransformGroup)))
		{
			TManagedArray<int32>& SimType = this->AddAttribute<int32>(FGeometryCollection::SimulationTypeAttribute, FTransformCollection::TransformGroup);
			for (int32 Idx = 0; Idx < NumTransforms; ++Idx)
			{
				SimType[Idx] = FGeometryCollection::ESimulationTypes::FST_None;
			}
		}

		// for backwards compatibility convert old BoneHierarchy struct into split out arrays
		enum ENodeFlags : uint32
		{
			// additional flags
			FS_Clustered = 0x00000002,
		};

		TManagedArray<FGeometryCollectionBoneNode>* BoneHierarchyPtr = FindAttribute<FGeometryCollectionBoneNode>("BoneHierarchy", FTransformCollection::TransformGroup);
		if (BoneHierarchyPtr)
		{
			TManagedArray<FGeometryCollectionBoneNode>& BoneHierarchy = *BoneHierarchyPtr;

			for (int Idx = 0; Idx < BoneHierarchy.Num(); Idx++)
			{
				if (!HasAttribute("Level", FGeometryCollection::TransformGroup))
				{
					AddAttribute<int32>("Level", FGeometryCollection::TransformGroup);
				}
				TManagedArray<int32>& Level = GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
				Level[Idx] = BoneHierarchy[Idx].Level;

				SimulationType[Idx] = ESimulationTypes::FST_Rigid;
				StatusFlags[Idx] = FGeometryCollection::ENodeFlags::FS_None;

				if (BoneHierarchy[Idx].StatusFlags & ENodeFlags::FS_Clustered)
				{
					SimulationType[Idx] = ESimulationTypes::FST_Clustered;
				}
				if (BoneHierarchy[Idx].StatusFlags & FGeometryCollection::ENodeFlags::FS_RemoveOnFracture)
				{
					StatusFlags[Idx] |= FGeometryCollection::ENodeFlags::FS_RemoveOnFracture;
				}
			}
		}

		RemoveAttribute("ExplodedTransform", FTransformCollection::TransformGroup);
		RemoveAttribute("ExplodedVector", FTransformCollection::TransformGroup);

		// Version 5 introduced accurate SimulationType tagging
		if (Version < 5)
		{
			UE_LOG(FGeometryCollectionLogging, Warning, TEXT("GeometryCollection is has inaccurate simulation type tags. Updating tags based on transform topology."));
			TManagedArray<bool>* SimulatableParticles = FindAttribute<bool>(FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);
			TArray<bool> RigidChildren; RigidChildren.Init(false,NumElements(FTransformCollection::TransformGroup));
			const TArray<int32> RecursiveOrder = GeometryCollectionAlgo::ComputeRecursiveOrder(*this);
			for (const int32 TransformGroupIndex : RecursiveOrder)
			{
				//SimulationType[TransformGroupIndex] = ESimulationTypes::FST_None;
				SimulationType[TransformGroupIndex] = ESimulationTypes::FST_Rigid;
				
				if(!Children[TransformGroupIndex].Num())
				{ // leaf nodes
					if (TransformToGeometryIndex[TransformGroupIndex] > INDEX_NONE)
					{
						SimulationType[TransformGroupIndex] = ESimulationTypes::FST_Rigid;
					}

					if (SimulatableParticles && !(*SimulatableParticles)[TransformGroupIndex])
					{
						SimulationType[TransformGroupIndex] = ESimulationTypes::FST_None;
					}
				}
				else
				{ // interior nodes
					if (RigidChildren[TransformGroupIndex])
					{
						SimulationType[TransformGroupIndex] = ESimulationTypes::FST_Clustered;
					}
					else if (TransformToGeometryIndex[TransformGroupIndex] > INDEX_NONE)
					{
						SimulationType[TransformGroupIndex] = ESimulationTypes::FST_Rigid;
					}
				}

				if (SimulationType[TransformGroupIndex] != ESimulationTypes::FST_None &&
					Parent[TransformGroupIndex] != INDEX_NONE )
				{
					RigidChildren[Parent[TransformGroupIndex]] = true;
				}

			}
			
			// Structure is conditioned, now considered up to date.
			Version = 5;
		}

	}
}

bool FGeometryCollection::HasContiguousVertices( ) const
{
	int32 NumTransforms = NumElements(FGeometryCollection::TransformGroup);

	TSet<int32> TransformIDs;
	TArray<int32> RecreatedBoneIds;
	RecreatedBoneIds.Init(-1, NumElements(FGeometryCollection::VerticesGroup));
	int32 NumTransformIndex = TransformIndex.Num();
	int32 NumBoneIndex = BoneMap.Num();
	for (int32 GeometryIndex = 0; GeometryIndex < NumTransformIndex; GeometryIndex++)
	{ // for each known geometry...
		int32 TransformIDFromGeometry = TransformIndex[GeometryIndex];
		int32 StartIndex = VertexStart[GeometryIndex];
		int32 NumVertices = VertexCount[GeometryIndex];

		if (TransformIDs.Contains(TransformIDFromGeometry))
		{
			return false;
		}
		TransformIDs.Add(TransformIDFromGeometry);

		int32 Counter = NumVertices;
		for (int32 BoneIndex = 0 ; BoneIndex < NumBoneIndex; ++BoneIndex)
		{ // for each mapping from the vertex to the transform hierarchy ... 
			if (StartIndex <= BoneIndex && BoneIndex < (StartIndex + NumVertices))
			{ // process just the specified range
				int32 TransformIDFromBoneMap = BoneMap[BoneIndex];
				RecreatedBoneIds[BoneIndex] = BoneMap[BoneIndex];
				if (TransformIDFromBoneMap < 0 || NumTransforms <= TransformIDFromBoneMap)
				{ // not contiguous if index is out of range
					return false;
				}
				if (TransformIDFromGeometry != TransformIDFromBoneMap)
				{ // not contiguous if indexing into a different transform
					return false;
				}
				--Counter;
			}
		}

		if (Counter)
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < NumElements(FGeometryCollection::VerticesGroup); ++Index)
	{
		if (RecreatedBoneIds[Index] < 0)
		{
			return false;
		}
	}

	return true;
}


bool FGeometryCollection::HasContiguousFaces() const
{
	int32 TotalNumTransforms = NumElements(FGeometryCollection::TransformGroup);

	// vertices
	int32 TotalNumVertices = NumElements(FGeometryCollection::VerticesGroup);

	int32 NumIndices = Indices.Num();
	int32 NumTransformIndex = TransformIndex.Num();
	for (int32 GeometryIndex = 0; GeometryIndex < NumTransformIndex ; ++GeometryIndex)
	{ // for each known geometry...
		int32 TransformIDFromGeometry = TransformIndex[GeometryIndex];
		int32 StartIndex = FaceStart[GeometryIndex];
		int32 NumFaces = FaceCount[GeometryIndex];

		int32 Counter = NumFaces;
		for (int32 FaceIndex = 0 ; FaceIndex < NumIndices; ++FaceIndex)
		{ // for each mapping from the vertex to the transform hierarchy ... 
			if (StartIndex <= FaceIndex && FaceIndex < (StartIndex + NumFaces))
			{ // process just the specified range
				for (int32 i = 0; i < 3; ++i)
				{
					int32 VertexIndex = Indices[FaceIndex][i];
					if (VertexIndex < 0 || TotalNumVertices <= VertexIndex)
					{
						return false;
					}

					int32 TransformIDFromBoneMap = BoneMap[VertexIndex];

					if (TransformIDFromBoneMap < 0 && TotalNumTransforms < TransformIDFromBoneMap)
					{ // not contiguous if index is out of range
						return false;
					}
					if (TransformIDFromGeometry != TransformIDFromBoneMap)
					{ // not contiguous if indexing into a different transform
						return false;
					}
				}
				--Counter;
			}
		}

		if (Counter)
		{
			return false;
		}
	}
	return true;
}

bool FGeometryCollection::HasContiguousRenderFaces() const
{
	// validate all remapped indexes have their materials ID's grouped an in increasing order
	int LastMaterialID = 0;
	for (int32 IndexIdx = 0, NumElementsFaceGroup = NumElements(FGeometryCollection::FacesGroup); IndexIdx < NumElementsFaceGroup ; ++IndexIdx)
	{
		if (LastMaterialID > MaterialID[MaterialIndex[IndexIdx]])
			return false;
		LastMaterialID = MaterialID[MaterialIndex[IndexIdx]];
	}

	// check sections ranges do all point to a single material
	for (int32 MaterialIdx = 0, NumElementsMaterialGroup = NumElements(FGeometryCollection::MaterialGroup) ; MaterialIdx < NumElementsMaterialGroup ; ++MaterialIdx)
	{
		int first = Sections[MaterialIdx].FirstIndex / 3;
		int last = first + Sections[MaterialIdx].NumTriangles;

		for (int32 IndexIdx = first; IndexIdx < last; ++IndexIdx)
		{
			if ( (MaterialID[MaterialIndex[IndexIdx]]) != MaterialIdx )
				return false;
		}

	}

	return true;
}
FGeometryCollection* FGeometryCollection::NewGeometryCollection(const TArray<float>& RawVertexArray, const TArray<int32>& RawIndicesArray, bool ReverseVertexOrder)
{

	FGeometryCollection* RestCollection = new FGeometryCollection();

	int NumNewVertices = RawVertexArray.Num() / 3;
	int VerticesIndex = RestCollection->AddElements(NumNewVertices, FGeometryCollection::VerticesGroup);
	
	int NumNewIndices = RawIndicesArray.Num() / 3;
	int IndicesIndex = RestCollection->AddElements(NumNewIndices, FGeometryCollection::FacesGroup);
	
	int NumNewParticles = 1; // 1 particle for this geometry structure
	int ParticlesIndex = RestCollection->AddElements(NumNewParticles, FGeometryCollection::TransformGroup);

	TManagedArray<FVector>& Vertices = RestCollection->Vertex;
	TManagedArray<FVector>&  Normals = RestCollection->Normal;
	TManagedArray<FVector>&  TangentU = RestCollection->TangentU;
	TManagedArray<FVector>&  TangentV = RestCollection->TangentV;
	TManagedArray<FVector2D>&  UVs = RestCollection->UV;
	TManagedArray<FLinearColor>&  Colors = RestCollection->Color;
	TManagedArray<FIntVector>&  Indices = RestCollection->Indices;
	TManagedArray<bool>&  Visible = RestCollection->Visible;
	TManagedArray<int32>&  MaterialID = RestCollection->MaterialID;
	TManagedArray<int32>&  MaterialIndex = RestCollection->MaterialIndex;
	TManagedArray<FTransform>&  Transform = RestCollection->Transform;

	// set the vertex information
	FVector TempVertices(0.f, 0.f, 0.f);
	for (int32 Idx = 0; Idx < NumNewVertices; ++Idx)
	{
		Vertices[Idx] = FVector(RawVertexArray[3 * Idx], RawVertexArray[3 * Idx + 1], RawVertexArray[3 * Idx + 2]);
		TempVertices += Vertices[Idx];

		UVs[Idx] = FVector2D(0, 0);
		Colors[Idx] = FLinearColor::White;
	}

	// set the particle information
	TempVertices /= (float)NumNewVertices;
	Transform[0] = FTransform(TempVertices);
	Transform[0].NormalizeRotation();

	// set the index information
	TArray<FVector> FaceNormals;
	FaceNormals.SetNum(NumNewIndices);
	for (int32 Idx = 0; Idx < NumNewIndices; ++Idx)
	{
		int32 VertexIdx1, VertexIdx2, VertexIdx3;
		if (!ReverseVertexOrder)
		{
			VertexIdx1 = RawIndicesArray[3 * Idx];
			VertexIdx2 = RawIndicesArray[3 * Idx + 1];
			VertexIdx3 = RawIndicesArray[3 * Idx + 2];
		}
		else
		{
			VertexIdx1 = RawIndicesArray[3 * Idx];
			VertexIdx2 = RawIndicesArray[3 * Idx + 2];
			VertexIdx3 = RawIndicesArray[3 * Idx + 1];
		}

		Indices[Idx] = FIntVector(VertexIdx1, VertexIdx2, VertexIdx3);
		Visible[Idx] = true;
		MaterialID[Idx] = 0;
		MaterialIndex[Idx] = Idx;

		const FVector Edge1 = Vertices[VertexIdx1] - Vertices[VertexIdx2];
		const FVector Edge2 = Vertices[VertexIdx1] - Vertices[VertexIdx3];
		FaceNormals[Idx] = (Edge2 ^ Edge1).GetSafeNormal();
	}

	// Compute vertexNormals
	TArray<FVector> VertexNormals;
	VertexNormals.SetNum(NumNewVertices);
	for (int32 Idx = 0; Idx < NumNewVertices; ++Idx)
	{
		VertexNormals[Idx] = FVector(0.f, 0.f, 0.f);
	}

	for (int32 Idx = 0; Idx < NumNewIndices; ++Idx)
	{
		VertexNormals[Indices[Idx][0]] += FaceNormals[Idx];
		VertexNormals[Indices[Idx][1]] += FaceNormals[Idx];
		VertexNormals[Indices[Idx][2]] += FaceNormals[Idx];
	}

	for (int32 Idx = 0; Idx < NumNewVertices; ++Idx)
	{
		Normals[Idx] = (VertexNormals[Idx] / 3.f).GetSafeNormal();
	}

	for (int IndexIdx = 0; IndexIdx < NumNewIndices; IndexIdx++)
	{
		FIntVector Tri = Indices[IndexIdx];
		for (int idx = 0; idx < 3; idx++)
		{
			const FVector Normal = Normals[Tri[idx]];
			const FVector Edge = (Vertices[Tri[(idx + 1) % 3]] - Vertices[Tri[idx]]);
			TangentU[Tri[idx]] = (Edge ^ Normal).GetSafeNormal();
			TangentV[Tri[idx]] = (Normal ^ TangentU[Tri[idx]]).GetSafeNormal();
		}
	}

	// Build the Geometry Group
	GeometryCollection::AddGeometryProperties(RestCollection);

	// add a material section
	TManagedArray<FGeometryCollectionSection>&  Sections = RestCollection->Sections;
	int Element = RestCollection->AddElements(1, FGeometryCollection::MaterialGroup);
	Sections[Element].MaterialID = 0;
	Sections[Element].FirstIndex = 0;
	Sections[Element].NumTriangles = Indices.Num();
	Sections[Element].MinVertexIndex = 0;
	Sections[Element].MaxVertexIndex = Vertices.Num() - 1;

	return RestCollection;
}

void FGeometryCollection::WriteDataToHeaderFile(const FString &Name, const FString &Path)
{
	using namespace std;

	static const FString DataFilePath = "D:";
	FString FullPath = (Path.IsEmpty() || Path.Equals(TEXT("None"))) ? DataFilePath : Path;
	FullPath.RemoveFromEnd("\\");
	FullPath += "\\" + Name + ".h";

	ofstream DataFile;
	DataFile.open(string(TCHAR_TO_UTF8(*FullPath)));
	DataFile << "// Copyright Epic Games, Inc. All Rights Reserved." << endl << endl;
	DataFile << "#pragma once" << endl << endl;
	DataFile << "class " << TCHAR_TO_UTF8(*Name) << endl;
	DataFile << "{" << endl;
	DataFile << "public:" << endl;
	DataFile << "    " << TCHAR_TO_UTF8(*Name) << "();" << endl;
	DataFile << "    ~" << TCHAR_TO_UTF8(*Name) << "() {};" << endl << endl;
	DataFile << "    static const TArray<float>	RawVertexArray;" << endl;
	DataFile << "    static const TArray<int32>	RawIndicesArray;" << endl;
	DataFile << "    static const TArray<int32>	RawBoneMapArray;" << endl;
	DataFile << "    static const TArray<FTransform> RawTransformArray;" << endl;
	DataFile << "    static const TArray<int32> RawParentArray;" << endl;
	DataFile << "    static const TArray<TSet<int32>> RawChildrenArray;" << endl;
	DataFile << "    static const TArray<int32> RawSimulationTypeArray;" << endl;
	DataFile << "    static const TArray<int32> RawStatusFlagsArray;" << endl;
	DataFile << "};" << endl << endl;
	DataFile << "const TArray<float> " << TCHAR_TO_UTF8(*Name) << "::RawVertexArray = {" << endl;

	int32 NumVertices = NumElements(FGeometryCollection::VerticesGroup);
	const TManagedArray<FVector>& VertexArray = Vertex;
	for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
	{
		DataFile << "                                                    " <<
			VertexArray[IdxVertex].X << ", " <<
			VertexArray[IdxVertex].Y << ", " <<
			VertexArray[IdxVertex].Z << ", " << endl;
	}
	DataFile << "};" << endl << endl;
	DataFile << "const TArray<int32> " << TCHAR_TO_UTF8(*Name) << "::RawIndicesArray = {" << endl;

	int32 NumFaces = NumElements(FGeometryCollection::FacesGroup);
	for (int32 IdxFace = 0; IdxFace < NumFaces; ++IdxFace)
	{
		DataFile << "                                                    " <<
			Indices[IdxFace].X << ", " <<
			Indices[IdxFace].Y << ", " <<
			Indices[IdxFace].Z << ", " << endl;
	}

	DataFile << "};" << endl << endl;
	DataFile << "const TArray<int32> " << TCHAR_TO_UTF8(*Name) << "::RawBoneMapArray = {" << endl;

	for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
	{
		DataFile << "                                                    " <<
			BoneMap[IdxVertex] << ", " << endl;
	}
	DataFile << "};" << endl << endl;

	DataFile << "const TArray<FTransform> " << TCHAR_TO_UTF8(*Name) << "::RawTransformArray = {" << endl;

	int32 NumTransforms = NumElements(FGeometryCollection::TransformGroup);
	const TManagedArray<FTransform>& TransformArray = Transform;
	for (int32 IdxTransform = 0; IdxTransform < NumTransforms; ++IdxTransform)
	{
		FQuat Rotation = TransformArray[IdxTransform].GetRotation();
		FVector Translation = TransformArray[IdxTransform].GetTranslation();
		FVector Scale3D = TransformArray[IdxTransform].GetScale3D();

		DataFile << "   FTransform(FQuat(" <<
			Rotation.X << ", " <<
			Rotation.Y << ", " <<
			Rotation.Z << ", " <<
			Rotation.W << "), " <<
			"FVector(" <<
			Translation.X << ", " <<
			Translation.Y << ", " <<
			Translation.Z << "), " <<
			"FVector(" <<
			Scale3D.X << ", " <<
			Scale3D.Y << ", " <<
			Scale3D.Z << ")), " << endl;
	}
	DataFile << "};" << endl << endl;

	// Write BoneHierarchy array
	DataFile << "const TArray<FGeometryCollectionBoneNode> " << TCHAR_TO_UTF8(*Name) << "::RawBoneHierarchyArray = {" << endl;

	for (int32 IdxTransform = 0; IdxTransform < NumTransforms; ++IdxTransform)
	{
		DataFile << "   FGeometryCollectionBoneNode(" <<
			Parent[IdxTransform] << ", " <<
			SimulationType[IdxTransform] << "), " <<
			StatusFlags[IdxTransform] << "), " << endl;
	}

	DataFile << "};" << endl << endl;
	DataFile.close();
}

void FGeometryCollection::WriteDataToOBJFile(const FString &Name, const FString &Path, const bool WriteTopology, const bool WriteAuxStructures)
{
	using namespace std;

	static const FString DataFilePath = "D:";

	int32 NumVertices = NumElements(FGeometryCollection::VerticesGroup);
	int32 NumFaces = NumElements(FGeometryCollection::FacesGroup);

	TArray<FTransform> GlobalTransformArray;
	GeometryCollectionAlgo::GlobalMatrices(Transform, Parent, GlobalTransformArray);

	TArray<FVector> VertexInWorldArray;
	VertexInWorldArray.SetNum(NumVertices);

	for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
	{
		FTransform LocalTransform = GlobalTransformArray[BoneMap[IdxVertex]];
		FVector VertexInWorld = LocalTransform.TransformPosition(Vertex[IdxVertex]);

		VertexInWorldArray[IdxVertex] = VertexInWorld;
	}

	ofstream DataFile;
	if (WriteTopology)
	{
		FString FullPath = (Path.IsEmpty() || Path.Equals(TEXT("None"))) ? DataFilePath : Path;
		FullPath.RemoveFromEnd("\\");
		FullPath += "\\" + Name + ".obj";

		DataFile.open(string(TCHAR_TO_UTF8(*FullPath)));

		DataFile << "# File exported from UE4" << endl;
		DataFile << "# " << NumVertices << " points" << endl;
		DataFile << "# " << NumVertices * 3 << " vertices" << endl;
		DataFile << "# " << NumFaces << " primitives" << endl;
		DataFile << "g" << endl;
		for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
		{
			DataFile << "v " << VertexInWorldArray[IdxVertex].X << " " <<
				VertexInWorldArray[IdxVertex].Y << " " <<
				VertexInWorldArray[IdxVertex].Z << endl;
		}
		DataFile << "g" << endl;

		// FaceIndex in the OBJ format starts with 1
		for (int32 IdxFace = 0; IdxFace < NumFaces; ++IdxFace)
		{
			DataFile << "f " << Indices[IdxFace].X + 1 << " " <<
				Indices[IdxFace].Z + 1 << " " <<
				Indices[IdxFace].Y + 1 << endl;
		}
		DataFile << endl;
		DataFile.close();
	}
	if(WriteAuxStructures && HasAttribute("VertexVisibility", FGeometryCollection::VerticesGroup))
	{
		FString FullPath = (Path.IsEmpty() || Path.Equals(TEXT("None"))) ? DataFilePath : Path;
		FullPath.RemoveFromEnd("\\");
		FullPath += "\\" + Name + "_VertexVisibility.obj";

		DataFile.open(string(TCHAR_TO_UTF8(*FullPath)));
		DataFile << "# Vertex Visibility - vertices whose visibility flag are true" << endl;

		TManagedArray<bool>& VertexVisibility = GetAttribute<bool>("VertexVisibility", FGeometryCollection::VerticesGroup);
		int num = 0;
		for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
		{
			if (VertexVisibility[IdxVertex])
			{
				num++;
			}
		}
		DataFile << "# " << num << " Vertices" << endl;

		DataFile << "g" << endl;
		for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
		{
			if(VertexVisibility[IdxVertex])
			{
				DataFile << "v " 
					<< VertexInWorldArray[IdxVertex].X << " " 
					<< VertexInWorldArray[IdxVertex].Y << " " 
					<< VertexInWorldArray[IdxVertex].Z << endl;
			}
		}
		DataFile << endl;
		DataFile.close();
	}
}

FGeometryCollection* FGeometryCollection::NewGeometryCollection(const TArray<float>& RawVertexArray,
																const TArray<int32>& RawIndicesArray,
																const TArray<int32>& RawBoneMapArray,
																const TArray<FTransform>& RawTransformArray,
																const TManagedArray<int32>& RawLevelArray,
																const TManagedArray<int32>& RawParentArray,
																const TManagedArray<TSet<int32>>& RawChildrenArray,
																const TManagedArray<int32>& RawSimulationTypeArray,
															    const TManagedArray<int32>& RawStatusFlagsArray)
{
	FGeometryCollection* RestCollection = new FGeometryCollection();

	int NumNewVertices = RawVertexArray.Num() / 3;
	int VerticesIndex = RestCollection->AddElements(NumNewVertices, FGeometryCollection::VerticesGroup);

	int NumNewIndices = RawIndicesArray.Num() / 3;
	int IndicesIndex = RestCollection->AddElements(NumNewIndices, FGeometryCollection::FacesGroup);

	TManagedArray<FVector>& Vertices = RestCollection->Vertex;
	TManagedArray<FVector>&  Normals = RestCollection->Normal;
	TManagedArray<FVector>&  TangentU = RestCollection->TangentU;
	TManagedArray<FVector>&  TangentV = RestCollection->TangentV;
	TManagedArray<FVector2D>&  UVs = RestCollection->UV;
	TManagedArray<FLinearColor>&  Colors = RestCollection->Color;
	TManagedArray<int32>& BoneMap = RestCollection->BoneMap;
	TManagedArray<FIntVector>&  Indices = RestCollection->Indices;
	TManagedArray<bool>&  Visible = RestCollection->Visible;
	TManagedArray<int32>&  MaterialID = RestCollection->MaterialID;
	TManagedArray<int32>&  MaterialIndex = RestCollection->MaterialIndex;
	TManagedArray<FTransform>&  Transform = RestCollection->Transform;
	TManagedArray<int32>& Parent = RestCollection->Parent;
	TManagedArray<TSet<int32>>& Children = RestCollection->Children;
	TManagedArray<int32>& SimulationType = RestCollection->SimulationType;
	TManagedArray<int32>& StatusFlags = RestCollection->StatusFlags;
	TManagedArray<int32>& InitialDynamicState = RestCollection->InitialDynamicState;

	// set the vertex information
	for (int32 Idx = 0; Idx < NumNewVertices; ++Idx)
	{
		Vertices[Idx] = FVector(RawVertexArray[3 * Idx], RawVertexArray[3 * Idx + 1], RawVertexArray[3 * Idx + 2]);
		BoneMap[Idx] = RawBoneMapArray[Idx];

		UVs[Idx] = FVector2D(0, 0);
		Colors[Idx] = FLinearColor::White;
	}

	// Transforms
	int NumNewTransforms = RawTransformArray.Num(); // 1 particle for this geometry structure
	int TransformIndex = RestCollection->AddElements(NumNewTransforms, FGeometryCollection::TransformGroup);

	for (int32 Idx = 0; Idx < NumNewTransforms; ++Idx)
	{
		Transform[Idx] = RawTransformArray[Idx];
		Transform[Idx].NormalizeRotation();

		Parent[Idx] = RawParentArray[Idx];
		if (RawChildrenArray.Num() > 0)
		{
			Children[Idx] = RawChildrenArray[Idx];
		}
		SimulationType[Idx] = RawSimulationTypeArray[Idx];
		StatusFlags[Idx] = RawStatusFlagsArray[Idx];

		for (int32 Idx1 = 0; Idx1 < NumNewTransforms; ++Idx1)
		{
			if (RawParentArray[Idx1] == Idx)
			{
				Children[Idx].Add(Idx1);
			}
		}

	}

	// set the index information
	TArray<FVector> FaceNormals;
	FaceNormals.SetNum(NumNewIndices);
	for (int32 Idx = 0; Idx < NumNewIndices; ++Idx)
	{
		int32 VertexIdx1, VertexIdx2, VertexIdx3;
		VertexIdx1 = RawIndicesArray[3 * Idx];
		VertexIdx2 = RawIndicesArray[3 * Idx + 1];
		VertexIdx3 = RawIndicesArray[3 * Idx + 2];

		Indices[Idx] = FIntVector(VertexIdx1, VertexIdx2, VertexIdx3);
		Visible[Idx] = true;
		MaterialID[Idx] = 0;
		MaterialIndex[Idx] = Idx;

		const FVector Edge1 = Vertices[VertexIdx1] - Vertices[VertexIdx2];
		const FVector Edge2 = Vertices[VertexIdx1] - Vertices[VertexIdx3];
		FaceNormals[Idx] = (Edge2 ^ Edge1).GetSafeNormal();
	}

	// Compute vertexNormals
	TArray<FVector> VertexNormals;
	VertexNormals.SetNum(NumNewVertices);
	for (int32 Idx = 0; Idx < NumNewVertices; ++Idx)
	{
		VertexNormals[Idx] = FVector(0.f, 0.f, 0.f);
	}

	for (int32 Idx = 0; Idx < NumNewIndices; ++Idx)
	{
		VertexNormals[Indices[Idx][0]] += FaceNormals[Idx];
		VertexNormals[Indices[Idx][1]] += FaceNormals[Idx];
		VertexNormals[Indices[Idx][2]] += FaceNormals[Idx];
	}

	for (int32 Idx = 0; Idx < NumNewVertices; ++Idx)
	{
		Normals[Idx] = (VertexNormals[Idx] / 3.f).GetSafeNormal();
	}

	for (int IndexIdx = 0; IndexIdx < NumNewIndices; IndexIdx++)
	{
		FIntVector Tri = Indices[IndexIdx];
		for (int idx = 0; idx < 3; idx++)
		{
			const FVector Normal = Normals[Tri[idx]];
			const FVector Edge = (Vertices[Tri[(idx + 1) % 3]] - Vertices[Tri[idx]]);
			TangentU[Tri[idx]] = (Edge ^ Normal).GetSafeNormal();
			TangentV[Tri[idx]] = (Normal ^ TangentU[Tri[idx]]).GetSafeNormal();
		}
	}

	// Build the Geometry Group
	GeometryCollection::AddGeometryProperties(RestCollection);

	FGeometryCollectionProximityUtility::UpdateProximity(RestCollection);

	// add a material section
	TManagedArray<FGeometryCollectionSection>&  Sections = RestCollection->Sections;
	int Element = RestCollection->AddElements(1, FGeometryCollection::MaterialGroup);
	Sections[Element].MaterialID = 0;
	Sections[Element].FirstIndex = 0;
	Sections[Element].NumTriangles = Indices.Num();
	Sections[Element].MinVertexIndex = 0;
	Sections[Element].MaxVertexIndex = Vertices.Num() - 1;

	return RestCollection;
}


TArray<TArray<int32>> FGeometryCollection::ConnectionGraph()
{
	int32 NumTransforms = NumElements(TransformGroup);

	TArray<TArray<int32>> Connectivity;
	Connectivity.Init(TArray<int32>(), NumTransforms);

	TArray<FTransform> GlobalMatrices;
	GeometryCollectionAlgo::GlobalMatrices(Transform, Parent, GlobalMatrices);

	TArray<FVector> Pts;
	TMap< int32,int32> Remap;
	for (int32 TransformGroupIndex = 0; TransformGroupIndex < NumTransforms; ++TransformGroupIndex)
	{
		if (IsGeometry(TransformGroupIndex))
		{
			Remap.Add(Pts.Num(), TransformGroupIndex);
			Pts.Add(GlobalMatrices[TransformGroupIndex].GetTranslation());
		}
	}

	TArray<TArray<int>> Neighbors;
	VoronoiNeighbors(Pts, Neighbors);

	for (int i = 0; i < Neighbors.Num(); i++)
	{
		for (int j = 0; j < Neighbors[i].Num(); j++)
		{
			Connectivity[Remap[i]].Add(Remap[Neighbors[i][j]]);
		}
	}
	return Connectivity;
}

void FGeometryCollection::UpdateOldAttributeNames()
{

	// Faces Group
	int32 NumOldGeometryElements = this->NumElements("Geometry");
	check(!this->NumElements(FGeometryCollection::FacesGroup));
	this->AddElements(NumOldGeometryElements, FGeometryCollection::FacesGroup);

	TArray<FName> GeometryAttributes = this->AttributeNames("Geometry");

	const TManagedArray<FIntVector> & OldIndices = this->GetAttribute<FIntVector>("Indices", "Geometry");
	const TManagedArray<bool> & OldVisible = this->GetAttribute<bool>("Visible", "Geometry");
	const TManagedArray<int32> & OldMaterialIndex = this->GetAttribute<int32>("MaterialIndex", "Geometry");
	const TManagedArray<int32> & OldMaterialID = this->GetAttribute<int32>("MaterialID", "Geometry");
	for (int i = NumOldGeometryElements - 1; 0 <= i; i--)
	{
		this->Indices[i] = OldIndices[i];
		this->Visible[i] = OldVisible[i];
		this->MaterialIndex[i] = OldMaterialIndex[i];
		this->MaterialID[i] = OldMaterialID[i];
	}
	this->RemoveAttribute("Indices", "Geometry");
	this->RemoveAttribute("Visible", "Geometry");
	this->RemoveAttribute("MaterialIndex", "Geometry");
	this->RemoveAttribute("MaterialID", "Geometry");

	// reset the geometry group
	TArray<int32> DelArray;
	GeometryCollectionAlgo::ContiguousArray(DelArray, NumOldGeometryElements);
	FManagedArrayCollection::FProcessingParameters Params;
	Params.bDoValidation = false;
	Params.bReindexDependentAttibutes = false;
	Super::RemoveElements("Geometry", DelArray, Params);


	// Geometry Group
	TArray<FName> StructureAttributes = this->AttributeNames("Structure");

	int32 NumOldStructureElements = this->NumElements("Structure");
	check(!this->NumElements(FGeometryCollection::GeometryGroup));
	this->AddElements(NumOldStructureElements, FGeometryCollection::GeometryGroup);

	const TManagedArray<int32> & OldTransformIndex = this->GetAttribute<int32>("TransformIndex", "Structure");
	const TManagedArray<FBox> & OldBoundingBox = this->GetAttribute<FBox>("BoundingBox", "Structure");
	const TManagedArray<float> & OldInnerRadius = this->GetAttribute<float>("InnerRadius", "Structure");
	const TManagedArray<float> & OldOuterRadius = this->GetAttribute<float>("OuterRadius", "Structure");
	const TManagedArray<int32> & OldVertexStart = this->GetAttribute<int32>("VertexStart", "Structure");
	const TManagedArray<int32> & OldVertexCount = this->GetAttribute<int32>("VertexCount", "Structure");
	const TManagedArray<int32> & OldFaceStart = this->GetAttribute<int32>("FaceStart", "Structure");
	const TManagedArray<int32> & OldFaceCount = this->GetAttribute<int32>("FaceCount", "Structure");
	for (int i = NumOldStructureElements - 1; 0 <= i; i--)
	{
		this->TransformIndex[i] = OldTransformIndex[i];
		this->BoundingBox[i] = OldBoundingBox[i];
		this->InnerRadius[i] = OldInnerRadius[i];
		this->OuterRadius[i] = OldOuterRadius[i];
		this->VertexStart[i] = OldVertexStart[i];
		this->VertexCount[i] = OldVertexCount[i];
		this->FaceStart[i] = OldFaceStart[i];
		this->FaceCount[i] = OldFaceCount[i];
	}
	this->RemoveGroup("Structure");
}