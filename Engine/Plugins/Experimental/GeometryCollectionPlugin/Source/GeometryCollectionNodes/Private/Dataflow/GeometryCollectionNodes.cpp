// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineUtility.h"
#include "GeometryCollection/GeometryCollectionEngineRemoval.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "Logging/LogMacros.h"
#include "Templates/SharedPointer.h"
#include "UObject/UnrealTypePrivate.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshEditor.h"
#include "Operations/MeshBoolean.h"

#include "EngineGlobals.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "FractureEngineClustering.h"
#include "FractureEngineSelection.h"
#include "GeometryCollection/Facades/CollectionBoundsFacade.h"
#include "GeometryCollection/Facades/CollectionAnchoringFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionNodes)

namespace Dataflow
{
	void GeometryCollectionEngineNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetCollectionAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExampleCollectionEditDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetCollectionAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FAppendCollectionAssetsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FResetGeometryCollectionDataflowNode);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPrintStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FLogStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeLiteralStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBoundingBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExpandBoundingBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVectorToStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFloatToStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakePointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeLiteralFloatDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeLiteralIntDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeLiteralBoolDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeLiteralVectorDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FIntToStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBoolToStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExpandVectorDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FIntToFloatDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FStringAppendDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRandomFloatDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRandomFloatInRangeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRandomUnitVectorDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRandomUnitVectorInConeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadiansToDegreesDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDegreesToRadiansDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExplodedViewDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCreateNonOverlappingConvexHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FHashStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FHashVectorDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFloatToIntDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMathConstantsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetArrayElementDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetNumArrayElementsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetBoundingBoxesDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetCentroidsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FTransformDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCompareIntDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBranchDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSchemaDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRemoveOnBreakDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetAnchorStateDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FProximityDataflowNode);

		// GeometryCollection
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("GeometryCollection", FLinearColor(0.55f, 0.45f, 1.0f), CDefaultNodeBodyTintColor);
		// Development
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Development", FLinearColor(1.f, 0.f, 0.f), CDefaultNodeBodyTintColor);
		// Utilities|String
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Utilities|String", FLinearColor(0.5f, 0.f, 0.5f), CDefaultNodeBodyTintColor);
		// Fracture
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Fracture", FLinearColor(1.f, 1.f, 0.8f), CDefaultNodeBodyTintColor);
		// Utilities
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Utilities", FLinearColor(1.f, 1.f, 0.f), CDefaultNodeBodyTintColor);
		// Math
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Math", FLinearColor(0.f, 0.4f, 0.8f), CDefaultNodeBodyTintColor);
		// Generators
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Generators", FLinearColor(.7f, 0.7f, 0.7f), CDefaultNodeBodyTintColor);
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

		if (bActive)
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

void FAppendCollectionAssetsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection1))
	{
		FManagedArrayCollection InCollection1 = GetValue<DataType>(Context, &Collection1);
		const FManagedArrayCollection& InCollection2 = GetValue<DataType>(Context, &Collection2);

		InCollection1.Append(InCollection2);
		SetValue<DataType>(Context, InCollection1, &Collection1);
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
				GeometryCollectionObject->UpdateGeometryDependentProperties();
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

void FPrintStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FString Value = GetValue<FString>(Context, &String);

	if (PrintToScreen)
	{
		GEngine->AddOnScreenDebugMessage(-1, Duration, Color, Value);
	}
	if (PrintToLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("Text, %s"), *Value);
	}
}

void FLogStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (PrintToLog)
	{
		FString Value = GetValue<FString>(Context, &String);
		UE_LOG(LogTemp, Warning, TEXT("[Dataflow Log] %s"), *Value);
	}
}

void FMakeLiteralStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		SetValue<FString>(Context, Value, &String);
	}
}

void ComputeBoundingBox(const FManagedArrayCollection& Collection, FBox& BoundingBox)
{
	if (Collection.HasAttribute("Transform", FGeometryCollection::TransformGroup) &&
		Collection.HasAttribute("Parent", FGeometryCollection::TransformGroup) &&
		Collection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup) &&
		Collection.HasAttribute("BoundingBox", FGeometryCollection::GeometryGroup))
	{
		const TManagedArray<FTransform>& Transforms = Collection.GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
		const TManagedArray<int32>& ParentIndices = Collection.GetAttribute<int32>("Parent", FGeometryCollection::TransformGroup);
		const TManagedArray<int32>& TransformIndices = Collection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);
		const TManagedArray<FBox>& BoundingBoxes = Collection.GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

		TArray<FMatrix> TmpGlobalMatrices;
		GeometryCollectionAlgo::GlobalMatrices(Transforms, ParentIndices, TmpGlobalMatrices);

		if (TmpGlobalMatrices.Num() > 0)
		{
			for (int32 BoxIdx = 0; BoxIdx < BoundingBoxes.Num(); ++BoxIdx)
			{
				const int32 TransformIndex = TransformIndices[BoxIdx];
				BoundingBox += BoundingBoxes[BoxIdx].TransformBy(TmpGlobalMatrices[TransformIndex]);
			}
		}
	}
}

void FBoundingBoxDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FBox>(&BoundingBox))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FBox BBox(ForceInit);

		ComputeBoundingBox(InCollection, BBox);

		SetValue<FBox>(Context, BBox, &BoundingBox);
	}
}

void FExpandBoundingBoxDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FBox BBox = GetValue<FBox>(Context, &BoundingBox);

	if (Out->IsA<FVector>(&Min))
	{
		SetValue<FVector>(Context, BBox.Min, &Min);
	}
	else if (Out->IsA<FVector>(&Max))
	{
		SetValue<FVector>(Context, BBox.Max, &Max);
	}
	else if (Out->IsA<FVector>(&Center))
	{
		SetValue<FVector>(Context, BBox.GetCenter(), &Center);
	}
	else if (Out->IsA<FVector>(&HalfExtents))
	{
		SetValue<FVector>(Context, BBox.GetExtent(), &HalfExtents);
	}
	else if (Out->IsA<float>(&Volume))
	{
		SetValue<float>(Context, BBox.GetVolume(), &Volume);
	}
}

void FVectorToStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		FString Value = GetValue<FVector>(Context, &Vector).ToString();
		SetValue<FString>(Context, Value, &String);
	}
}

void FFloatToStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		FString Value = FString::Printf(TEXT("%f"), GetValue<float>(Context, &Float));
		SetValue<FString>(Context, Value, &String);
	}
}

void FMakePointsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		SetValue<TArray<FVector>>(Context, Point, &Points);
	}
}

void FMakeBoxDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FBox>(&Box))
	{
		if (DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax)
		{
			FVector MinVal = GetValue<FVector>(Context, &Min);
			FVector MaxVal = GetValue<FVector>(Context, &Max);

			SetValue<FBox>(Context, FBox(MinVal, MaxVal), &Box);
		}
		else if (DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_CenterSize)
		{
			FVector CenterVal = GetValue<FVector>(Context, &Center);
			FVector SizeVal = GetValue<FVector>(Context, &Size);

			SetValue<FBox>(Context, FBox(CenterVal - 0.5 * SizeVal, CenterVal + 0.5 * SizeVal), &Box);
		}
	}
}


void FMakeLiteralFloatDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Float))
	{
		SetValue<float>(Context, Value, &Float);
	}
}

void FMakeLiteralIntDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&Int))
	{
		SetValue<int32>(Context, Value, &Int);
	}
}

void FMakeLiteralBoolDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<bool>(&Bool))
	{
		SetValue<bool>(Context, Value, &Bool);
	}
}

void FMakeLiteralVectorDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FVector>(&Vector))
	{
		SetValue<FVector>(Context, Value, &Vector);
	}
}

void FIntToStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		FString Value = FString::Printf(TEXT("%d"), GetValue<int32>(Context, &Int));
		SetValue<FString>(Context, Value, &String);
	}
}

void FBoolToStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		FString Value = FString::Printf(TEXT("%s"), GetValue<bool>(Context, &Bool) ? TEXT("true") : TEXT("false"));
		SetValue<FString>(Context, Value, &String);
	}
}

void FExpandVectorDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FVector VectorVal = GetValue<FVector>(Context, &Vector);

	if (Out->IsA<float>(&X))
	{
		SetValue<float>(Context, VectorVal.X, &X);
	}
	else if (Out->IsA<float>(&Y))
	{
		SetValue<float>(Context, VectorVal.Y, &Y);
	}
	else if (Out->IsA<float>(&Z))
	{
		SetValue<float>(Context, VectorVal.Z, &Z);
	}
}

void FIntToFloatDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Float))
	{
		float Value = float(GetValue<int32>(Context, &Int));
		SetValue<float>(Context, Value, &Float);
	}
}


void FStringAppendDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		const FString StringOut = GetValue<FString>(Context, &String1) + GetValue<FString>(Context, &String2);
		SetValue<FString>(Context, StringOut, &String);
	}
}

void FRandomFloatDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Float))
	{
		if (Deterministic)
		{
			float RandomSeedVal = GetValue<float>(Context, &RandomSeed);

			FRandomStream Stream(RandomSeedVal);
			SetValue<float>(Context, Stream.FRand(), &Float);
		}
		else
		{
			SetValue<float>(Context, FMath::FRand(), &Float);
		}
	}
}

void FRandomFloatInRangeDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Float))
	{
		float MinVal = GetValue<float>(Context, &Min);
		float MaxVal = GetValue<float>(Context, &Max);

		if (Deterministic)
		{
			float RandomSeedVal = GetValue<float>(Context, &RandomSeed);

			FRandomStream Stream(RandomSeedVal);
			SetValue<float>(Context, Stream.FRandRange(MinVal, MaxVal), &Float);
		}
		else
		{
			SetValue<float>(Context, FMath::FRandRange(MinVal, MaxVal), &Float);
		}
	}
}

void FRandomUnitVectorDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FVector>(&Vector))
	{
		if (Deterministic)
		{
			float RandomSeedVal = GetValue<float>(Context, &RandomSeed);

			FRandomStream Stream(RandomSeedVal);
			SetValue<FVector>(Context, Stream.VRand(), &Vector);
		}
		else
		{
			SetValue<FVector>(Context, FMath::VRand(), &Vector);
		}
	}
}

void FRandomUnitVectorInConeDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FVector>(&Vector))
	{
		FVector ConeDirectionVal = GetValue<FVector>(Context, &ConeDirection);
		float ConeHalfAngleVal = GetValue<float>(Context, &ConeHalfAngle);

		if (Deterministic)
		{
			float RandomSeedVal = GetValue<float>(Context, &RandomSeed);

			FRandomStream Stream(RandomSeedVal);
			SetValue<FVector>(Context, Stream.VRandCone(ConeDirectionVal, ConeHalfAngleVal), &Vector);
		}
		else
		{
			SetValue<FVector>(Context, FMath::VRandCone(ConeDirectionVal, ConeHalfAngleVal), &Vector);
		}
	}
}

void FRadiansToDegreesDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Degrees))
	{
		SetValue<float>(Context, FMath::RadiansToDegrees(GetValue<float>(Context, &Radians)), &Degrees);
	}
}

void FDegreesToRadiansDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Radians))
	{
		SetValue<float>(Context, FMath::DegreesToRadians(GetValue<float>(Context, &Degrees)), &Radians);
	}
}

static void AddAdditionalAttributesIfRequired(FGeometryCollection* GeometryCollection)
{
	FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
}

bool FExplodedViewDataflowNode::GetValidGeoCenter(FGeometryCollection* Collection, const TManagedArray<int32>& TransformToGeometryIndex, const TArray<FTransform>& Transforms, const TManagedArray<TSet<int32>>& Children, const TManagedArray<FBox>& BoundingBox, int32 TransformIndex, FVector& OutGeoCenter)
{
	if (Collection->IsRigid(TransformIndex))
	{
		OutGeoCenter = Transforms[TransformIndex].TransformPosition(BoundingBox[TransformToGeometryIndex[TransformIndex]].GetCenter());

		return true;
	}
	else if (Collection->SimulationType[TransformIndex] == FGeometryCollection::ESimulationTypes::FST_None) // ie this is embedded geometry
	{
		int32 Parent = Collection->Parent[TransformIndex];
		int32 ParentGeo = Parent != INDEX_NONE ? TransformToGeometryIndex[Parent] : INDEX_NONE;
		if (ensureMsgf(ParentGeo != INDEX_NONE, TEXT("Embedded geometry should always have a rigid geometry parent!  Geometry collection may be malformed.")))
		{
			OutGeoCenter = Transforms[Collection->Parent[TransformIndex]].TransformPosition(BoundingBox[ParentGeo].GetCenter());
		}
		else
		{
			return false; // no valid value to return
		}

		return true;
	}
	else
	{
		FVector AverageCenter;
		int32 ValidVectors = 0;
		for (int32 ChildIndex : Children[TransformIndex])
		{

			if (GetValidGeoCenter(Collection, TransformToGeometryIndex, Transforms, Children, BoundingBox, ChildIndex, OutGeoCenter))
			{
				if (ValidVectors == 0)
				{
					AverageCenter = OutGeoCenter;
				}
				else
				{
					AverageCenter += OutGeoCenter;
				}
				++ValidVectors;
			}
		}

		if (ValidVectors > 0)
		{
			OutGeoCenter = AverageCenter / ValidVectors;
			return true;
		}
	}
	return false;
}

void FExplodedViewDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			GeomCollection->AddAttribute<FVector3f>("ExplodedVector", FGeometryCollection::TransformGroup, FManagedArrayCollection::FConstructionParameters(FName()));
			check(GeomCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup));

			TManagedArray<FVector3f>& ExplodedVectors = GeomCollection->ModifyAttribute<FVector3f>("ExplodedVector", FGeometryCollection::TransformGroup);
			const TManagedArray<FTransform>& Transform = GeomCollection->GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
			const TManagedArray<int32>& TransformToGeometryIndex = GeomCollection->GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::TransformGroup);
			const TManagedArray<FBox>& BoundingBox = GeomCollection->GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

			// Make sure we have valid "Level"
			AddAdditionalAttributesIfRequired(GeomCollection.Get());

			const TManagedArray<int32>& Levels = GeomCollection->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);
			const TManagedArray<int32>& Parent = GeomCollection->GetAttribute<int32>("Parent", FTransformCollection::TransformGroup);
			const TManagedArray<TSet<int32>>& Children = GeomCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

			int32 ViewFractureLevel = -1;
			int32 MaxFractureLevel = ViewFractureLevel;
			for (int32 Idx = 0, ni = Transform.Num(); Idx < ni; ++Idx)
			{
				if (Levels[Idx] > MaxFractureLevel)
					MaxFractureLevel = Levels[Idx];
			}

			TArray<FTransform> Transforms;
			GeometryCollectionAlgo::GlobalMatrices(Transform, GeomCollection->Parent, Transforms);

			TArray<FVector> TransformedCenters;
			TransformedCenters.SetNumUninitialized(Transforms.Num());

			int32 TransformsCount = 0;

			FVector Center(ForceInitToZero);
			for (int32 Idx = 0, ni = Transform.Num(); Idx < ni; ++Idx)
			{
				ExplodedVectors[Idx] = FVector3f::ZeroVector;
				FVector GeoCenter;

				if (GetValidGeoCenter(GeomCollection.Get(), TransformToGeometryIndex, Transforms, Children, BoundingBox, Idx, GeoCenter))
				{
					TransformedCenters[Idx] = GeoCenter;
					if ((ViewFractureLevel < 0) || Levels[Idx] == ViewFractureLevel)
					{
						Center += TransformedCenters[Idx];
						++TransformsCount;
					}
				}
			}

			Center /= TransformsCount;

			for (int Level = 1; Level <= MaxFractureLevel; Level++)
			{
				for (int32 Idx = 0, ni = Transforms.Num(); Idx < ni; ++Idx)
				{
					if ((ViewFractureLevel < 0) || Levels[Idx] == ViewFractureLevel)
					{
						FVector ScaleVal = GetValue<FVector>(Context, &Scale);
						float UniformScaleVal = GetValue<float>(Context, &UniformScale);

						FVector ScaleVec = ScaleVal * UniformScaleVal;
						ExplodedVectors[Idx] = (FVector3f)(TransformedCenters[Idx] - Center) * (FVector3f)ScaleVec;
					}
					else
					{
						if (Parent[Idx] > -1)
						{
							ExplodedVectors[Idx] = ExplodedVectors[Parent[Idx]];
						}
					}
				}
			}

			SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
		}
	}
}

void FCreateNonOverlappingConvexHullsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			float CanRemoveFractionVal = GetValue<float>(Context, &CanRemoveFraction);
			float CanExceedFractionVal = GetValue<float>(Context, &CanExceedFraction);
			float SimplificationDistanceThresholdVal = GetValue<float>(Context, &SimplificationDistanceThreshold);

			FGeometryCollectionConvexUtility::FGeometryCollectionConvexData ConvexData = FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(GeomCollection.Get(), CanRemoveFractionVal, SimplificationDistanceThresholdVal, CanExceedFractionVal);

			SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
		}
	}
}


void FHashStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&Hash))
	{
		SetValue<int32>(Context, GetTypeHash(GetValue<FString>(Context, &String)), &Hash);
	}
}

void FHashVectorDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&Hash))
	{
		SetValue<int32>(Context, GetTypeHash(GetValue<FVector>(Context, &Vector)), &Hash);
	}
}

void FFloatToIntDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&Int))
	{
		float FloatVal = GetValue<float>(Context, &Float);
		if (Function == EFloatToIntFunctionEnum::Dataflow_FloatToInt_Function_Floor)
		{
			SetValue<int32>(Context, FMath::FloorToInt32(FloatVal), &Int);
		}
		else if (Function == EFloatToIntFunctionEnum::Dataflow_FloatToInt_Function_Ceil)
		{
			SetValue<int32>(Context, FMath::CeilToInt32(FloatVal), &Int);
		}
		else if (Function == EFloatToIntFunctionEnum::Dataflow_FloatToInt_Function_Round)
		{
			SetValue<int32>(Context, FMath::RoundToInt32(FloatVal), &Int);
		}
		else if (Function == EFloatToIntFunctionEnum::Dataflow_FloatToInt_Function_Truncate)
		{
			SetValue<int32>(Context, int32(FMath::TruncToFloat(FloatVal)), &Int);
		}
	}
}

void FMathConstantsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<float>(&Float))
	{
		if (Constant == EMathConstantsEnum::Dataflow_MathConstants_Pi)
		{
			SetValue<float>(Context, FMathf::Pi, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_HalfPi)
		{
			SetValue<float>(Context, FMathf::HalfPi, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_TwoPi)
		{
			SetValue<float>(Context, FMathf::TwoPi, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_FourPi)
		{
			SetValue<float>(Context, FMathf::FourPi, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_InvPi)
		{
			SetValue<float>(Context, FMathf::InvPi, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_InvTwoPi)
		{
			SetValue<float>(Context, FMathf::InvTwoPi, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_Sqrt2)
		{
			SetValue<float>(Context, FMathf::Sqrt2, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_InvSqrt2)
		{
			SetValue<float>(Context, FMathf::InvSqrt2, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_Sqrt3)
		{
			SetValue<float>(Context, FMathf::Sqrt3, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_MathConstants_InvSqrt3)
		{
			SetValue<float>(Context, FMathf::InvSqrt3, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_FloatToInt_Function_E)
		{
			SetValue<float>(Context, 2.71828182845904523536f, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_FloatToInt_Function_Gamma)
		{
			SetValue<float>(Context, 0.577215664901532860606512090082f, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_FloatToInt_Function_GoldenRatio)
		{
			SetValue<float>(Context, 1.618033988749894f, &Float);
		}
		else if (Constant == EMathConstantsEnum::Dataflow_FloatToInt_Function_ZeroTolerance)
		{
			SetValue<float>(Context, FMathf::ZeroTolerance, &Float);
		}
	}
}

void FGetArrayElementDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FVector>(&Point))
	{
		const TArray<FVector>& Array = GetValue<TArray<FVector>>(Context, &Points);
		if (Index >= 0 && Index < Array.Num())
		{
			SetValue<FVector>(Context, Array[Index], &Point);
		}
	}
}

void FGetNumArrayElementsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&NumElements))
	{
		SetValue<int32>(Context, GetValue<TArray<FVector>>(Context, &Points).Num(), &NumElements);
	}
}

void FGetBoundingBoxesDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FBox>>(&BoundingBoxes))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		const TManagedArray<FBox>& InBoundingBoxes = GeometryCollection::Facades::FBoundsFacade(InCollection).GetBoundingBoxes();

		TArray<FBox> BoundingBoxesArr;
		for (int32 Idx = 0; Idx < InBoundingBoxes.Num(); ++Idx)
		{
			if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
			{
				if (InTransformSelection.IsSelected(Idx))
				{
					BoundingBoxesArr.Add(InBoundingBoxes[Idx]);
				}
			}
			else
			{
				BoundingBoxesArr.Add(InBoundingBoxes[Idx]);
			}

		}
		SetValue<TArray<FBox>>(Context, BoundingBoxesArr, &BoundingBoxes);
	}
}

void FGetCentroidsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Centroids))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		const TManagedArray<FBox>& InBoundingBoxes = GeometryCollection::Facades::FBoundsFacade(InCollection).GetBoundingBoxes();

		TArray<FVector> CentroidsArr;
		for (int32 Idx = 0; Idx < InBoundingBoxes.Num(); ++Idx)
		{
			const FBox& BoundingBox = InBoundingBoxes[Idx];
			if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
			{
				if (InTransformSelection.IsSelected(Idx))
				{
					CentroidsArr.Add(BoundingBox.GetCenter());
				}
			}
			else
			{
				CentroidsArr.Add(BoundingBox.GetCenter());
			}
		}
		SetValue<TArray<FVector>>(Context, CentroidsArr, &Centroids);
	}
}


void FTransformDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
	}
}


void FCompareIntDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<bool>(&Result))
	{
		int32 IntAValue = GetValue<int32>(Context, &IntA);
		int32 IntBValue = GetValue<int32>(Context, &IntB);
		bool ResultValue;

		if (Operation == ECompareOperationEnum::Dataflow_Compare_Equal)
		{
			ResultValue = IntAValue == IntBValue ? true : false;
		}
		else if (Operation == ECompareOperationEnum::Dataflow_Compare_Smaller)
		{
			ResultValue = IntAValue < IntBValue ? true : false;
		}
		else if (Operation == ECompareOperationEnum::Dataflow_Compare_SmallerOrEqual)
		{
			ResultValue = IntAValue <= IntBValue ? true : false;
		}
		else if (Operation == ECompareOperationEnum::Dataflow_Compare_Greater)
		{
			ResultValue = IntAValue > IntBValue ? true : false;
		}
		else if (Operation == ECompareOperationEnum::Dataflow_Compare_GreaterOrEqual)
		{
			ResultValue = IntAValue >= IntBValue ? true : false;
		}

		SetValue<bool>(Context, ResultValue, &Result);
	}
}

void FBranchDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		if (GetValue<bool>(Context, &Condition))
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, GetValue<TObjectPtr<UDynamicMesh>>(Context, &MeshA), &Mesh);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, GetValue<TObjectPtr<UDynamicMesh>>(Context, &MeshB), &Mesh);
		}
	}
}


namespace {
	inline FName GetArrayTypeString(FManagedArrayCollection::EArrayType ArrayType)
	{
		switch (ArrayType)
		{
#define MANAGED_ARRAY_TYPE(a,A)	case EManagedArrayType::F##A##Type:\
			return FName(#A);
#include "GeometryCollection/ManagedArrayTypeValues.inl"
#undef MANAGED_ARRAY_TYPE
		}
		return FName();
	}
}

void FGetSchemaDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FString OutputStr;
		OutputStr.Appendf(TEXT("\n----------------------------------------\n"));
		for (auto& Group : InCollection.GroupNames())
		{
			if (InCollection.HasGroup(Group))
			{
				int32 NumElems = InCollection.NumElements(Group);

				OutputStr.Appendf(TEXT("Group: %s  Number of Elements: %d\n"), *Group.ToString(), NumElems);
				OutputStr.Appendf(TEXT("Attributes:\n"));

				for (auto& Attr : InCollection.AttributeNames(Group))
				{
					if (InCollection.HasAttribute(Attr, Group))
					{
						FString TypeStr = GetArrayTypeString(InCollection.GetAttributeType(Attr, Group)).ToString();
						OutputStr.Appendf(TEXT("\t%s\t[%s]\n"), *Attr.ToString(), *TypeStr);
					}
				}

				OutputStr.Appendf(TEXT("\n--------------------\n"));
			}
		}
		OutputStr.Appendf(TEXT("----------------------------------------\n"));

		SetValue<FString>(Context, OutputStr, &String);
	}
}


void FRemoveOnBreakDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (!InCollection.HasAttribute("RemoveOnBreak", FGeometryCollection::TransformGroup))
		{
			TManagedArray<FVector4f>& NewRemoveOnBreak = InCollection.AddAttribute<FVector4f>("RemoveOnBreak", FGeometryCollection::TransformGroup);
			NewRemoveOnBreak.Fill(FRemoveOnBreakData::DisabledPackedData);
		}

		TManagedArray<FVector4f>& RemoveOnBreak = InCollection.ModifyAttribute<FVector4f>("RemoveOnBreak", FGeometryCollection::TransformGroup);

		const FVector2f PostBreakTimerData = GetValue<FVector2f>(Context, &PostBreakTimer);
		const FVector2f RemovalTimerData = GetValue<FVector2f>(Context, &RemovalTimer);
		const bool ClusterCrumblingData = GetValue<bool>(Context, &ClusterCrumbling);

		RemoveOnBreak.Fill(FVector4f{ PostBreakTimerData.X, PostBreakTimerData.Y, RemovalTimerData.X, RemovalTimerData.Y });

			// @todo(harsha) Implement with Selection
			// const FRemoveOnBreakData RemoveOnBreakData(true, PostBreakTimerData, ClusterCrumblingData, RemovalTimerData);
			// for (int32 Index : SelectedBones)
			// {
				// // if root bone, then do not set 
				// if (GeometryCollection->Parent[Index] == INDEX_NONE)
				// {
				// 	RemoveOnBreak[Index] = FRemoveOnBreakData::DisabledPackedData;
				// }
				// else
				// {
				// 	RemoveOnBreak[Index] = RemoveOnBreakData.GetPackedData();
				// }
			// }

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}


void FSetAnchorStateDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			Chaos::Facades::FCollectionAnchoringFacade AnchoringFacade(*GeomCollection);
			if (!AnchoringFacade.HasAnchoredAttribute())
			{
				AnchoringFacade.AddAnchoredAttribute();
			}

			bool bAnchored = (AnchorState == EAnchorStateEnum::Dataflow_AnchorState_Anchored) ? true : false;
			TArray<int32> BoneIndices;
			InTransformSelection.AsArray(BoneIndices);
			AnchoringFacade.SetAnchored(BoneIndices, bAnchored);

			if (SetNotSelectedBonesToOppositeState)
			{
				InTransformSelection.Invert();
				InTransformSelection.AsArray(BoneIndices);
				AnchoringFacade.SetAnchored(BoneIndices, !bAnchored);
			}

			SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
		}
	}
}


void FProximityDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			FGeometryCollectionProximityPropertiesInterface::FProximityProperties Properties = GeomCollection->GetProximityProperties();

			Properties.Method = (EProximityMethod)ProximityMethod;
			Properties.DistanceThreshold = DistanceThreshold;
			Properties.bUseAsConnectionGraph = bUseAsConnectionGraph;

			GeomCollection->SetProximityProperties(Properties);

			// Invalidate proximity
			FGeometryCollectionProximityUtility ProximityUtility(GeomCollection.Get());
			ProximityUtility.InvalidateProximity();

			SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
		}
	}
}









