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

void FGetCollectionAssetDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	if (Output.Get() == Out)
	{
		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			if (UGeometryCollection* CollectionAsset = Cast<UGeometryCollection>(EngineContext->Owner))
			{
				if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> AssetCollection = CollectionAsset->GetGeometryCollection())
				{
					FManagedArrayCollection* NewCollection = AssetCollection->NewCopy<FManagedArrayCollection>();
					Output->SetValue(DataType(NewCollection), Context);
				}
			}
		}
	}
}

void FExampleCollectionEditDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	if (Output.Get() == Out)
	{
		DataType Collection = Input->GetValue(Context);
		if (Active)
		{
			TManagedArray<FVector3f>* Vertex = Collection->FindAttribute<FVector3f>("Vertex", "Vertices");
			for (int i = 0; i < Vertex->Num(); i++)
			{
				(*Vertex)[i][1] *= Scale;
			}
		}
		Output->SetValue(Collection, Context);
	}
}

void FSetCollectionAssetDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	if (Out == nullptr)
	{
		DataType Collection = Input->GetValue(Context);
		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
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

void FResetGeometryCollectionDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	ManagedArrayOut->SetValue(TSharedPtr<FManagedArrayCollection>(nullptr), Context);

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
				FManagedArrayCollection* NewCollection = AssetCollection->NewCopy<FManagedArrayCollection>();
				ManagedArrayOut->SetValue(DataType(NewCollection), Context);
			}
		}
	}
}



