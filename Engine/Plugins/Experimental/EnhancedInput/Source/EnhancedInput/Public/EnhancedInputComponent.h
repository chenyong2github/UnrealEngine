// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/InputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "UObject/ObjectMacros.h"

#include "EnhancedInputComponent.generated.h"

// TODO: Disable in test too?
#define DEV_ONLY_KEY_BINDINGS_AVAILABLE (!UE_BUILD_SHIPPING)

enum class ETriggerEvent : uint8;

/** Delegate signature for debug key events. */
DECLARE_DELEGATE_OneParam(FInputDebugKeyHandlerSignature, FKey);
DECLARE_DYNAMIC_DELEGATE_OneParam(FInputDebugKeyHandlerDynamicSignature, FKey, Key);

/** Delegate signature for action events. */
DECLARE_DELEGATE(FEnhancedInputActionHandlerSignature);
DECLARE_DELEGATE_OneParam(FEnhancedInputActionHandlerValueSignature, const FInputActionValue&);
DECLARE_DELEGATE_OneParam(FEnhancedInputActionHandlerInstanceSignature, const FInputActionInstance&);	// Provides full access to value and timers
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FEnhancedInputActionHandlerDynamicSignature, FInputActionValue, ActionValue, float, ElapsedTime, float, TriggeredTime);

/** Unified storage for both native and dynamic delegates with any signature  */
template<typename TSignature>
struct TEnhancedInputUnifiedDelegate
{
private:
	/** Holds the delegate to call. */
	TSharedPtr<TSignature> Delegate;

public:
	bool IsBound() const
	{
		return Delegate.IsValid() && Delegate->IsBound();
	}

	bool IsBoundToObject(void const* Object) const
	{
		return IsBound() && Delegate->IsBoundToObject(Object);
	}

	void Unbind()
	{
		if (Delegate)
		{
			Delegate->Unbind();
		}
	}

	/** Binds a native delegate, hidden for script delegates */
	template<	typename UserClass,
				typename TSig = TSignature>
	void BindDelegate(UserClass* Object, typename TSig::template TUObjectMethodDelegate<UserClass>::FMethodPtr Func)
	{
		Unbind();
		Delegate = MakeShared<TSig>(TSig::CreateUObject(Object, Func));
	}

	/** Binds a script delegate on an arbitrary UObject, hidden for native delegates */
	template<	typename TSig = TSignature,
				typename = typename TEnableIf<TIsDerivedFrom<TSig, TScriptDelegate<FWeakObjectPtr> >::IsDerived || TIsDerivedFrom<TSig, TMulticastScriptDelegate<FWeakObjectPtr> >::IsDerived>::Type>
	void BindDelegate(UObject* Object, const FName FuncName)
	{
		Unbind();
		Delegate = MakeShared<TSig>();
		Delegate->BindUFunction(Object, FuncName);
	}

	template<typename TSig = TSignature>
	TSig& MakeDelegate()
	{
		Unbind();
		Delegate = MakeShared<TSig>();
		return *Delegate;
	}

	template<typename... TArgs>
	void Execute(TArgs... Args) const
	{
		if (IsBound())
		{
			Delegate->Execute(Args...);
		}
	}
};


// Used to force clone constructor calls only
enum class EInputBindingClone : uint8 { ForceClone };

/** A basic binding unique identifier */
struct FInputBindingHandle
{
private:
	uint32 Handle = 0;

protected:
	ENHANCEDINPUT_API FInputBindingHandle();	// Generates a handle
	FInputBindingHandle(const FInputBindingHandle& CloneFrom, EInputBindingClone) : Handle(CloneFrom.Handle) {}	// Clones a handle

public:
	virtual ~FInputBindingHandle() = default;

	bool operator==(const FInputBindingHandle& Rhs) const
	{
		return (GetHandle() == Rhs.GetHandle());
	}

	uint32 GetHandle() const { return Handle; }
};

/** A binding to an input action trigger event. */
struct FEnhancedInputActionEventBinding : public FInputBindingHandle
{
private:

	/** Action against which we are bound */
	TWeakObjectPtr<const UInputAction> Action;

	/** Trigger event that raises the delegate */
	ETriggerEvent TriggerEvent = ETriggerEvent::None;

protected:
	// Clone constructor
	FEnhancedInputActionEventBinding(const FEnhancedInputActionEventBinding& CloneFrom, EInputBindingClone Clone) : FInputBindingHandle(CloneFrom, Clone), Action(CloneFrom.Action), TriggerEvent(CloneFrom.TriggerEvent) {}

public:
	FEnhancedInputActionEventBinding() = default;
	FEnhancedInputActionEventBinding(const UInputAction* InAction, ETriggerEvent InTriggerEvent) : Action(InAction), TriggerEvent(InTriggerEvent) {}

	const UInputAction* GetAction() const { return Action.Get(); }
	ETriggerEvent GetTriggerEvent() const { return TriggerEvent; }

	virtual void Execute(const FInputActionInstance& ActionData) const = 0;
	virtual TUniquePtr<FEnhancedInputActionEventBinding> Clone() const = 0;
};

/** Binds an action value for later reference. CurrentValue will be kept up to date with the value of the bound action */
struct FEnhancedInputActionValueBinding : public FInputBindingHandle
{
private:
	friend class UEnhancedPlayerInput;

	/** Action against which we are bound */
	TWeakObjectPtr<const UInputAction> Action;

	/** Copy of the current value of the action */
	mutable FInputActionValue CurrentValue;

public:
	FEnhancedInputActionValueBinding() = default;
	FEnhancedInputActionValueBinding(const UInputAction* InAction) : Action(InAction) {}

	const UInputAction* GetAction() const { return Action.Get(); }
	FInputActionValue GetValue() const { return CurrentValue; }
};

/** Binds a delegate to an event on a key chord. */
struct FInputDebugKeyBinding : public FInputBindingHandle
{
protected:
	// Clone constructor
	FInputDebugKeyBinding(const FInputDebugKeyBinding& CloneFrom, EInputBindingClone Clone)
		: FInputBindingHandle(CloneFrom, Clone)
		, KeyEvent(CloneFrom.KeyEvent)
		, bExecuteWhenPaused(CloneFrom.bExecuteWhenPaused)
		, Chord(CloneFrom.Chord)
	{ }

public:
	/** Key event to bind it to (e.g. pressed, released, double click) */
	TEnumAsByte<EInputEvent> KeyEvent = EInputEvent::IE_Pressed;

	bool bExecuteWhenPaused = false;

	/** Input Chord to bind to */
	FInputChord Chord;

	//ControllerId ControllerId;	// TODO: Controller id/player id (hybrid?) allowing binding e.g. multiple pads to a series of debug actions. Make this part of FInputChord?

	FInputDebugKeyBinding() = default;
	FInputDebugKeyBinding(const FInputChord InChord, const EInputEvent InKeyEvent, bool bInExecuteWhenPaused)
		: KeyEvent(InKeyEvent)
		, bExecuteWhenPaused(bInExecuteWhenPaused)
		, Chord(InChord)
	{ }

	virtual void Execute() const = 0;
	virtual TUniquePtr<FInputDebugKeyBinding> Clone() const = 0;
};
// TODO:: Add FInputDebugKeyValueBinding?



/**
 * Binding wrapper structs.
 * You will need to create these to do manual binding.
 * They permit storage of delegates with differing signatures within a single array of bindings.
 */

template<typename TSignature>
struct FEnhancedInputActionEventDelegateBinding : FEnhancedInputActionEventBinding
{
private:
	FEnhancedInputActionEventDelegateBinding(const FEnhancedInputActionEventDelegateBinding<TSignature>& CloneFrom, EInputBindingClone Clone) : FEnhancedInputActionEventBinding(CloneFrom, Clone), Delegate(CloneFrom.Delegate) {}
public:
	FEnhancedInputActionEventDelegateBinding(const UInputAction* Action, ETriggerEvent InTriggerEvent) : FEnhancedInputActionEventBinding(Action, InTriggerEvent) {}

	// Implemented below.
	virtual void Execute(const FInputActionInstance& ActionData) const override;
	virtual TUniquePtr<FEnhancedInputActionEventBinding> Clone() const override
	{
		return TUniquePtr<FEnhancedInputActionEventBinding>(new FEnhancedInputActionEventDelegateBinding<TSignature>(*this, EInputBindingClone::ForceClone));
	}

	TEnhancedInputUnifiedDelegate<TSignature> Delegate;
};

template<typename TSignature>
struct FInputDebugKeyDelegateBinding : FInputDebugKeyBinding
{
private:
	FInputDebugKeyDelegateBinding(const FInputDebugKeyDelegateBinding<TSignature>& CloneFrom, EInputBindingClone Clone) : FInputDebugKeyBinding(CloneFrom, Clone), Delegate(CloneFrom.Delegate) {}
public:
	FInputDebugKeyDelegateBinding(const FInputChord Chord, const EInputEvent KeyEvent, bool bExecuteWhenPaused) : FInputDebugKeyBinding(Chord, KeyEvent, bExecuteWhenPaused) {}

	virtual void Execute() const override
	{
		Delegate.Execute(Chord.Key);	// TODO: Remove FKey param? We don't support AnyKey, so it isn't terribly useful. TODO: Provide key raw value param to support axis keys.
	}
	virtual TUniquePtr<FInputDebugKeyBinding> Clone() const override 
	{
		return TUniquePtr<FInputDebugKeyBinding>(new FInputDebugKeyDelegateBinding<TSignature>(*this, EInputBindingClone::ForceClone));
	}

	TEnhancedInputUnifiedDelegate<TSignature> Delegate;
};


// Action event delegate execution by signature.

template<>
inline void FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerSignature>::Execute(const FInputActionInstance& ActionData) const
{
	Delegate.Execute();
}

template<>
inline void FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerValueSignature>::Execute(const FInputActionInstance& ActionData) const
{
	Delegate.Execute(ActionData.GetValue());
}

template<>
inline void FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerInstanceSignature>::Execute(const FInputActionInstance& ActionData) const
{
	Delegate.Execute(ActionData);
}

template<>
inline void FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerDynamicSignature>::Execute(const FInputActionInstance& ActionData) const
{
	Delegate.Execute(ActionData.GetValue(), ActionData.GetElapsedTime(), ActionData.GetTriggeredTime());
}



/**
 * Implement an Actor component for input bindings.
 *
 * An Enhanced Input Component is a transient component that enables an Actor to bind enhanced actions to delegate functions, or monitor those actions.
 * Input components are processed from a stack managed by the PlayerController and processed by the PlayerInput.
 * These bindings will not consume input events, but this behaviour can be replicated using UInputMappingContext::Priority.
 *
 * @see https://docs.unrealengine.com/latest/INT/Gameplay/Input/index.html
 */
UCLASS(transient, config=Input, hidecategories=(Activation, "Components|Activation"))
class ENHANCEDINPUT_API UEnhancedInputComponent
	: public UInputComponent
{
	GENERATED_UCLASS_BODY()

private:

	/** The collection of action bindings. */
	TArray<TUniquePtr<FEnhancedInputActionEventBinding>> EnhancedActionEventBindings;

	/** The collection of action value bindings. These do not have delegates and are used to store a copy of the current action value only. */
	TArray<FEnhancedInputActionValueBinding> EnhancedActionValueBindings;	// TODO: TSortedMap?

	/** Debug key bindings, available in non-shipping builds only. */
	TArray<TUniquePtr<FInputDebugKeyBinding>> DebugKeyBindings;

public:


	/**
	 * Checks whether this component has any input bindings.
	 *
	 * @return true if any bindings are set, false otherwise.
	 */
	bool HasBindings() const;

	/**
	 * Removes all action bindings.
	 *
	 */
	void ClearActionEventBindings() { EnhancedActionEventBindings.Reset(); }
	void ClearActionValueBindings() { EnhancedActionValueBindings.Reset(); }
	void ClearDebugKeyBindings() { DebugKeyBindings.Reset(); }

	/**
	 * Gets all action bindings of type
	 *
	 * @see BindAction, ClearActionEventBindings, GetNumActionEventBindings, RemoveActionEventBinding
	*/
	const TArray<TUniquePtr<FEnhancedInputActionEventBinding>>& GetActionEventBindings() const { return EnhancedActionEventBindings; }
	const TArray<FEnhancedInputActionValueBinding>& GetActionValueBindings() const { return EnhancedActionValueBindings; }
	const TArray<TUniquePtr<FInputDebugKeyBinding>>& GetDebugKeyBindings() const { return DebugKeyBindings; }

	/**
	 * Removes the action binding at the specified index.
	 *
	 * @param BindingIndex The index of the binding to remove.
	 * @return Whether the binding was successfully removed.
	 * @see BindAction, ClearActionEventBindings, GetActionEventBinding, GetNumActionEventBindings
	 */
	bool RemoveActionEventBinding(const int32 BindingIndex);
	bool RemoveDebugKeyBinding(const int32 BindingIndex);
	bool RemoveActionValueBinding(const int32 BindingIndex);

	/**
	 * Removes the binding with the specified handle. Binding handles are unique across all bindings.
	 *
	 * @param Handle The handle of the binding to remove.
	 * @return Whether the binding was successfully removed.
	 * @see BindAction, ClearActionEventBindings, GetActionEventBinding, GetNumActionEventBindings
	 */
	bool RemoveBindingByHandle(const uint32 Handle);

	/**
	 * Removes an arbitrary binding
	 *
	 * @param BindingToRemove The binding to remove, as returned by BindAction/BindActionValue/BindDebugKey
	 * @return Whether the binding was successfully removed.
	 * @see BindAction, BindDebugKey, ClearActionEventBindings, ClearDebugKeyBindings
	 */
	bool RemoveBinding(const FInputBindingHandle& BindingToRemove);

	/**
	 * Binds a delegate function matching any of the handler signatures to a UInputAction assigned via UInputMappingContext to the owner of this component.
	 */
#define DEFINE_BIND_ACTION(HANDLER_SIG)																																									\
	template<class UserClass>																																											\
	FEnhancedInputActionEventBinding& BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, UserClass* Object, typename HANDLER_SIG::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) \
	{																																																	\
		TUniquePtr<FEnhancedInputActionEventDelegateBinding<HANDLER_SIG>> AB = MakeUnique<FEnhancedInputActionEventDelegateBinding<HANDLER_SIG>>(Action, TriggerEvent);									\
		AB->Delegate.BindDelegate<UserClass>(Object, Func);																																				\
		return *EnhancedActionEventBindings.Add_GetRef(MoveTemp(AB));																																	\
	}

	DEFINE_BIND_ACTION(FEnhancedInputActionHandlerSignature);
	DEFINE_BIND_ACTION(FEnhancedInputActionHandlerValueSignature);
	DEFINE_BIND_ACTION(FEnhancedInputActionHandlerInstanceSignature);

	template< class FuncType, class UserClass, typename... VarTypes >
	FEnhancedInputActionEventBinding& BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, VarTypes... Vars)
	{
		TUniquePtr<FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerSignature>> AB = MakeUnique<FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerSignature>>(Action, TriggerEvent);
		AB->Delegate.MakeDelegate().BindUObject(Object, Func, Vars...);
		return *EnhancedActionEventBindings.Add_GetRef(MoveTemp(AB));
	}

	/**
	 * Binds to an object UFUNCTION
	 */
	FEnhancedInputActionEventBinding& BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, UObject* Object, FName FunctionName)
	{
		TUniquePtr<FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerDynamicSignature>> AB = MakeUnique<FEnhancedInputActionEventDelegateBinding<FEnhancedInputActionHandlerDynamicSignature>>(Action, TriggerEvent);
		AB->Delegate.BindDelegate(Object, FunctionName);
		return *EnhancedActionEventBindings.Add_GetRef(MoveTemp(AB));
	}

	/**
	 * Binds a UInputAction assigned via UInputMappingContext to this component.
	 * No delegate will be called when this action triggers. The binding simply reflects the current value of the action.
	 */
	FEnhancedInputActionValueBinding& BindActionValue(const UInputAction* Action)
	{
		// Only one action value binding is required per action
		if (FEnhancedInputActionValueBinding* Existing = EnhancedActionValueBindings.FindByPredicate([Action](const FEnhancedInputActionValueBinding& TestBinding) { return TestBinding.GetAction() == Action; }))
		{
			return *Existing;
		}

		return EnhancedActionValueBindings.Add_GetRef(FEnhancedInputActionValueBinding(Action));
	}

	/**
	 * Binds a chord event to a delegate function in development builds only.
	 */
	template<class UserClass>
	FInputDebugKeyBinding& BindDebugKey(const FInputChord Chord, const EInputEvent KeyEvent, UserClass* Object, typename FInputDebugKeyHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func, bool bExecuteWhenPaused = true)
	{
#ifdef DEV_ONLY_KEY_BINDINGS_AVAILABLE
		TUniquePtr<FInputDebugKeyDelegateBinding<FInputDebugKeyHandlerSignature>> KB = MakeUnique<FInputDebugKeyDelegateBinding<FInputDebugKeyHandlerSignature>>(Chord, KeyEvent, bExecuteWhenPaused);
		KB->Delegate.BindDelegate(Object, Func);
		return *DebugKeyBindings.Emplace_GetRef(MoveTemp(KB));
#endif
	}

	/**
	 * Binds to an object UFUNCTION
	 */
	FInputDebugKeyBinding& BindDebugKey(const FInputChord Chord, const EInputEvent KeyEvent, UObject* Object, FName FunctionName, bool bExecuteWhenPaused = true)
	{
#ifdef DEV_ONLY_KEY_BINDINGS_AVAILABLE
		TUniquePtr<FInputDebugKeyDelegateBinding<FInputDebugKeyHandlerDynamicSignature>> KB = MakeUnique<FInputDebugKeyDelegateBinding<FInputDebugKeyHandlerDynamicSignature>>(Chord, KeyEvent, bExecuteWhenPaused);
		KB->Delegate.BindDelegate(Object, FunctionName);
		return *DebugKeyBindings.Emplace_GetRef(MoveTemp(KB));
#endif
	}


	/**
	 * Helper function to pull the action value for a bound action value.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HideSelfPin = "true", HidePin = "Action"))
	FInputActionValue GetBoundActionValue(const UInputAction* Action) const;

	// Delete all InputComponent binding helpers. Indicates intentions going forward and improves intellisense/VAX when working with EnhancedInputCompoennts.
	template<class UserClass>
	FInputActionBinding& BindAction(const FName ActionName, const EInputEvent KeyEvent, UserClass* Object, typename FInputActionHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	template<class UserClass>
	FInputActionBinding& BindAction(const FName ActionName, const EInputEvent KeyEvent, UserClass* Object, typename FInputActionHandlerWithKeySignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	template< class DelegateType, class UserClass, typename... VarTypes >
	FInputActionBinding& BindAction(const FName ActionName, const EInputEvent KeyEvent, UserClass* Object, typename DelegateType::template TUObjectMethodDelegate< UserClass >::FMethodPtr Func, VarTypes... Vars) = delete;
	template<class UserClass>
	FInputAxisBinding& BindAxis(const FName AxisName, UserClass* Object, typename FInputAxisHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	FInputAxisBinding& BindAxis(const FName AxisName) = delete;
	template<class UserClass>
	FInputAxisKeyBinding& BindAxisKey(const FKey AxisKey, UserClass* Object, typename FInputAxisHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	FInputAxisKeyBinding& BindAxisKey(const FKey AxisKey) = delete;
	template<class UserClass>
	FInputVectorAxisBinding& BindVectorAxis(const FKey AxisKey, UserClass* Object, typename FInputVectorAxisHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	FInputVectorAxisBinding& BindVectorAxis(const FKey AxisKey) = delete;
	template<class UserClass>
	FInputKeyBinding& BindKey(const FInputChord Chord, const EInputEvent KeyEvent, UserClass* Object, typename FInputActionHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	template<class UserClass>
	FInputKeyBinding& BindKey(const FKey Key, const EInputEvent KeyEvent, UserClass* Object, typename FInputActionHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	template<class UserClass>
	FInputTouchBinding& BindTouch(const EInputEvent KeyEvent, UserClass* Object, typename FInputTouchHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
	template<class UserClass>
	FInputGestureBinding& BindGesture(const FKey GestureKey, UserClass* Object, typename FInputGestureHandlerSignature::TUObjectMethodDelegate< UserClass >::FMethodPtr Func) = delete;
};
