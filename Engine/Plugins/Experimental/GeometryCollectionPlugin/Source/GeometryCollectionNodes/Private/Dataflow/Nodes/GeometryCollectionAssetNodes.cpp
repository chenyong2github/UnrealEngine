// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/Nodes/GeometryCollectionAssetNodes.h"
#include "Dataflow/DataflowCore.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/Facades/CollectionHierarchyFacade.h"

namespace Dataflow
{
	void GeometryCollectionEngineAssetNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGeometryCollectionTerminalDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetGeometryCollectionAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetGeometryCollectionSourcesDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCreateGeometryCollectionFromSourcesDataflowNode);

		// Terminal
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Terminal", FLinearColor(0.f, 0.f, 0.f), CDefaultNodeBodyTintColor);
	}
}


// ===========================================================================================================================

void FGeometryCollectionTerminalDataflowNode::SetAssetValue(TObjectPtr<UObject> Asset, Dataflow::FContext& Context) const
{
	using FGeometryCollectionPtr = TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>;
	using FMaterialArray = TArray<TObjectPtr<UMaterial>>;

	if (UGeometryCollection* CollectionAsset = Cast<UGeometryCollection>(Asset.Get()))
	{
		if (FGeometryCollectionPtr GeometryCollection = CollectionAsset->GetGeometryCollection())
		{
			const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
			const FMaterialArray& InMaterials = GetValue<FMaterialArray>(Context, &Materials);
			CollectionAsset->ResetFrom(InCollection, InMaterials);
		}
	}
}

void FGeometryCollectionTerminalDataflowNode::Evaluate(Dataflow::FContext& Context) const
{
	using FMaterialArray = TArray<TObjectPtr<UMaterial>>;

	const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
	const FMaterialArray& InMaterials = GetValue<FMaterialArray>(Context, &Materials);

	SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	SetValue<FMaterialArray>(Context, InMaterials, &Materials);
}

// ===========================================================================================================================

FGetGeometryCollectionAssetDataflowNode::FGetGeometryCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Asset);
}

void FGetGeometryCollectionAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	ensure(Out->IsA(&Asset));
	if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
	{
		if (const TObjectPtr<UGeometryCollection> CollectionAsset = Cast<UGeometryCollection>(EngineContext->Owner))
		{
			SetValue(Context, CollectionAsset, &Asset);
		}
	}
}

// ===========================================================================================================================

FGetGeometryCollectionSourcesDataflowNode::FGetGeometryCollectionSourcesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
		: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Asset);
	RegisterOutputConnection(&Sources);
}

void FGetGeometryCollectionSourcesDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	ensure(Out->IsA(&Sources));

	TArray<FGeometryCollectionSource> OutSources;
	
	if (const TObjectPtr<UGeometryCollection> InAsset = GetValue(Context, &Asset))
	{
#if WITH_EDITORONLY_DATA
		OutSources = InAsset->GeometrySource; 
#else
		ensureMsgf(false, TEXT("FGetGeometryCollectionSourcesDataflowNode - GeometrySource is only available in editor, returning an empty array"));
#endif

	}

	SetValue(Context, OutSources, &Sources);
}

// ===========================================================================================================================

FCreateGeometryCollectionFromSourcesDataflowNode::FCreateGeometryCollectionFromSourcesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
		: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Sources);
	RegisterOutputConnection(&Collection);
	RegisterOutputConnection(&Materials);
}

void FCreateGeometryCollectionFromSourcesDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	ensure(Out->IsA(&Collection) || Out->IsA(&Materials));
	
	using FGeometryCollectionSourceArray = TArray<FGeometryCollectionSource>;
	const FGeometryCollectionSourceArray& InSources = GetValue<FGeometryCollectionSourceArray>(Context, &Sources);

	FGeometryCollection OutCollection;
	TArray<TObjectPtr<UMaterial>> OutMaterials;
	constexpr bool bReindexMaterialsInLoop = false;
	for (const FGeometryCollectionSource& Source: InSources)
	{
		// todo: change AppendGeometryCollectionSource to take a FManagedArrayCollection so we could move the collection when assigning it to the output
		FGeometryCollectionEngineConversion::AppendGeometryCollectionSource(Source, OutCollection, OutMaterials, bReindexMaterialsInLoop);
	}
	if (bReindexMaterialsInLoop == false)
	{
		OutCollection.ReindexMaterials();
	}

	// make sure we have only one root
	if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(&OutCollection))
	{
		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(&OutCollection);
	}

	// make sure we have a level attribute
	Chaos::Facades::FCollectionHierarchyFacade HierarchyFacade(OutCollection);
	HierarchyFacade.GenerateLevelAttribute();

	// we have to make a copy since we have generated a FGeometryCollection which is inherited from FManagedArrayCollection
	SetValue(Context, static_cast<const FManagedArrayCollection&>(OutCollection), &Collection);
	SetValue(Context, MoveTemp(OutMaterials), &Materials);
}
