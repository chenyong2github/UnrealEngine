// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "GeometryCollectionNodeConnectionTypes.h"

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"



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

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override;
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

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override;

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

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override;

	};


	class ResetGeometryCollection : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(ResetGeometryCollection)

	public:
		typedef FManagedArrayCollectionSharedPtr DataType;

		TSharedPtr< class TOutput<DataType> > ManagedArrayOut;

		ResetGeometryCollection(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, ManagedArrayOut(new TOutput<DataType>(TOutputParameters<DataType>({ FName("ManagedArrayOut"), this })))
		{}


		virtual void Evaluate(const FContext& Context, FConnection* Out) const override;

	};

	void GeometryCollectionEngineAssetNodes();


}

