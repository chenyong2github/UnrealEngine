// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "Param/Param.h"
#include "AnimNextInterfaceState.h"
#include "Param/ParamStorage.h"
#include "AnimNextInterfaceKey.h"
#include "Param/ParamType.h"
#include "Misc/MemStack.h"

class IAnimNextInterface;
class IAnimNextParamInterface;

namespace UE::AnimNext
{

struct FContext;
struct FState;
enum class EStatePersistence : uint8;

// Helper to provide a non-callstack bridge for interfacing with anim interface contexts
// Declaring one of these on the stack will enable all calls in its scope to access the passed-in context
// via FThreadContext::Get()
struct ANIMNEXTINTERFACE_API FThreadContext
{
	FThreadContext(const FContext& InContext);
	~FThreadContext();

	static const FContext& Get();
};

// Context providing methods for mutating & interrogating the anim interface runtime
struct ANIMNEXTINTERFACE_API FContext
{
private:
	friend class ::IAnimNextInterface;
	friend struct FState;

public:
	// Root public constructor. Constructs a context given a state.
	FContext(float InDeltaTime, FState& InState, FParamStorage& InParamStorage, IAnimNextParamInterface* InParameters = nullptr);
	
	FContext(const FContext& other) = delete;
	FContext& operator=(const FContext&) = delete;

	FContext(FContext&& Other)
		: FContext()
	{
		Swap(*this, Other);
	}

	FContext& operator= (FContext&& Other)
	{
		Swap(*this, Other);
		return *this;
	}

	~FContext();

public:
	// --- Sub Context Creation --- 

	// Create a sub context from this one that includes the provided Result
	FContext WithResult(FParam& InResult) const;

	// Create a sub context from this one that includes the provided parameter
	FContext WithParameter(FName ParameterId, const FParam& InParameter) const;

	// Create a sub context from this one that includes the provided parameters
	FContext WithParameters(TArrayView<const TPair<FName, FParam>> InParameters) const;

	// Create a sub context from this one that includes the provided Result and parameters
	FContext WithResultAndParameters(FParam& InResult, TArrayView<const TPair<FName, FParam>> InParameters) const;

	// Create a sub context from this one that includes the provided interface parameter
	FContext WithParameters(IAnimNextParamInterface* InParameters) const;

public:

	// --- Interface for direct Param storage (prototype) ---

	FContext CreateSubContext() const;

	enum class EParamType : uint8
	{
		None		= 0,
		Input		= 1 << 0,
		Output		= 1 << 1,
	};

	// Add an Input parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddInputValue(FName ParameterId, ValueType& Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(EParamType::Input, ParameterId, Flags, &Value);
	}

	// Add an Input parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddInputValue(FName ParameterId, ValueType&& Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(EParamType::Input, ParameterId, Flags, &Value);
	}

	// Add an Input parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddInputValue(FName ParameterId, ValueType* Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(EParamType::Input, ParameterId, Flags, Value);
	}

	// Add an Output parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddOutputValue(FName ParameterId, ValueType& Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(EParamType::Output, ParameterId, Flags, &Value);
	}

	// Add an Output parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddOutputValue(FName ParameterId, ValueType&& Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(EParamType::Output, ParameterId, Flags, &Value);
	}

	// Add an Output parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddOutputValue(FName ParameterId, ValueType* Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(EParamType::Output, ParameterId, Flags, Value);
	}

	// Add a parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddValue(EParamType InParamType, FName ParameterId, ValueType &Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(InParamType, ParameterId, Flags, &Value);
	}

	// Add a parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddValue(EParamType InParamType, FName ParameterId, ValueType&& Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(InParamType, ParameterId, Flags, &Value);
	}

	// Add a parameter by value, copying it to the shared storage
	template<typename ValueType>
	FParamHandle AddValue(EParamType InParamType, FName ParameterId, ValueType* Value)
	{
		// Values are added mutable even if source is const
		const FParam::EFlags Flags = FParam::EFlags::Value | FParam::EFlags::Mutable;

		return AddParameter(InParamType, ParameterId, Flags, Value);
	}

	// --- Parameters by refecence / pointer ---

	// Add an Input parameter by reference, adding just a pointer to the shared storage
	template<typename ValueType>
	FParamHandle AddInputReference(FName ParameterId, ValueType& Value)
	{
		const FParam::EFlags Flags = FParam::EFlags::Reference;

		return AddParameter(EParamType::Input, ParameterId, Flags, &Value);
	}

	// Add an Input parameter by reference, adding just a pointer to the shared storage
	template<typename ValueType>
	FParamHandle AddInputReference(FName ParameterId, ValueType* Value)
	{
		FParam::EFlags Flags = FParam::EFlags::Reference;

		return AddParameter(EParamType::Input, ParameterId, Flags, Value);
	}

	// Add an Output parameter by reference, adding just a pointer to the shared storage
	template<typename ValueType>
	FParamHandle AddOutputReference(FName ParameterId, ValueType& Value)
	{
		const FParam::EFlags Flags = FParam::EFlags::Reference;

		return AddParameter(EParamType::Output, ParameterId, Flags, &Value);
	}

	// Add an Output parameter by reference, adding just a pointer to the shared storage
	template<typename ValueType>
	FParamHandle AddOutputReference(FName ParameterId, ValueType* Value)
	{
		FParam::EFlags Flags = FParam::EFlags::Reference;

		return AddParameter(EParamType::Output, ParameterId, Flags, Value);
	}

	// Add a parameter by reference, adding just a pointer to the shared storage
	template<typename ValueType>
	FParamHandle AddReference(EParamType InParamType, FName ParameterId, ValueType& Value)
	{
		const FParam::EFlags Flags = FParam::EFlags::Reference;

		return AddParameter(InParamType, ParameterId, Flags, &Value);
	}

	// Add a parameter by reference, adding just a pointer to the shared storage
	template<typename ValueType>
	FParamHandle AddReference(EParamType InParamType, FName ParameterId, ValueType *Value)
	{
		FParam::EFlags Flags = FParam::EFlags::Reference;

		return AddParameter(InParamType, ParameterId, Flags, Value);
	}

	template<typename ValueType>
	FParamHandle AddParameter(EParamType InParamType, FName ParameterId, FParam::EFlags Flags, ValueType* Value)
	{
		FParamHandle ParamHandle;

		check(InParamType == EParamType::Input || InParamType == EParamType::Output);
		check(EnumHasAnyFlags(Flags, FParam::EFlags::Value) || EnumHasAnyFlags(Flags, FParam::EFlags::Reference));

		// check if it is a simple FParamHandle copy
		if constexpr (TIsDerivedFrom<ValueType, FParamHandle>::Value)
		{
			const FParam* ExistingParam = ParamStorage->GetParam(Value->ParamHandle);
			check(ExistingParam != nullptr);

			const bool bIsReadOnly = EnumHasAnyFlags(ExistingParam->Flags, FParam::EFlags::Mutable) == false;

			// Verify that we want to create an Input param or if we want an output, the source is not read only
			check(InParamType == EParamType::Input || bIsReadOnly == false);

			// For now create a copy of the Param (TODO : change ref to value or value to ref, if the caller requests it?)
			ParamHandle = *Value;
		}
		else
		{
			using MutableValueType = std::remove_const_t<ValueType>;

			// If non const set the mutable flag
			if constexpr (TIsConst<ValueType>::Value == false)
			{
				EnumAddFlags(Flags, FParam::EFlags::Mutable);
			}

			ParamHandle = EnumHasAnyFlags(Flags, FParam::EFlags::Value)
				? ParamStorage->AddValue(const_cast<MutableValueType*>(Value), Flags)
				: ParamStorage->AddReference(const_cast<MutableValueType*>(Value), Flags);
		}

		AdditionalParameterHandles.Add(ParameterId, ParamHandle);

		return ParamHandle;
	}

	// Get a TParam from a handle
	template<typename ValueType>
	TParam<ValueType> GetParameterChecked(const FParamHandle &InParamHandle) const
	{
		const FParam* Param = ParamStorage->GetParam(InParamHandle.ParamHandle);

		TParam<ValueType> RetVal(Param, Param->GetFlags());

		check(RetVal.IsValid());							// found something
		checkSlow(Private::CheckParam<ValueType>(RetVal));	// the type found is valid for what was requested

		return RetVal;
	}

	// Get a parameter from a handle as a specified type
	template <typename ValueType>
	ValueType& GetParameterAs(const FParamHandle& InParamHandle)
	{
		const FParam* Param = ParamStorage->GetParam(InParamHandle.ParamHandle);

		check(Param != nullptr);
		check(Param->GetTypeHandle() == FParamTypeHandle::GetHandle<ValueType>());

		// If a non cost is requested, check the param has mutable flag
		if constexpr (TIsConst<ValueType>::Value == false)
		{
			check(Param->IsMutable());
		}

		ValueType* ValueData = EnumHasAnyFlags(Param->Flags, FParam::EFlags::Embedded)
			? static_cast<ValueType*>((void*)&Param->Data)
			: static_cast<ValueType*>(Param->Data);

		return *ValueData;
	}

	// --- Parameter management --- 

	// Get a parameter if exist, returns false if not
	bool GetParameter(FName InKey, FParam& OutParam) const;

	// Get a parameter as a specified type, checking it exist and the type
	template<typename ValueType>
	TParam<ValueType> GetParameterChecked(FName InKey) const
	{
		TParam<ValueType> RetVal;

		GetParameter(InKey, RetVal);

		check(RetVal.IsValid());							// found something
		checkSlow(Private::CheckParam<ValueType>(RetVal));	// the type found is valid for what was requested

		return RetVal;
	}

	// Get a parameter as a specified type, returning a TOptional
	template<typename ValueType>
	TOptional<TParam<ValueType>> GetParameter(FName InKey) const
	{
		TOptional<TParam<ValueType>> RetVal;

		TParam<ValueType> Param(FParam::EFlags::Mutable);
		if (GetParameter(InKey, Param))
		{
			checkSlow(Private::CheckParam<ValueType>(Param));

			RetVal = Param;
		}

		return RetVal;
	}

public:
	// --- Result Management ---

	// Get the ResultParam, checking it has been set
	FParam& GetResultParam()
	{
		check(Result != nullptr);
		return *Result;
	}

	// Get the ResultParam, checking it has been set
	FParam& GetResultParam() const
	{
		check(Result != nullptr);
		return *Result;
	}

	// Set a HParam as a result (for compatibility reasons)
	void SetHParamAsResult(const FParamHandle& InHParam)
	{
		Result = ParamStorage->GetParam(InHParam.ParamHandle);
	}

	// Set a result value directly
	// The receiver Result Param must be set prior to this call
	template<typename ValueType>
	void SetResult(const ValueType& InValue) const
	{
		check(Result != nullptr);
		TParam<ValueType> TypedResult(*Result);
		*TypedResult = InValue;
	}

	// Get the current result as a TParam
	template<typename ValueType>
	TParam<ValueType> GetResultParam() const
	{
		check(Result != nullptr);
		return TParam<ValueType>(*Result);
	}

	// Get the current result as a mutable reference
	template<typename ValueType>
	ValueType& GetResult() const
	{
		check(Result != nullptr);
		TParam<ValueType> TypedResult(*Result);
		return *TypedResult;
	}

	// Get the current result as a mutable ptr
	template<typename ValueType>
	ValueType* GetResultPtr() const
	{
		check(Result != nullptr);
		TParam<ValueType> TypedResult(*Result);
		return &*TypedResult;
	}

public:
	// --- State management ---

	// Gets (and allocates, if necessary) state for the specified node given this calling context
	template<typename ValueType, EStatePersistence Persistence = EStatePersistence::Relevancy>
	TParam<ValueType> GetStateParam(const FInterfaceKeyWithId& InKey) const
	{
		return State->GetState<ValueType, Persistence>(InKey, *this, CallstackHash);
	}

	// Gets (and allocates, if necessary) state for the specified node given this calling context
	template<typename ValueType, EStatePersistence Persistence = EStatePersistence::Relevancy>
	TParam<ValueType> GetStateParam(const IAnimNextInterface* InAnimNextInterface, uint32 InId) const
	{
		return State->GetState<ValueType, Persistence>(InAnimNextInterface, InId, *this, CallstackHash);
	}

	// Gets (and allocates, if necessary) state for the specified node given this calling context
	template<typename ValueType, EStatePersistence Persistence = EStatePersistence::Relevancy>
	ValueType& GetState(const FInterfaceKeyWithId& InKey) const
	{
		TParam<ValueType> Param = State->GetState<ValueType, Persistence>(InKey, *this, CallstackHash); 
		return *Param;
	}

	// Gets (and allocates, if necessary) state for the specified node given this calling context
	template<typename ValueType, EStatePersistence Persistence = EStatePersistence::Relevancy>
	ValueType& GetState(const IAnimNextInterface* InAnimNextInterface, uint32 InId) const
	{
		TParam<ValueType> Param = State->GetState<ValueType, Persistence>(InAnimNextInterface, InId, *this, CallstackHash);
		return *Param;
	}
	
public:
	// --- Mix Context Utils ---

	// Access delta time as a param
	TParam<const float> GetDeltaTimeParam() const;

	// Raw access to delta time
	float GetDeltaTime() const { return DeltaTime; }

private:
	FContext(float InDeltaTime, FState& InState, FParamStorage& InParamStorage, FParam& InResult)
		: State(&InState)
		, ParamStorage(&InParamStorage)
		, Result(&InResult)
		, DeltaTime(InDeltaTime)
	{}

	FContext() = default;

	FContext WithCallRaw(const IAnimNextInterface* InAnimNextInterface) const;

	void FlushRelevancy() const;

	int32 GetParametersSize(TArrayView<const TPair<FName, FParam>> InParameters, TArray<int32>& ParamAllocSizes) const;
	void AddParameters(TArrayView<const TPair<FName, FParam>> InParameters);

private:
	TMap<FName, FParam> AdditionalParameters;
	TMap<FName, FParamHandle> AdditionalParameterHandles;  // Temp param storage prototype
	const FContext* Parent = nullptr;
	const FContext* Root = nullptr;
	FState* State = nullptr;
	FParamStorage* ParamStorage = nullptr;
	FParamStorageHandle BlockHandle = InvalidBlockHandle;
	FParam* Result = nullptr;
	IAnimNextParamInterface* Parameters = nullptr;
	float DeltaTime = 0.0f;
	uint32 CallstackHash = 0;
	uint32 UpdateCounter = 0;
};

}
