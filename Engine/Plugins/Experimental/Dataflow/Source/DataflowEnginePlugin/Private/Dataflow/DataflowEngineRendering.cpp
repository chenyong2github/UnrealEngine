// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEngineRendering.h"

#include "Dataflow/DataflowRenderingFactory.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

namespace Dataflow
{

	void RenderingCallbacks()
	{
		using namespace Dataflow;

		/**
		* DatflowNode (FGeometryCollection) Rendering
		*
		*		@param Type : FGeometryCollection::StaticType()

		*		@param Outputs : {FManagedArrayCollection : "Collection"}
		*/
		FRenderingFactory::GetInstance()->RegisterOutput(FGeometryCollection::StaticType(),
			[](GeometryCollection::Facades::FRenderingFacade& RenderCollection, const Dataflow::FGraphRenderingState& State)
			{
				if (State.GetRenderOutputs().Num())
				{
					FName PrimaryOutput = State.GetRenderOutputs()[0]; // "Collection"

					FManagedArrayCollection Default;
					const FManagedArrayCollection& Collection = State.GetValue<FManagedArrayCollection>(PrimaryOutput, Default);

					if (Collection.FindAttributeTyped<FIntVector>("Indices", FGeometryCollection::FacesGroup)
						&& Collection.FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup)
						&& Collection.FindAttributeTyped<FTransform>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup)
						&& Collection.FindAttributeTyped<int32>("BoneMap", FGeometryCollection::VerticesGroup)
						&& Collection.FindAttributeTyped<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup))
					{
						const TManagedArray<int32>& BoneIndex = Collection.GetAttribute<int32>("BoneMap", FGeometryCollection::VerticesGroup);
						const TManagedArray<int32>& Parents = Collection.GetAttribute<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup);
						const TManagedArray<FTransform>& Transforms = Collection.GetAttribute<FTransform>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup);

						TArray<FTransform> M;
						GeometryCollectionAlgo::GlobalMatrices(Transforms, Parents, M);

						auto ToD = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
						auto ToF = [](FVector3d V) { return FVector3f(V.X, V.Y, V.Z); };


						const TManagedArray<FVector3f>& Vertex = Collection.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
						const TManagedArray<FIntVector>& Faces = Collection.GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);

						TArray<FVector3f> Vertices; Vertices.AddUninitialized(Vertex.Num());
						TArray<FIntVector> Tris; Tris.AddUninitialized(Faces.Num());
						TArray<bool> Visited; Visited.Init(false, Vertices.Num());

						int32 Tdx = 0;
						for (const FIntVector& Face : Faces)
						{
							FIntVector Tri = FIntVector(Face[0], Face[1], Face[2]);
							FTransform Ms[3] = { M[BoneIndex[Tri[0]]], M[BoneIndex[Tri[1]]], M[BoneIndex[Tri[2]]] };

							Tris[Tdx++] = Tri;
							if (!Visited[Tri[0]]) Vertices[Tri[0]] = ToF(Ms[0].TransformPosition(ToD(Vertex[Tri[0]])));
							if (!Visited[Tri[1]]) Vertices[Tri[1]] = ToF(Ms[1].TransformPosition(ToD(Vertex[Tri[1]])));
							if (!Visited[Tri[2]]) Vertices[Tri[2]] = ToF(Ms[2].TransformPosition(ToD(Vertex[Tri[2]])));

							Visited[Tri[0]] = true; Visited[Tri[1]] = true; Visited[Tri[2]] = true;
						}

						// Maybe these buffers should be shrunk, but there are unused vertices in the buffer. 
						for (int i = 0; i < Visited.Num(); i++) if (!Visited[i]) Vertices[i] = FVector3f(0);

						RenderCollection.AddSurface(MoveTemp(Vertices), MoveTemp(Tris));
					}
				}
			});

	}


}
