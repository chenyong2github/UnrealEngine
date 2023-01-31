// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionFieldNodes.h"
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

#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"

#include "GeometryCollection/Facades/CollectionMeshFacade.h"
#include "GeometryCollection/Facades/CollectionTransformFacade.h"


//#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionFieldNodes)

namespace Dataflow
{
	void GeometryCollectionFieldNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadialFalloffFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPlaneFalloffFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadialIntMaskFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformScalarFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformVectorFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadialVectorFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRandomVectorFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FNoiseFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformIntegerFieldDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBoxFalloffFieldDataflowNode);

		// Field nodes
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Fields", FLinearColor(.0f, 0.8f, 1.f), CDefaultNodeBodyTintColor);
	}
}


void FRadialFalloffFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<float>>(&WeightArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);
		const FSphere& InSphere = GetValue<FSphere>(Context, &Sphere);
		const FVector& InTranslation = GetValue<FVector>(Context, &Translation);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FRadialFalloff* RadialFalloffField = new FRadialFalloff();
		RadialFalloffField->Position = InSphere.Center + InTranslation;
		RadialFalloffField->Radius = InSphere.W;
		RadialFalloffField->Magnitude = Magnitude;
		RadialFalloffField->MinRange = MinRange;
		RadialFalloffField->MaxRange = MaxRange;
		RadialFalloffField->Default = Default;
		RadialFalloffField->Falloff = (EFieldFalloffType)FalloffType;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<float> ResultsView(ResultsArray, 0, ResultsArray.Num());
		RadialFalloffField->Evaluate(FieldContext, ResultsView);

		TArray<float> NewWeightArray;
		NewWeightArray.Init(0.f, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewWeightArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewWeightArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<float>>(Context, NewWeightArray, &WeightArray);
	}
}


void FPlaneFalloffFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<float>>(&WeightArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);
		const FVector& InTranslation = GetValue<FVector>(Context, &Translation);
		const FVector& InPosition = GetValue<FVector>(Context, &Position);
		const FVector& InNormal = GetValue<FVector>(Context, &Normal);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FPlaneFalloff* PlaneFalloffField = new FPlaneFalloff();
		PlaneFalloffField->Position = InPosition + InTranslation;
		PlaneFalloffField->Normal = InNormal;
		PlaneFalloffField->Magnitude = Magnitude;
		PlaneFalloffField->MinRange = MinRange;
		PlaneFalloffField->MaxRange = MaxRange;
		PlaneFalloffField->Distance = Distance;
		PlaneFalloffField->Default = Default;
		PlaneFalloffField->Falloff = (EFieldFalloffType)FalloffType;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<float> ResultsView(ResultsArray, 0, ResultsArray.Num());
		PlaneFalloffField->Evaluate(FieldContext, ResultsView);

		TArray<float> NewWeightArray;
		NewWeightArray.Init(0.f, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewWeightArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewWeightArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<float>>(Context, NewWeightArray, &WeightArray);
	}
}


void FRadialIntMaskFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<int32>>(&MaskArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);
		const FSphere& InSphere = GetValue<FSphere>(Context, &Sphere);
		const FVector& InTranslation = GetValue<FVector>(Context, &Translation);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FRadialIntMask* RadialIntMaskField = new FRadialIntMask();
		RadialIntMaskField->Position = InSphere.Center + InTranslation;
		RadialIntMaskField->Radius = InSphere.W;
		RadialIntMaskField->InteriorValue = InteriorValue;
		RadialIntMaskField->ExteriorValue = ExteriorValue;
		RadialIntMaskField->SetMaskCondition = (ESetMaskConditionType)SetMaskCondition;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<int32> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<int32> ResultsView(ResultsArray, 0, ResultsArray.Num());
		RadialIntMaskField->Evaluate(FieldContext, ResultsView);

		TArray<int32> NewMaskArray;
		NewMaskArray.Init(0, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewMaskArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewMaskArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<int32>>(Context, NewMaskArray, &MaskArray);
	}
}


void FUniformScalarFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<float>>(&WeightArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FUniformScalar* UniformScalarField = new FUniformScalar();
		UniformScalarField->Magnitude = Magnitude;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<float> ResultsView(ResultsArray, 0, ResultsArray.Num());
		UniformScalarField->Evaluate(FieldContext, ResultsView);

		TArray<float> NewWeightArray;
		NewWeightArray.Init(0.f, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewWeightArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewWeightArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<float>>(Context, NewWeightArray, &WeightArray);
	}
}


void FUniformVectorFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&VectorArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FUniformVector* UniformVectorField = new FUniformVector();
		UniformVectorField->Magnitude = Magnitude;
		UniformVectorField->Direction = Direction;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0.f), NumVertices);
		TFieldArrayView<FVector> ResultsView(ResultsArray, 0, ResultsArray.Num());
		UniformVectorField->Evaluate(FieldContext, ResultsView);

		TArray<FVector> NewVectorArray;
		NewVectorArray.Init(FVector(0.f), NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewVectorArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewVectorArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<FVector>>(Context, NewVectorArray, &VectorArray);
	}
}


void FRadialVectorFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&VectorArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FRadialVector* RadialVectorField = new FRadialVector();
		RadialVectorField->Magnitude = Magnitude;
		RadialVectorField->Position = Position;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0.f), NumVertices);
		TFieldArrayView<FVector> ResultsView(ResultsArray, 0, ResultsArray.Num());
		RadialVectorField->Evaluate(FieldContext, ResultsView);

		TArray<FVector> NewVectorArray;
		NewVectorArray.Init(FVector(0.f), NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewVectorArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewVectorArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<FVector>>(Context, NewVectorArray, &VectorArray);
	}
}


void FRandomVectorFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&VectorArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FRandomVector* RandomVectorField = new FRandomVector();
		RandomVectorField->Magnitude = Magnitude;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0.f), NumVertices);
		TFieldArrayView<FVector> ResultsView(ResultsArray, 0, ResultsArray.Num());
		RandomVectorField->Evaluate(FieldContext, ResultsView);

		TArray<FVector> NewVectorArray;
		NewVectorArray.Init(FVector(0.f), NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewVectorArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewVectorArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<FVector>>(Context, NewVectorArray, &VectorArray);
	}
}



void FNoiseFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<float>>(&WeightArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FNoiseField* NoiseField = new FNoiseField();
		NoiseField->MinRange = MinRange;
		NoiseField->MaxRange = MaxRange;
		NoiseField->Transform = Transform;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<float> ResultsView(ResultsArray, 0, ResultsArray.Num());
		NoiseField->Evaluate(FieldContext, ResultsView);

		TArray<float> NewWeightArray;
		NewWeightArray.Init(0.f, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewWeightArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewWeightArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<float>>(Context, NewWeightArray, &WeightArray);
	}
}


void FUniformIntegerFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<int32>>(&MaskArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FUniformInteger* UniformIntegerField = new FUniformInteger();
		UniformIntegerField->Magnitude = Magnitude;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<int32> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<int32> ResultsView(ResultsArray, 0, ResultsArray.Num());
		UniformIntegerField->Evaluate(FieldContext, ResultsView);

		TArray<int32> NewMaskArray;
		NewMaskArray.Init(0, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewMaskArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewMaskArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<int32>>(Context, NewMaskArray, &MaskArray);
	}
}


void FWaveScalarFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<float>>(&WeightArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);
		const FVector& InTranslation = GetValue<FVector>(Context, &Translation);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FWaveScalar* WaveScalarField = new FWaveScalar();
		WaveScalarField->Magnitude = Magnitude;
		WaveScalarField->Position = Position + InTranslation;
		WaveScalarField->Wavelength = Wavelength;
		WaveScalarField->Period = Period;
		WaveScalarField->Function = (EWaveFunctionType)FunctionType;
		WaveScalarField->Falloff = (EFieldFalloffType)FalloffType;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<float> ResultsView(ResultsArray, 0, ResultsArray.Num());
		WaveScalarField->Evaluate(FieldContext, ResultsView);

		TArray<float> NewWeightArray;
		NewWeightArray.Init(0.f, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewWeightArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewWeightArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<float>>(Context, NewWeightArray, &WeightArray);
	}
}


void FBoxFalloffFieldDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<float>>(&WeightArray))
	{
		const TArray<FVector3f>& InVertexArray = GetValue<TArray<FVector3f>>(Context, &VertexArray);
		const FBox& InBox = GetValue<FBox>(Context, &Box);
		const FTransform& InTransform = GetValue<FTransform>(Context, &Transform);

		const int32 NumVertices = InVertexArray.Num();

		FFieldExecutionDatas ExecutionDatas;

		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, NumVertices);
		ExecutionDatas.SamplePositions.Init(FVector(0.f), NumVertices);

		for (int32 Index = 0; Index < NumVertices; Index++)
		{
			ExecutionDatas.SamplePositions[Index] = (FVector)InVertexArray[Index];
		}

		FBoxFalloff* BoxFalloffField = new FBoxFalloff();
		BoxFalloffField->Magnitude = Magnitude;
		BoxFalloffField->MinRange = MinRange;
		BoxFalloffField->MaxRange = MaxRange;
		BoxFalloffField->Default = Default;
		BoxFalloffField->Transform = InTransform;
		BoxFalloffField->Falloff = (EFieldFalloffType)FalloffType;

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			0.0
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, NumVertices);
		TFieldArrayView<float> ResultsView(ResultsArray, 0, ResultsArray.Num());
		BoxFalloffField->Evaluate(FieldContext, ResultsView);

		TArray<float> NewWeightArray;
		NewWeightArray.Init(0.f, NumVertices);

		if (IsConnected<FDataflowVertexSelection>(&VertexSelection))
		{
			const FDataflowVertexSelection& InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

			if (InVertexArray.Num() == InVertexSelection.Num())
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					if (InVertexSelection.IsSelected(Idx))
					{
						NewWeightArray[Idx] = ResultsView[Idx];
					}
				}
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				NewWeightArray[Idx] = ResultsView[Idx];
			}
		}

		SetValue<TArray<float>>(Context, NewWeightArray, &WeightArray);
	}
}










