// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowSelection.h"
#include "Field/FieldSystemTypes.h"

#include "GeometryCollectionFieldNodes.generated.h"


/**
 *
 * 
 *
 */
UENUM(BlueprintType)
enum class EDataflowFieldFalloffType : uint8
{
	Dataflow_FieldFalloffType_None			UMETA(DisplayName = "None", ToolTip = "No falloff function is used"),
	Dataflow_FieldFalloffType_Linear		UMETA(DisplayName = "Linear", ToolTip = "The falloff function will be proportional to x"),
	Dataflow_FieldFalloffType_Inverse		UMETA(DisplayName = "Inverse", ToolTip = "The falloff function will be proportional to 1.0/x"),
	Dataflow_FieldFalloffType_Squared		UMETA(DisplayName = "Squared", ToolTip = "The falloff function will be proportional to x*x"),
	Dataflow_FieldFalloffType_Logarithmic	UMETA(DisplayName = "Logarithmic", ToolTip = "The falloff function will be proportional to log(x)"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * RadialFalloff Field Dataflow node
 *
 */
USTRUCT()
struct FRadialFalloffFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialFalloffFieldDataflowNode, "RadialFalloffField", "Fields", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FSphere Sphere = FSphere(ForceInit);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field", meta = (DataflowInput))
	FVector Translation = FVector(0.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MinRange = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MaxRange = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Default = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
		EDataflowFieldFalloffType FalloffType = EDataflowFieldFalloffType::Dataflow_FieldFalloffType_Linear;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> WeightArray;

	FRadialFalloffFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Sphere);
		RegisterInputConnection(&Translation);
		RegisterOutputConnection(&WeightArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * PlaneFalloff Field Dataflow node
 *
 */
USTRUCT()
struct FPlaneFalloffFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPlaneFalloffFieldDataflowNode, "PlaneFalloffField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field", meta = (DataflowInput))
	FVector Translation = FVector(0.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MinRange = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MaxRange = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Distance = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	FVector Position = FVector(0.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	FVector Normal = FVector(0.f, 0.f, 1.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Default = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	EDataflowFieldFalloffType FalloffType = EDataflowFieldFalloffType::Dataflow_FieldFalloffType_Linear;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> WeightArray;

	FPlaneFalloffFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Position);
		RegisterInputConnection(&Normal);
		RegisterInputConnection(&Translation);
		RegisterOutputConnection(&WeightArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 *
 *
 */
UENUM(BlueprintType)
enum class EDataflowSetMaskConditionType : uint8
{
	Dataflow_SetMaskConditionType_Always			UMETA(DisplayName = "Set Always", ToolTip = "The particle output value will be equal to Interior-value if the particle position is inside a sphere / Exterior-value otherwise."),
	Dataflow_SetMaskConditionType_IFF_NOT_Interior	UMETA(DisplayName = "Merge Interior", ToolTip = "The particle output value will be equal to Interior-value if the particle position is inside the sphere or if the particle input value is already Interior-Value / Exterior-value otherwise."),
	Dataflow_SetMaskConditionType_IFF_NOT_Exterior  UMETA(DisplayName = "Merge Exterior", ToolTip = "The particle output value will be equal to Exterior-value if the particle position is outside the sphere or if the particle input value is already Exterior-Value / Interior-value otherwise."),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * RadialIntMask Field Dataflow node
 *
 */
USTRUCT()
struct FRadialIntMaskFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialIntMaskFieldDataflowNode, "RadialIntMaskField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FSphere Sphere = FSphere(ForceInit);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field", meta = (DataflowInput))
	FVector Translation = FVector(0.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	int32 InteriorValue = 1;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	int32 ExteriorValue = 0;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	EDataflowSetMaskConditionType SetMaskCondition = EDataflowSetMaskConditionType::Dataflow_SetMaskConditionType_Always;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<int32> MaskArray;

	FRadialIntMaskFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Sphere);
		RegisterInputConnection(&Translation);
		RegisterOutputConnection(&MaskArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * UniformScalar Field Dataflow node
 *
 */
USTRUCT()
struct FUniformScalarFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformScalarFieldDataflowNode, "UniformScalarField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> WeightArray;

	FUniformScalarFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterOutputConnection(&WeightArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * UniformVector Field Dataflow node
 *
 */
USTRUCT()
struct FUniformVectorFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformVectorFieldDataflowNode, "UniformVectorField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	FVector Direction = FVector(0.f);

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> VectorArray;

	FUniformVectorFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterOutputConnection(&VectorArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * RadialVector Field Dataflow node
 *
 */
USTRUCT()
struct FRadialVectorFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialVectorFieldDataflowNode, "RadialVectorField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	FVector Position = FVector(0.f);

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> VectorArray;

	FRadialVectorFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterOutputConnection(&VectorArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * RandomVector Field Dataflow node
 *
 */
USTRUCT()
struct FRandomVectorFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRandomVectorFieldDataflowNode, "RandomVectorField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> VectorArray;

	FRandomVectorFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterOutputConnection(&VectorArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Noise Field Dataflow node
 *
 */
USTRUCT()
struct FNoiseFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FNoiseFieldDataflowNode, "NoiseField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MinRange = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MaxRange = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field", meta = (DataflowInput))
	FTransform Transform = FTransform::Identity;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> WeightArray;

	FNoiseFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Transform);
		RegisterOutputConnection(&WeightArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * UniformInteger Field Dataflow node
 *
 */
USTRUCT()
struct FUniformIntegerFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformIntegerFieldDataflowNode, "UniformIntegerField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	int32 Magnitude = 0;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<int32> MaskArray;

	FUniformIntegerFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterOutputConnection(&MaskArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 *
 *
 */
UENUM(BlueprintType)
enum class EDataflowWaveFunctionType : uint8
{
	Dataflow_WaveFunctionType_Cosine	UMETA(DisplayName = "Cosine", ToolTip = "Cosine wave that will move in time."),
	Dataflow_WaveFunctionType_Gaussian  UMETA(DisplayName = "Gaussian", ToolTip = "Gaussian wave that will move in time."),
	Dataflow_WaveFunctionType_Falloff	UMETA(DisplayName = "Falloff", ToolTip = "The radial falloff radius will move along temporal wave."),
	Dataflow_WaveFunctionType_Decay		UMETA(DisplayName = "Decay", ToolTip = "The magnitude of the field will decay in time."),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * WaveScalar Field Dataflow node v2
 *
 */
USTRUCT()
struct FWaveScalarFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FWaveScalarFieldDataflowNode, "WaveScalarField", "Fields", "")

public:
	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FVector Position = FVector(0.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field", meta = (DataflowInput))
	FVector Translation = FVector(0.f);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Wavelength = 1000.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Period = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	EDataflowWaveFunctionType FunctionType = EDataflowWaveFunctionType::Dataflow_WaveFunctionType_Cosine;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	EDataflowFieldFalloffType FalloffType = EDataflowFieldFalloffType::Dataflow_FieldFalloffType_Linear;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> WeightArray;

	FWaveScalarFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Translation);
		RegisterOutputConnection(&WeightArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * BoxFalloff Field Dataflow node
 *
 */
USTRUCT()
struct FBoxFalloffFieldDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FBoxFalloffFieldDataflowNode, "BoxFalloffField", "Fields", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector3f> VertexArray;

	/**  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/**  */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FBox Box = FBox(ForceInit);

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field", meta = (DataflowInput))
	FTransform Transform = FTransform::Identity;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Magnitude = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MinRange = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float MaxRange = 1.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	float Default = 0.f;

	/**  */
	UPROPERTY(EditAnywhere, Category = "Field")
	EDataflowFieldFalloffType FalloffType = EDataflowFieldFalloffType::Dataflow_FieldFalloffType_Linear;

	/**  */
	UPROPERTY(meta = (DataflowOutput))
	TArray<float> WeightArray;

	FBoxFalloffFieldDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexArray);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Box);
		RegisterInputConnection(&Transform);
		RegisterOutputConnection(&WeightArray);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};




namespace Dataflow
{
	void GeometryCollectionFieldNodes();
}
