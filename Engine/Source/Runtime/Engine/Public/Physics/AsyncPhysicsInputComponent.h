// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Engine/NetConnection.h"
#include "Engine/PackageMapClient.h"
#include "Net/RepLayout.h"

#include "AsyncPhysicsInputComponent.generated.h"

class UAsyncPhysicsInputComponent;

/** 
	The base struct for async physics input. Inherit from this to create custom input data for async physics tick.
	When no input is available (say due to massive latency or packet loss) we fall back on the default constructed input.
	This means you should set the default values to something equivalent to no input (for example bPlayerWantsToJump should probably default to false)
*/
USTRUCT()
struct FAsyncPhysicsInput
{
	GENERATED_BODY()
public:
	virtual ~FAsyncPhysicsInput() = default;
	int32 GetServerFrame() const { return ServerFrame; }
private:
	UPROPERTY()
	int32 ServerFrame = INDEX_NONE;

	int32 Replicated = 4;	//how many times we want to replicate to server. TODO: use cvar

	friend UAsyncPhysicsInputComponent;
};

/** Helper struct to replicate polymorphic input data */
USTRUCT()
struct FAsyncPhysicsInputWrapper
{
	GENERATED_BODY()

	FAsyncPhysicsInput* Input = nullptr;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	UPROPERTY()
	TObjectPtr<UAsyncPhysicsInputComponent> OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FAsyncPhysicsInputWrapper> : public TStructOpsTypeTraitsBase2<FAsyncPhysicsInputWrapper>
{
	enum
	{
		WithNetSerializer = true
	};
};

class FAsyncPhysicsInputPool;

/** Base component used to easily send data from variable tick to async physics.
	Has networking support so server and client execute AsyncPhysicsTickComponent on same data for same step.
	Inherit from this class to create your own component that you can act on inputs with.

	Extended class must hold a TAsyncPhysicsInputPool<T> where T is your custom input extended from FAsyncPhysicsInput.
	Must call RegisterInputPool in InitializeComponent
*/
UCLASS(BlueprintType)
class ENGINE_API UAsyncPhysicsInputComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAsyncPhysicsInputComponent();
	void InitializeComponent() override;

	/** Must be called in InitializeComponent by extended class */
	void RegisterInputPool(FAsyncPhysicsInputPool* InPool);

	/** Executes physics logic based on the current input (See TNetworkedInputPool::GetCurrentInput)*/
	virtual void AsyncPhysicsTickComponent(float DeltaTime, float SimTime) override;

	UFUNCTION(Server, unreliable)
	void ServerRPCBufferInput(FAsyncPhysicsInputWrapper PlayerInput);

	void OnDispatchPhysicsTick(int32 PhysicsStep, int32 NumSteps, int32 ServerFrame);

	FAsyncPhysicsInputPool* GetInputPool() { return Pool; }

protected:
	UPrimitiveComponent* UpdateComponent = nullptr;

	APlayerController* GetPlayerController();

private:
	TArray<FAsyncPhysicsInput*> BufferedInputs;
	FAsyncPhysicsInputPool* Pool = nullptr;
};


class FAsyncPhysicsInputPool
{
public:
	virtual ~FAsyncPhysicsInputPool() = default;

private:
	friend class UAsyncPhysicsInputComponent;
	friend FAsyncPhysicsInputWrapper;

	/** Flushes the current input to populate so that new inputs can be populated. Returns the previous input to be saved off */
	virtual FAsyncPhysicsInput* FlushLatestInputToPopulate() = 0;

	/** Makes a deep copy of the input so that we can treat it as identical input over multiple physics steps from one slow gamethread step */
	virtual FAsyncPhysicsInput* CloneInput(const FAsyncPhysicsInput* Input) = 0;

	/** Sets the current input for execution during async physics */
	virtual void SetCurrentInputToAsyncExecute(FAsyncPhysicsInput* Input) = 0;

	/** Frees the input back into the pool*/
	virtual void FreeInputToPool(FAsyncPhysicsInput* Input) = 0;

	/** Serialize the underlying data. If needed a new entry in the pool will be created (during deserialization) */
	virtual void NetSerializeHelper(FAsyncPhysicsInput*& Data, FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) = 0;
};

/** Helper class to pool inputs and do various operations on like serialization.
*   T must have FAsyncPhysicsInput as its base */
template <typename T>
class TAsyncPhysicsInputPool final : public FAsyncPhysicsInputPool
{
public:
	TAsyncPhysicsInputPool()
	{
		PendingInputToPopulate = NewInput();
	}

	~TAsyncPhysicsInputPool()
	{
		delete PendingInputToPopulate;
		delete CurrentInputToExecute;
		while (Pool.Num())
		{
			T* Input = Pool.Pop();
			delete Input;
		}
	}

	/** Gets the pending input object for populating. This is what gets sent to the server and the async physics tick to execute logic off of.
		Should not be used in async tick as it may not be the right physics step*/
	T& GetPendingInputToPopulate() { return *PendingInputToPopulate; }
	const T& GetPendingInputToPopulate() const { return *PendingInputToPopulate; }

	/** Gets the current input object to execute logic off of. This should be used during async physics tick. If no input is available we use default constructed values*/
	const T& GetCurrentInput()
	{
		static T NoInput;
		return CurrentInputToExecute ? *CurrentInputToExecute : NoInput;
	}

private:

	virtual FAsyncPhysicsInput* FlushLatestInputToPopulate() override
	{
		//Assumes CurrentInputToPopulate has been saved into the send to server buffer and will be properly freed back into pool
		FAsyncPhysicsInput* OldInput = PendingInputToPopulate;
		PendingInputToPopulate = NewInput();
		return OldInput;
	}

	virtual FAsyncPhysicsInput* CloneInput(const FAsyncPhysicsInput* Input) override
	{
		T* Input2 = NewInput();
		*Input2 = *(static_cast<const T*>(Input));
		return Input2;
	}

	virtual void SetCurrentInputToAsyncExecute(FAsyncPhysicsInput* Input) override
	{
		CurrentInputToExecute = static_cast<T*>(Input);
	}

	virtual void NetSerializeHelper(FAsyncPhysicsInput*& Data, FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override
	{
		if (Ar.IsLoading())
		{
			Data = NewInput();
		}

		UScriptStruct* ScriptStruct = T::StaticStruct();
		if (ScriptStruct->StructFlags & STRUCT_NetSerializeNative)
		{
			ScriptStruct->GetCppStructOps()->NetSerialize(Ar, Map, bOutSuccess, Data);
		}
		else
		{
			UNetConnection* Connection = CastChecked<UPackageMapClient>(Map)->GetConnection();
			UNetDriver* NetDriver = Connection ? Connection->GetDriver() : nullptr;
			TSharedPtr<FRepLayout> RepLayout = NetDriver ? NetDriver->GetStructRepLayout(ScriptStruct) : nullptr;

			if (RepLayout.IsValid())
			{
				if (FBitArchive* BitAr = static_cast<FBitArchive*>(&Ar))
				{
					bool bHasUnmapped = false;
					RepLayout->SerializePropertiesForStruct(ScriptStruct, *BitAr, Map, Data, bHasUnmapped);

					bOutSuccess = true;
				}
			}
		}
	}

	T* NewInput()
	{
		T* Input;
		if (Pool.Num())
		{
			Input = Pool.Pop();
			new (Input) T();
		}
		else
		{
			Input = new T();
		}

		return Input;
	}

	void FreeInput(T* Input)
	{
		Input->~T();
		Pool.Add(Input);
	}

	virtual void FreeInputToPool(FAsyncPhysicsInput* Input) override
	{
		FreeInput(static_cast<T*>(Input));
	}

	T* CurrentInputToExecute = nullptr;
	T* PendingInputToPopulate = nullptr;
	TArray<T*> Pool;
};