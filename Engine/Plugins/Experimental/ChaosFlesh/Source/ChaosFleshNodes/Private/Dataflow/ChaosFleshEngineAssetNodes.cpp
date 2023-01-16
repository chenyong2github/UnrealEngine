// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshEngineAssetNodes.h"

#include "Chaos/Math/Poisson.h"
#include "ChaosFlesh/FleshAsset.h"
#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/FleshCollectionUtility.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "GeometryCollection/TransformCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosFleshEngineAssetNodes)

DEFINE_LOG_CATEGORY(LogChaosFlesh);

namespace Dataflow
{
	void RegisterChaosFleshEngineAssetNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetFleshAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFleshAssetTerminalDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FImportFleshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FComputeFleshMassNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FComputeFiberFieldNode);
	}
}

void FGetFleshAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Output))
	{
		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			if (UFleshAsset* FleshAsset = Cast<UFleshAsset>(EngineContext->Owner))
			{
				if (const FFleshCollection* AssetCollection = FleshAsset->GetCollection())
				{
					SetValue<FManagedArrayCollection>(Context, *AssetCollection, &Output);
				}
			}
		}
	}
}

void FFleshAssetTerminalDataflowNode::SetAssetValue(TObjectPtr<UObject> Asset, Dataflow::FContext& Context) const
{
	if (UFleshAsset* FleshAsset = Cast<UFleshAsset>(Asset.Get()))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FleshAsset->SetCollection(InCollection.NewCopy<FFleshCollection>());
	}
}

void FFleshAssetTerminalDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
	SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
}

void FImportFleshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FString TempFileName = Filename.FilePath;
		TempFileName = TempFileName.Replace(TEXT("\\"), TEXT("/"));
		if (TUniquePtr<FFleshCollection> FleshCollection = ChaosFlesh::ImportTetFromFile(TempFileName))
		{
			SetValue<FManagedArrayCollection>(Context, *FleshCollection.Release(), &Collection);
		}
		else
		{
			SetValue<FManagedArrayCollection>(Context, Collection, &Collection);
		}
	}
}


template <class T> using MType = FManagedArrayCollection::TManagedType<T>;

void FComputeFleshMassNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (InCollection.HasAttributes({
			MType< float >("Mass", FGeometryCollection::VerticesGroup),
			MType< FIntVector4 >(FTetrahedralCollection::TetrahedronAttribute, FTetrahedralCollection::TetrahedralGroup),
			MType< FVector3f >("Vertex", "Vertices"),
			MType< TArray<int32> >(FTetrahedralCollection::IncidentElementsAttribute, FGeometryCollection::VerticesGroup),
			MType< TArray<int32> >(FTetrahedralCollection::IncidentElementsLocalIndexAttribute, FGeometryCollection::VerticesGroup) }))
		{

			int32 VertsNum = InCollection.NumElements(FGeometryCollection::VerticesGroup);
			int32 TetsNum = InCollection.NumElements(FTetrahedralCollection::TetrahedralGroup);
			if (VertsNum && TetsNum)
			{
				TManagedArray<float>& Mass = InCollection.ModifyAttribute<float>("Mass", FGeometryCollection::VerticesGroup);
				const TManagedArray<FIntVector4>& Tetrahedron = InCollection.GetAttribute<FIntVector4>(FTetrahedralCollection::TetrahedronAttribute, FTetrahedralCollection::TetrahedralGroup);
				const TManagedArray<FVector3f>& Vertex = InCollection.GetAttribute<FVector3f>("Vertex", "Vertices");
				const TManagedArray<TArray<int32>>& ElemVertexIndex = InCollection.GetAttribute<TArray<int32>>(FTetrahedralCollection::IncidentElementsAttribute, FGeometryCollection::VerticesGroup);
				const TManagedArray<TArray<int32>>& ElemLocalIndex = InCollection.GetAttribute<TArray<int32>>(FTetrahedralCollection::IncidentElementsLocalIndexAttribute, FGeometryCollection::VerticesGroup);

				double Volume = 0.0;
				TArray<float> ElementMass, Measure;
				ElementMass.Init(0.f, 4 * TetsNum);
				Measure.Init(0.f, TetsNum);
				for (int e = 0; e < TetsNum; e++)
				{
					FVector3f X0 = Vertex[Tetrahedron[e][0]];
					FVector3f X1 = Vertex[Tetrahedron[e][1]];
					FVector3f X2 = Vertex[Tetrahedron[e][2]];
					FVector3f X3 = Vertex[Tetrahedron[e][3]];
					Measure[e] = ((X1 - X0).Dot(FVector3f::CrossProduct(X3 - X0, X2 - X0))) / 6.f;
					if (Measure[e] < 0.f)
					{
						Measure[e] = -Measure[e];
					}
					Volume += Measure[e];
				}
				for (int32 e = 0; e < TetsNum; e++)
				{
					float ElementNodalMass = Density * Measure[e] / 4.f;
					for (int i = 0; i < 4; i++)
					{
						ElementMass[4 * e + i] = ElementNodalMass;
					}
				}

				bool WasMassSet = false;
				for (int32 i = 0; i < ElemVertexIndex.Num(); i++)
				{
					if (ensure(ElemVertexIndex[i].Num() == ElemLocalIndex[i].Num()))
					{
						for (int32 j = 0; j < ElemVertexIndex[i].Num(); j++)
						{
							int32 TetIndex = ElemVertexIndex[i][j];
							if (0 <= TetIndex && TetIndex < TetsNum)
							{
								int32 MassIndex = Tetrahedron[TetIndex][ElemLocalIndex[i][j]];
								if (0 <= MassIndex && MassIndex < VertsNum)
								{
									WasMassSet = true;
									Mass[MassIndex] += ElementMass[ElemVertexIndex[i][j] * 4 + ElemLocalIndex[i][j]];
								}
							}
						}
					}
				}
				if (!WasMassSet && Vertex.Num())
				{
					Mass.Fill(Density * Volume / Vertex.Num());
				}
			}
		}
		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}

void FComputeFiberFieldNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		//
		// Gather inputs
		//

		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		TArray<int32> InOriginIndices;// = GetValue<TArray<int32>>(Context, &OriginIndices);
		TArray<int32> InInsertionIndices;// = GetValue<TArray<int32>>(Context, &InsertionIndices);

		// Tetrahedra
		TManagedArray<FIntVector4>* Elements = InCollection.FindAttribute<FIntVector4>(
			FTetrahedralCollection::TetrahedronAttribute, FTetrahedralCollection::TetrahedralGroup);
		if (!Elements)
		{
			UE_LOG(LogChaosFlesh, Warning,
				TEXT("ComputeFiberFieldNode: Failed to find geometry collection attr '%s' in group '%s'"),
				*FTetrahedralCollection::TetrahedronAttribute.ToString(), *FTetrahedralCollection::TetrahedralGroup.ToString());
			Out->SetValue<FManagedArrayCollection>(InCollection, Context);
			return;
		}

		// Vertices
		TManagedArray<FVector3f>* Vertex = InCollection.FindAttribute<FVector3f>("Vertex", "Vertices");
		if (!Vertex)
		{
			UE_LOG(LogChaosFlesh, Warning,
				TEXT("ComputeFiberFieldNode: Failed to find geometry collection attr 'Vertex' in group 'Vertices'"));
			Out->SetValue<FManagedArrayCollection>(InCollection, Context);
			return;
		}

		// Incident elements
		TManagedArray<TArray<int32>>* IncidentElements = InCollection.FindAttribute<TArray<int32>>(
			FTetrahedralCollection::IncidentElementsAttribute, FGeometryCollection::VerticesGroup);
		if (!IncidentElements)
		{
			UE_LOG(LogChaosFlesh, Warning,
				TEXT("ComputeFiberFieldNode: Failed to find geometry collection attr '%s' in group '%s'"),
				*FTetrahedralCollection::IncidentElementsAttribute.ToString(), *FGeometryCollection::VerticesGroup.ToString());
			Out->SetValue<FManagedArrayCollection>(InCollection, Context);
			return;
		}
		TManagedArray<TArray<int32>>* IncidentElementsLocalIndex = InCollection.FindAttribute<TArray<int32>>(
			FTetrahedralCollection::IncidentElementsLocalIndexAttribute, FGeometryCollection::VerticesGroup);
		if (!IncidentElementsLocalIndex)
		{
			UE_LOG(LogChaosFlesh, Warning,
				TEXT("ComputeFiberFieldNode: Failed to find geometry collection attr '%s' in group '%s'"),
				*FTetrahedralCollection::IncidentElementsLocalIndexAttribute.ToString(), *FGeometryCollection::VerticesGroup.ToString());
			Out->SetValue<FManagedArrayCollection>(InCollection, Context);
			return;
		}

		//
		// Pull Origin & Insertion data out of the geometry collection.  We may want other ways of specifying
		// these via an input on the node...
		//

		// Origin & Insertion
		TManagedArray<int32>* Origin = nullptr; 
		TManagedArray<int32>* Insertion = nullptr;
		if (InOriginIndices.IsEmpty() || InInsertionIndices.IsEmpty())
		{
			// Origin & Insertion group
			if (OriginInsertionGroupName.IsEmpty())
			{
				UE_LOG(LogChaosFlesh, Warning, TEXT("ComputeFiberFieldNode: Attr 'OriginInsertionGroupName' cannot be empty."));
				Out->SetValue<FManagedArrayCollection>(InCollection, Context);
				return;
			}

			// Origin vertices
			if (InOriginIndices.IsEmpty())
			{
				if (OriginVertexFieldName.IsEmpty())
				{
					UE_LOG(LogChaosFlesh, Warning, TEXT("ComputeFiberFieldNode: Attr 'OriginVertexFieldName' cannot be empty."));
					Out->SetValue<FManagedArrayCollection>(InCollection, Context);
					return;
				}
				Origin = InCollection.FindAttribute<int32>(FName(OriginVertexFieldName), FName(OriginInsertionGroupName));
				if (!Origin)
				{
					UE_LOG(LogChaosFlesh, Warning,
						TEXT("ComputeFiberFieldNode: Failed to find geometry collection attr '%s' in group '%s'"),
						*OriginVertexFieldName, *OriginInsertionGroupName);
					Out->SetValue<FManagedArrayCollection>(InCollection, Context);
					return;
				}
			}

			// Insertion vertices
			if (InInsertionIndices.IsEmpty())
			{
				if (InsertionVertexFieldName.IsEmpty())
				{
					UE_LOG(LogChaosFlesh, Warning, TEXT("ComputeFiberFieldNode: Attr 'InsertionVertexFieldName' cannot be empty."));
					Out->SetValue<FManagedArrayCollection>(InCollection, Context);
					return;
				}
				Insertion = InCollection.FindAttribute<int32>(FName(InsertionVertexFieldName), FName(OriginInsertionGroupName));
				if (!Insertion)
				{
					UE_LOG(LogChaosFlesh, Warning,
						TEXT("ComputeFiberFieldNode: Failed to find geometry collection attr '%s' in group '%s'"),
						*InsertionVertexFieldName, *OriginInsertionGroupName);
					Out->SetValue<FManagedArrayCollection>(InCollection, Context);
					return;
				}
			}
		}

		//
		// Do the thing
		//

		TArray<FVector3f> FiberDirs =
			ComputeFiberField(*Elements, *Vertex, *IncidentElements, *IncidentElementsLocalIndex, 
				Origin ? Origin->GetConstArray() : InOriginIndices,
				Insertion ? Insertion->GetConstArray() : InInsertionIndices);

		//
		// Set output(s)
		//

		TManagedArray<FVector3f>* FiberDirections =
			InCollection.FindAttribute<FVector3f>("FiberDirection", FTetrahedralCollection::TetrahedralGroup);
		if (!FiberDirections)
		{
			FiberDirections =
				&InCollection.AddAttribute<FVector3f>("FiberDirection", FTetrahedralCollection::TetrahedralGroup);
		}
		(*FiberDirections) = MoveTemp(FiberDirs);

		Out->SetValue<FManagedArrayCollection>(InCollection, Context);
	}
}


TArray<int32> 
FComputeFiberFieldNode::GetNonZeroIndices(const TArray<uint8>& Map) const
{
	int32 NumNonZero = 0;
	for (int32 i = 0; i < Map.Num(); i++)
		if (Map[i])
			NumNonZero++;
	TArray<int32> Indices; Indices.AddUninitialized(NumNonZero);
	int32 Idx = 0;
	for (int32 i = 0; i < Map.Num(); i++)
		if (Map[i])
			Indices[Idx++] = i;
	return Indices;
}

TArray<FVector3f>
FComputeFiberFieldNode::ComputeFiberField(
	const TManagedArray<FIntVector4>& Elements,
	const TManagedArray<FVector3f>& Vertex,
	const TManagedArray<TArray<int32>>& IncidentElements,
	const TManagedArray<TArray<int32>>& IncidentElementsLocalIndex,
	const TArray<int32>& Origin,
	const TArray<int32>& Insertion) const
{
	TArray<FVector3f> Directions;
	Chaos::ComputeFiberField<float>(
		Elements.GetConstArray(),
		Vertex.GetConstArray(),
		IncidentElements.GetConstArray(),
		IncidentElementsLocalIndex.GetConstArray(),
		Origin,
		Insertion,
		Directions,
		MaxIterations,
		Tolerance);
	return Directions;
}
