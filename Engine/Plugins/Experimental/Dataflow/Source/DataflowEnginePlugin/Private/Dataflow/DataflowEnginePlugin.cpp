// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEnginePlugin.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"


class FDataflowEnginePlugin : public IDataflowEnginePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FDataflowEnginePlugin, FleshEngine )



void FDataflowEnginePlugin::StartupModule()
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

					const TManagedArray<FVector3f>& Vertex = Collection.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
					const TManagedArray<FIntVector>& Faces = Collection.GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
					for (const FIntVector& Face : Faces)
					{
						FTransform Ms[3] = { M[BoneIndex[Face[0]]], M[BoneIndex[Face[1]]], M[BoneIndex[Face[2]]] };
						RenderCollection.AddTriangle(Chaos::FTriangle(Ms[0].TransformPosition(ToD(Vertex[Face[0]])),
							Ms[1].TransformPosition(ToD(Vertex[Face[1]])), Ms[2].TransformPosition(ToD(Vertex[Face[2]]))));
					}
				}
			}
		});

}


void FDataflowEnginePlugin::ShutdownModule()
{
	
}



