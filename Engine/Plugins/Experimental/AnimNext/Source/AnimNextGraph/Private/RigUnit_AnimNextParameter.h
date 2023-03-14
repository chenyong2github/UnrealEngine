// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ExecuteContext.h"
#include "Interface/IAnimNextInterface.h"
#include "Units/RigUnit.h"
#include "AnimNext_LODPose.h"
#include "RigUnit_AnimNextAnimSequence.h"
// --- ---
#include "RigUnit_AnimNextParameter.generated.h"

struct FAnimNextUnitContext;
class UAnimNextGraph;

/** Unit for reading parameters from context */
USTRUCT(meta = (DisplayName = "Parameter", Category = "Parameters", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNextParameter : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

protected:
	static bool GetParameterInternal(FName InName, const FAnimNextExecuteContext& InContext, void* OutResult);

	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (Input))
	FName Parameter = NAME_None;
};

/** Unit for reading float parameter from context */
USTRUCT(meta = (DisplayName = "Float Parameter", Category = "Parameters", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNextParameter_Float : public FRigUnit_AnimNextParameter
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Result", meta = (Output))
	float Result = 0.0f;
};

/** Unit for reading a GraphLODPose parameter from context */
USTRUCT(meta = (DisplayName = "LODPose Parameter", Category = "Parameters", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNextParameter_GraphLODPose : public FRigUnit_AnimNextParameter
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Result", meta = (Output))
	FAnimNextGraphLODPose Result;
};

/** Unit for reading an AnimSequence parameter from context */
USTRUCT(meta = (DisplayName = "AnimSequence Parameter", Category = "Parameters", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNextParameter_AnimSequence : public FRigUnit_AnimNextParameter
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();

protected:
	UPROPERTY(EditAnywhere, Category = "Result", meta = (Output))
	FAnimNextGraph_AnimSequence Result;
};

/** Unit for reading anim interface graph parameter from context */
USTRUCT(meta = (DisplayName = "Anim Interface Parameter", Category = "Parameters", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNextParameter_AnimNextInterface : public FRigUnit_AnimNextParameter
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Result", meta = (Output))
	TScriptInterface<IAnimNextInterface> Result = nullptr;
};

USTRUCT(BlueprintType)
struct FAnimNextParameter
{
	GENERATED_BODY()

	FAnimNextParameter()
	: Name(NAME_None)
	{}

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	FName Name;
};

USTRUCT(BlueprintType)
struct FAnimNextParameter_Float
{
	GENERATED_BODY()
	
	FAnimNextParameter_Float()
		: Name(NAME_None)
		, Value(0.f)
	{}

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	float Value;
};

USTRUCT(BlueprintType)
struct FAnimNextParameter_Int
{
	GENERATED_BODY()

	FAnimNextParameter_Int()
		: Name(NAME_None)
		, Value(0)
	{}

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	int32 Value;
};

USTRUCT(BlueprintType)
struct FAnimNextParameter_Bool
{
	GENERATED_BODY()

	FAnimNextParameter_Bool()
		: Name(NAME_None)
		, bValue(false)
	{}

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	bool bValue;
};

USTRUCT(BlueprintType)
struct FAnimNextParameters1
{
	GENERATED_BODY()

	FAnimNextParameters1()
		: Param0()
		, FloatParam(0.f)
	{}

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	FAnimNextParameter_Int Param0;

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	float FloatParam;
};

USTRUCT(BlueprintType)
struct FAnimNextParameter_AnimNextInterface : public FAnimNextParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Anim Interface")
	TScriptInterface<IAnimNextInterface> Value = nullptr;
};

/** Base unit for calling anim interfaces from graphs */
USTRUCT(meta = (DisplayName = "Anim Interface", Category = "Execution", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNextInterface : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Anim Interface", meta = (Input))
	TScriptInterface<IAnimNextInterface> AnimNextInterface = nullptr;
};

/** Unit for getting a float via an anim interface */
USTRUCT(meta = (DisplayName = "Anim Interface Float", Category = "Parameters", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNext_Float : public FRigUnit_AnimNextInterface
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Anim Interface", meta = (Input))
	TArray<FAnimNextParameter_AnimNextInterface> Parameters;

	UPROPERTY(EditAnywhere, Category = "Result", meta = (Output))
	float Result = 0.0f;
};

/** Unit for getting a pose via an anim interface */
/*
USTRUCT(meta=(DisplayName="Get Pose"))
struct FRigUnit_AnimNext_Pose : public FRigUnit
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	TScriptInterface<IAnimNextInterface> PoseInterface = nullptr;

	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	TArray<FAnimNextParameter_AnimNextInterface> Parameters;

	UPROPERTY(EditAnywhere, Transient, DisplayName = "Result", Category = "BeginExecution", meta = (Output))
	FAnimNextInterfaceExecuteContext Result;
};
*/
/** Unit for getting a pose via an anim interface */
USTRUCT(meta = (DisplayName = "Float Operator", Category = "Operators", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_FloatOperator : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()
	
	FRigUnit_FloatOperator()
		: ParamA(0.f)
		, ParamB(0.f)
		, Result(0.f)
	{}

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	TScriptInterface<IAnimNextInterface> Operator = nullptr;

	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	float ParamA;

	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	float ParamB;

	UPROPERTY(EditAnywhere, Transient, DisplayName = "Result", Category = "BeginExecution", meta = (Output))
	float Result;
};

/** Unit for getting a pose via an anim interface */
USTRUCT(meta = (DisplayName = "Pose Operator", Category = "Operators", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_PoseOperator : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	TScriptInterface<IAnimNextInterface> Operator = nullptr;

	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	TArray<FRigVMExecuteContext> InputPoses;

	UPROPERTY(EditAnywhere, Category = "Pose Interface", meta = (Input))
	FAnimNextParameters1 Parameters;

	UPROPERTY(EditAnywhere, Transient, DisplayName = "Result", Category = "BeginExecution", meta = (Output))
	FAnimNextExecuteContext Result;
};

/** Unit for getting a pose via an anim interface */
USTRUCT(meta = (DisplayName = "Anim Sequence", Category = "Animation", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_AnimNext_SequencePlayer : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

		RIGVM_METHOD()
		void Execute();

protected:
	UPROPERTY(EditAnywhere, Category = "Anim Interface", meta = (Input))
	FAnimSequenceParameters Parameters;

	UPROPERTY(EditAnywhere, Category = "Anim Interface", meta = (Input))
	TScriptInterface<IAnimNextInterface> Sequence = nullptr;

	UPROPERTY(EditAnywhere, Transient, DisplayName = "Result", Category = "BeginExecution", meta = (Output))
	FAnimNextExecuteContext Result;
};

USTRUCT()
struct FRigUnit_TestFloatState_SpringDamperState
{
	GENERATED_BODY()

	UPROPERTY()
	float Value = 0.0f;

	UPROPERTY()
	float ValueRate = 0.0f;
};

/** Unit for getting a pose via an anim interface */
USTRUCT(meta = (DisplayName = "Test Float State - Spring Damper Smoothing", Varying, Category = "Animation", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct FRigUnit_TestFloatState : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();
	
protected:
	UPROPERTY(EditAnywhere, Category = "Parameters", meta=(Input))
	float TargetValue = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta=(Input))
	float TargetValueRate = 1.0f;
	
	UPROPERTY(EditAnywhere, Category = "Parameters", meta=(Input))
	float SmoothingTime = 1.0f;
	
	UPROPERTY(EditAnywhere, Category = "Parameters", meta=(Input))
	float DampingRatio = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta=(Output))
	float Result = 0.0f;
};
