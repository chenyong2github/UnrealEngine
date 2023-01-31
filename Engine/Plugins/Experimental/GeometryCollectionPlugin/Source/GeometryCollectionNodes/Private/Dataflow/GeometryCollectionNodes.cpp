// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/Facades/CollectionMeshFacade.h"
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
#include "Materials/Material.h"

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
#include "GeometryCollection/Facades/CollectionRemoveOnBreakFacade.h"
#include "GeometryCollection/Facades/CollectionTransformFacade.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMesh3.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionNodes)

namespace Dataflow
{
	void GeometryCollectionEngineNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetCollectionFromAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FAppendCollectionAssetsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPrintStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FLogStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeLiteralStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBoundingBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExpandBoundingBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVectorToStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFloatToStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakePointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeSphereDataflowNode);
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
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FHashStringDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FHashVectorDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFloatToIntDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMathConstantsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetArrayElementDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetNumArrayElementsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetBoundingBoxesFromCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetCentroidsFromCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FTransformCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBakeTransformsInCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FTransformMeshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCompareIntDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBranchDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSchemaDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRemoveOnBreakDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetAnchorStateDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FProximityDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionSetPivotDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FAddCustomCollectionAttributeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetNumElementsInCollectionGroupDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetCollectionAttributeDataTypedDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetCollectionAttributeDataTypedDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBoolArrayToFaceSelectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFloatArrayToVertexSelectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetVertexColorInCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeTransformDataflowNode);

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
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Generators", FLinearColor(.6f, 0.1f, 1.f), CDefaultNodeBodyTintColor);
	}
}

void FGetCollectionFromAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		if (CollectionAsset)
		{
			if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> AssetCollection = CollectionAsset->GetGeometryCollection())
			{
				SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*AssetCollection), &Collection);
			}
			else
			{
				SetValue<FManagedArrayCollection>(Context, FManagedArrayCollection(), &Collection);
			}
		}
		else
		{
			SetValue<FManagedArrayCollection>(Context, FManagedArrayCollection(), &Collection);
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


void FPrintStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FString Value = GetValue<FString>(Context, &String);

	if (bPrintToScreen)
	{
		GEngine->AddOnScreenDebugMessage(-1, Duration, Color, Value);
	}
	if (bPrintToLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("Text, %s"), *Value);
	}
}

void FLogStringDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (bPrintToLog)
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


void FBoundingBoxDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FBox>(&BoundingBox))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FBoundsFacade BoundsFacade(InCollection);
		const FBox& BoundingBoxInCollectionSpace = BoundsFacade.GetBoundingBoxInCollectionSpace();

		SetValue<FBox>(Context, BoundingBoxInCollectionSpace, &BoundingBox);
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


void FMakeSphereDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FSphere>(&Sphere))
	{
		FVector CenterVal = GetValue<FVector>(Context, &Center);
		float RadiusVal = GetValue<float>(Context, &Radius);

		SetValue<FSphere>(Context, FSphere(CenterVal, RadiusVal), &Sphere);
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
		if (bDeterministic)
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

		if (bDeterministic)
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
		if (bDeterministic)
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

		if (bDeterministic)
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
		if (Array.Num() > 0 && Index >= 0 && Index < Array.Num())
		{
			SetValue<FVector>(Context, Array[Index], &Point);
		}
	}
}

void FGetNumArrayElementsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&NumElements))
	{
		if (IsConnected<TArray<FVector>>(&Points))
		{
			SetValue<int32>(Context, GetValue<TArray<FVector>>(Context, &Points).Num(), &NumElements);
		}
		else if (IsConnected<TArray<FVector3f>>(&Vector3fArray))
		{
			SetValue<int32>(Context, GetValue<TArray<FVector3f>>(Context, &Vector3fArray).Num(), &NumElements);
		}
	}
}

void FGetBoundingBoxesFromCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FBox>>(&BoundingBoxes))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FBoundsFacade BoundsFacade(InCollection);
		const TManagedArray<FBox>& InBoundingBoxes = BoundsFacade.GetBoundingBoxes();

		GeometryCollection::Facades::FCollectionTransformFacade TransformFacade(InCollection);

		TArray<FBox> BoundingBoxesArr;
		for (int32 Idx = 0; Idx < InBoundingBoxes.Num(); ++Idx)
		{
			const FBox BoundingBoxInBoneSpace = InBoundingBoxes[Idx];

			// Transform from BoneSpace to CollectionSpace
			const FTransform CollectionSpaceTransform = TransformFacade.ComputeCollectionSpaceTransform(Idx);
			const FBox BoundingBoxInCollectionSpace = BoundingBoxInBoneSpace.TransformBy(CollectionSpaceTransform);

			if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
			{
				if (InTransformSelection.IsSelected(Idx))
				{
					BoundingBoxesArr.Add(BoundingBoxInCollectionSpace);
				}
			}
			else
			{
				BoundingBoxesArr.Add(BoundingBoxInCollectionSpace);
			}

		}

		SetValue<TArray<FBox>>(Context, BoundingBoxesArr, &BoundingBoxes);
	}
}

void FGetCentroidsFromCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Centroids))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FBoundsFacade BoundsFacade(InCollection);
		const TArray<FVector>& InCentroids = BoundsFacade.GetCentroids();

		GeometryCollection::Facades::FCollectionTransformFacade TransformFacade(InCollection);

		TArray<FVector> CentroidsArr;
		for (int32 Idx = 0; Idx < InCentroids.Num(); ++Idx)
		{
			const FVector PositionInBoneSpace(InCentroids[Idx]);

			// Transform from BoneSpace to CollectionSpace
			const FTransform CollectionSpaceTransform = TransformFacade.ComputeCollectionSpaceTransform(Idx);
			const FVector PositionInCollectionSpace = CollectionSpaceTransform.TransformPosition(PositionInBoneSpace);

			if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
			{
				if (InTransformSelection.IsSelected(Idx))
				{
					CentroidsArr.Add(PositionInCollectionSpace);
				}
			}
			else
			{
				CentroidsArr.Add(PositionInCollectionSpace);
			}
		}

		SetValue<TArray<FVector>>(Context, CentroidsArr, &Centroids);
	}
}


void FTransformCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FTransform NewTransform = GeometryCollection::Facades::FCollectionTransformFacade::BuildTransform(Translate,
			(uint8)RotationOrder,
			Rotate,
			Scale,
			UniformScale,
			RotatePivot,
			ScalePivot,
			bInvertTransformation);

		GeometryCollection::Facades::FCollectionTransformFacade TransformFacade(InCollection);
		TransformFacade.Transform(NewTransform);

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}


void FBakeTransformsInCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformFacade TransformFacade(InCollection);
		const TArray<FTransform>& CollectionSpaceTransforms = TransformFacade.ComputeCollectionSpaceTransforms();

		GeometryCollection::Facades::FCollectionMeshFacade MeshFacade(InCollection);

		const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);

		for (int32 TransformIdx = 0; TransformIdx < NumTransforms; ++TransformIdx)
		{
			MeshFacade.BakeTransform(TransformIdx, CollectionSpaceTransforms[TransformIdx]);
			TransformFacade.SetBoneTransformToIdentity(TransformIdx);
		}

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}


void FTransformMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		if (TObjectPtr<const UDynamicMesh> InMesh = GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh))
		{
			// Creating a new mesh object from InMesh
			TObjectPtr<UDynamicMesh> NewMesh = NewObject<UDynamicMesh>();
			NewMesh->SetMesh(InMesh->GetMeshRef());

			FTransform NewTransform = GeometryCollection::Facades::FCollectionTransformFacade::BuildTransform(Translate,
				(uint8)RotationOrder,
				Rotate,
				Scale,
				UniformScale,
				RotatePivot,
				ScalePivot,
				bInvertTransformation);

			UE::Geometry::FDynamicMesh3& DynamicMesh = NewMesh->GetMeshRef();

			MeshTransforms::ApplyTransform(DynamicMesh, UE::Geometry::FTransformSRT3d(NewTransform), true);

			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewMesh, &Mesh);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
		}
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
		bool InCondition = GetValue<bool>(Context, &bCondition);

		if (InCondition)
		{
			if (TObjectPtr<UDynamicMesh> InMeshA = GetValue<TObjectPtr<UDynamicMesh>>(Context, &MeshA))
			{
				SetValue<TObjectPtr<UDynamicMesh>>(Context, InMeshA, &Mesh);

				return;
			}
		}
		else
		{
			if (TObjectPtr<UDynamicMesh> InMeshB = GetValue<TObjectPtr<UDynamicMesh>>(Context, &MeshB))
			{
				SetValue<TObjectPtr<UDynamicMesh>>(Context, InMeshB, &Mesh);

				return;
			}
		}

		SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
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
		const bool& InEnableRemoval = GetValue(Context, &bEnabledRemoval, true);
		const FVector2f& InPostBreakTimer = GetValue(Context, &PostBreakTimer);
		const FVector2f& InRemovalTimer = GetValue(Context, &RemovalTimer);
		const bool& InClusterCrumbling = GetValue(Context, &bClusterCrumbling);

		// we are making a copy of the collection because we are modifying it 
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FCollectionRemoveOnBreakFacade RemoveOnBreakFacade(InCollection);
		RemoveOnBreakFacade.DefineSchema();

		GeometryCollection::Facades::FRemoveOnBreakData Data;
		Data.SetBreakTimer(InPostBreakTimer.X, InPostBreakTimer.Y);
		Data.SetRemovalTimer(InRemovalTimer.X, InRemovalTimer.Y);
		Data.SetEnabled(InEnableRemoval);
		Data.SetClusterCrumbling(InClusterCrumbling);

		// selection is optional
		if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			const FDataflowTransformSelection& InTransformSelection = GetValue(Context, &TransformSelection);
			TArray<int32> TransformIndices;
			InTransformSelection.AsArray(TransformIndices);
			RemoveOnBreakFacade.SetFromIndexArray(TransformIndices, Data);
		}
		else
		{
			RemoveOnBreakFacade.SetToAll(Data);
		}

		// move the collection to the output to avoid making another copy
		SetValue<FManagedArrayCollection>(Context, MoveTemp(InCollection), &Collection);
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

			if (bSetNotSelectedBonesToOppositeState)
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
			Properties.RequireContactAmount = ContactThreshold;

			GeomCollection->SetProximityProperties(Properties);

			// Invalidate proximity
			FGeometryCollectionProximityUtility ProximityUtility(GeomCollection.Get());
			ProximityUtility.InvalidateProximity();
			ProximityUtility.UpdateProximity();

			SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
		}
	}
}


void FCollectionSetPivotDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FTransform& InTransform = GetValue<FTransform>(Context, &Transform);

		GeometryCollection::Facades::FCollectionTransformFacade TransformFacade(InCollection);
		TransformFacade.SetPivot(InTransform);

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}


static FName GetGroupName(const EStandardGroupNameEnum& InGroupName)
{
	FName GroupNameToUse;
	if (InGroupName == EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Transform)
	{
		GroupNameToUse = FGeometryCollection::TransformGroup;
	}
	else if (InGroupName == EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Geometry)
	{
		GroupNameToUse = FGeometryCollection::GeometryGroup;
	}
	else if (InGroupName == EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Faces)
	{
		GroupNameToUse = FGeometryCollection::FacesGroup;
	}
	else if (InGroupName == EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Vertices)
	{
		GroupNameToUse = FGeometryCollection::VerticesGroup;
	}
	else if (InGroupName == EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Material)
	{
		GroupNameToUse = FGeometryCollection::MaterialGroup;
	}
	else if (InGroupName == EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Breaking)
	{
		GroupNameToUse = FGeometryCollection::BreakingGroup;
	}

	return GroupNameToUse;
}

void FAddCustomCollectionAttributeDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const int32 InNumElements = GetValue<int32>(Context, &NumElements);

		FName GroupNameToUse;
		if (GroupName != EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Custom)
		{
			GroupNameToUse = GetGroupName(GroupName);
		}
		else
		{
			GroupNameToUse = FName(*CustomGroupName);
		}

		if (GroupNameToUse.GetStringLength() > 0 && AttrName.Len() > 0)
		{
			if (CustomAttributeType == ECustomAttributeTypeEnum::Dataflow_CustomAttributeType_Bool)
			{
				TManagedArrayAccessor<bool> CustomAttribute(InCollection, FName(*AttrName), GroupNameToUse);

				// If the group already exists don't change the number of elements
				if (!InCollection.HasGroup(GroupNameToUse))
				{
					CustomAttribute.AddElements(InNumElements);
				}

				CustomAttribute.AddAndFill(false);
			}
			else if (CustomAttributeType == ECustomAttributeTypeEnum::Dataflow_CustomAttributeType_Float)
			{
				TManagedArrayAccessor<float> CustomAttribute(InCollection, FName(*AttrName), GroupNameToUse);

				// If the group already exists don't change the number of elements
				if (!InCollection.HasGroup(GroupNameToUse))
				{
					CustomAttribute.AddElements(InNumElements);
				}

				CustomAttribute.AddAndFill(0.f);
			}
			else if (CustomAttributeType == ECustomAttributeTypeEnum::Dataflow_CustomAttributeType_Int)
			{
				TManagedArrayAccessor<int32> CustomAttribute(InCollection, FName(*AttrName), GroupNameToUse);

				// If the group already exists don't change the number of elements
				if (!InCollection.HasGroup(GroupNameToUse))
				{
					CustomAttribute.AddElements(InNumElements);
				}

				CustomAttribute.AddAndFill(0);
			}
			else if (CustomAttributeType == ECustomAttributeTypeEnum::Dataflow_CustomAttributeType_Vector)
			{
				TManagedArrayAccessor<FVector> CustomAttribute(InCollection, FName(*AttrName), GroupNameToUse);

				// If the group already exists don't change the number of elements
				if (!InCollection.HasGroup(GroupNameToUse))
				{
					CustomAttribute.AddElements(InNumElements);
				}

				CustomAttribute.AddAndFill(FVector(EForceInit::ForceInitToZero));
			}
		}

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}


void FGetNumElementsInCollectionGroupDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&NumElements))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		
		int32 OutNumElements = 0;

		FName GroupNameToUse;
		if (GroupName != EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Custom)
		{
			GroupNameToUse = GetGroupName(GroupName);
		}
		else
		{
			GroupNameToUse = FName(*CustomGroupName);
		}

		if (GroupNameToUse.GetStringLength() > 0)
		{
			if (InCollection.HasGroup(GroupNameToUse))
			{
				OutNumElements = InCollection.NumElements(GroupNameToUse);
			}
		}

		SetValue<int32>(Context, OutNumElements, &NumElements);
	}
}


void FGetCollectionAttributeDataTypedDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<bool>>(&BoolAttributeData) ||
		Out->IsA<TArray<float>>(&FloatAttributeData) ||
		Out->IsA<TArray<int32>>(&IntAttributeData) ||
		Out->IsA<TArray<FVector3f>>(&VectorAttributeData))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FName GroupNameToUse;
		if (GroupName != EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Custom)
		{
			GroupNameToUse = GetGroupName(GroupName);
		}
		else
		{
			GroupNameToUse = FName(*CustomGroupName);
		}

		if (GroupNameToUse.GetStringLength() > 0 && AttrName.Len() > 0)
		{
			if (InCollection.HasGroup(GroupNameToUse))
			{
				if (InCollection.HasAttribute(FName(*AttrName), GroupNameToUse))
				{
					FString TypeStr = GetArrayTypeString(InCollection.GetAttributeType(FName(*AttrName), GroupNameToUse)).ToString();

					if (Out->IsA<TArray<bool>>(&BoolAttributeData))
					{
						if (TypeStr == FString("bool"))
						{
							const TManagedArray<bool>& BoolAttributeArr = InCollection.GetAttribute<bool>(FName(*AttrName), GroupNameToUse);

							SetValue<TArray<bool>>(Context, BoolAttributeArr.GetConstArray(), &BoolAttributeData);
						}
						else
						{
							SetValue<TArray<bool>>(Context, TArray<bool>(), &BoolAttributeData);
						}
					}
					else if (Out->IsA<TArray<float>>(&FloatAttributeData))
					{
						if (TypeStr == FString("float"))
						{
							const TManagedArray<float>& FloatAttributeArr = InCollection.GetAttribute<float>(FName(*AttrName), GroupNameToUse);

							SetValue<TArray<float>>(Context, FloatAttributeArr.GetConstArray(), &FloatAttributeData);
						}
						else
						{
							SetValue<TArray<float>>(Context, TArray<float>(), &FloatAttributeData);
						}
					}
					else if (Out->IsA<TArray<int32>>(&IntAttributeData))
					{
						if (TypeStr == FString("int32"))
						{
							const TManagedArray<int32>& IntAttributeArr = InCollection.GetAttribute<int32>(FName(*AttrName), GroupNameToUse);

							SetValue<TArray<int32>>(Context, IntAttributeArr.GetConstArray(), &IntAttributeData);
						}
						else
						{
							SetValue<TArray<int32>>(Context, TArray<int32>(), &IntAttributeData);
						}
					}
					else if (Out->IsA<TArray<FVector3f>>(&VectorAttributeData))
					{
						if (TypeStr == FString("Vector"))
						{
							const TManagedArray<FVector3f>& VectorAttributeArr = InCollection.GetAttribute<FVector3f>(FName(*AttrName), GroupNameToUse);

							SetValue<TArray<FVector3f>>(Context, VectorAttributeArr.GetConstArray(), &VectorAttributeData);
						}
						else
						{
							SetValue<TArray<FVector3f>>(Context, TArray<FVector3f>(), &VectorAttributeData);
						}
					}
				}
			}
		}
	}
}


void FSetCollectionAttributeDataTypedDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FName GroupNameToUse;
		if (GroupName != EStandardGroupNameEnum::Dataflow_EStandardGroupNameEnum_Custom)
		{
			GroupNameToUse = GetGroupName(GroupName);
		}
		else
		{
			GroupNameToUse = FName(*CustomGroupName);
		}

		if (GroupNameToUse.GetStringLength() > 0 && AttrName.Len() > 0)
		{
			if (InCollection.HasGroup(GroupNameToUse))
			{
				if (InCollection.HasAttribute(FName(*AttrName), GroupNameToUse))
				{
					FString TypeStr = GetArrayTypeString(InCollection.GetAttributeType(FName(*AttrName), GroupNameToUse)).ToString();

					if (TypeStr == FString("bool"))
					{
						if (IsConnected<TArray<bool>>(&BoolAttributeData))
						{
							TArray<bool> InBoolAttributeData = GetValue<TArray<bool>>(Context, &BoolAttributeData);

							TManagedArray<bool>& BoolAttributeArr = InCollection.ModifyAttribute<bool>(FName(*AttrName), GroupNameToUse);

							if (InBoolAttributeData.Num() == BoolAttributeArr.Num())
							{
								for (int32 Idx = 0; Idx < BoolAttributeArr.Num(); ++Idx)
								{
									BoolAttributeArr[Idx] = InBoolAttributeData[Idx];
								}
							}

							SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
						}
					}
					else if (TypeStr == FString("float"))
					{
						if (IsConnected<TArray<float>>(&FloatAttributeData))
						{
							TArray<float> InFloatAttributeData = GetValue<TArray<float>>(Context, &FloatAttributeData);

							TManagedArray<float>& FloatAttributeArr = InCollection.ModifyAttribute<float>(FName(*AttrName), GroupNameToUse);

							if (InFloatAttributeData.Num() == FloatAttributeArr.Num())
							{
								for (int32 Idx = 0; Idx < FloatAttributeArr.Num(); ++Idx)
								{
									FloatAttributeArr[Idx] = InFloatAttributeData[Idx];
								}
							}

							SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
						}
					}
					else if (TypeStr == FString("int32"))
					{
						if (IsConnected<TArray<int32>>(&IntAttributeData))
						{
							TArray<int32> InIntAttributeData = GetValue<TArray<int32>>(Context, &IntAttributeData);

							TManagedArray<int32>& IntAttributeArr = InCollection.ModifyAttribute<int32>(FName(*AttrName), GroupNameToUse);

							if (InIntAttributeData.Num() == IntAttributeArr.Num())
							{
								for (int32 Idx = 0; Idx < IntAttributeArr.Num(); ++Idx)
								{
									IntAttributeArr[Idx] = InIntAttributeData[Idx];
								}
							}

							SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
						}
					}
					else if (TypeStr == FString("Vector3d"))
					{
						if (IsConnected<TArray<FVector>>(&VectorAttributeData))
						{
							TArray<FVector> InVectorAttributeData = GetValue<TArray<FVector>>(Context, &VectorAttributeData);

							TManagedArray<FVector>& VectorAttributeArr = InCollection.ModifyAttribute<FVector>(FName(*AttrName), GroupNameToUse);

							if (InVectorAttributeData.Num() == VectorAttributeArr.Num())
							{
								for (int32 Idx = 0; Idx < VectorAttributeArr.Num(); ++Idx)
								{
									VectorAttributeArr[Idx] = InVectorAttributeData[Idx];
								}
							}

							SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
						}
					}
				}
			}
		}

		SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
	}
}


void FBoolArrayToFaceSelectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowFaceSelection>(&FaceSelection))
	{
		const TArray<bool>& InBoolAttributeData = GetValue<TArray<bool>>(Context, &BoolAttributeData);

		FDataflowFaceSelection NewFaceSelection;
		NewFaceSelection.Initialize(InBoolAttributeData.Num(), false);
		NewFaceSelection.SetFromArray(InBoolAttributeData);

		SetValue<FDataflowFaceSelection>(Context, NewFaceSelection, &FaceSelection);
	}
}


void FFloatArrayToVertexSelectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowVertexSelection>(&VertexSelection))
	{
		const TArray<float>& InFloatArray = GetValue<TArray<float>>(Context, &FloatArray);

		FDataflowVertexSelection NewVertexSelection;
		NewVertexSelection.Initialize(InFloatArray.Num(), false);

		for (int32 Idx = 0; Idx < InFloatArray.Num(); ++Idx)
		{
			if (Operation == ECompareOperationEnum::Dataflow_Compare_Equal)
			{
				if (InFloatArray[Idx] == Threshold)
				{
					NewVertexSelection.SetSelected(Idx);
				}
			}
			else if (Operation == ECompareOperationEnum::Dataflow_Compare_Smaller)
			{
				if (InFloatArray[Idx] < Threshold)
				{
					NewVertexSelection.SetSelected(Idx);
				}
			}
			else if (Operation == ECompareOperationEnum::Dataflow_Compare_SmallerOrEqual)
			{
				if (InFloatArray[Idx] <= Threshold)
				{
					NewVertexSelection.SetSelected(Idx);
				}
			}
			else if (Operation == ECompareOperationEnum::Dataflow_Compare_Greater)
			{
				if (InFloatArray[Idx] > Threshold)
				{
					NewVertexSelection.SetSelected(Idx);
				}
			}
			else if (Operation == ECompareOperationEnum::Dataflow_Compare_GreaterOrEqual)
			{
				if (InFloatArray[Idx] >= Threshold)
				{
					NewVertexSelection.SetSelected(Idx);
				}
			}
		}

		SetValue<FDataflowVertexSelection>(Context, NewVertexSelection, &VertexSelection);
	}
}


void FSetVertexColorInCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

		if (InCollection.NumElements(FGeometryCollection::VerticesGroup) == InVertexSelection.Num())
		{
			const int32 NumVertices = InCollection.NumElements(FGeometryCollection::VerticesGroup);

//			TManagedArray<FLinearColor>& VertexColors = InCollection.ModifyAttribute<FLinearColor>("Color", FGeometryCollection::VerticesGroup);
			if (TManagedArray<FLinearColor>* VertexColors = InCollection.FindAttribute<FLinearColor>("Color", FGeometryCollection::VerticesGroup))
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						(*VertexColors)[Idx] = SelectedColor;
					}
					else
					{
						(*VertexColors)[Idx] = NonSelectedColor;
					}
				}
			}

			SetValue<FManagedArrayCollection>(Context, MoveTemp(InCollection), &Collection);
		}
	}
}


void FMakeTransformDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FTransform>(&Transform))
	{
		SetValue<FTransform>(Context, FTransform(InTransform), &Transform);
	}
}



