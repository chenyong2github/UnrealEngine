// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMExecuteContext.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.h"
#include "RigVMStatistics.h"
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
struct RIGVM_API FRigVMParameter
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

	void Serialize(FArchive& Ar);
	void Save(FArchive& Ar);
	void Load(FArchive& Ar);
	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FRigVMParameter& P)
	{
		P.Serialize(Ar);
		return Ar;
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

	// UObject interface
	void Serialize(FArchive& Ar);
	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	// resets the container and maintains all memory
	void Reset();

	// resets the container and removes all memory
	void Empty();

	// resets the container and clones the input VM
	void CopyFrom(URigVM* InVM, bool bDeferCopy = false, bool bReferenceLiteralMemory = false, bool bReferenceByteCode = false, bool bCopyExternalVariables = false);

	// Initializes all execute ops and their memory.
	bool Initialize(FRigVMMemoryContainerPtrArray Memory, FRigVMFixedArray<void*> AdditionalArguments);

	// Executes the VM.
	// You can optionally provide external memory to the execution
	// and provide optional additional operands.
	bool Execute(FRigVMMemoryContainerPtrArray Memory, FRigVMFixedArray<void*> AdditionalArguments, const FName& InEntryName = NAME_None);

	// Executes the VM.
	// You can optionally provide external memory to the execution
	// and provide optional additional operands.
	UFUNCTION(BlueprintCallable, Category = RigVM)
	bool Execute(const FName& InEntryName = NAME_None);

	// Add a function for execute instructions to this VM.
	// Execute instructions can then refer to the function by index.
	UFUNCTION()
	int32 AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InMethodName);

	// Returns the name of a function given its index
	UFUNCTION()
	FString GetRigVMFunctionName(int32 InFunctionIndex) const;

	// The default mutable work memory
	UPROPERTY()
	FRigVMMemoryContainer WorkMemoryStorage;
	FRigVMMemoryContainer* WorkMemoryPtr;
	FORCEINLINE FRigVMMemoryContainer& GetWorkMemory() { return *WorkMemoryPtr; }
	FORCEINLINE const FRigVMMemoryContainer& GetWorkMemory() const { return *WorkMemoryPtr; }

	// The default const literal memory
	UPROPERTY()
	FRigVMMemoryContainer LiteralMemoryStorage;
	FRigVMMemoryContainer* LiteralMemoryPtr;
	FORCEINLINE FRigVMMemoryContainer& GetLiteralMemory() { return *LiteralMemoryPtr; }
	FORCEINLINE const FRigVMMemoryContainer& GetLiteralMemory() const { return *LiteralMemoryPtr; }

	// The byte code of the VM
	UPROPERTY()
	FRigVMByteCode ByteCodeStorage;
	FRigVMByteCode* ByteCodePtr;
	FORCEINLINE FRigVMByteCode& GetByteCode() { return *ByteCodePtr; }
	FORCEINLINE const FRigVMByteCode& GetByteCode() const { return *ByteCodePtr; }

	// Returns the instructions of the VM
	const FRigVMInstructionArray& GetInstructions();

	// Returns true if this VM's bytecode contains a given entry
	bool ContainsEntry(const FName& InEntryName) const;

	// Returns a list of all valid entry names for this VM's bytecode
	TArray<FName> GetEntryNames() const;

#if WITH_EDITOR
	// Returns true if the given instruction has been visited during the last run
	FORCEINLINE bool WasInstructionVisitedDuringLastRun(int32 InIndex) const
	{
		if (InstructionVisitedDuringLastRun.IsValidIndex(InIndex))
		{
			return InstructionVisitedDuringLastRun[InIndex] > 0;
		}
		return false;
	}

	// Returns the order of all instructions during the last run
	FORCEINLINE const TArray<int32> GetInstructionVisitOrder() const { return InstructionVisitOrder; }

	FORCEINLINE const void SetFirstEntryEventInEventQueue(const FName& InFirstEventName) { FirstEntryEventInQueue = InFirstEventName; }
#endif

	// Returns the parameters of the VM
	const TArray<FRigVMParameter>& GetParameters() const;

	// Returns a parameter given it's name
	FRigVMParameter GetParameterByName(const FName& InParameterName);

	// Adds a new input / output to the VM
	template<class T>
	FORCEINLINE_DEBUGGABLE FRigVMParameter AddParameter(ERigVMParameterType InParameterType, const FName& InName, const FString& InCPPType, const TArray<T>& DefaultValues)
	{
		ensure(InParameterType != ERigVMParameterType::Invalid);
		ensure(DefaultValues.Num() > 0);

		int32 RegisterIndex = INDEX_NONE;
		
		if (DefaultValues.Num() == 1)
		{
			RegisterIndex = WorkMemoryPtr->Add<T>(WorkMemoryPtr->SupportsNames() ? InName : NAME_None, DefaultValues[0], 1);
		}
		else
	{
			RegisterIndex = WorkMemoryPtr->AddFixedArray<T>(WorkMemoryPtr->SupportsNames() ? InName : NAME_None, FRigVMFixedArray<T>(DefaultValues), 1);
	}

		if (RegisterIndex == INDEX_NONE)
	{
			return FRigVMParameter();
	}

		FName Name = WorkMemoryPtr->SupportsNames() ? GetWorkMemory()[RegisterIndex].Name : InName;

		FRigVMParameter Parameter(InParameterType, Name, RegisterIndex, InCPPType, GetWorkMemory().GetScriptStruct(RegisterIndex));
		ParametersNameMap.Add(Parameter.Name, Parameters.Add(Parameter));
		return Parameter;

	}

	// Adds a new input / output to the VM
	template<class T>
	FORCEINLINE_DEBUGGABLE FRigVMParameter AddParameter(ERigVMParameterType InParameterType, const FName& InName, const FString& InCPPType, const T& DefaultValue)
	{
		TArray<T> DefaultValues;
		DefaultValues.Add(DefaultValue);
		return AddParameter(InParameterType, InName, InCPPType, DefaultValues);
	}

	// Retrieve the array size of the parameter
	int32 GetParameterArraySize(const FRigVMParameter& InParameter) const
	{
		return (int32)GetWorkMemory()[InParameter.GetRegisterIndex()].GetTotalElementCount();
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
	T GetParameterValue(const FRigVMParameter& InParameter, int32 InArrayIndex = 0, T DefaultValue = T{})
	{
		if (InParameter.GetRegisterIndex() != INDEX_NONE)
		{
			return WorkMemoryPtr->GetFixedArray<T>(InParameter.GetRegisterIndex())[InArrayIndex];
		}
		return DefaultValue;
	}

	// Retrieve the value of a parameter given its index
	template<class T>
	T GetParameterValue(int32 InParameterIndex, int32 InArrayIndex = 0, T DefaultValue = T{})
	{
		return GetParameterValue<T>(Parameters[InParameterIndex], InArrayIndex, DefaultValue);
	}

	// Retrieve the value of a parameter given its name
	template<class T>
	T GetParameterValue(const FName& InParameterName, int32 InArrayIndex = 0, T DefaultValue = T{})
	{
		int32 ParameterIndex = ParametersNameMap.FindChecked(InParameterName);
		return GetParameterValue<T>(ParameterIndex, InArrayIndex, DefaultValue);
	}

	// Set the value of a parameter
	template<class T>
	void SetParameterValue(const FRigVMParameter& InParameter, const T& InNewValue, int32 InArrayIndex = 0)
	{
		if (InParameter.GetRegisterIndex() != INDEX_NONE)
		{
			WorkMemoryPtr->GetFixedArray<T>(InParameter.GetRegisterIndex())[InArrayIndex] = InNewValue;
		}
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
		return GetParameterValue<FVector2D>(InParameterName, InArrayIndex, FVector2D::ZeroVector);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FVector GetParameterValueVector(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FVector>(InParameterName, InArrayIndex, FVector::ZeroVector);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FQuat GetParameterValueQuat(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FQuat>(InParameterName, InArrayIndex, FQuat::Identity);
	}

	UFUNCTION(BlueprintCallable, Category = RigVM)
	FTransform GetParameterValueTransform(const FName& InParameterName, int32 InArrayIndex = 0)
	{
		return GetParameterValue<FTransform>(InParameterName, InArrayIndex, FTransform::Identity);
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

	// Returns the external variables of the VM
	void ClearExternalVariables() { ExternalVariables.Reset(); }

	// Returns the external variables of the VM
	const TArray<FRigVMExternalVariable>& GetExternalVariables() const { return ExternalVariables; }

	// Returns an external variable given it's name
	FRigVMExternalVariable GetExternalVariableByName(const FName& InExternalVariableName);

	// Adds a new external / unowned variable to the VM
	FORCEINLINE_DEBUGGABLE FRigVMOperand AddExternalVariable(const FRigVMExternalVariable& InExternalVariable)
	{
		int32 VariableIndex = ExternalVariables.Add(InExternalVariable);
		return FRigVMOperand(ERigVMMemoryType::External, VariableIndex);
	}

	// Adds a new external / unowned variable to the VM
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMOperand AddExternalVariable(const FName& InExternalVariableName, T& InValue)
	{
		return AddExternalVariable(FRigVMExternalVariable::Make(InExternalVariableName, InValue));
	}

	// Adds a new external / unowned variable to the VM
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMOperand AddExternalVariable(const FName& InExternalVariableName, TArray<T>& InValue)
	{
		return AddExternalVariable(FRigVMExternalVariable::Make(InExternalVariableName, InValue));
	}

	void SetRegisterValueFromString(const FRigVMOperand& InOperand, const FString& InCPPType, const UObject* InCPPTypeObject, const TArray<FString>& InDefaultValues);

	// returns the statistics information
	FRigVMStatistics GetStatistics() const
	{
		FRigVMStatistics Statistics;
		Statistics.LiteralMemory = LiteralMemoryPtr->GetStatistics();
		Statistics.WorkMemory = WorkMemoryPtr->GetStatistics();
		Statistics.ByteCode = ByteCodePtr->GetStatistics();
		Statistics.BytesForCaching = FirstHandleForInstruction.GetAllocatedSize() + CachedMemoryHandles.GetAllocatedSize();
		Statistics.BytesForCDO =
			Statistics.LiteralMemory.TotalBytes +
			Statistics.WorkMemory.TotalBytes +
			Statistics.ByteCode.DataBytes +
			Statistics.BytesForCaching;
		Statistics.BytesPerInstance =
			Statistics.WorkMemory.TotalBytes +
			Statistics.BytesForCaching;

		return Statistics;
	}


#if WITH_EDITOR
	// returns the instructions as text, OperandFormatFunction is an optional argument that allows you to override how operands are displayed, for example, see SControlRigStackView::PopulateStackView 
	TArray<FString> DumpByteCodeAsTextArray(const TArray<int32> & InInstructionOrder = TArray<int32>(), bool bIncludeLineNumbers = true, TFunction<FString(const FString& RegisterName, const FString& RegisterOffsetName)> OperandFormatFunction = nullptr);
	FString DumpByteCodeAsText(const TArray<int32>& InInstructionOrder = TArray<int32>(), bool bIncludeLineNumbers = true);
#endif

#if WITH_EDITOR
	// FormatFunction is an optional argument that allows you to override how operands are displayed, for example, see SControlRigStackView::PopulateStackView
	FString GetOperandLabel(const FRigVMOperand & InOperand, TFunction<FString(const FString& RegisterName, const FString& RegisterOffsetName)> FormatFunction = nullptr) const;
#endif

private:

	void ResolveFunctionsIfRequired();
	void RefreshInstructionsIfRequired();
	void InvalidateCachedMemory();
	void CacheMemoryHandlesIfRequired(FRigVMMemoryContainerPtrArray InMemory);

	UPROPERTY(transient)
	FRigVMInstructionArray Instructions;

	UPROPERTY(transient)
	FRigVMExecuteContext Context;

	UPROPERTY()
	TArray<FName> FunctionNamesStorage;
	TArray<FName>* FunctionNamesPtr;
	FORCEINLINE TArray<FName>& GetFunctionNames() { return *FunctionNamesPtr; }
	FORCEINLINE const TArray<FName>& GetFunctionNames() const { return *FunctionNamesPtr; }

	TArray<FRigVMFunctionPtr> FunctionsStorage;
	TArray<FRigVMFunctionPtr>* FunctionsPtr;
	FORCEINLINE TArray<FRigVMFunctionPtr>& GetFunctions() { return *FunctionsPtr; }
	FORCEINLINE const TArray<FRigVMFunctionPtr>& GetFunctions() const { return *FunctionsPtr; }

	UPROPERTY()
	TArray<FRigVMParameter> Parameters;

	UPROPERTY()
	TMap<FName, int32> ParametersNameMap;

	TArray<uint32> FirstHandleForInstruction;
	TArray<FRigVMMemoryHandle> CachedMemoryHandles;
	TArray<FRigVMMemoryContainer*> CachedMemory;
	TArray<FRigVMExternalVariable> ExternalVariables;

#if WITH_EDITOR
	// stores the number of times each instruction was visited
	TArray<int32> InstructionVisitedDuringLastRun;
	TArray<int32> InstructionVisitOrder;
	
	// Control Rig can run multiple events per evaluation, such as the Backward&Forward Solve Mode,
	// store the first event such that we know when to reset data for a new round of rig evaluation
	FName FirstEntryEventInQueue;
#endif

	void CacheSingleMemoryHandle(const FRigVMOperand& InArg, bool bForExecute = false);

	int32 ExecutingThreadId;

	UPROPERTY(transient)
	URigVM* DeferredVMToCopy;

	void CopyDeferredVMIfRequired();

	friend class URigVMCompiler;
};
