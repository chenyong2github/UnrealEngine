// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMExternalVariable.h"
#include "RigVMCore/RigVMTraits.h"
#include "RigVMStruct.generated.h"

// delegates used for variable introspection / creation
DECLARE_DELEGATE_RetVal(TArray<FRigVMExternalVariable>, FRigVMGetExternalVariablesDelegate)
DECLARE_DELEGATE_RetVal_TwoParams(FName, FRigVMCreateExternalVariableDelegate, FRigVMExternalVariable, FString)
DECLARE_DELEGATE_RetVal_TwoParams(bool, FRigVMBindPinToExternalVariableDelegate, FString, FString)

/** Context as of why the node was created */
enum class ERigVMNodeCreatedReason : uint8
{
	NodeSpawner,
	ScriptedEvent,
	Paste,
	BackwardsCompatibility,
	Unknown,
};

/**
 * A context struct passed to FRigVMStruct::OnUnitNodeCreated
 */
struct RIGVM_API FRigVMUnitNodeCreatedContext
{
public:

	struct FScope
	{
	public:
		FScope(FRigVMUnitNodeCreatedContext& InContext, ERigVMNodeCreatedReason InReason)
			: Context(InContext)
			, PreviousReason(InContext.GetReason())
		{
			Context.Reason = InReason;
		}

		~FScope()
		{
			Context.Reason = PreviousReason;
		}

	private:
		FRigVMUnitNodeCreatedContext& Context;
		ERigVMNodeCreatedReason PreviousReason;
	};

	/** Returns true if this context is valid to use */
	bool IsValid() const;

	/** Get the reason why this node was created */
	ERigVMNodeCreatedReason GetReason() const { return Reason; }

	/** Get the name of this node */
	FName GetNodeName() const { return NodeName; }

	/** Returns all currently existing external variables */
	TArray<FRigVMExternalVariable> GetExternalVariables() const;

	/** Creates a new variable within the host of this VM */
	FName AddExternalVariable(const FRigVMExternalVariable& InVariableToCreate, FString InDefaultValue = FString());

	/** Binds a pin to an external variable on the created node */
	bool BindPinToExternalVariable(FString InPinPath, FString InVariablePath);

	/** Returns a variable given a name (or a non-valid variable if not found) */
	FRigVMExternalVariable FindVariable(FName InVariableName) const;

	/** Returns the name of the first variable given a(or NAME_None if not found) */
	FName FindFirstVariableOfType(FName InCPPTypeName) const;

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value>::Type * = nullptr
	>
	FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(TBaseStructure<T>::Get());
	}

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUStruct, T>::Value>::Type * = nullptr
	>
	FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(T::StaticStruct());
	}

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUClass, T>::Value>::Type * = nullptr
	>
	FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(T::StaticClass());
	}

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TIsEnum<T>::Value>::Type * = nullptr
	>
		FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(StaticEnum<T>());
	}

	FRigVMGetExternalVariablesDelegate& GetAllExternalVariablesDelegate() { return AllExternalVariablesDelegate; }
	FRigVMCreateExternalVariableDelegate& GetCreateExternalVariableDelegate() { return CreateExternalVariableDelegate; }
	FRigVMBindPinToExternalVariableDelegate& GetBindPinToExternalVariableDelegate() { return BindPinToExternalVariableDelegate; }

private:
	
	FName NodeName = NAME_None;
	ERigVMNodeCreatedReason Reason = ERigVMNodeCreatedReason::Unknown;
	FRigVMGetExternalVariablesDelegate AllExternalVariablesDelegate;
	FRigVMCreateExternalVariableDelegate CreateExternalVariableDelegate;
	FRigVMBindPinToExternalVariableDelegate BindPinToExternalVariableDelegate;

	FName FindFirstVariableOfType(UObject* InCPPTypeObject) const;

	friend class URigVMController;
	friend struct FScope;
};

/**
 * The base class for all RigVM enabled structs.
 */
USTRUCT()
struct RIGVM_API FRigVMStruct
{
	GENERATED_BODY()

	virtual ~FRigVMStruct() {}
	virtual FString ProcessPinLabelForInjection(const FString& InLabel) const { return InLabel; }
	virtual FName GetEventName() const { return NAME_None; }

public:

	FORCEINLINE virtual int32 GetArraySize(const FName& InParameterName, const FRigVMUserDataArray& RigVMUserData) { return INDEX_NONE; }

	// loop related
	FORCEINLINE virtual bool IsForLoop() const { return false; }
	FORCEINLINE virtual int32 GetNumSlices() const { return 1; }

	// node creation
	FORCEINLINE virtual void OnUnitNodeCreated(FRigVMUnitNodeCreatedContext& InContext) const {}

#if WITH_EDITOR

	static bool ValidateStruct(UScriptStruct* InStruct, FString* OutErrorMessage);
	static bool CheckPinType(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType, FString* OutErrorMessage = nullptr);
	static bool CheckPinDirection(UScriptStruct* InStruct, const FName& PinName, const FName& InDirectionMetaName);
	static ERigVMPinDirection GetPinDirectionFromProperty(FProperty* InProperty);
	static bool CheckPinExists(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType = FString(), FString* OutErrorMessage = nullptr);
	static bool CheckMetadata(UScriptStruct* InStruct, const FName& PinName, const FName& InMetadataKey, FString* OutErrorMessage = nullptr);
	static bool CheckFunctionExists(UScriptStruct* InStruct, const FName& FunctionName, FString* OutErrorMessage = nullptr);
	static FString ExportToFullyQualifiedText(FProperty* InMemberProperty, const uint8* InMemberMemoryPtr);
	static FString ExportToFullyQualifiedText(UScriptStruct* InStruct, const uint8* InStructMemoryPtr);
#endif

	static const FName DeprecatedMetaName;
	static const FName InputMetaName;
	static const FName OutputMetaName;
	static const FName IOMetaName;
	static const FName HiddenMetaName;
	static const FName VisibleMetaName;
	static const FName DetailsOnlyMetaName;
	static const FName AbstractMetaName;
	static const FName CategoryMetaName;
	static const FName DisplayNameMetaName;
	static const FName MenuDescSuffixMetaName;
	static const FName ShowVariableNameInTitleMetaName;
	static const FName CustomWidgetMetaName;
	static const FName ConstantMetaName;
	static const FName TitleColorMetaName;
	static const FName NodeColorMetaName;
	static const FName KeywordsMetaName;
	static const FName PrototypeNameMetaName;
	static const FName ExpandPinByDefaultMetaName;
	static const FName DefaultArraySizeMetaName;
	static const FName VaryingMetaName;
	static const FName SingletonMetaName;
	static const FName SliceContextMetaName;
	static const FName ExecuteName;
	static const FName ExecuteContextName;
	static const FName ForLoopCountPinName;
	static const FName ForLoopContinuePinName;
	static const FName ForLoopCompletedPinName;
	static const FName ForLoopIndexPinName;

protected:

	static float GetRatioFromIndex(int32 InIndex, int32 InCount);

};
