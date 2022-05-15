// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNode.h"

#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace Dataflow
{
	typedef TSharedPtr<FManagedArrayCollection> FManagedArrayCollectionSharedPtr;

	enum class EManagedArrayType : uint8
	{
		EgManagedArrayTypeNone = 0,
		EgManagedArrayTypeBool,
		EgManagedArrayTypeInt,
		EgManagedArrayTypeFloat,
		EgManagedArrayTypeVector
	};

	class DATAFLOWNODES_API FNewManagedArrayCollectionNode : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(FNewManagedArrayCollectionNode)

	public:
		typedef TSharedPtr<FManagedArrayCollection> DataType;
		DataType Value;
		TSharedPtr< class TOutput<DataType> > Output;

		FNewManagedArrayCollectionNode(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, Value(new FManagedArrayCollection())
			, Output(new TOutput<DataType>(TOutputParameters<DataType>({FName("CollectionOut"), this})))
		{}

		virtual void Evaluate(const FContext& Context, FConnection* Out) override
		{
			if (Output.Get() == Out)
			{
				Output->SetValue(Value, Context);
			}
		}
	};

	class DATAFLOWNODES_API FAddAttributeNode : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(FAddAttributeNode)

	public:

		typedef TSharedPtr<FManagedArrayCollection> DataType;

		FName AttributeName;
		FName GroupName;
		EManagedArrayType AttributeType;

		TSharedPtr< class TInput<DataType> > Input;
		TSharedPtr< class TInput<int32> > SizeInput;
		TSharedPtr< class TOutput<DataType> > Output;

		FAddAttributeNode(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, AttributeName("Position")
			, GroupName("Particle")
			, AttributeType(EManagedArrayType::EgManagedArrayTypeVector)
			, Input(new TInput<DataType>(TInputParameters<DataType>({FName("CollectionIn"), this})))
			, SizeInput(new TInput<int32>(TInputParameters<int32>({FName("SizeIn"), this })))
			, Output(new TOutput<DataType>(TOutputParameters<DataType>({FName("CollectionOut"), this })))
		{}

		virtual void Evaluate(const FContext& Context, FConnection* Out) override
		{
			if (Output.Get() == Out)
			{
				DataType Collection(new FManagedArrayCollection());

				// its an error to not set the requested output
				if (DataType Val = Input->GetValue(Context))
				{
					Collection = Val;

					int NumElements = 100;
					if (SizeInput->GetConnection())
					{
						NumElements = SizeInput->GetValue(Context);
					}

					if (!Collection->HasGroup(GroupName))
					{
						Collection->AddGroup(GroupName);
					}
					if (!Collection->HasAttribute(AttributeName, GroupName))
					{
						Collection->AddAttribute<FVector>(AttributeName, GroupName);
					}

					if (Collection->NumElements(GroupName) < NumElements)
					{
						Collection->AddElements(NumElements, GroupName);

						TManagedArray<FVector>& Pos = Collection->ModifyAttribute<FVector>(AttributeName, GroupName);
						for (int i = 0; i < NumElements; i++)
						{
							Pos[i] = FVector(i);
						}
					}
				}
				Output->SetValue(Collection, Context);
			}
		}

		virtual void SerializeInternal(FArchive& Ar) override { 
			Ar << AttributeName << GroupName << AttributeType;
		};

	};



	void DATAFLOWNODES_API RegisterManagedArrayCollectionNodes();
}

