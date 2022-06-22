// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionNodes.h"

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
	void GeometryCollectionEngineAssetNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetCollectionAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExampleCollectionEditDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetCollectionAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FResetGeometryCollectionDataflowNode);
	}
}

void FGetCollectionAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Output))
	{
		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			if (UGeometryCollection* CollectionAsset = Cast<UGeometryCollection>(EngineContext->Owner))
			{
				if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> AssetCollection = CollectionAsset->GetGeometryCollection())
				{
					SetValue<DataType>(Context, DataType(*AssetCollection), &Output);
				}
			}
		}
	}
}

void FExampleCollectionEditDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection);

		if (Active)
		{
			TManagedArray<FVector3f>* Vertex = InCollection.FindAttribute<FVector3f>("Vertex", "Vertices");
			for (int i = 0; i < Vertex->Num(); i++)
			{
				(*Vertex)[i][1] *= Scale;
			}
		}
		SetValue<DataType>(Context, InCollection, &Collection);
	}
}

void FSetCollectionAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	DataType InCollection = GetValue<DataType>(Context, &Collection);

	if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
	{
		if (UGeometryCollection* CollectionAsset = Cast<UGeometryCollection>(EngineContext->Owner))
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> NewCollection(InCollection.NewCopy<FGeometryCollection>());
			CollectionAsset->SetGeometryCollection(NewCollection);
			CollectionAsset->InvalidateCollection();
		}
	}
}

void FResetGeometryCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		SetValue<DataType>(Context, Collection, &Collection); // prime to avoid ensure

		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
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
				GeometryCollectionObject->UpdateConvexGeometry();
				GeometryCollectionObject->InitializeMaterials();
				GeometryCollectionObject->InvalidateCollection();

				if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> AssetCollection = GeometryCollectionObject->GetGeometryCollection())
				{
					SetValue<DataType>(Context, *AssetCollection, &Collection);
				}
			}
		}
	}
}




