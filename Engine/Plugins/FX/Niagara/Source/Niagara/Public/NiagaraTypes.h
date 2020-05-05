// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "Engine/UserDefinedStruct.h"
#include "Templates/SharedPointer.h"
#include "Misc/SecureHash.h"
#include "NiagaraTypes.generated.h"

class UNiagaraDataInterfaceBase;

DECLARE_LOG_CATEGORY_EXTERN(LogNiagara, Log, Verbose);

// helper methods for basic struct definitions
struct NIAGARA_API FNiagaraTypeUtilities
{
	static FString GetNamespaceStringForScriptParameterScope(const ENiagaraParameterScope& InScope);
};

// basic type struct definitions

USTRUCT(meta = (DisplayName = "float"))
struct FNiagaraFloat
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category=Parameters)
	float Value = 0;
};

USTRUCT(meta = (DisplayName = "int32"))
struct FNiagaraInt32
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Parameters)
	int32 Value = 0;
};

USTRUCT(meta=(DisplayName="bool"))
struct FNiagaraBool
{
	GENERATED_USTRUCT_BODY()

	// The Niagara VM expects this bitmask for its compare and select operators for false.
	enum BoolValues { 
		True = INDEX_NONE,
		False = 0
	}; 

	void SetValue(bool bValue) { Value = bValue ? True : False; }
	bool GetValue() const { return Value != False; }

	/** Sets this niagara bool's raw integer value directly using the special raw integer values expected by the VM and HLSL. */
	FORCEINLINE void SetRawValue(int32 RawValue) { Value = RawValue; }

	/** Gets this niagara bools raw integer value expected by the VM and HLSL. */
	FORCEINLINE int32 GetRawValue() const { return Value; }

	bool IsValid() const { return Value == True || Value == False; }
	
	FNiagaraBool() : Value(False) {}
	FNiagaraBool(bool bInValue) : Value(bInValue ? True : False) {}
	FORCEINLINE operator bool() { return GetValue(); }

private:
	UPROPERTY(EditAnywhere, Category = Parameters)// Must be either FNiagaraBool::True or FNiagaraBool::False.
	int32 Value = FNiagaraBool::False;
};

USTRUCT(meta = (DisplayName = "Half"))
struct FNiagaraHalf
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 Value = 0;
};

USTRUCT(meta = (DisplayName = "Half Vector2"))
struct FNiagaraHalfVector2
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 x = 0;

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 y = 0;
};

USTRUCT(meta = (DisplayName = "Half Vector3"))
struct FNiagaraHalfVector3
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 x = 0;

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 y = 0;

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 z = 0;
};

USTRUCT(meta = (DisplayName = "Half Vector4"))
struct FNiagaraHalfVector4
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 x = 0;

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 y = 0;

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 z = 0;

	UPROPERTY(EditAnywhere, Category = Parameters)
	uint16 w = 0;
};

USTRUCT()
struct FNiagaraNumeric
{
	GENERATED_USTRUCT_BODY()
};


USTRUCT()
struct FNiagaraParameterMap
{
	GENERATED_USTRUCT_BODY()
};

USTRUCT()
struct FNiagaraTestStructInner
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = TestStruct)
	FVector InnerVector1 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = TestStruct)
	FVector InnerVector2 = FVector::ZeroVector;
};

USTRUCT()
struct FNiagaraTestStruct
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = TestStruct)
	FVector Vector1 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = TestStruct)
	FVector Vector2 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = TestStruct)
	FNiagaraTestStructInner InnerStruct1;

	UPROPERTY(EditAnywhere, Category = TestStruct)
	FNiagaraTestStructInner InnerStruct2;
};

USTRUCT(meta = (DisplayName = "Matrix"))
struct FNiagaraMatrix
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category=NiagaraMatrix)
	FVector4 Row0 = FVector4(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = NiagaraMatrix)
	FVector4 Row1 = FVector4(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = NiagaraMatrix)
	FVector4 Row2 = FVector4(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = NiagaraMatrix)
	FVector4 Row3 = FVector4(EForceInit::ForceInitToZero);
};

/** Data controlling the spawning of particles */
USTRUCT(meta = (DisplayName = "Spawn Info", NiagaraClearEachFrame = "true"))
struct FNiagaraSpawnInfo
{
	GENERATED_USTRUCT_BODY();
	
	/** How many particles to spawn. */
	UPROPERTY(EditAnywhere, Category = SpawnInfo)
	int32 Count = 0;
	/** The sub frame delta time at which to spawn the first particle. */
	UPROPERTY(EditAnywhere, Category = SpawnInfo)
	float InterpStartDt = 0.0f;
	/** The sub frame delta time between each particle. */
	UPROPERTY(EditAnywhere, Category = SpawnInfo)
	float IntervalDt = 1.0f;
	/**
	 * An integer used to identify this spawn info.
	 * Typically this is unused.
	 * An example usage is when using multiple spawn modules to spawn from multiple discreet locations.
	 */
	UPROPERTY(EditAnywhere, Category = SpawnInfo)
	int32 SpawnGroup = 0;
};

USTRUCT(Blueprintable, meta = (DisplayName = "Niagara ID"))
struct FNiagaraID
{
	GENERATED_USTRUCT_BODY()

	/** 
	Index in the indirection table for this particle. Allows fast access to this particles data.
	Is always unique among currently living particles but will be reused after the particle dies.
	*/
	UPROPERTY(EditAnywhere, Category = ID)
	int32 Index = 0;

	/** 
	A unique tag for when this ID was acquired. 
	Allows us to differentiate between particles when one dies and another reuses it's Index.
	*/
	UPROPERTY(EditAnywhere, Category = ID)
	int32 AcquireTag = 0;

	FNiagaraID() : Index(INDEX_NONE), AcquireTag(INDEX_NONE) {}
	FNiagaraID(int32 InIndex, int32 InAcquireTag): Index(InIndex), AcquireTag(InAcquireTag){}

	bool operator==(const FNiagaraID& Other)const { return Index == Other.Index && AcquireTag == Other.AcquireTag; }
	bool operator!=(const FNiagaraID& Other)const { return !(*this == Other); }
	bool operator<(const FNiagaraID& Other)const { return Index < Other.Index || (Index == Other.Index && AcquireTag < Other.AcquireTag); }
};

#define NIAGARA_INVALID_ID (FNiagaraID({(INDEX_NONE), (INDEX_NONE)}))

FORCEINLINE uint32 GetTypeHash(const FNiagaraID& ID)
{
	return HashCombine(GetTypeHash(ID.Index), GetTypeHash(ID.AcquireTag));
}

/** Information about how this type should be laid out in an FNiagaraDataSet */
USTRUCT()
struct FNiagaraTypeLayoutInfo
{
	GENERATED_BODY()

	FNiagaraTypeLayoutInfo()
	{}

	/** Byte offset of each float component in a structured layout. */
	UPROPERTY()
	TArray<uint32> FloatComponentByteOffsets;

	/** Offset into register table for each float component. */
	UPROPERTY()
	TArray<uint32> FloatComponentRegisterOffsets;

	/** Byte offset of each int32 component in a structured layout. */
	UPROPERTY()
	TArray<uint32> Int32ComponentByteOffsets;

	/** Offset into register table for each int32 component. */
	UPROPERTY()
	TArray<uint32> Int32ComponentRegisterOffsets;

	/** Byte offset of each half component in a structured layout. */
	UPROPERTY()
	TArray<uint32> HalfComponentByteOffsets;

	/** Offset into register table for each half component. */
	UPROPERTY()
	TArray<uint32> HalfComponentRegisterOffsets;

	static void GenerateLayoutInfo(FNiagaraTypeLayoutInfo& Layout, const UScriptStruct* Struct)
	{
		Layout.FloatComponentByteOffsets.Empty();
		Layout.FloatComponentRegisterOffsets.Empty();
		Layout.Int32ComponentByteOffsets.Empty();
		Layout.Int32ComponentRegisterOffsets.Empty();
		Layout.HalfComponentByteOffsets.Empty();
		Layout.HalfComponentRegisterOffsets.Empty();
		GenerateLayoutInfoInternal(Layout, Struct);
	}

private:
	static void GenerateLayoutInfoInternal(FNiagaraTypeLayoutInfo& Layout, const UScriptStruct* Struct, int32 BaseOffest = 0)
	{
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			int32 PropOffset = BaseOffest + Property->GetOffset_ForInternal();
			if (Property->IsA(FFloatProperty::StaticClass()))
			{
				Layout.FloatComponentRegisterOffsets.Add(Layout.FloatComponentByteOffsets.Num());
				Layout.FloatComponentByteOffsets.Add(PropOffset);
			}
			else if (Property->IsA(FUInt16Property::StaticClass()))
			{
				Layout.HalfComponentRegisterOffsets.Add(Layout.HalfComponentByteOffsets.Num());
				Layout.HalfComponentByteOffsets.Add(PropOffset);
			}
			else if (Property->IsA(FIntProperty::StaticClass()) || Property->IsA(FBoolProperty::StaticClass()))
			{
				Layout.Int32ComponentRegisterOffsets.Add(Layout.Int32ComponentByteOffsets.Num());
				Layout.Int32ComponentByteOffsets.Add(PropOffset);
			}
			//Should be able to support double easily enough
			else if (FStructProperty* StructProp = CastFieldChecked<FStructProperty>(Property))
			{
				GenerateLayoutInfoInternal(Layout, StructProp->Struct, PropOffset);
			}
			else
			{
				check(false);
			}
		}
	}
};

/*
*  Can convert a UStruct with fields of base types only (float, int... - will likely add native vector types here as well)
*	to an FNiagaraTypeDefinition (internal representation)
*/
class NIAGARA_API FNiagaraTypeHelper
{
public:
	static FString ToString(const uint8* ValueData, const UObject* StructOrEnum);
};

/** Defines different modes for selecting the output numeric type of a function or operation based on the types of the inputs. */
UENUM()
enum class ENiagaraNumericOutputTypeSelectionMode : uint8
{
	/** Output type selection not supported. */
	None UMETA(Hidden),
	/** Select the largest of the numeric inputs. */
	Largest,
	/** Select the smallest of the numeric inputs. */
	Smallest,
	/** Selects the base scalar type for this numeric inputs. */
	Scalar,
};

/** 
The source from which a script execution state was set. Used to allow scalability etc to change the state but only if the state has not been defined by something with higher precedence. 
If this changes, all scripts must be recompiled by bumping the NiagaraCustomVersion
*/
UENUM()
enum class ENiagaraExecutionStateSource : uint32
{
	Scalability, //State set by Scalability logic. Lowest precedence.
	Internal, //Misc internal state. For example becoming inactive after we finish our set loops.
	Owner, //State requested by the owner. Takes precedence over everything but internal completion logic.
	InternalCompletion, // Internal completion logic. Has to take highest precedence for completion to be ensured.
};

UENUM()
enum class ENiagaraExecutionState : uint32
{
	/**  Run all scripts. Allow spawning.*/
	Active,
	/** Run all scripts but suppress any new spawning.*/
	Inactive,
	/** Clear all existing particles and move to inactive.*/
	InactiveClear,
	/** Complete. When the system or all emitters are complete the effect is considered finished. */
	Complete,
	/** Emitter only. Emitter is disabled. Will not tick or render again until a full re initialization of the system. */
	Disabled UMETA(Hidden),

	// insert new states before
	Num UMETA(Hidden)
};

USTRUCT()
struct NIAGARA_API FNiagaraCompileHashVisitorDebugInfo
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY()
	FString Object;

	UPROPERTY()
	TArray<FString> PropertyKeys;

	UPROPERTY()
	TArray<FString> PropertyValues;
};

/**
Used to store the state of a graph when deciding if it has been dirtied for recompile.
*/
struct NIAGARA_API FNiagaraCompileHashVisitor
{
public:
	FNiagaraCompileHashVisitor(FSHA1& InHashState) : HashState(InHashState) {}

	FSHA1& HashState;
	TArray<const void*> ObjectList;

	static int LogCompileIdGeneration;

#if WITH_EDITORONLY_DATA

	// Debug data about the compilation hash, including key value pairs to detect differences.
	TArray<FNiagaraCompileHashVisitorDebugInfo> Values;

	template<typename T>
	void ToDebugString(const T* InData, uint64 InDataCount, FString& OutStr)
	{
		for (int32 i = 0; i < InDataCount; i++)
		{
			FString DataStr = LexToString(InData[i]);
			OutStr.Appendf(TEXT("%s "), *DataStr);
		}
	}
#endif
	/**
	Registers a pointer for later reference in the compile id in a deterministic manner.
	*/
	int32 RegisterReference(const void* InObject)
	{
		if (InObject == nullptr)
		{
			return -1;
		}

		int32 Index = ObjectList.Find(InObject);
		if (Index < 0)
		{
			Index = ObjectList.Add(InObject);
		}
		return Index;
	}

	/**
	We don't usually want to save GUID's or pointer values because they have nondeterministic values. Consider a PostLoad upgrade operation that creates a new node.
	Each pin and node gets a unique ID. If you close the editor and reopen, you'll get a different set of values. One of the characteristics we want for compilation
	behavior is that the same graph structure produces the same compile results, so we only want to embed information that is deterministic. This method is for use
	when registering a pointer to an object that is serialized within the compile hash.
	*/
	bool UpdateReference(const TCHAR* InDebugName, const void* InObject)
	{
		int32 Index = RegisterReference(InObject);
		return UpdatePOD(InDebugName, Index);
	}

	/**
	Adds an array of POD (plain old data) values to the hash.
	*/
	template<typename T>
	bool UpdateArray(const TCHAR* InDebugName, const T* InData, uint64 InDataCount = 1)
	{
		static_assert(TIsPODType<T>::Value, "UpdateArray does not support a constructor / destructor on the element class.");
		HashState.Update((const uint8 *)InData, sizeof(T)*InDataCount);
#if WITH_EDITORONLY_DATA
		if (LogCompileIdGeneration != 0)
		{
			FString ValuesStr = InDebugName;
			ValuesStr.Append(TEXT(" = "));
			ToDebugString(InData, InDataCount, ValuesStr);
			Values.Top().PropertyKeys.Push(InDebugName);
			Values.Top().PropertyValues.Push(ValuesStr);
		}
#endif
		return true;
	}

	/**
	Adds a single value of typed POD (plain old data) to the hash.
	*/
	template<typename T>
	bool UpdatePOD(const TCHAR* InDebugName, const T& InData)
	{
		static_assert(TIsPODType<T>::Value, "Update does not support a constructor / destructor on the element class.");
		HashState.Update((const uint8 *)&InData, sizeof(T));
#if WITH_EDITORONLY_DATA
		if (LogCompileIdGeneration != 0)
		{
			FString ValuesStr;
			ToDebugString(&InData, 1, ValuesStr);
			Values.Top().PropertyKeys.Push(InDebugName);
			Values.Top().PropertyValues.Push(ValuesStr);
		}
#endif
		return true;
	}

	/**
	Adds an string value to the hash.
	*/
	bool UpdateString(const TCHAR* InDebugName, const FString& InData)
	{
		HashState.Update((const uint8 *)(*InData), sizeof(TCHAR)*InData.Len());
#if WITH_EDITORONLY_DATA
		if (LogCompileIdGeneration != 0)
		{
			Values.Top().PropertyKeys.Push(InDebugName);
			Values.Top().PropertyValues.Push(InData);
		}
#endif
		return true;
	}
};


UENUM()
enum class ENiagaraParameterScope : uint32
{
	/** Parameter that is an input argument into this graph.*/
	Input UMETA(DisplayName = "Input"),

	/** Parameter that is exposed to the owning component for editing and are read-only when used in the graph*/
	User UMETA(DisplayName = "User"),

	/** Parameter provided by the engine. These are explicitly defined by the engine codebase and read-only. */
	Engine UMETA(DisplayName = "Engine (Generic)", Hidden),

	/** Parameter provided by the engine focused on the owning component. These are explicitly defined by the engine codebase and read-only.*/
	Owner UMETA(DisplayName = "Engine (Owner)", Hidden),

	/** Parameter is an attribute of the owning system payload. It is persistent across frames and initialized in the System Spawn stage of the stack.*/
	System  UMETA(DisplayName = "System"),

	/** Parameter is an attribute of the owning emitter payload. It is persistent across frames and initialized in the Emitter Spawn stage of the stack.*/
	Emitter   UMETA(DisplayName = "Emitter"),

	/** Parameter is an attribute of the owning particle payload. It is persistent across frames and initialized in the Particle Spawn stage of the stack.*/
	Particles  UMETA(DisplayName = "Particles"),

	/** Parameter is initialized in the appropriate spawn stage for the stack. It is persistent from frame to frame. For example, if used consistently in an Emitter stage, this parameter will turn into an emitter attribute. Similarly, if used in a Particle stage, it will turn into a particle attribute.*/
	ScriptPersistent UMETA(DisplayName = "Stage (Persistent)", Hidden), //@todo(ng) hiding until autotest verification is made.

	/** Parameter is initialized at the start of this stage and can be shared amongst other modules within this stack stage, but is not persistent across runs or from stack stage to stack stage.*/
	ScriptTransient UMETA(DisplayName = "Stage (Transient)"),
	
	/** Parameter is initialized at the start of this script and is only used within the context of this script. It is invisible to the parent stage stack.*/
	Local UMETA(DisplayName = "Local"), //Convenience markup for ScopeToString functions, only use in conjunction with ENiagaraScriptParameterUsage::Local.

	Custom UMETA(Hidden), //Convenience markup for expressing parameters using legacy editor mode to freetype namespace and name.

	DISPLAY_ONLY_StaticSwitch UMETA(DisplayName="Static Switch", Hidden), //Only use for display string in SEnumComboBoxes; does not have implementation for classes that interact with ENiagaraParameterScope.
	
	/** Parameter is output to the owning stack stage from this script, but is only meaningful if bound elsewhere in the stage.*/
	Output UMETA(DisplayName = "Output"),

	// insert new scopes before
	None UMETA(Hidden),

	Num UMETA(Hidden)
};

UENUM()
enum class ENiagaraScriptParameterUsage : uint32
{
	Input,

	Output,

	Local,

	InputOutput,

	InitialValueInput,

	// insert new script parameter usages before
	None UMETA(Hidden),
	
	Num UMETA(Hidden)
};

/** Defines options for conditionally editing and showing script inputs in the UI. */
USTRUCT()
struct NIAGARA_API FNiagaraInputConditionMetadata
{
	GENERATED_USTRUCT_BODY()
public:
	/** The name of the input to use for matching the target values. */
	UPROPERTY(EditAnywhere, Category="Input Condition")
	FName InputName;

	/** The list of target values which will satisfy the input condition.  If this is empty it's assumed to be a single value of "true" for matching bool inputs. */
	UPROPERTY(EditAnywhere, Category="Input Condition")
	TArray<FString> TargetValues;
};

USTRUCT()
struct NIAGARA_API FNiagaraParameterScopeInfo
{
	GENERATED_BODY();

public:
	FNiagaraParameterScopeInfo()
		: Scope(ENiagaraParameterScope::None)
		, NamespaceString()
	{};

	FNiagaraParameterScopeInfo(const ENiagaraParameterScope InScope, const FString& InNamespaceString)
		: Scope(InScope)
		, NamespaceString(InNamespaceString)
	{};

	bool operator == (const FNiagaraParameterScopeInfo& Other) const
	{
		return Scope == Other.Scope && NamespaceString == Other.NamespaceString;
	}

	ENiagaraParameterScope GetScope() const { return Scope; };
	const FString& GetNamespaceString() const { return NamespaceString; };

private:
	UPROPERTY()
		ENiagaraParameterScope Scope;

	UPROPERTY()
		FString NamespaceString;
};

UENUM()
enum class ENiagaraParameterPanelCategory : uint32
{
	/** Parameter that is an input argument into this graph.*/
	Input UMETA(DisplayName = "Module Inputs"),

	Attributes UMETA(DisplayName = "Input Attributes"),

	/** Parameter is output to the owning stack stage from this script, but is only meaningful if bound elsewhere in the stage.*/
	Output UMETA(DisplayName = "Output Attributes"),

	/** Parameter is initialized at the start of this script and is only used within the context of this script. It is invisible to the parent stage stack.*/
	Local UMETA(DisplayName = "Local"),

	/** Parameter that is exposed to the owning component for editing and are read-only when used in the graph*/
	User UMETA(DisplayName = "User"),

	/** Parameter provided by the engine. These are explicitly defined by the engine codebase and read-only. */
	Engine UMETA(DisplayName = "Engine (Generic)", Hidden),

	/** Parameter provided by the engine focused on the owning component. These are explicitly defined by the engine codebase and read-only.*/
	Owner UMETA(DisplayName = "Engine (Owner)", Hidden),

	/** Parameter is an attribute of the owning system payload. It is persistent across frames and initialized in the System Spawn stage of the stack.*/
	System  UMETA(DisplayName = "System"),

	/** Parameter is an attribute of the owning emitter payload. It is persistent across frames and initialized in the Emitter Spawn stage of the stack.*/
	Emitter   UMETA(DisplayName = "Emitter"),

	/** Parameter is an attribute of the owning particle payload. It is persistent across frames and initialized in the Particle Spawn stage of the stack.*/
	Particles  UMETA(DisplayName = "Particles"),

	/** Parameter is initialized at the start of this stage and can be shared amongst other modules within this stack stage, but is not persistent across runs or from stack stage to stack stage.*/
	ScriptTransient UMETA(DisplayName = "Stage (Transient)"),

	StaticSwitch UMETA(DisplayName = "Static Switch"),

	// insert new categories before
	None UMETA(Hidden),

	Num UMETA(Hidden)
};

USTRUCT()
struct NIAGARA_API FNiagaraVariableMetaData
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraVariableMetaData()
		: bAdvancedDisplay(false)
		, EditorSortPriority(0)
		, bInlineEditConditionToggle(false)
		, ScopeName()
		, Usage(ENiagaraScriptParameterUsage::None)
		, bIsStaticSwitch(false)
		, StaticSwitchDefaultValue(0)
		, bAddedToNodeGraphDeepCopy(false)
		, bOutputIsPersistent(false)
		, bCreatedInSystemEditor(false)
		, bUseLegacyNameString(false)
	{};
public:
	UPROPERTY(EditAnywhere, Category = "Variable", meta = (MultiLine = true, SkipForCompileHash = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, Category = "Variable", meta = (SkipForCompileHash = "true"))
	FText CategoryName;

	/** Declares that this input is advanced and should only be visible if expanded inputs have been expanded. */
	UPROPERTY(EditAnywhere, Category = "Variable", meta = (SkipForCompileHash = "true"))
	bool bAdvancedDisplay;

	UPROPERTY(EditAnywhere, Category = "Variable", meta = (ToolTip = "Affects the sort order in the editor stacks. Use a smaller number to push it to the top. Defaults to zero.", SkipForCompileHash = "true"))
	int32 EditorSortPriority;

	/** Declares the associated input is used as an inline edit condition toggle, so it should be hidden and edited as a 
	checkbox inline with the input which was designated as its edit condition. */
	UPROPERTY(EditAnywhere, Category = "Variable", meta = (SkipForCompileHash = "true"))
	bool bInlineEditConditionToggle;

	/** Declares the associated input should be conditionally editable based on the value of another input. */
	UPROPERTY(EditAnywhere, Category = "Variable", meta = (SkipForCompileHash = "true"))
	FNiagaraInputConditionMetadata EditCondition;

	/** Declares the associated input should be conditionally visible based on the value of another input. */
	UPROPERTY(EditAnywhere, Category = "Variable", meta = (SkipForCompileHash = "true"))
	FNiagaraInputConditionMetadata VisibleCondition;

	UPROPERTY(EditAnywhere, Category = "Variable", DisplayName = "Property Metadata", meta = (ToolTip = "Property Metadata", SkipForCompileHash = "true"))
	TMap<FName, FString> PropertyMetaData;

public:

	const FName& GetScopeName() const { return ScopeName; };
	void SetScopeName(const FName& InScopeName) { ScopeName = InScopeName; };

	ENiagaraScriptParameterUsage GetUsage() const { return Usage; };
	void SetUsage(ENiagaraScriptParameterUsage InUsage) { Usage = InUsage; };

	/** Gets the CachedNamespacelessParameterName and notifies if it cannot be returned due to an override being set.
	 * @params OutName		The Name to return;
	 * @return bool			Whether the CachedNamespacelessParameterName can be returned. Is false if bUseLegacyNameString is set.
	 */
	bool GetParameterName(FName& OutName) const;
	void SetCachedNamespacelessVariableName(const FName& InVariableName);;

	bool GetWasCreatedInSystemEditor() const { return bCreatedInSystemEditor; };
	void SetWasCreatedInSystemEditor(bool bWasCreatedInSystemEditor) { bCreatedInSystemEditor = bWasCreatedInSystemEditor; };

	bool GetWasAddedToNodeGraphDeepCopy() const { return bAddedToNodeGraphDeepCopy; };
	void SetWasAddedToNodeGraphDeepCopy(bool bWasAddedToNodeGraphDeepCopy) { bAddedToNodeGraphDeepCopy = bWasAddedToNodeGraphDeepCopy; };

	bool GetIsStaticSwitch() const { return bIsStaticSwitch; };
	void SetIsStaticSwitch(bool bInIsStaticSwitch) { bIsStaticSwitch = bInIsStaticSwitch; };

	int32 GetStaticSwitchDefaultValue() const { return StaticSwitchDefaultValue; };
	void SetStaticSwitchDefaultValue(int32 InStaticSwitchDefaultValue) { StaticSwitchDefaultValue = InStaticSwitchDefaultValue; };

	bool GetOutputIsPersistent() const { return bOutputIsPersistent; };
	void SetOutputIsPersistent(bool bInOutputIsPersistent) { bOutputIsPersistent = bInOutputIsPersistent; };

	bool GetIsUsingLegacyNameString() const { return bUseLegacyNameString; };
	void SetIsUsingLegacyNameString(bool bInUseLegacyNameString) { bUseLegacyNameString = bInUseLegacyNameString; };

	void CopyPerScriptMetaData(const FNiagaraVariableMetaData& OtherMetaData);

private:
	/** Defines the scope of a variable that is an input to a script. Used to lookup registered scope infos and resolve the actual ENiagaraParameterScope and Namespace string to use. */
	UPROPERTY(meta = (SkipForCompileHash = "true"))
	FName ScopeName;

	/** Defines the usage of a variable as an argument or output relative to the script. */
	UPROPERTY(meta = (SkipForCompileHash = "true"))
	ENiagaraScriptParameterUsage Usage;

	/** This is a read-only variable that designates if the metadata is tied to a static switch or not. */
	UPROPERTY()
	bool bIsStaticSwitch; // TODO: This should be moved to the UNiagaraScriptVariable in the future

	/** The default value to use when creating new pins or stack entries for a static switch parameter */
	UPROPERTY()
	int32 StaticSwitchDefaultValue;  // TODO: This should be moved to the UNiagaraScriptVariable in the future

	/** Transient data to mark variables set in the node graph deep copy as having been derived from a module namespace parameter default. */
	UPROPERTY(Transient, meta = (SkipForCompileHash = "true"))
	bool bAddedToNodeGraphDeepCopy;

	/** Only valid if Usage is Output. Marks the associated FNiagaraVariable as Persistent across script runs and therefore should be retained in the Dataset during compilation/translation. */
	UPROPERTY(meta = (SkipForCompileHash = "true"))
	bool bOutputIsPersistent;

	/** Namespace-less name for associated FNiagaraVariable. Edited directly by user and then used to generate full Name of associated FNiagaraVariable. */
	UPROPERTY(DisplayName = "Property Name", meta = (SkipForCompileHash = "true"))
	FName CachedNamespacelessVariableName;

	/** Track if the associated parameter was created in the Emitter/System editor. Used to determine whether the associated parameter can be deleted from the Emitter/System editor. */
	UPROPERTY(meta = (SkipForCompileHash = "true"))
	bool bCreatedInSystemEditor;

	UPROPERTY(meta = (ToolTip = "Enable using a legacy custom name string.", SkipForCompileHash = "true"))
	bool bUseLegacyNameString;

public:
	FORCEINLINE bool IsInputUsage() const { return Usage == ENiagaraScriptParameterUsage::Input || Usage == ENiagaraScriptParameterUsage::InputOutput; };
	FORCEINLINE bool IsInputOrLocalUsage() const { return Usage == ENiagaraScriptParameterUsage::Input || Usage == ENiagaraScriptParameterUsage::InputOutput || Usage == ENiagaraScriptParameterUsage::InitialValueInput || Usage == ENiagaraScriptParameterUsage::Local; };
};

USTRUCT()
struct NIAGARA_API FNiagaraTypeDefinition
{
	GENERATED_USTRUCT_BODY()

	enum FUnderlyingType
	{
		UT_None = 0,
		UT_Class,
		UT_Struct,
		UT_Enum
	};

public:

	// Construct blank raw type definition 
	FORCEINLINE FNiagaraTypeDefinition(UClass *ClassDef)
		: ClassStructOrEnum(ClassDef), UnderlyingType(UT_Class), Size(INDEX_NONE), Alignment(INDEX_NONE)
#if WITH_EDITORONLY_DATA
		, Struct_DEPRECATED(nullptr), Enum_DEPRECATED(nullptr)
#endif
	{
		checkSlow(ClassStructOrEnum != nullptr);
	}

	FORCEINLINE FNiagaraTypeDefinition(UEnum *EnumDef)
		: ClassStructOrEnum(EnumDef), UnderlyingType(UT_Enum), Size(INDEX_NONE), Alignment(INDEX_NONE)
#if WITH_EDITORONLY_DATA
		, Struct_DEPRECATED(nullptr), Enum_DEPRECATED(nullptr)
#endif
	{
		checkSlow(ClassStructOrEnum != nullptr);
	}

	FORCEINLINE FNiagaraTypeDefinition(UScriptStruct *StructDef)
		: ClassStructOrEnum(StructDef), UnderlyingType(UT_Struct), Size(INDEX_NONE), Alignment(INDEX_NONE)
#if WITH_EDITORONLY_DATA
		, Struct_DEPRECATED(nullptr), Enum_DEPRECATED(nullptr)
#endif
	{
		checkSlow(ClassStructOrEnum != nullptr);
	}

	FORCEINLINE FNiagaraTypeDefinition(const FNiagaraTypeDefinition &Other)
		: ClassStructOrEnum(Other.ClassStructOrEnum), UnderlyingType(Other.UnderlyingType), Size(INDEX_NONE), Alignment(INDEX_NONE)
#if WITH_EDITORONLY_DATA
		, Struct_DEPRECATED(nullptr), Enum_DEPRECATED(nullptr)
#endif
	{
	}

	// Construct a blank raw type definition
	FORCEINLINE FNiagaraTypeDefinition()
		: ClassStructOrEnum(nullptr), UnderlyingType(UT_None), Size(INDEX_NONE), Alignment(INDEX_NONE)
#if WITH_EDITORONLY_DATA
		, Struct_DEPRECATED(nullptr), Enum_DEPRECATED(nullptr)
#endif
	{}

	FORCEINLINE bool operator !=(const FNiagaraTypeDefinition &Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE bool operator == (const FNiagaraTypeDefinition &Other) const
	{
		return ClassStructOrEnum == Other.ClassStructOrEnum && UnderlyingType == Other.UnderlyingType;
	}

	FText GetNameText()const
	{
		if (IsValid() == false)
		{
			return NSLOCTEXT("NiagaraTypeDefinition", "InvalidNameText", "Invalid (null type)");
		}

#if WITH_EDITOR
		if ( UnderlyingType != UT_Enum )
		{
			return GetStruct()->GetDisplayNameText();
		}
#endif
		return FText::FromString(ClassStructOrEnum->GetName());
	}

	FName GetFName() const
	{
		if ( IsValid() == false )
		{
			return FName();
		}
		return ClassStructOrEnum->GetFName();
	}

	FString GetName()const
	{
		if (IsValid() == false)
		{
			return TEXT("Invalid");
		}
		return ClassStructOrEnum->GetName();
	}

	UStruct* GetStruct() const
	{
		return UnderlyingType == UT_Enum ? IntStruct : Cast<UStruct>(ClassStructOrEnum);
	}

	UScriptStruct* GetScriptStruct()const
	{
		return Cast<UScriptStruct>(GetStruct());
	}

	/** Gets the class ptr for this type if it is a class. */
	UClass* GetClass() const
	{
		return UnderlyingType == UT_Class ? CastChecked<UClass>(ClassStructOrEnum) : nullptr;
	}

	UEnum* GetEnum() const
	{
		return UnderlyingType == UT_Enum ? CastChecked<UEnum>(ClassStructOrEnum) : nullptr;
	}

	bool IsDataInterface() const;

	FORCEINLINE bool IsUObject() const
	{
		return GetStruct()->IsChildOf<UObject>();
	}

	bool IsEnum() const { return UnderlyingType == UT_Enum; }
	
	int32 GetSize() const
	{
		if (Size == INDEX_NONE)
		{
			checkfSlow(IsValid(), TEXT("Type definition is not valid."));
			if (GetClass())
			{
				Size = 0;//TODO: sizeof(void*);//If we're a class then we allocate space for the user to instantiate it. This and stopping it being GCd is up to the user.
			}
			else
			{
				Size = CastChecked<UScriptStruct>(GetStruct())->GetStructureSize();
			}
		}
		return Size;
	}

	int32 GetAlignment() const
	{
		if (Alignment == INDEX_NONE)
		{
			checkfSlow(IsValid(), TEXT("Type definition is not valid."));
			if (GetClass())
			{
				Alignment = 0;//TODO: sizeof(void*);//If we're a class then we allocate space for the user to instantiate it. This and stopping it being GCd is up to the user.
			}
			else
			{
				Alignment = CastChecked<UScriptStruct>(GetStruct())->GetMinAlignment();
			}
		}
		return Alignment;
	}

	bool IsFloatPrimitive() const
	{
		return ClassStructOrEnum == FNiagaraTypeDefinition::GetFloatStruct() || ClassStructOrEnum == FNiagaraTypeDefinition::GetVec2Struct() || ClassStructOrEnum == FNiagaraTypeDefinition::GetVec3Struct() || ClassStructOrEnum == FNiagaraTypeDefinition::GetVec4Struct() ||
			ClassStructOrEnum == FNiagaraTypeDefinition::GetMatrix4Struct() || ClassStructOrEnum == FNiagaraTypeDefinition::GetColorStruct() || ClassStructOrEnum == FNiagaraTypeDefinition::GetQuatStruct();
 	}

	bool IsValid() const 
	{ 
		return ClassStructOrEnum != nullptr;
	}

	bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;

	/*
	Underlying type for this variable, use FUnderlyingType to determine type without casting
	This can be a UClass, UStruct or UEnum.  Pointing to something like the struct for an FVector, etc.
	In occasional situations this may be a UClass when we're dealing with DataInterface etc.
	*/
	UPROPERTY(EditAnywhere, Category=Type)
	UObject* ClassStructOrEnum;

	// See enumeration FUnderlyingType for possible values
	UPROPERTY(EditAnywhere, Category=Type)
	uint16 UnderlyingType;

	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);

private:
	mutable int16 Size;
	mutable int16 Alignment;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UStruct* Struct_DEPRECATED;

	UPROPERTY()
	UEnum* Enum_DEPRECATED;
#endif

public:
	static void Init();
#if WITH_EDITOR
	static void RecreateUserDefinedTypeRegistry();
#endif
	static const FNiagaraTypeDefinition& GetFloatDef() { return FloatDef; }
	static const FNiagaraTypeDefinition& GetBoolDef() { return BoolDef; }
	static const FNiagaraTypeDefinition& GetIntDef() { return IntDef; }
	static const FNiagaraTypeDefinition& GetVec2Def() { return Vec2Def; }
	static const FNiagaraTypeDefinition& GetVec3Def() { return Vec3Def; }
	static const FNiagaraTypeDefinition& GetVec4Def() { return Vec4Def; }
	static const FNiagaraTypeDefinition& GetColorDef() { return ColorDef; }
	static const FNiagaraTypeDefinition& GetQuatDef() { return QuatDef; }
	static const FNiagaraTypeDefinition& GetMatrix4Def() { return Matrix4Def; }
	static const FNiagaraTypeDefinition& GetGenericNumericDef() { return NumericDef; }
	static const FNiagaraTypeDefinition& GetParameterMapDef() { return ParameterMapDef; }
	static const FNiagaraTypeDefinition& GetIDDef() { return IDDef; }
	static const FNiagaraTypeDefinition& GetUObjectDef() { return UObjectDef; }
	static const FNiagaraTypeDefinition& GetUMaterialDef() { return UMaterialDef; }

	static const FNiagaraTypeDefinition& GetHalfDef() { return HalfDef; }
	static const FNiagaraTypeDefinition& GetHalfVec2Def() { return HalfVec2Def; }
	static const FNiagaraTypeDefinition& GetHalfVec3Def() { return HalfVec3Def; }
	static const FNiagaraTypeDefinition& GetHalfVec4Def() { return HalfVec4Def; }

	template<typename T>
	static const FNiagaraTypeDefinition& Get();

	static UScriptStruct* GetFloatStruct() { return FloatStruct; }
	static UScriptStruct* GetBoolStruct() { return BoolStruct; }
	static UScriptStruct* GetIntStruct() { return IntStruct; }
	static UScriptStruct* GetVec2Struct() { return Vec2Struct; }
	static UScriptStruct* GetVec3Struct() { return Vec3Struct; }
	static UScriptStruct* GetVec4Struct() { return Vec4Struct; }
	static UScriptStruct* GetColorStruct() { return ColorStruct; }
	static UScriptStruct* GetQuatStruct() { return QuatStruct; }
	static UScriptStruct* GetMatrix4Struct() { return Matrix4Struct; }
	static UScriptStruct* GetGenericNumericStruct() { return NumericStruct; }
	static UScriptStruct* GetParameterMapStruct() { return ParameterMapStruct; }
	static UScriptStruct* GetIDStruct() { return IDStruct; }

	static UScriptStruct* GetHalfStruct() { return HalfStruct; }
	static UScriptStruct* GetHalfVec2Struct() { return HalfVec2Struct; }
	static UScriptStruct* GetHalfVec3Struct() { return HalfVec3Struct; }
	static UScriptStruct* GetHalfVec4Struct() { return HalfVec4Struct; }

	static UEnum* GetExecutionStateEnum() { return ExecutionStateEnum; }
	static UEnum* GetExecutionStateSouceEnum() { return ExecutionStateSourceEnum; }
	static UEnum* GetSimulationTargetEnum() { return SimulationTargetEnum; }
	static UEnum* GetScriptUsageEnum() { return ScriptUsageEnum; }
	static UEnum* GetParameterPanelCategoryEnum() { return ParameterPanelCategoryEnum; }

	static UEnum* GetParameterScopeEnum() { return ParameterScopeEnum; }

	static const FNiagaraTypeDefinition& GetCollisionEventDef() { return CollisionEventDef; }

	static bool IsScalarDefinition(const FNiagaraTypeDefinition& Type);

	FString ToString(const uint8* ValueData)const
	{
		checkf(IsValid(), TEXT("Type definition is not valid."));
		if (ValueData == nullptr)
		{
			return TEXT("(null)");
		}
		return FNiagaraTypeHelper::ToString(ValueData, ClassStructOrEnum);
	}

	static bool TypesAreAssignable(const FNiagaraTypeDefinition& TypeA, const FNiagaraTypeDefinition& TypeB);
	static bool IsLossyConversion(const FNiagaraTypeDefinition& TypeA, const FNiagaraTypeDefinition& TypeB);
	static FNiagaraTypeDefinition GetNumericOutputType(const TArray<FNiagaraTypeDefinition> TypeDefinintions, ENiagaraNumericOutputTypeSelectionMode SelectionMode);

	static const TArray<FNiagaraTypeDefinition>& GetNumericTypes() { return OrderedNumericTypes; }
	static bool IsValidNumericInput(const FNiagaraTypeDefinition& TypeDef);
private:

	static FNiagaraTypeDefinition FloatDef;
	static FNiagaraTypeDefinition BoolDef;
	static FNiagaraTypeDefinition IntDef;
	static FNiagaraTypeDefinition Vec2Def;
	static FNiagaraTypeDefinition Vec3Def;
	static FNiagaraTypeDefinition Vec4Def;
	static FNiagaraTypeDefinition ColorDef;
	static FNiagaraTypeDefinition QuatDef;
	static FNiagaraTypeDefinition Matrix4Def;
	static FNiagaraTypeDefinition NumericDef;
	static FNiagaraTypeDefinition ParameterMapDef;
	static FNiagaraTypeDefinition IDDef;
	static FNiagaraTypeDefinition UObjectDef;
	static FNiagaraTypeDefinition UMaterialDef;

	static FNiagaraTypeDefinition HalfDef;
	static FNiagaraTypeDefinition HalfVec2Def;
	static FNiagaraTypeDefinition HalfVec3Def;
	static FNiagaraTypeDefinition HalfVec4Def;

	static UScriptStruct* FloatStruct;
	static UScriptStruct* BoolStruct;
	static UScriptStruct* IntStruct;
	static UScriptStruct* Vec2Struct;
	static UScriptStruct* Vec3Struct;
	static UScriptStruct* Vec4Struct;
	static UScriptStruct* QuatStruct;
	static UScriptStruct* ColorStruct;
	static UScriptStruct* Matrix4Struct;
	static UScriptStruct* NumericStruct;

	static UScriptStruct* HalfStruct;
	static UScriptStruct* HalfVec2Struct;
	static UScriptStruct* HalfVec3Struct;
	static UScriptStruct* HalfVec4Struct;

	static UClass* UObjectClass;
	static UClass* UMaterialClass;

	static UEnum* SimulationTargetEnum;
	static UEnum* ScriptUsageEnum;
	static UEnum* ExecutionStateEnum;
	static UEnum* ExecutionStateSourceEnum;

	static UEnum* ParameterScopeEnum;
	static UEnum* ParameterPanelCategoryEnum;

	static UScriptStruct* ParameterMapStruct;
	static UScriptStruct* IDStruct;

	static TSet<UScriptStruct*> NumericStructs;
	static TArray<FNiagaraTypeDefinition> OrderedNumericTypes;

	static TSet<UScriptStruct*> ScalarStructs;

	static TSet<UStruct*> FloatStructs;
	static TSet<UStruct*> IntStructs;
	static TSet<UStruct*> BoolStructs;

	static FNiagaraTypeDefinition CollisionEventDef;
};

template<>
struct TStructOpsTypeTraits<FNiagaraTypeDefinition> : public TStructOpsTypeTraitsBase2<FNiagaraTypeDefinition>
{
	enum
	{
		WithSerializer = true,
		WithPostSerialize = true,
	};
};


//Helper to get the correct typedef for templated code.
template<typename T>
const FNiagaraTypeDefinition& FNiagaraTypeDefinition::Get()
{
	if (TIsSame<T, float>::Value) { return FNiagaraTypeDefinition::GetFloatDef(); }
	if (TIsSame<T, FVector2D>::Value) { return FNiagaraTypeDefinition::GetVec2Def(); }
	if (TIsSame<T, FVector>::Value) { return FNiagaraTypeDefinition::GetVec3Def(); }
	if (TIsSame<T, FVector4>::Value) { return FNiagaraTypeDefinition::GetVec4Def(); }
	if (TIsSame<T, float>::Value) { return FNiagaraTypeDefinition::GetFloatDef(); }
	if (TIsSame<T, int32>::Value) { return FNiagaraTypeDefinition::GetIntDef(); }
	if (TIsSame<T, FNiagaraBool>::Value) { return FNiagaraTypeDefinition::GetBoolDef(); }
	if (TIsSame<T, FQuat>::Value) { return FNiagaraTypeDefinition::GetQuatDef(); }
	if (TIsSame<T, FMatrix>::Value) { return FNiagaraTypeDefinition::GetMatrix4Def(); }
	if (TIsSame<T, FLinearColor>::Value) { return FNiagaraTypeDefinition::GetColorDef(); }
	if (TIsSame<T, FNiagaraID>::Value) { return FNiagaraTypeDefinition::GetIDDef(); }
}

//////////////////////////////////////////////////////////////////////////


/* Contains all types currently available for use in Niagara
* Used by UI to provide selection; new uniforms and variables
* may be instanced using the types provided here
*/
class NIAGARA_API FNiagaraTypeRegistry
{
public:
	static const TArray<FNiagaraTypeDefinition> &GetRegisteredTypes()
	{
		return RegisteredTypes;
	}

	static const TArray<FNiagaraTypeDefinition> &GetRegisteredParameterTypes()
	{
		return RegisteredParamTypes;
	}

	static const TArray<FNiagaraTypeDefinition> &GetRegisteredPayloadTypes()
	{
		return RegisteredPayloadTypes;
	}

	static const TArray<FNiagaraTypeDefinition>& GetUserDefinedTypes()
	{
		return RegisteredUserDefinedTypes;
	}

	static const TArray<FNiagaraTypeDefinition>& GetNumericTypes()
	{ 
		return RegisteredNumericTypes;
	}

	static UNiagaraDataInterfaceBase* GetDefaultDataInterfaceByName(const FString& DIClassName);

	static void ClearUserDefinedRegistry()
	{
		for (const FNiagaraTypeDefinition& Def : RegisteredUserDefinedTypes)
		{
			RegisteredTypes.Remove(Def);
			RegisteredPayloadTypes.Remove(Def);
			RegisteredParamTypes.Remove(Def);
		}

		RegisteredNumericTypes.Empty();
		RegisteredUserDefinedTypes.Empty();
	}

	static void Register(const FNiagaraTypeDefinition &NewType, bool bCanBeParameter, bool bCanBePayload, bool bIsUserDefined)
	{
		//TODO: Make this a map of type to a more verbose set of metadata? Such as the hlsl defs, offset table for conversions etc.
		RegisteredTypes.AddUnique(NewType);

		if (bCanBeParameter)
		{
			RegisteredParamTypes.AddUnique(NewType);
		}

		if (bCanBePayload)
		{
			RegisteredPayloadTypes.AddUnique(NewType);
		}

		if (bIsUserDefined)
		{
			RegisteredUserDefinedTypes.AddUnique(NewType);
		}

		if (FNiagaraTypeDefinition::IsValidNumericInput(NewType))
		{
			RegisteredNumericTypes.AddUnique(NewType);
		}
	}

	static void Deregister(const FNiagaraTypeDefinition& Type)
	{
		RegisteredTypes.Remove(Type);
		RegisteredParamTypes.Remove(Type);
		RegisteredPayloadTypes.Remove(Type);
		RegisteredUserDefinedTypes.Remove(Type);
		RegisteredNumericTypes.Remove(Type);
	}

	FNiagaraTypeDefinition GetTypeDefFromStruct(UStruct* Struct)
	{
		for (FNiagaraTypeDefinition& TypeDef : RegisteredTypes)
		{
			if (Struct == TypeDef.GetStruct())
			{
				return TypeDef;
			}
		}

		return FNiagaraTypeDefinition();
	}

private:
	static TArray<FNiagaraTypeDefinition> RegisteredTypes;
	static TArray<FNiagaraTypeDefinition> RegisteredParamTypes;
	static TArray<FNiagaraTypeDefinition> RegisteredPayloadTypes;
	static TArray<FNiagaraTypeDefinition> RegisteredUserDefinedTypes;
	static TArray<FNiagaraTypeDefinition> RegisteredNumericTypes;
};

FORCEINLINE uint32 GetTypeHash(const FNiagaraTypeDefinition& Type)
{
	return HashCombine(GetTypeHash(Type.GetStruct()), GetTypeHash(Type.GetEnum()));
}

//////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FNiagaraVariableBase
{
	GENERATED_USTRUCT_BODY()

	FORCEINLINE FNiagaraVariableBase() : Name(NAME_None), TypeDef(FNiagaraTypeDefinition::GetVec4Def()) { }
	FORCEINLINE FNiagaraVariableBase(const FNiagaraVariableBase &Other) : Name(Other.Name), TypeDef(Other.TypeDef) { }
	FORCEINLINE FNiagaraVariableBase(const FNiagaraTypeDefinition& InType, const FName& InName) : Name(InName), TypeDef(InType) { }
	
	/** Check if Name and Type definition are the same. The actual stored value is not checked here.*/
	bool operator==(const FNiagaraVariableBase& Other)const
	{
		return Name == Other.Name && TypeDef == Other.TypeDef;
	}

	/** Check if Name and Type definition are not the same. The actual stored value is not checked here.*/
	bool operator!=(const FNiagaraVariableBase& Other)const
	{
		return !(*this == Other);
	}

	/** Variables are the same name but if types are auto-assignable, allow them to match. */
	bool IsEquivalent(const FNiagaraVariableBase& Other, bool bAllowAssignableTypes = true)const
	{
		return Name == Other.Name && (TypeDef == Other.TypeDef || (bAllowAssignableTypes && FNiagaraTypeDefinition::TypesAreAssignable(TypeDef, Other.TypeDef)));
	}
	
	FORCEINLINE void SetName(FName InName) { Name = InName; }
	FORCEINLINE const FName& GetName() const { return Name; }

	void SetType(const FNiagaraTypeDefinition& InTypeDef) { TypeDef = InTypeDef; }
	const FNiagaraTypeDefinition& GetType()const { return TypeDef; }

	FORCEINLINE bool IsDataInterface()const { return GetType().IsDataInterface(); }
	FORCEINLINE bool IsUObject()const { return GetType().IsUObject(); }

	int32 GetSizeInBytes() const
	{
		return TypeDef.GetSize();
	}

	int32 GetAlignment()const
	{
		return TypeDef.GetAlignment();
	}

	bool IsValid() const
	{
		return Name != NAME_None && TypeDef.IsValid();
	}

	FORCEINLINE bool IsInNameSpace(FString Namespace) const
	{
		return Name.ToString().StartsWith(Namespace + TEXT("."));
	}

protected:
	UPROPERTY(EditAnywhere, Category = "Variable")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Variable")
	FNiagaraTypeDefinition TypeDef;
};

FORCEINLINE uint32 GetTypeHash(const FNiagaraVariableBase& Var)
{
	return HashCombine(GetTypeHash(Var.GetType()), GetTypeHash(Var.GetName()));
}

USTRUCT()
struct FNiagaraVariable : public FNiagaraVariableBase
{
	GENERATED_USTRUCT_BODY()

	FNiagaraVariable()
	{
	}

	FNiagaraVariable(const FNiagaraVariable &Other)
		: FNiagaraVariableBase(Other)
	{
		if (Other.IsDataAllocated())
		{
			SetData(Other.GetData());
		}
	}

	FNiagaraVariable(const FNiagaraVariableBase& Other)
		: FNiagaraVariableBase(Other)
	{
	}

	FORCEINLINE FNiagaraVariable(const FNiagaraTypeDefinition& InType, const FName& InName)
		: FNiagaraVariableBase(InType, InName)
	{
	}
	
	/** Check if Name and Type definition are the same. The actual stored value is not checked here.*/
	bool operator==(const FNiagaraVariable& Other)const
	{
		//-TODO: Should this check the value???
		return Name == Other.Name && TypeDef == Other.TypeDef;
	}

	/** Check if Name and Type definition are not the same. The actual stored value is not checked here.*/
	bool operator!=(const FNiagaraVariable& Other)const
	{
		return !(*this == Other);
	}

	/** Checks if the types match and either both variables are uninitialized or both hold exactly the same data.*/
	bool HoldsSameData(const FNiagaraVariable& Other) const
	{
		if (TypeDef != Other.TypeDef) {
			return false;
		}
		if (!IsDataAllocated() && !Other.IsDataAllocated()) {
			return true;
		}
		return IsDataAllocated() && Other.IsDataAllocated() && VarData.Num() == Other.VarData.Num() && FMemory::Memcmp(VarData.GetData(), Other.VarData.GetData(), VarData.Num()) == 0;
	}

	// Var data operations
	void AllocateData()
	{
		if (VarData.Num() != TypeDef.GetSize())
		{
			VarData.SetNumZeroed(TypeDef.GetSize());
		}
	}

	bool IsDataAllocated()const { return VarData.Num() > 0 && VarData.Num() == TypeDef.GetSize(); }

	void CopyTo(uint8* Dest) const
	{
		check(TypeDef.GetSize() == VarData.Num());
		check(IsDataAllocated());
		FMemory::Memcpy(Dest, VarData.GetData(), VarData.Num());
	}
		
	template<typename T>
	void SetValue(const T& Data)
	{
		check(sizeof(T) == TypeDef.GetSize());
		AllocateData();
		FMemory::Memcpy(VarData.GetData(), &Data, VarData.Num());
	}

	template<typename T>
	T GetValue() const
	{
		check(sizeof(T) == TypeDef.GetSize());
		check(IsDataAllocated());
		T Value;
		FMemory::Memcpy(&Value, GetData(), TypeDef.GetSize());
		return Value;
	}

	void SetData(const uint8* Data)
	{
		check(Data);
		AllocateData();
		FMemory::Memcpy(VarData.GetData(), Data, VarData.Num());
	}

	const uint8* GetData() const
	{
		return VarData.GetData();
	}

	uint8* GetData()
	{
		return VarData.GetData();
	}

	void ClearData()
	{
		VarData.Empty();
	}

	int32 GetAllocatedSizeInBytes() const
	{
		return VarData.Num();
	}

	FString ToString()const
	{
		FString Ret = Name.ToString() + TEXT("(");
		Ret += TypeDef.ToString(VarData.GetData());
		Ret += TEXT(")");
		return Ret;
	}

	static FNiagaraVariable ResolveAliases(const FNiagaraVariable& InVar, const TMap<FString, FString>& InAliases, const TCHAR* InJoinSeparator = TEXT("."))
	{
		FNiagaraVariable OutVar = InVar;

		FString OutVarStrName = InVar.GetName().ToString();
		TArray<FString> SplitName;
		OutVarStrName.ParseIntoArray(SplitName, TEXT("."));

		for (int32 i = 0; i < SplitName.Num() - 1; i++)
		{
			TMap<FString, FString>::TConstIterator It = InAliases.CreateConstIterator();
			while (It)
			{
				if (SplitName[i].Equals(It.Key()))
				{
					SplitName[i] = It.Value();
				}
				++It;
			}
		}

		OutVarStrName = FString::Join(SplitName, InJoinSeparator);

		OutVar.SetName(*OutVarStrName);
		return OutVar;
	}

	static int32 SearchArrayForPartialNameMatch(const TArray<FNiagaraVariable>& Variables, const FName& VariableName)
	{
		FString VarNameStr = VariableName.ToString();
		FString BestMatchSoFar;
		int32 BestMatchIdx = INDEX_NONE;

		for (int32 i = 0; i < Variables.Num(); i++)
		{
			const FNiagaraVariable& TestVar = Variables[i];
			FString TestVarNameStr = TestVar.GetName().ToString();
			if (TestVarNameStr == VarNameStr)
			{
				return i;
			}
			else if (VarNameStr.StartsWith(TestVarNameStr + TEXT(".")) && (BestMatchSoFar.Len() == 0 || TestVarNameStr.Len() > BestMatchSoFar.Len()))
			{
				BestMatchIdx = i;
				BestMatchSoFar = TestVarNameStr;
			}
		}

		return BestMatchIdx;
	}

private:
	//This gets serialized but do we need to worry about endianness doing things like this? If not, where does that get handled?
	//TODO: Remove storage here entirely and move everything to an FNiagaraParameterStore.
	UPROPERTY(meta = (SkipForCompileHash = "true"))
	TArray<uint8> VarData;
};

template<>
inline bool FNiagaraVariable::GetValue<bool>() const
{
	check(TypeDef == FNiagaraTypeDefinition::GetBoolDef());
	check(IsDataAllocated());
	FNiagaraBool* BoolStruct = (FNiagaraBool*)GetData();
	return BoolStruct->GetValue();
}

template<>
inline void FNiagaraVariable::SetValue<bool>(const bool& Data)
{
	check(TypeDef == FNiagaraTypeDefinition::GetBoolDef());
	AllocateData();
	FNiagaraBool* BoolStruct = (FNiagaraBool*)GetData();
	BoolStruct->SetValue(Data);
}

// Any change to this structure, or it's GetVariables implementation will require a bump in the CustomNiagaraVersion so that we
// properly rebuild the scripts
struct alignas(16) FNiagaraGlobalParameters
{
#if WITH_EDITOR
	NIAGARA_API static const TArray<FNiagaraVariable>& GetVariables();
#endif

	float EngineDeltaTime =  0.0f;
	float EngineInvDeltaTime = 0.0f;
	float EngineTime = 0.0f;
	float EngineRealTime = 0.0f;
};

// Any change to this structure, or it's GetVariables implementation will require a bump in the CustomNiagaraVersion so that we
// properly rebuild the scripts
struct alignas(16) FNiagaraSystemParameters
{
#if WITH_EDITOR
	NIAGARA_API static const TArray<FNiagaraVariable>& GetVariables();
#endif

	float EngineTimeSinceRendered = 0.0f;
	float EngineLodDistance = 0.0f;
	float EngineLodDistanceFraction = 0.0f;
	float EngineSystemAge = 0.0f;
	uint32 EngineExecutionState = 0;
	int32 EngineTickCount = 0;
	int32 EngineEmitterCount = 0;
	int32 EngineAliveEmitterCount = 0;
};

// Any change to this structure, or it's GetVariables implementation will require a bump in the CustomNiagaraVersion so that we
// properly rebuild the scripts
struct alignas(16) FNiagaraOwnerParameters
{
#if WITH_EDITOR
	NIAGARA_API static const TArray<FNiagaraVariable>& GetVariables();
#endif

	FMatrix EngineLocalToWorld = FMatrix::Identity;
	FMatrix EngineWorldToLocal = FMatrix::Identity;
	FMatrix EngineLocalToWorldTransposed = FMatrix::Identity;
	FMatrix EngineWorldToLocalTransposed = FMatrix::Identity;
	FMatrix EngineLocalToWorldNoScale = FMatrix::Identity;
	FMatrix EngineWorldToLocalNoScale = FMatrix::Identity;
	FQuat EngineRotation = FQuat::Identity;
	FVector4 EnginePosition = FVector4(EForceInit::ForceInitToZero);
	FVector4 EngineVelocity = FVector4(EForceInit::ForceInitToZero);
	FVector4 EngineXAxis = FVector4(1.0f, 0.0f, 0.0f, 0.0f);
	FVector4 EngineYAxis = FVector4(0.0f, 1.0f, 0.0f, 0.0f);
	FVector4 EngineZAxis = FVector4(0.0f, 0.0f, 1.0f, 0.0f);
	FVector4 EngineScale = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
};

// Any change to this structure, or it's GetVariables implementation will require a bump in the CustomNiagaraVersion so that we
// properly rebuild the scripts
struct alignas(16) FNiagaraEmitterParameters
{
#if WITH_EDITOR
	NIAGARA_API static const TArray<FNiagaraVariable>& GetVariables();
#endif

	int32 EmitterNumParticles = 0;
	int32 EmitterTotalSpawnedParticles = 0;
	float EmitterSpawnCountScale = 1.0f;
	float EmitterAge = 0.0f;
	int32 EmitterRandomSeed = 0;

	// todo - what else should be inserted here?  we could put an array of spawninfos/interp spawn values
	int32 _Pad0;
	int32 _Pad1;
	int32 _Pad2;
};