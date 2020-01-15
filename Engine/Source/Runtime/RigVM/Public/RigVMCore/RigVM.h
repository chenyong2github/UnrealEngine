// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.h"
#include "RigVM.generated.h"

// The type of parameter for a VM
UENUM(BlueprintType)
enum class ERigVMParameterType : uint8
{
	Input,
	Output,
	Invalid
};

/**
 * The RigVMParameter define an input or output of the RigVM.
 * Parameters are mapped to work state memory registers and can be
 * used to set input parameters as well as retrieve output parameters.
 */
USTRUCT(BlueprintType)
struct FRigVMParameter
{
	GENERATED_BODY()

public:

	FRigVMParameter()
		: Type(ERigVMParameterType::Invalid)
		, Name(NAME_None)
		, RegisterIndex(INDEX_NONE)
		, CPPType()
		, ScriptStruct(nullptr)
		, ScriptStructPath()
	{
	}

	// returns true if the parameter is valid
	bool IsValid() const { return Type != ERigVMParameterType::Invalid; }

	// returns the type of this parameter
	ERigVMParameterType GetType() const { return Type; }

	// returns the name of this parameters
	const FName& GetName() const { return Name; }

	// returns the register index of this parameter in the work memory
	int32 GetRegisterIndex() const { return RegisterIndex; }

	// returns the cpp type of the parameter
	FString GetCPPType() const { return CPPType; }
	
	// Returns the script struct used by this parameter (in case it is a struct)
	UScriptStruct* GetScriptStruct() const;

private:

	FRigVMParameter(ERigVMParameterType InType, const FName& InName, int32 InRegisterIndex, const FString& InCPPType, UScriptStruct* InScriptStruct)
		: Type(InType)
		, Name(InName)
		, RegisterIndex(InRegisterIndex)
		, CPPType(InCPPType)
		, ScriptStruct(InScriptStruct)
		, ScriptStructPath(NAME_None)
	{
		if (ScriptStruct)
		{
			ScriptStructPath = *ScriptStruct->GetPathName();
		}
	}

	UPROPERTY()
	ERigVMParameterType Type;

	UPROPERTY()
	FName Name;

	UPROPERTY()
	int32 RegisterIndex;

	UPROPERTY()
	FString CPPType;

	UPROPERTY(transient)
	UScriptStruct* ScriptStruct;

	UPROPERTY()
	FName ScriptStructPath;

	friend class URigVM;
	friend class URigVMCompiler;
};

/**
 * The RigVM is the main object for evaluating FRigVMByteCode instructions.
 * It combines the byte code, a list of required function pointers for 
 * execute instructions and required memory in one class.
 */
UCLASS(BlueprintType)
class RIGVM_API URigVM : public UObject
{
	GENERATED_BODY()

public:

	URigVM();
	virtual ~URigVM();

	// resets the container and removes all memory
	void Reset();

	// Executes the VM.
	// You can optionally provide external memory to the execution
	// and provide optional additional operands.
	bool Execute(FRigVMMemoryContainerPtrArray Memory, TArrayView<void*> AdditionalArguments);

	// Executes the VM.
	// You can optionally provide external memory to the execution
	// and provide optional additional operands.
	UFUNCTION(BlueprintCallable, Category = RigVM)
	bool Execute();

	// Add a function for execute instructions to this VM.
	// Execute instructions can then refer to the function by index.
	UFUNCTION()
	int32 AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InMethodName);

	// The default mutable work memory
	UPROPERTY()
	FRigVMMemoryContainer WorkMemory;

	// The default const literal memory
	UPROPERTY()
	FRigVMMemoryContainer LiteralMemory;

	// The byte code of the VM
	UPROPERTY()
	FRigVMByteCode ByteCode;

	// Returns the instructions of the VM
	const FRigVMInstructionArray& GetInstructions();

	// Returns the parameters of the VM
	const TArray<FRigVMParameter>& GetParameters();

	// Adds a new input / output to the VM
	template<class T>
	FORCEINLINE FRigVMParameter AddPlainParameter(ERigVMParameterType InParameterType, const FName& InName, const FString& InCPPType, const TArray<T>& DefaultValue)
	{
		return AddPlainParameter(InParameterType, InName, InCPPType, (uint8*)DefaultValue.GetData(), sizeof(T), DefaultValue.Num());
	}

	// Adds a new input / output to the VM
	template<class T>
	FORCEINLINE FRigVMParameter AddStructParameter(ERigVMParameterType InParameterType, const FName& InName, const TArray<T>& DefaultValue)
	{
		UScriptStruct* ScriptStruct = T::StaticStruct();
		return AddStructParameter(InParameterType, InName, ScriptStruct, (uint8*)DefaultValue.GetData(), DefaultValue.Num());
	}

	// Adds a new input / output to the VM
	template<class T>
	FORCEINLINE FRigVMParameter AddPlainParameter(ERigVMParameterType InParameterType, const FName& InName, const FString& InCPPType, T DefaultValue)
	{
		TArray<T> DefaultValues;
		DefaultValues.Add(DefaultValue);
		return AddPlainParameter<T>(InParameterType, InName, InCPPType, DefaultValues);
	}

	// Adds a new input / output to the VM
	template<class T>
	FORCEINLINE FRigVMParameter AddStructParameter(ERigVMParameterType InParameterType, const FName& InName, const T& DefaultValue)
	{
		TArray<T> DefaultValues;
		DefaultValues.Add(DefaultValue);
		return AddStructParameter<T>(InParameterType, InName, DefaultValues);
	}

	// Retrieve the array size of the parameter
	int32 GetParameterArraySize(const FRigVMParameter& InParameter) const
	{
		return (int32)WorkMemory[InParameter.GetRegisterIndex()].GetTotalElementCount();
	}

	// Retrieve the array size of the parameter
	int32 GetParameterArraySize(int32 InParameterIndex) const
	{
		return GetParameterArraySize(Parameters[InParameterIndex]);
	}

	// Retrieve the array size of the parameter
	UFUNCTION(BlueprintCallable, Category = RigVM)
	int32 GetParameterArraySize(const FName& InParameterName) const
	{
		int32 ParameterIndex = ParametersNameMap.FindChecked(InParameterName);
		return GetParameterArraySize(ParameterIndex);
	}

	// Retrieve the value of a parameter
	template<class T>
	const T& GetParameterValue(const FRigVMParameter& InParameter, int32 InArrayIndex = 0)
	{
		return WorkMemory.GetArray<T>(InParameter.GetRegisterIndex())[InArrayIndex];
	}

	// Retrieve the value of a parameter given its index
	template<class T>
	const T& GetParameterValue(int32 InParameterIndex, int32 InArrayIndex = 0)
	{
		return GetParameterValue<T>(Parameters[InParameterIndex], InArrayIndex);
	}

	// Retrieve the value of a parameter given its name
	template<class T>
	const T& GetParameterValue(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		int32 ParameterIndex = ParametersNameMap.FindChecked(InParameterName);
		return GetParameterValue<T>(ParameterIndex, InArrayIndex);
	}

	// Set the value of a parameter
	template<class T>
	void SetParameterValue(const FRigVMParameter& InParameter, const T& InNewValue, int32 InArrayIndex = 0)
	{
		WorkMemory.GetArray<T>(InParameter.GetRegisterIndex())[InArrayIndex] = InNewValue;
	}

	// Set the value of a parameter given its index
	template<class T>
	void SetParameterValue(int32 ParameterIndex, const T& InNewValue, int32 InArrayIndex = 0)
	{
		return SetParameterValue<T>(Parameters[ParameterIndex], InNewValue, InArrayIndex);
	}

	// Set the value of a parameter given its name
	template<class T>
	void SetParameterValue(const FName& InParameterName, const T& InNewValue, int32 InArrayIndex = 0)
	{
		int32 ParameterIndex = ParametersNameMap.FindChecked(InParameterName);
		return SetParameterValue<T>(ParameterIndex, InNewValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	bool GetParameterValueBool(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<bool>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	float GetParameterValueFloat(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<float>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	int32 GetParameterValueInt(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<int32>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FName GetParameterValueName(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FName>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FString GetParameterValueString(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FString>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FVector2D GetParameterValueVector2D(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FVector2D>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FVector GetParameterValueVector(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FVector>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FQuat GetParameterValueQuat(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FQuat>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FTransform GetParameterValueTransform(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FTransform>(InParameterName, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueBool(const FName& InParameterName, bool InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<bool>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueFloat(const FName& InParameterName, float InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<float>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueInt(const FName& InParameterName, int32 InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<int32>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueName(const FName& InParameterName, const FName& InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<FName>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueString(const FName& InParameterName, const FString& InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<FString>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueVector2D(const FName& InParameterName, const FVector2D& InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<FVector2D>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueVector(const FName& InParameterName, const FVector& InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<FVector>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueQuat(const FName& InParameterName, const FQuat& InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<FQuat>(InParameterName, InValue, InArrayIndex);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	void SetParameterValueTransform(const FName& InParameterName, const FTransform& InValue, int32 InArrayIndex = 0)
	{
		SetParameterValue<FTransform>(InParameterName, InValue, InArrayIndex);
	}

private:

	FORCEINLINE FRigVMParameter AddPlainParameter(ERigVMParameterType InParameterType, const FName& InName, const FString& InCPPType, const uint8* InPtr, uint16 InSize, int32 InArraySize)
	{
		ensure(InParameterType != ERigVMParameterType::Invalid);
		int32 ArraySize = InArraySize <= 0 ? 1 : InArraySize;
		int32 RegisterIndex = WorkMemory.AddPlainArray(WorkMemory.SupportsNames() ? InName : NAME_None, InSize, ArraySize, InPtr, 1);
		if (RegisterIndex == INDEX_NONE)
		{
			return FRigVMParameter();
		}
		FName Name = WorkMemory.SupportsNames() ? WorkMemory[RegisterIndex].Name : InName;
		FRigVMParameter Parameter(InParameterType, Name, RegisterIndex, InCPPType, nullptr);
		ParametersNameMap.Add(Parameter.Name, Parameters.Add(Parameter));
		return Parameter;
	}

	FORCEINLINE FRigVMParameter AddStructParameter(ERigVMParameterType InParameterType, const FName& InName, UScriptStruct* InScriptStruct, const uint8* InPtr, int32 InArraySize)
	{
		ensure(InParameterType != ERigVMParameterType::Invalid);
		int32 ArraySize = InArraySize <= 0 ? 1 : InArraySize;
		int32 RegisterIndex = WorkMemory.AddStructArray(WorkMemory.SupportsNames() ? InName : NAME_None, InScriptStruct, ArraySize, InPtr, 1);
		if (RegisterIndex == INDEX_NONE)
		{
			return FRigVMParameter();
		}
		FName Name = WorkMemory.SupportsNames() ? WorkMemory[RegisterIndex].Name : InName;
		FRigVMParameter Parameter(InParameterType, Name, RegisterIndex, InScriptStruct->GetName(), InScriptStruct);
		ParametersNameMap.Add(Parameter.Name, Parameters.Add(Parameter));
		return Parameter;
	}

	void ResolveFunctionsIfRequired();
	void RefreshInstructionsIfRequired();

	UPROPERTY(transient)
	FRigVMInstructionArray Instructions;

	UPROPERTY()
	TArray<FString> FunctionNames;

	TArray<FRigVMFunctionPtr> Functions;

	UPROPERTY()
	TArray<FRigVMParameter> Parameters;

	UPROPERTY()
	TMap<FName, int32> ParametersNameMap;

	friend class URigVMCompiler;
};
