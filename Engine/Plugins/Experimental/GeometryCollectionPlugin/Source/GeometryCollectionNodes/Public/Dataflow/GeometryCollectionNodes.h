// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "GeometryCollectionNodeConnectionTypes.h"

#include "CoreMinimal.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowEngineUtil.h"
#include "Dataflow/DataflowProperty.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "Logging/LogMacros.h"
#include "Templates/SharedPointer.h"
#include "UObject/UnrealTypePrivate.h"


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
						CollectionAsset->InvalidateCollection();
					}
				}
			}
		}
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


		virtual void Evaluate(const FContext& Context, FConnection* Out) const override
		{
			ManagedArrayOut->SetValue(TSharedPtr<FManagedArrayCollection>(nullptr), Context);

			if (const FEngineContext* EngineContext = (const FEngineContext*)(&Context))
			{
				if (UGeometryCollection* GeometryCollectionObject = Cast<UGeometryCollection>(EngineContext->Owner))
				{
					GeometryCollectionObject->Reset();

					const UObject* Owner = EngineContext->Owner;
					FName AName("GeometrySource");
					if (Owner && Owner->GetClass())
					{
						if (const ::FProperty* UEProperty = Owner->GetClass()->FindPropertyByName(AName))
						{
							if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(UEProperty))
							{
								FScriptArrayHelper_InContainer ArrayHelper(ArrayProperty, Owner);
								const int32 ArraySize = ArrayHelper.Num();
								for (int32 Index = 0; Index < ArraySize; ++Index)
								{
									if (FGeometryCollectionSource* SourceObject = (FGeometryCollectionSource*)(ArrayHelper.GetRawPtr(Index)))
									{
										if (UObject* ResolvedObject = SourceObject->SourceGeometryObject.ResolveObject())
										{
											if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(ResolvedObject))
											{
												TArray<UMaterialInterface*> Materials;
												Materials.Reserve(StaticMesh->GetStaticMaterials().Num());

												for (int32 Index2 = 0; Index2 < StaticMesh->GetStaticMaterials().Num(); ++Index2)
												{
													UMaterialInterface* CurrMaterial = StaticMesh->GetMaterial(Index2);
													Materials.Add(CurrMaterial);
												}

												// Geometry collections usually carry the selection material, which we'll delete before appending
												UMaterialInterface* BoneSelectedMaterial = LoadObject<UMaterialInterface>(nullptr, UGeometryCollection::GetSelectedMaterialPath(), nullptr, LOAD_None, nullptr);
												GeometryCollectionObject->Materials.Remove(BoneSelectedMaterial);
												Materials.Remove(BoneSelectedMaterial);

												FGeometryCollectionEngineConversion::AppendStaticMesh(StaticMesh, Materials, FTransform(), GeometryCollectionObject);
											}
										}
									}
								}
							}
						}
					}
					GeometryCollectionObject->InvalidateCollection();

					if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> AssetCollection = GeometryCollectionObject->GetGeometryCollection())
					{
						FManagedArrayCollection* NewCollection = AssetCollection->NewCopy<FManagedArrayCollection>();
						ManagedArrayOut->SetValue(FManagedArrayCollectionSharedPtr(NewCollection), Context);
					}
				}
			}
		}
	};

	void GeometryCollectionEngineAssetNodes();


}

