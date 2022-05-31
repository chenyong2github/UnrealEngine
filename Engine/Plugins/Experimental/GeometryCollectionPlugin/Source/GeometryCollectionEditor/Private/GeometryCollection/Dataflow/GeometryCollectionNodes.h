// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "GeometryCollectionNodeConnectionTypes.h"

#include "CoreMinimal.h"
#include "GeometryCollection/Dataflow/GeometryCollectionEditorToolkit.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowProperty.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"


namespace Dataflow
{

	class GetCollectionAssetNode : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(GetCollectionAssetNode)

	public:
		typedef FManagedArrayCollectionSharedPtr DataType;

		TSharedPtr< class TOutput<DataType> > Output;

		GetCollectionAssetNode(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, Output(new TOutput<DataType>(TOutputParameters<DataType>({ FName("CollectionOut"), this })))
		{}

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override
		{
			if (Output.Get() == Out)
			{
				if (const FEngineContext* EngineContext = Context.AsType<FEngineContext>(FName("UGeometryCollection")))
				{
					if (UGeometryCollection* CollectionAsset = Cast<UGeometryCollection>(EngineContext->Owner))
					{
						if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> AssetCollection = CollectionAsset->GetGeometryCollection())
						{
							FManagedArrayCollection* NewCollection = AssetCollection->NewCopy<FManagedArrayCollection>();
							Output->SetValue(FManagedArrayCollectionSharedPtr(NewCollection), Context);
						}
					}
				}
			}
		}
	};


	class ExampleCollectionEditNode : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(ExampleCollectionEditNode)

	public:
		typedef FManagedArrayCollectionSharedPtr DataType;
		TSharedPtr< class TInput<DataType> > Input;
		TSharedPtr< class TOutput<DataType> > Output;
		TProperty<bool> Active;
		TProperty<float> Scale;

		ExampleCollectionEditNode(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, Input(new TInput<DataType>(TInputParameters<DataType>({ FName("CollectionIn"), this })))
			, Output(new TOutput<DataType>(TOutputParameters<DataType>({ FName("CollectionOut"), this })))
			, Active({ FName("Active"), true, this })
			, Scale({ FName("Scale"), 10.f, this })
		{}

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override
		{
			if (Output.Get() == Out)
			{
				DataType Collection = Input->GetValue(Context);
				if (Active.GetValue())
				{
					TManagedArray<FVector3f>* Vertex = Collection->FindAttribute<FVector3f>("Vertex", "Vertices");
					for (int i = 0; i < Vertex->Num(); i++)
					{
						(*Vertex)[i][1] *= Scale.GetValue();
					}
				}
				Output->SetValue(Collection,Context);
			}
		}
	};

	class SetCollectionAssetNode : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(SetCollectionAssetNode)

	public:
		typedef FManagedArrayCollectionSharedPtr DataType;
		TSharedPtr< class TInput<DataType> > Input;

		SetCollectionAssetNode(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, Input(new TInput<DataType>(TInputParameters<DataType>({ FName("CollectionIn"), this })))
		{}

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override
		{
			if (Out == nullptr)
			{
				DataType Collection = Input->GetValue(Context);
				if (const FEngineContext* EngineContext = Context.AsType<FEngineContext>(FName("UGeometryCollection")))
				{
					if (UGeometryCollection* CollectionAsset = Cast<UGeometryCollection>(EngineContext->Owner))
					{
						TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> NewCollection(Collection->NewCopy<FGeometryCollection>());
						CollectionAsset->SetGeometryCollection(NewCollection);
					}
				}
			}
		}
	};


	void GeometryCollectionEngineAssetNodes();


}

