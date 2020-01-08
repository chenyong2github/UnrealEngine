// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "Field/FieldSystemTypes.h"
#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"
#include "Math/Vector.h"
#include "Components/PrimitiveComponent.h"

#include "FieldSystemObjects.generated.h"

/**
* Context : 
*   Contexts are used to pass extra data into the field evaluation.
*/
UCLASS()
class FIELDSYSTEMENGINE_API UFieldSystemMetaData : public UActorComponent
{
	GENERATED_BODY()

public:
	virtual ~UFieldSystemMetaData() {}
	virtual FFieldSystemMetaData::EMetaType Type() const { return FFieldSystemMetaData::EMetaType::ECommandData_None; }
	virtual FFieldSystemMetaData* NewMetaData() const { return nullptr; }
};



/*
* UFieldSystemMetaDataIteration
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UFieldSystemMetaDataIteration : public UFieldSystemMetaData
{
	GENERATED_BODY()

public:
	virtual ~UFieldSystemMetaDataIteration() {}
	virtual FFieldSystemMetaData::EMetaType Type() const override { return  FFieldSystemMetaData::EMetaType::ECommandData_Iteration; }

	virtual FFieldSystemMetaData* NewMetaData() const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Iterations = "1"))
	UFieldSystemMetaDataIteration* SetMetaDataIteration(int Iterations);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	int Iterations;
};



/*
* UFieldSystemMetaDataProcessingResolution
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UFieldSystemMetaDataProcessingResolution : public UFieldSystemMetaData
{
	GENERATED_BODY()

public:
	virtual ~UFieldSystemMetaDataProcessingResolution() {}
	virtual FFieldSystemMetaData::EMetaType Type() const override { return  FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution; }
	virtual FFieldSystemMetaData* NewMetaData() const override;

	UFUNCTION(BlueprintPure, Category = "Field" )
	UFieldSystemMetaDataProcessingResolution* SetMetaDataaProcessingResolutionType(EFieldResolutionType ResolutionType);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
	TEnumAsByte<EFieldResolutionType> ResolutionType;
};




/**
* Field Evaluation
*/
UCLASS()
class FIELDSYSTEMENGINE_API UFieldNodeBase : public UActorComponent
{
	GENERATED_BODY()

public:
	virtual ~UFieldNodeBase() {}
	virtual FFieldNodeBase::EFieldType Type() const { return FFieldNodeBase::EFieldType::EField_None; }
	virtual bool ResultsExpector() const { return false; }
	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const { return nullptr; }
};


/**
* FieldNodeInt
*/
UCLASS()
class FIELDSYSTEMENGINE_API UFieldNodeInt : public UFieldNodeBase
{
	GENERATED_BODY()

public:
	virtual ~UFieldNodeInt() {}
	virtual FFieldNodeBase::EFieldType Type() const override { return  FFieldNodeBase::EFieldType::EField_Int32; }
};

/**
* FieldNodeFloat
*/
UCLASS()
class FIELDSYSTEMENGINE_API UFieldNodeFloat : public UFieldNodeBase
{
	GENERATED_BODY()

public:
	virtual ~UFieldNodeFloat() {}
	virtual FFieldNodeBase::EFieldType Type() const override { return  FFieldNodeBase::EFieldType::EField_Float; }
};

/**
* FieldNodeVector
*/
UCLASS()
class FIELDSYSTEMENGINE_API UFieldNodeVector : public UFieldNodeBase
{
	GENERATED_BODY()
public:
	virtual ~UFieldNodeVector() {}
	virtual FFieldNodeBase::EFieldType Type() const override { return  FFieldNodeBase::EFieldType::EField_FVector; }
};


/**
* UUniformInteger
**/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UUniformInteger : public UFieldNodeInt
{
	GENERATED_BODY()
public:

	UUniformInteger()
		: Super()
		, Magnitude(0)
	{}

	virtual ~UUniformInteger() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "0"))
	UUniformInteger* SetUniformInteger(int32 Magnitude);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	int32 Magnitude;
};


/**
* URadialIntMask
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API URadialIntMask : public UFieldNodeInt
{
	GENERATED_BODY()
public:

	URadialIntMask()
		: Super()
		, Radius(0)
		, Position(FVector(0, 0, 0))
		, InteriorValue(1.0)
		, ExteriorValue(0.0)
		, SetMaskCondition(ESetMaskConditionType::Field_Set_Always)
	{}
	virtual ~URadialIntMask() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (InteriorValue="1.0"))
	URadialIntMask* SetRadialIntMask(float Radius, FVector Position,int32 InteriorValue,int32 ExteriorValue, ESetMaskConditionType SetMaskConditionIn);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Radius;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FVector Position;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	int32 InteriorValue;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	int32 ExteriorValue;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	TEnumAsByte<ESetMaskConditionType> SetMaskCondition;
};


/**
* UUniformScalar
**/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UUniformScalar : public UFieldNodeFloat
{
	GENERATED_BODY()
public:

	UUniformScalar()
		: Super()
		, Magnitude(1.0)
	{}

	virtual ~UUniformScalar() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0"))
	UUniformScalar* SetUniformScalar(float Magnitude);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;

};


/**
* RadialFalloff
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API URadialFalloff : public UFieldNodeFloat
{
	GENERATED_BODY()
public:

	URadialFalloff()
		: Super()
		, Magnitude(1.0)
		, MinRange(0.f)
		, MaxRange(1.f)
		, Default(0.f)
		, Radius(0)
		, Position(FVector(0, 0, 0))
		, Falloff(EFieldFalloffType::Field_Falloff_Linear)
	{}

	virtual ~URadialFalloff() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude ="1.0", MinRange="0.0", MaxRange="1.0"))
	URadialFalloff* SetRadialFalloff(float Magnitude, float MinRange, float MaxRange, float Default,  float Radius, FVector Position, EFieldFalloffType Falloff);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MinRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MaxRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Radius;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FVector Position;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	TEnumAsByte<EFieldFalloffType> Falloff;
};

/**
* PlaneFalloff
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UPlaneFalloff : public UFieldNodeFloat
{
	GENERATED_BODY()

public:

	UPlaneFalloff()
		: Super()
		, Magnitude(1.0)
		, MinRange(0.f)
		, MaxRange(1.f)
		, Default(0.f)
		, Distance(0.f)
		, Position(FVector(0, 0, 0))
		, Normal(FVector(0, 0, 1))
		, Falloff(EFieldFalloffType::Field_Falloff_Linear)
	{}

	virtual ~UPlaneFalloff() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0", MinRange = "0.0", MaxRange = "1.0"))
	UPlaneFalloff* SetPlaneFalloff(float Magnitude, float MinRange, float MaxRange, float Default, float Distance, FVector Position, FVector Normal, EFieldFalloffType Falloff);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MinRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MaxRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Distance;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FVector Position;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FVector Normal;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	TEnumAsByte<EFieldFalloffType> Falloff;
};

/**
* BoxFalloff
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UBoxFalloff : public UFieldNodeFloat
{
	GENERATED_BODY()

public:

	UBoxFalloff()
		: Super()
		, Magnitude(1.0)
		, MinRange(0.f)
		, MaxRange(1.f)
		, Default(0.0)
		, Transform(FTransform::Identity)
		, Falloff(EFieldFalloffType::Field_Falloff_Linear)
	{}

	virtual ~UBoxFalloff() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0", MinRange = "0.0", MaxRange = "1.0"))
	UBoxFalloff* SetBoxFalloff(float Magnitude, float MinRange, float MaxRange, float Default, FTransform Transform, EFieldFalloffType Falloff);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MinRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MaxRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FTransform Transform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	TEnumAsByte<EFieldFalloffType> Falloff;
};


/**
* NoiseField
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UNoiseField : public UFieldNodeFloat
{
	GENERATED_BODY()

public:

	UNoiseField()
		: Super()
		, MinRange(0.f)
		, MaxRange(1.f)
		, Transform()
	{}

	virtual ~UNoiseField() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (MinRange = "0.0", MaxRange = "1.0"))
	UNoiseField* SetNoiseField(float MinRange, float MaxRange, FTransform Transform);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MinRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float MaxRange;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FTransform Transform;
};



/**
* UniformVector
**/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UUniformVector : public UFieldNodeVector
{
	GENERATED_BODY()
public:

	UUniformVector()
		: Super()
		, Magnitude(1.0)
		, Direction(FVector(0, 0, 0))
	{}

	virtual ~UUniformVector() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0"))
	UUniformVector* SetUniformVector(float Magnitude, FVector Direction);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FVector Direction;
};


/**
* RadialVector
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API URadialVector : public UFieldNodeVector
{
	GENERATED_BODY()
public:

	URadialVector()
		: Super()
		, Magnitude(1.0)
		, Position(FVector(0, 0, 0))
	{}

	virtual ~URadialVector() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0"))
	URadialVector* SetRadialVector(float Magnitude, FVector Position);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	FVector Position;
};

/**
* URandomVector
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API URandomVector : public UFieldNodeVector
{
	GENERATED_BODY()
public:

	URandomVector()
		: Super()
		, Magnitude(1.0)
	{}

	virtual ~URandomVector() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0"))
	URandomVector* SetRandomVector(float Magnitude);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
		float Magnitude;
};


/**
* UOperatorField
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UOperatorField : public UFieldNodeBase
{
	GENERATED_BODY()
public:

	UOperatorField()
		: Super()
		, Magnitude(1.0)
		, RightField(nullptr)
		, LeftField(nullptr)
		, Operation(EFieldOperationType::Field_Multiply)
	{}

	virtual ~UOperatorField() {}

	virtual FFieldNodeBase::EFieldType Type() const;
	virtual bool ResultsExpector() const override { return true; }

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field", meta = (Magnitude = "1.0"))
	UOperatorField* SetOperatorField(float Magnitude, const UFieldNodeBase* RightField, const UFieldNodeBase* LeftField, EFieldOperationType Operation);
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	float Magnitude;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	const UFieldNodeBase* RightField;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	const UFieldNodeBase* LeftField;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	TEnumAsByte<EFieldOperationType> Operation;

};

/**
* UToIntegerField
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UToIntegerField : public UFieldNodeInt
{
	GENERATED_BODY()
public:

	UToIntegerField()
		: Super()
		, FloatField(nullptr)
	{}

	virtual ~UToIntegerField() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field")
	UToIntegerField* SetToIntegerField(const UFieldNodeFloat* FloatField);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	const UFieldNodeFloat* FloatField;
};

/**
* UToFloatField
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UToFloatField : public UFieldNodeFloat
{
	GENERATED_BODY()
public:

	UToFloatField()
		: Super()
		, IntField(nullptr)
	{}

	virtual ~UToFloatField() {}
	 
	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field")
	UToFloatField* SetToFloatField(const UFieldNodeInt* IntegerField);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	const UFieldNodeInt* IntField;
};

/**
* UCullingField
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UCullingField : public UFieldNodeBase
{
	GENERATED_BODY()
public:

	UCullingField()
		: Super()
		, Culling(nullptr)
		, Field(nullptr)
		, Operation(EFieldCullingOperationType::Field_Culling_Inside)
	{}

	virtual ~UCullingField() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;

	UFUNCTION(BlueprintPure, Category = "Field")
	UCullingField* SetCullingField( const UFieldNodeBase* Culling, const UFieldNodeBase* Field, EFieldCullingOperationType Operation);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	const UFieldNodeBase* Culling;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	const UFieldNodeBase* Field;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Field")
	TEnumAsByte<EFieldCullingOperationType> Operation;

};

/**
* UReturnResultsField
*/
UCLASS(ClassGroup = "Field", meta = (BlueprintSpawnableComponent), ShowCategories = ("Field"))
class FIELDSYSTEMENGINE_API UReturnResultsTerminal : public UFieldNodeBase
{
	GENERATED_BODY()
public:

	UReturnResultsTerminal() : Super()
	{}

	virtual ~UReturnResultsTerminal() {}

	virtual FFieldNodeBase* NewEvaluationGraph(TArray<const UFieldNodeBase*>& Nodes) const override;
	virtual FFieldNodeBase::EFieldType Type() const override { return  FFieldNodeBase::EFieldType::EField_Results; }

	UFUNCTION(BlueprintPure, Category = "Field")
	UReturnResultsTerminal* SetReturnResultsTerminal( );

};




