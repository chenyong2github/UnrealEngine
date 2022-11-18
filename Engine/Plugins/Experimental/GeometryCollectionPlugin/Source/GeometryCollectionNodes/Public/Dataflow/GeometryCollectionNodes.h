// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "Dataflow/DataflowSelection.h"

#include "GeometryCollectionNodes.generated.h"


class FGeometryCollection;
class UStaticMesh;

USTRUCT()
struct FGetCollectionAssetDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetCollectionAssetDataflowNode, "GetCollectionAsset", "GeometryCollection", "")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Collection"))
	mutable FManagedArrayCollection Output;

	FGetCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Output);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
	Description for this node
*/
USTRUCT()
struct FExampleCollectionEditDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExampleCollectionEditDataflowNode, "ExampleCollectionEdit", "GeometryCollection", "")

public:
	typedef FManagedArrayCollection DataType;

	/** Description for this parameter */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (EditCondition = "false", EditConditionHides));
	float Scale = 1.0;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	FExampleCollectionEditDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Collection, &Collection);
		RegisterInputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FSetCollectionAssetDataflowNode : public FDataflowTerminalNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSetCollectionAssetDataflowNode, "SetCollectionAsset", "GeometryCollection", "")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	FSetCollectionAssetDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowTerminalNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT()
struct FAppendCollectionAssetsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FAppendCollectionAssetsDataflowNode, "AppendCollections", "GeometryCollection", "")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection1"))
	FManagedArrayCollection Collection1;

	UPROPERTY(meta = (DataflowInput, DisplayName = "Collection"))
	FManagedArrayCollection Collection2;


	FAppendCollectionAssetsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection1);
		RegisterInputConnection(&Collection2);
		RegisterOutputConnection(&Collection1, &Collection1);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FResetGeometryCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FResetGeometryCollectionDataflowNode, "ResetGeometryCollection", "GeometryCollection", "")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowOutput))
	FManagedArrayCollection Collection;

	FResetGeometryCollectionDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT()
struct FPrintStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPrintStringDataflowNode, "PrintString", "Development", "")

public:
	UPROPERTY(EditAnywhere, Category = "Print");
	bool PrintToScreen = true;

	UPROPERTY(EditAnywhere, Category = "Print");
	bool PrintToLog = true;

	UPROPERTY(EditAnywhere, Category = "Print");
	FColor Color = FColor::White;

	UPROPERTY(EditAnywhere, Category = "Print");
	float Duration = 2.f;

	UPROPERTY(EditAnywhere, Category = "Print", meta = (DataflowInput));
	FString String = FString("");

	FPrintStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT()
struct FLogStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FLogStringDataflowNode, "LogString", "Development", "")

public:
	UPROPERTY(EditAnywhere, Category = "Print");
	bool PrintToLog = true;

	UPROPERTY(EditAnywhere, Category = "Print", meta = (DataflowInput));
	FString String = FString("");

	FLogStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT()
struct FMakeLiteralStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralStringDataflowNode, "MakeLiteralString", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "String");
	FString Value = FString("");

	UPROPERTY(meta = (DataflowOutput))
	FString String;

	FMakeLiteralStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FBoundingBoxDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FBoundingBoxDataflowNode, "BoundingBox", "Utilities|Box", "")

public:
	typedef FManagedArrayCollection DataType;

	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowOutput))
	FBox BoundingBox = FBox(ForceInit);

	FBoundingBoxDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&BoundingBox);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FExpandBoundingBoxDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExpandBoundingBoxDataflowNode, "ExpandBoundingBox", "Utilities|Box", "")

public:
	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);;

	UPROPERTY(meta = (DataflowOutput))
	FVector Min = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput))
	FVector Max = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput))
	FVector Center = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput))
	FVector HalfExtents = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput))
	float Volume = 0.0;

	FExpandBoundingBoxDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterOutputConnection(&Min);
		RegisterOutputConnection(&Max);
		RegisterOutputConnection(&Center);
		RegisterOutputConnection(&HalfExtents);
		RegisterOutputConnection(&Volume);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FVectorToStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FVectorToStringDataflowNode, "VectorToString", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "Vector", meta = (DataflowInput))
	FVector Vector = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput))
	FString String = FString("");

	FVectorToStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Vector);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FFloatToStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FFloatToStringDataflowNode, "FloatToString", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "Float", meta = (DataflowInput))
	float Float = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	FString String = FString("");

	FFloatToStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Float);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FMakePointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakePointsDataflowNode, "MakePoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Point")
	TArray<FVector> Point;

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FMakePointsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

UENUM(BlueprintType)
enum class EMakeBoxDataTypeEnum : uint8
{
	Dataflow_MakeBox_DataType_MinMax UMETA(DisplayName = "Min/Max"),
	Dataflow_MakeBox_DataType_CenterSize UMETA(DisplayName = "Center/Size"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

USTRUCT()
struct FMakeBoxDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeBoxDataflowNode, "MakeBox", "Generators|Box", "")

public:
	UPROPERTY(EditAnywhere, Category = "Box", meta = (DisplayName = "Input Data Type"));
	EMakeBoxDataTypeEnum DataType = EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax;

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax", EditConditionHides));
	FVector Min = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_MinMax", EditConditionHides));
	FVector Max = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_CenterSize", EditConditionHides));
	FVector Center = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Box", meta = (DataflowInput, EditCondition = "DataType == EMakeBoxDataTypeEnum::Dataflow_MakeBox_DataType_CenterSize", EditConditionHides));
	FVector Size = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput));
	FBox Box = FBox(ForceInit);

	FMakeBoxDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Min);
		RegisterInputConnection(&Max);
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Size);
		RegisterOutputConnection(&Box);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FUniformScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FUniformScatterPointsDataflowNode, "UniformScatterPoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	int32 MinNumberOfPoints = 20;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	int32 MaxNumberOfPoints = 20;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FUniformScatterPointsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&MinNumberOfPoints);
		RegisterInputConnection(&MaxNumberOfPoints);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FRadialScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialScatterPointsDataflowNode, "RadialScatterPoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Center = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Normal = FVector(0.0, 0.0, 1.0);

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 0.01f));
	float Radius = 50.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50));
	int32 AngularSteps = 5;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50));
	int32 RadialSteps = 5;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float AngleOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 0.f));
	float Variability = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FRadialScatterPointsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Normal);
		RegisterInputConnection(&Radius);
		RegisterInputConnection(&AngularSteps);
		RegisterInputConnection(&RadialSteps);
		RegisterInputConnection(&AngleOffset);
		RegisterInputConnection(&Variability);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


//
// GridScatterPoints
//



USTRUCT()
struct FMakeLiteralFloatDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralFloatDataflowNode, "MakeLiteralFloat", "Math|Float", "")

public:
	UPROPERTY(EditAnywhere, Category = "Float");
	float Value = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Float = 0.f;

	FMakeLiteralFloatDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Float);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FMakeLiteralIntDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralIntDataflowNode, "MakeLiteralInt", "Math|Int", "")

public:
	UPROPERTY(EditAnywhere, Category = "Int");
	int32 Value = 0;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Int"))
	int32 Int = 0;

	FMakeLiteralIntDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Int);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

USTRUCT()
struct FMakeLiteralBoolDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralBoolDataflowNode, "MakeLiteralBool", "Math|Boolean", "")

public:
	UPROPERTY(EditAnywhere, Category = "Bool");
	bool Value = false;

	UPROPERTY(meta = (DataflowOutput))
	bool Bool = false;

	FMakeLiteralBoolDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Bool);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts an integer to a float
 *
 */
USTRUCT()
struct FMakeLiteralVectorDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FMakeLiteralVectorDataflowNode, "MakeLiteralVector", "Math|Vector", "")

public:
	UPROPERTY(EditAnywhere, Category = "Vector");
	FVector Value = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput, DisplayName = "Vector"))
	FVector Vector = FVector(0.0);

	FMakeLiteralVectorDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Vector);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts an Int to a String
 *
 */
USTRUCT()
struct FIntToStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FIntToStringDataflowNode, "IntToString", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "Int", meta = (DataflowInput))
	int32 Int = 0;

	UPROPERTY(meta = (DataflowOutput))
	FString String = FString("");

	FIntToStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Int);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts a Bool to a String in a form of ("true", "false")
 *
 */
USTRUCT()
struct FBoolToStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FBoolToStringDataflowNode, "BoolToString", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "Bool", meta = (DataflowInput))
	bool Bool = false;

	UPROPERTY(meta = (DataflowOutput))
	FString String = FString("");

	FBoolToStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Bool);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Expands a Vector into X, Y, Z components
 *
 */
USTRUCT()
struct FExpandVectorDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExpandVectorDataflowNode, "ExpandVector", "Utilities|Vector", "")

public:
	UPROPERTY(meta = (DataflowInput))
	FVector Vector = FVector(0.0);

	UPROPERTY(meta = (DataflowOutput))
	float X = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Y = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Z = 0.f;


	FExpandVectorDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Vector);
		RegisterOutputConnection(&X);
		RegisterOutputConnection(&Y);
		RegisterOutputConnection(&Z);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts an Int to a Float
 *
 */
USTRUCT()
struct FIntToFloatDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FIntToFloatDataflowNode, "IntToFloat", "Math|Conversions", "")

public:
	UPROPERTY(EditAnywhere, Category = "Int", meta = (DataflowInput))
	int32 Int = 0;

	UPROPERTY(meta = (DataflowOutput))
	float Float = 0.f;

	FIntToFloatDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Int);
		RegisterOutputConnection(&Float);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates a Voronoi fracture
 *
 */
USTRUCT()
struct FVoronoiFractureDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FVoronoiFractureDataflowNode, "VoronoiFracture", "Fracture", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput))
	TArray<FVector> Points;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f));
	float ChanceToFracture = 1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture");
	bool GroupFracture = true;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f));
	float CollisionSampleSpacing = 50.f;

	FVoronoiFractureDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Points);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Concatenates two strings together to make a new string
 *
 */
USTRUCT()
struct FStringAppendDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FStringAppendDataflowNode, "StringAppend", "Utilities|String", "")

public:
	UPROPERTY(EditAnywhere, Category = "String", meta = (DataflowInput))
	FString String1 = FString("");

	UPROPERTY(EditAnywhere, Category = "String", meta = (DataflowInput))
	FString String2 = FString("");

	UPROPERTY(meta = (DataflowOutput))
	FString String = FString("");

	FStringAppendDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&String1);
		RegisterInputConnection(&String2);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates a random float
 *
 */
USTRUCT()
struct FRandomFloatDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FRandomFloatDataflowNode, "RandomFloat", "Math|Random", "")

public:
	UPROPERTY(EditAnywhere, Category = "Seed")
	bool Deterministic = false;

	UPROPERTY(EditAnywhere, Category = "Seed", meta = (DataflowInput, EditCondition = "Deterministic"))
	float RandomSeed = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Float = 0.f;

	FRandomFloatDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::FRandRange(-1e5, 1e5);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Float);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Generates a random float between Min and Max
 *
 */
USTRUCT()
struct FRandomFloatInRangeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FRandomFloatInRangeDataflowNode, "RandomFloatInRange", "Math|Random", "")

public:
	UPROPERTY(EditAnywhere, Category = "Seed")
	bool Deterministic = false;

	UPROPERTY(EditAnywhere, Category = "Seed", meta = (DataflowInput, EditCondition = "Deterministic"))
	float RandomSeed = 0.f;
	
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput))
	float Min = 0.f;

	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput))
	float Max = 1.f;

	UPROPERTY(meta = (DataflowOutput))
	float Float = 0.f;

	FRandomFloatInRangeDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::FRandRange(-1e5, 1e5);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&Min);
		RegisterInputConnection(&Max);
		RegisterOutputConnection(&Float);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Returns a random vector with length of 1
 *
 */
USTRUCT()
struct FRandomUnitVectorDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRandomUnitVectorDataflowNode, "RandomUnitVector", "Math|Random", "")

public:
	UPROPERTY(EditAnywhere, Category = "Seed")
	bool Deterministic = false;

	UPROPERTY(EditAnywhere, Category = "Seed", meta = (DataflowInput, EditCondition = "Deterministic"))
	float RandomSeed = 0.f;
	
	UPROPERTY(meta = (DataflowOutput))
	FVector Vector = FVector(0.0);

	FRandomUnitVectorDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::FRandRange(-1e5, 1e5);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Vector);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Returns a random vector with length of 1, within the specified cone, with uniform random distribution 
 *
 */
USTRUCT()
struct FRandomUnitVectorInConeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRandomUnitVectorInConeDataflowNode, "RandomUnitVectorInCone", "Math|Random", "")

public:
	UPROPERTY(EditAnywhere, Category = "Seed")
	bool Deterministic = false;

	UPROPERTY(EditAnywhere, Category = "Seed", meta = (DataflowInput, EditCondition = "Deterministic"))
	float RandomSeed = 0.f;

	/** The base "center" direction of the cone */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput))
	FVector ConeDirection = FVector(0.0, 0.0, 1.0);

	/** The half-angle of the cone (from ConeDir to edge), in degrees */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput))
	float ConeHalfAngle = PI / 4.f;

	UPROPERTY(meta = (DataflowOutput))
	FVector Vector = FVector(0.0);

	FRandomUnitVectorInConeDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::FRandRange(-1e5, 1e5);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ConeDirection);
		RegisterInputConnection(&ConeHalfAngle);
		RegisterOutputConnection(&Vector);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts radians to degrees
 *
 */
USTRUCT()
struct FRadiansToDegreesDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadiansToDegreesDataflowNode, "RadiansToDegrees", "Math|Trigonometry", "")

public:
	UPROPERTY(EditAnywhere, Category = "Radians", meta = (DataflowInput))
	float Radians = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Degrees = 0.f;

	FRadiansToDegreesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Radians);
		RegisterOutputConnection(&Degrees);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts degrees to radians
 *
 */
USTRUCT()
struct FDegreesToRadiansDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDegreesToRadiansDataflowNode, "DegreesToRadians", "Math|Trigonometry", "")

public:
	UPROPERTY(EditAnywhere, Category = "Degrees", meta = (DataflowInput))
	float Degrees = 0.f;

	UPROPERTY(meta = (DataflowOutput))
	float Radians = 0.f;

	FDegreesToRadiansDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Degrees);
		RegisterOutputConnection(&Radians);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * "Explodes" the pieces from the Collection for better visualization
 *
 */
USTRUCT()
struct FExplodedViewDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FExplodedViewDataflowNode, "ExplodedView", "Fracture|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Scale", meta = (DataflowInput))
	float UniformScale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Scale", meta = (DataflowInput))
	FVector Scale = FVector(1.0);

	FExplodedViewDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&UniformScale);
		RegisterInputConnection(&Scale);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

private:
	// todo(chaos) this is a copy of a function in FractureEditorModeToolkit, we should move this to a common place  
	static bool GetValidGeoCenter(FGeometryCollection* Collection, const TManagedArray<int32>& TransformToGeometryIndex, const TArray<FTransform>& Transforms, const TManagedArray<TSet<int32>>& Children, const TManagedArray<FBox>& BoundingBox, int32 TransformIndex, FVector& OutGeoCenter);

};


/**
 *
 * Generates convex hull representation for the bones for simulation
 *
 */
USTRUCT()
struct FCreateNonOverlappingConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCreateNonOverlappingConvexHullsDataflowNode, "CreateNonOverlappingConvexHulls", "Fracture|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.01f, UIMax = 1.f))
	float CanRemoveFraction = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.f))
	float CanExceedFraction = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.f))
	float SimplificationDistanceThreshold = 10.f;

	FCreateNonOverlappingConvexHullsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&CanRemoveFraction);
		RegisterInputConnection(&CanExceedFraction);
		RegisterInputConnection(&SimplificationDistanceThreshold);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Cuts geometry using a set of noised up planes
 *
 */
USTRUCT()
struct FPlaneCutterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPlaneCutterDataflowNode, "PlaneCutter", "Fracture", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 1))
	int32 NumPlanes = 1;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput))
	float RandomSeed = -1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f))
	float Grout = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f));
	float CollisionSampleSpacing = 50.f;

	FPlaneCutterDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&NumPlanes);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates a hash value from a string
 *
 */
USTRUCT()
struct FHashStringDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FHashStringDataflowNode, "HashString", "Utilities|String", "")

public:
	/** String to hash */
	UPROPERTY(meta = (DataflowInput))
	FString String = FString("");

	/** Generated hash value */
	UPROPERTY(meta = (DataflowOutput))
	int32 Hash = 0;

	FHashStringDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&String);
		RegisterOutputConnection(&Hash);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates a hash value from a vector
 *
 */
USTRUCT()
struct FHashVectorDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FHashVectorDataflowNode, "HashVector", "Utilities|Vector", "")

public:
	/** Vector to hash */
	UPROPERTY(meta = (DataflowInput))
	FVector Vector = FVector(0.0);

	/** Generated hash value */
	UPROPERTY(meta = (DataflowOutput))
	int32 Hash = 0;

	FHashVectorDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Vector);
		RegisterOutputConnection(&Hash);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EFloatToIntFunctionEnum : uint8
{
	Dataflow_FloatToInt_Function_Floor UMETA(DisplayName = "Floor()"),
	Dataflow_FloatToInt_Function_Ceil UMETA(DisplayName = "Ceil()"),
	Dataflow_FloatToInt_Function_Round UMETA(DisplayName = "Round()"),
	Dataflow_FloatToInt_Function_Truncate UMETA(DisplayName = "Truncate()"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Converts a Float to Int using the specified method
 *
 */
USTRUCT()
struct FFloatToIntDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FFloatToIntDataflowNode, "FloatToInt", "Math|Conversions", "")

public:
	/** Method to convert */
	UPROPERTY(EditAnywhere, Category = "Float");
	EFloatToIntFunctionEnum Function = EFloatToIntFunctionEnum::Dataflow_FloatToInt_Function_Round;

	/** Float value to convert */
	UPROPERTY(EditAnywhere, Category = "Float", meta = (DataflowInput))
	float Float = 0.f;

	/** Int output */
	UPROPERTY(meta = (DataflowOutput))
	int32 Int = 0;

	FFloatToIntDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Float);
		RegisterOutputConnection(&Int);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EMathConstantsEnum : uint8
{
	Dataflow_MathConstants_Pi UMETA(DisplayName = "Pi"),
	Dataflow_MathConstants_HalfPi UMETA(DisplayName = "HalfPi"),
	Dataflow_MathConstants_TwoPi UMETA(DisplayName = "TwoPi"),
	Dataflow_MathConstants_FourPi UMETA(DisplayName = "FourPi"),
	Dataflow_MathConstants_InvPi UMETA(DisplayName = "InvPi"),
	Dataflow_MathConstants_InvTwoPi UMETA(DisplayName = "InvTwoPi"),
	Dataflow_MathConstants_Sqrt2 UMETA(DisplayName = "Sqrt2"),
	Dataflow_MathConstants_InvSqrt2 UMETA(DisplayName = "InvSqrt2"),
	Dataflow_MathConstants_Sqrt3 UMETA(DisplayName = "Sqrt3"),
	Dataflow_MathConstants_InvSqrt3 UMETA(DisplayName = "InvSqrt3"),
	Dataflow_FloatToInt_Function_E UMETA(DisplayName = "e"),
	Dataflow_FloatToInt_Function_Gamma UMETA(DisplayName = "Gamma"),
	Dataflow_FloatToInt_Function_GoldenRatio UMETA(DisplayName = "GoldenRatio"),
	Dataflow_FloatToInt_Function_ZeroTolerance UMETA(DisplayName = "ZeroTolerance"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};


/**
 *
 * Offers a selection of Math constants
 *
 */
USTRUCT()
struct FMathConstantsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMathConstantsDataflowNode, "MathConstants", "Math|Utilities", "")

public:
	/** Math constant to output */
	UPROPERTY(EditAnywhere, Category = "Constants");
	EMathConstantsEnum Constant = EMathConstantsEnum::Dataflow_MathConstants_Pi;

	/** Selected Math constant */
	UPROPERTY(meta = (DataflowOutput))
	float Float = 0;

	FMathConstantsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Float);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Returns the specified element from an array
 *
 */
USTRUCT()
struct FGetArrayElementDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetArrayElementDataflowNode, "GetArrayElement", "Utilities|Array", "")

public:
	/** Element index */
	UPROPERTY(EditAnywhere, Category = "Index");
	int32 Index = 0;

	/** Array to get the element from */
	UPROPERTY(meta = (DataflowInput))
	TArray<FVector> Points;

	/** Specified element */
	UPROPERTY(meta = (DataflowOutput))
	FVector Point = FVector(0.0);

	FGetArrayElementDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Points);
		RegisterInputConnection(&Index);
		RegisterOutputConnection(&Point);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Returns the number of elements in an array
 *
 */
USTRUCT()
struct FGetNumArrayElementsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetNumArrayElementsDataflowNode, "GetNumArrayElements", "Utilities|Array", "")

public:
	/** Array input */
	UPROPERTY(meta = (DataflowInput))
	TArray<FVector> Points;

	/** Number of elements in the array */
	UPROPERTY(meta = (DataflowOutput))
	int32 NumElements = 0;

	FGetNumArrayElementsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Points);
		RegisterOutputConnection(&NumElements);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Gets BoundingBoxes of pieces from a Collection
 *
 */
USTRUCT()
struct FGetBoundingBoxesDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetBoundingBoxesDataflowNode, "GetBoundingBoxes", "GeometryCollection", "")

public:
	/** Input Collection */
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** The BoundingBoxes will be output for the bones selected in the TransformSelection  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Output BoundingBoxes */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FBox> BoundingBoxes;

	FGetBoundingBoxesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&BoundingBoxes);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Gets centroids of pieces from a Collection
 *
 */
USTRUCT()
struct FGetCentroidsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetCentroidsDataflowNode, "GetCentroids", "GeometryCollection", "")

public:
	/** Input Collection */
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** The centroids will be output for the bones selected in the TransformSelection  */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Output centroids */
	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Centroids = TArray<FVector>();

	FGetCentroidsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&Centroids);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts points into a DynamicMesh
 *
 */
USTRUCT()
struct FPointsToMeshDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPointsToMeshDataflowNode, "PointsToMesh", "Mesh|Utilities", "")

public:
	/** Points input */
	UPROPERTY(meta = (DataflowInput))
	TArray<FVector> Points;

	/** Mesh output */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	/** Mesh triangle count */
	UPROPERTY(meta = (DataflowOutput))
	int32 TriangleCount = 0;

	FPointsToMeshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Points);
		RegisterOutputConnection(&Mesh);
		RegisterOutputConnection(&TriangleCount);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts a BoundingBox into a DynamicMesh
 *
 */
USTRUCT()
struct FBoxToMeshDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FBoxToMeshDataflowNode, "BoxToMesh", "Mesh|Utilities", "")

public:
	/** BoundingBox input */
	UPROPERTY(meta = (DataflowInput))
	FBox Box = FBox(ForceInit);

	/** Mesh output */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	/** Mesh triangle count */
	UPROPERTY(meta = (DataflowOutput))
	int32 TriangleCount = 0;

	FBoxToMeshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Box);
		RegisterOutputConnection(&Mesh);
		RegisterOutputConnection(&TriangleCount);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Collects information from the DynamicMesh and outputs it into a formatted string
 *
 */
USTRUCT()
struct FMeshInfoDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshInfoDataflowNode, "MeshInfo", "Mesh|Utilities", "")

public:
	/** DynamicMesh for the information */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh;

	/** Formatted output string */
	UPROPERTY(meta = (DataflowOutput))
	FString InfoString = FString("");

	FMeshInfoDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Mesh);
		RegisterOutputConnection(&InfoString);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts a DynamicMesh to a Collection
 *
 */
USTRUCT()
struct FMeshToCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshToCollectionDataflowNode, "MeshToCollection", "Mesh|Utilities", "")

public:
	/** DynamicMesh to convert */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh;

	/** Output Collection */
	UPROPERTY(meta = (DataflowOutput))
	FManagedArrayCollection Collection;

	FMeshToCollectionDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Mesh);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts a StaticMesh into a DynamicMesh
 *
 */
USTRUCT()
struct FStaticMeshToMeshDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FStaticMeshToMeshDataflowNode, "StaticMeshToMesh", "Mesh|Utilities", "")

public:
	/** StaticMesh to convert */
	UPROPERTY(EditAnywhere, Category = "StaticMesh");
	TObjectPtr<UStaticMesh> StaticMesh;

	/** Output the HiRes representation, if set to true and HiRes doesn't exist it will output empty mesh */
	UPROPERTY(EditAnywhere, Category = "StaticMesh", meta = (DisplayName = "Use HiRes"));
	bool UseHiRes = true;

	/** Specifies the LOD level to use */
	UPROPERTY(EditAnywhere, Category = "StaticMesh", meta = (DisplayName = "LOD Level"));
	int32 LODLevel = 0;

	/** Output mesh */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	FStaticMeshToMeshDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterOutputConnection(&Mesh);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Transforms a mesh
 *
 */
USTRUCT()
struct FTransformDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FTransformDataflowNode, "Transform", "Math", "")

public:
	/** Translation */
	UPROPERTY(EditAnywhere, Category = "Transform");
	FVector Translate = FVector(0.0);

	/** Rotation */
	UPROPERTY(EditAnywhere, Category = "Transform");
	FVector Rotate = FVector(0.0);

	/** Scale */
	UPROPERTY(EditAnywhere, Category = "Transform");
	FVector Scale = FVector(1.0);

	/** Shear */
	UPROPERTY(EditAnywhere, Category = "Transform");
	FVector Shear = FVector(0.0);

	/** Uniform scale */
	UPROPERTY(EditAnywhere, Category = "Transform");
	float UniformScale = 1.f;

	/** Pivot for the rotation */
	UPROPERTY(EditAnywhere, Category = "Pivot");
	FVector RotatePivot = FVector(0.0);

	/** Pivot for the scale */
	UPROPERTY(EditAnywhere, Category = "Pivot");
	FVector ScalePivot = FVector(0.0);

	/** Invert the transformation */
	UPROPERTY(EditAnywhere, Category = "General");
	bool InvertTransformation = false;

	/** Output mesh */
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	FTransformDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Mesh);
		RegisterOutputConnection(&Mesh);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Appends two meshes
 *
 */
USTRUCT()
struct FMeshAppendDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshAppendDataflowNode, "MeshAppend", "Mesh|Utilities", "")

public:
	/** Mesh input */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh1;

	/** Mesh input */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh2;

	/** Output mesh */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	FMeshAppendDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Mesh1);
		RegisterInputConnection(&Mesh2);
		RegisterOutputConnection(&Mesh);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EMeshBooleanOperationEnum : uint8
{
	Dataflow_MeshBoolean_Union UMETA(DisplayName = "Union"),
	Dataflow_MeshBoolean_Intersect UMETA(DisplayName = "Intersect"),
	Dataflow_MeshBoolean_Difference UMETA(DisplayName = "Difference"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Mesh boolean (Union, Intersect, Difference) between two meshes
 *
 */
USTRUCT()
struct FMeshBooleanDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshBooleanDataflowNode, "MeshBoolean", "Mesh|Utilities", "")

public:
	/** Boolean operation */
	UPROPERTY(EditAnywhere, Category = "Boolean");
	EMeshBooleanOperationEnum Operation = EMeshBooleanOperationEnum::Dataflow_MeshBoolean_Intersect;

	/** Mesh input */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh1;

	/** Mesh input */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh2;

	/** Output mesh */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	FMeshBooleanDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Mesh1);
		RegisterInputConnection(&Mesh2);
		RegisterOutputConnection(&Mesh);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Copies the same mesh with scale onto points
 *
 */
USTRUCT()
struct FMeshCopyToPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshCopyToPointsDataflowNode, "MeshCopyToPoints", "Mesh|Utilities", "")

public:
	/** Points to copy meshes onto */
	UPROPERTY(meta = (DataflowInput))
	TArray<FVector> Points;

	/** Mesh to copy onto points */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> MeshToCopy;

	/** Scale appied to the mesh */
	UPROPERTY(EditAnywhere, Category = "Copy");
	float Scale = 1.f;

	/** Copied meshes */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	FMeshCopyToPointsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Points);
		RegisterInputConnection(&MeshToCopy);
		RegisterOutputConnection(&Mesh);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class ECompareOperationEnum : uint8
{
	Dataflow_Compare_Equal UMETA(DisplayName = "=="),
	Dataflow_Compare_Smaller UMETA(DisplayName = "<"),
	Dataflow_Compare_SmallerOrEqual UMETA(DisplayName = "<="),
	Dataflow_Compare_Greater UMETA(DisplayName = ">"),
	Dataflow_Compare_GreaterOrEqual UMETA(DisplayName = ">="),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Comparison between integers
 *
 */
USTRUCT()
struct FCompareIntDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCompareIntDataflowNode, "CompareInt", "Math|Int", "")

public:
	/** Comparison operation */
	UPROPERTY(EditAnywhere, Category = "Compare");
	ECompareOperationEnum Operation = ECompareOperationEnum::Dataflow_Compare_Equal;

	/** Int input */
	UPROPERTY(EditAnywhere, Category = "Compare");
	int32 IntA = 0;

	/** Int input */
	UPROPERTY(EditAnywhere, Category = "Compare");
	int32 IntB = 0;

	/** Boolean result of the comparison */
	UPROPERTY(meta = (DataflowOutput));
	bool Result = false;

	FCompareIntDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&IntA);
		RegisterInputConnection(&IntB);
		RegisterOutputConnection(&Result);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Branch between two inputs based on boolean condition
 *
 */
USTRUCT()
struct FBranchDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FBranchDataflowNode, "Branch", "Utilites|FlowControl", "")

public:
	/** Mesh input */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> MeshA;

	/** Mesh input */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> MeshB;

	/** If true, Output = MeshA, otherwise Output = MeshB */
	UPROPERTY(EditAnywhere, Category = "Branch");
	bool Condition = false;

	/** Output mesh */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	FBranchDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&MeshA);
		RegisterInputConnection(&MeshB);
		RegisterInputConnection(&Condition);
		RegisterOutputConnection(&Mesh);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Outputs Mesh data
 *
 */
USTRUCT()
struct FGetMeshDataDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetMeshDataDataflowNode, "GetMeshData", "Mesh|Utilities", "")

public:
	/** Mesh for the data */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDynamicMesh> Mesh;

	/** Number of vertices */
	UPROPERTY(meta = (DataflowOutput))
	int32 VertexCount = 0;

	/** Number of edges */
	UPROPERTY(meta = (DataflowOutput))
	int32 EdgeCount = 0;

	/** Number of triangles */
	UPROPERTY(meta = (DataflowOutput))
	int32 TriangleCount = 0;

	FGetMeshDataDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Mesh);
		RegisterOutputConnection(&VertexCount);
		RegisterOutputConnection(&EdgeCount);
		RegisterOutputConnection(&TriangleCount);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Collects grooup and attribute information from the Collection and outputs it into a formatted string
 *
 */
USTRUCT()
struct FGetSchemaDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetSchemaDataflowNode, "GetSchema", "GeometryCollection|Utilities", "")

public:
	/** GeometryCollection  for the information */
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** Formatted string containing the groups and attributes */
	UPROPERTY(meta = (DataflowOutput))
	FString String;

	FGetSchemaDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EClusterSizeMethodEnum : uint8
{
	Dataflow_ClusterSizeMethod_ByNumber UMETA(DisplayName = "By Number"),
	Dataflow_ClusterSizeMethod_ByFractionOfInput UMETA(DisplayName = "By Fraction Of Input"),
	Dataflow_ClusterSizeMethod_BySize UMETA(DisplayName = "By Size"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Automatically group pieces of a fractured Collection into a specified number of clusters
 *
 */
USTRUCT()
struct FAutoClusterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FAutoClusterDataflowNode, "AutoCluster", "GeometryCollection|Cluster", "")

public:
	/** How to choose the size of the clusters to create */
	UPROPERTY(EditAnywhere, Category = "Cluster Size");
	EClusterSizeMethodEnum ClusterSizeMethod = EClusterSizeMethodEnum::Dataflow_ClusterSizeMethod_ByNumber;

	/** Use a Voronoi diagram with this many Voronoi sites as a guide for deciding cluster boundaries */
	UPROPERTY(EditAnywhere, Category = "Cluster Size", meta = (DataflowInput, UIMin = 2, UIMax = 5000, EditCondition = "ClusterSizeMethod == EClusterSizeMethodEnum::Dataflow_ClusterSizeMethod_ByNumber"))
	int32 ClusterSites = 10;

	/** Choose the number of Voronoi sites used for clustering as a fraction of the number of child bones to process */
	UPROPERTY(EditAnywhere, Category = "Cluster Size", meta = (DataflowInput, UIMin = 0.f, UIMax = 0.5f, EditCondition = "ClusterSizeMethod == EClusterSizeMethodEnum::Dataflow_ClusterSizeMethod_ByFractionOfInput"))
	float ClusterFraction = 0.25;

	/** Choose the Edge-Size of the cube used to groups bones under a cluster (in cm). */
	UPROPERTY(EditAnywhere, Category = "ClusterSize", meta = (DataflowInput, DisplayName = "Cluster Size", UIMin = ".01", UIMax = "100", ClampMin = ".0001", ClampMax = "10000", EditCondition = "ClusterSizeMethod == EClusterSizeMethodEnum::Dataflow_ClusterSizeMethod_BySize"))
	float SiteSize = 1;

	/** If true, bones will only be added to the same cluster if they are physically connected (either directly, or via other bones in the same cluster) */
	UPROPERTY(EditAnywhere, Category = "Auto Cluster")
	bool AutoCluster = true;

	/** If true, prevent the creation of clusters with only a single child. Either by merging into a neighboring cluster, or not creating the cluster. */
	UPROPERTY(EditAnywhere, Category = "Auto Cluster")
	bool AvoidIsolated = true;

	/** Fractured GeometryCollection to cluster */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Bone selection for the clustering */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FAutoClusterDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&ClusterSites);
		RegisterInputConnection(&ClusterFraction);
		RegisterInputConnection(&SiteSize);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT()
struct FClusterFlattenDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FClusterFlattenDataflowNode, "Flatten", "GeometryCollection|Cluster", "")

public:
	// @todo(harsha) Support Selections

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FClusterFlattenDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT()
struct FRemoveOnBreakDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRemoveOnBreakDataflowNode, "RemoveOnBreak", "Fracture|Utilities", "")

public:
	// @todo(harsha) Support Selections

	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Removal", meta = (DataflowInput))
	FVector2f PostBreakTimer{0.0, 0.0};

	UPROPERTY(EditAnywhere, Category = "Removal", meta = (DataflowInput))
	FVector2f RemovalTimer{0.0, 1.0};

	UPROPERTY(EditAnywhere, Category = "Removal", meta = (DataflowInput))
	bool ClusterCrumbling = false;

	FRemoveOnBreakDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&PostBreakTimer);
		RegisterInputConnection(&RemovalTimer);
		RegisterInputConnection(&ClusterCrumbling);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EAnchorStateEnum : uint8
{
	Dataflow_AnchorState_Anchored UMETA(DisplayName = "Anchored"),
	Dataflow_AnchorState_NotAnchored UMETA(DisplayName = "Not Anchored"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Sets the anchored state on the selected bones in a Collection
 *
 */
USTRUCT()
struct FSetAnchorStateDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSetAnchorStateDataflowNode, "SetAnchorState", "GeometryCollection", "")

public:
	/** What anchor state to set on selected bones */
	UPROPERTY(EditAnywhere, Category = "Anchoring");
	EAnchorStateEnum AnchorState = EAnchorStateEnum::Dataflow_AnchorState_Anchored;

	/** If true, sets the non selected bones to opposite anchor state */
	UPROPERTY(EditAnywhere, Category = "Anchoring")
	bool SetNotSelectedBonesToOppositeState = false;

	/** GeometryCollection to set anchor state on */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Bone selection for setting the state on */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	FSetAnchorStateDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class EProximityMethodEnum : uint8
{
	/** Precise proximity mode looks for geometry with touching vertices or touching, coplanar, opposite - facing triangles.This works well with geometry fractured using our fracture tools. */
	Dataflow_ProximityMethod_Precise UMETA(DisplayName = "Precise"),
	/** Convex Hull proximity mode looks for geometry with overlapping convex hulls(with an optional offset) */
	Dataflow_ProximityMethod_ConvexHull UMETA(DisplayName = "ConvexHull"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Update the proximity (contact) graph for the bones in a Collection
 *
 */
USTRUCT()
struct FProximityDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FProximityDataflowNode, "Proximity", "GeometryCollection", "")

public:
	/** Which method to use to decide whether a given piece of geometry is in proximity with another */
	UPROPERTY(EditAnywhere, Category = "Proximity");
	EProximityMethodEnum ProximityMethod = EProximityMethodEnum::Dataflow_ProximityMethod_Precise;

	/** If hull-based proximity detection is enabled, amount to expand hulls when searching for overlapping neighbors */
	UPROPERTY(EditAnywhere, Category = "Proximity", meta = (ClampMin = "0", EditCondition = "ProximityMethod == EProximityMethodEnum::Dataflow_ProximityMethod_ConvexHull"))
	float DistanceThreshold = 1;

	/** Whether to automatically transform the proximity graph into a connection graph to be used for simulation */
	UPROPERTY(EditAnywhere, Category = "Proximity")
	bool bUseAsConnectionGraph = false;

	/** GeometryCollection to update the proximity graph on */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FProximityDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


namespace Dataflow
{
	void GeometryCollectionEngineNodes();
}

