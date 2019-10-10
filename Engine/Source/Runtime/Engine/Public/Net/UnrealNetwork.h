// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnrealNetwork.h: Unreal networking.
=============================================================================*/

/*-----------------------------------------------------------------------------
	Forward declarations.
-----------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/CoreNet.h"
#include "EngineLogs.h"
#include "UObject/UnrealType.h"

class AActor;

/*class	UNetDriver;*/
class	UNetConnection;
class	UPendingNetGame;

/*-----------------------------------------------------------------------------
	Types.
-----------------------------------------------------------------------------*/

// Return the value of Max/2 <= Value-Reference+some_integer*Max < Max/2.
inline int32 BestSignedDifference( int32 Value, int32 Reference, int32 Max )
{
	return ((Value-Reference+Max/2) & (Max-1)) - Max/2;
}
inline int32 MakeRelative( int32 Value, int32 Reference, int32 Max )
{
	return Reference + BestSignedDifference(Value,Reference,Max);
}

DECLARE_MULTICAST_DELEGATE_OneParam(FPreActorDestroyReplayScrub, AActor*);

DECLARE_MULTICAST_DELEGATE_OneParam(FPreReplayScrub, UWorld*);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnWriteGameSpecificDemoHeader, TArray<FString>& /*GameSpecificData*/);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnProcessGameSpecificDemoHeader, const TArray<FString>& /*GameSpecificData*/, FString& /*Error*/);

struct ENGINE_API FNetworkReplayDelegates
{
	/** global delegate called one time prior to scrubbing */
	static FPreReplayScrub OnPreScrub;

	/** Game specific demo headers */
	static FOnWriteGameSpecificDemoHeader OnWriteGameSpecificDemoHeader;
	static FOnProcessGameSpecificDemoHeader OnProcessGameSpecificDemoHeader;
};

/**
 * Struct containing various parameters that can be passed to DOREPLIFETIME_WITH_PARAMS to control
 * how variables are replicated.
 */
struct ENGINE_API FDoRepLifetimeParams
{
	/** Replication Condition. The property will only be replicated to connections where this condition is met. */
	ELifetimeCondition Condition = COND_None;
	
	/**
	 * RepNotify Condition. The property will only trigger a RepNotify if this condition is met, and has been
	 * properly set up to handle RepNotifies.
	 */
	ELifetimeRepNotifyCondition RepNotifyCondition = REPNOTIFY_OnChanged;
};

/*-----------------------------------------------------------------------------
	Replication.
-----------------------------------------------------------------------------*/

/** wrapper to find replicated properties that also makes sure they're valid */
static UProperty* GetReplicatedProperty(const UClass* CallingClass, const UClass* PropClass, const FName& PropName)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!CallingClass->IsChildOf(PropClass))
	{
		UE_LOG(LogNet, Fatal,TEXT("Attempt to replicate property '%s.%s' in C++ but class '%s' is not a child of '%s'"), *PropClass->GetName(), *PropName.ToString(), *CallingClass->GetName(), *PropClass->GetName());
	}
#endif
	UProperty* TheProperty = FindFieldChecked<UProperty>(PropClass, PropName);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!(TheProperty->PropertyFlags & CPF_Net))
	{
		UE_LOG(LogNet, Fatal,TEXT("Attempt to replicate property '%s' that was not tagged to replicate! Please use 'Replicated' or 'ReplicatedUsing' keyword in the UPROPERTY() declaration."), *TheProperty->GetFullName());
	}
#endif
	return TheProperty;
}

#define DOREPLIFETIME_WITH_PARAMS(c,v,params) \
{ \
	UProperty* ReplicatedProperty = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v)); \
	RegisterReplicatedLifetimeProperty(ReplicatedProperty, OutLifetimeProps, params); \
}

#define DOREPLIFETIME(c,v) DOREPLIFETIME_WITH_PARAMS(c,v,FDoRepLifetimeParams())

/** This macro is used by nativized code (DynamicClasses), so the Property may be recreated. */
#define DOREPLIFETIME_DIFFNAMES(c,v, n) \
{ \
	static TWeakObjectPtr<UProperty> __swp##v{};							\
	const UProperty* sp##v = __swp##v.Get();								\
	if (nullptr == sp##v)													\
	{																		\
		sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(), n);	\
		__swp##v = sp##v;													\
	}																		\
	for ( int32 i = 0; i < sp##v->ArrayDim; i++ )							\
	{																		\
		OutLifetimeProps.AddUnique( FLifetimeProperty( sp##v->RepIndex + i ) );	\
	}																		\
}

#define DOREPLIFETIME_CONDITION(c,v,cond) \
{ \
	FDoRepLifetimeParams LocalDoRepParams; \
	LocalDoRepParams.Condition = cond; \
	DOREPLIFETIME_WITH_PARAMS(c,v,LocalDoRepParams); \
}

/** Allows gamecode to specify RepNotify condition: REPNOTIFY_OnChanged (default) or REPNOTIFY_Always for when repnotify function is called  */
#define DOREPLIFETIME_CONDITION_NOTIFY(c,v,cond,rncond) \
{ \
	FDoRepLifetimeParams LocalDoRepParams; \
	LocalDoRepParams.Condition = cond; \
	LocalDoRepParams.RepNotifyCondition = rncond; \
	DOREPLIFETIME_WITH_PARAMS(c,v,LocalDoRepParams); \
}


#define DOREPLIFETIME_ACTIVE_OVERRIDE(c,v,active)	\
{													\
	static UProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v)); \
	for ( int32 i = 0; i < sp##v->ArrayDim; i++ )											\
	{																						\
		ChangedPropertyTracker.SetCustomIsActiveOverride( sp##v->RepIndex + i, active );	\
	}																						\
}

UE_DEPRECATED(4.24, "Please use the RESET_REPLIFETIME_CONDITION macro")
ENGINE_API void DeprecatedChangeCondition(UProperty* ReplicatedProperty, TArray< FLifetimeProperty >& OutLifetimeProps, ELifetimeCondition InCondition);

#define DOREPLIFETIME_CHANGE_CONDITION(c,v,cond) \
{ \
	UProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v));			\
	DeprecatedChangeCondition(sp##v, OutLifetimeProps, cond);														\
}

UE_DEPRECATED(4.24, "Use RegisterReplicatedLifetimeProperty that takes FDoRepLifetimeParams.")
ENGINE_API void RegisterReplicatedLifetimeProperty(
	const UProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	ELifetimeCondition InCondition,
	ELifetimeRepNotifyCondition InRepNotifyCondition = REPNOTIFY_OnChanged);

ENGINE_API void RegisterReplicatedLifetimeProperty(
	const UProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	const FDoRepLifetimeParams& Params);

/*-----------------------------------------------------------------------------
	Disable macros.
	Use these macros to state that properties tagged replicated 
	are voluntarely not replicated. This silences an error about missing
	registered properties when class replication is started
-----------------------------------------------------------------------------*/

/** Use this macro in GetLifetimeReplicatedProps to flag a replicated property as not-replicated */
#define DISABLE_REPLICATED_PROPERTY(c,v) \
DisableReplicatedLifetimeProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v), OutLifetimeProps);

/** Use this macro in GetLifetimeReplicatedProps to flag all replicated properties of a class as not-replicated.
    Use the EFieldIteratorFlags enum to disable all inherited properties or only those of the class specified
*/
#define DISABLE_ALL_CLASS_REPLICATED_PROPERTIES(c, SuperClassBehavior) \
DisableAllReplicatedPropertiesOfClass(StaticClass(), c::StaticClass(), SuperClassBehavior, OutLifetimeProps);

ENGINE_API void DisableReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, TArray< FLifetimeProperty >& OutLifetimeProps);
ENGINE_API void DisableAllReplicatedPropertiesOfClass(const UClass* ThisClass, const UClass* ClassToDisable, EFieldIteratorFlags::SuperClassFlags SuperClassBehavior, TArray< FLifetimeProperty >& OutLifetimeProps);

/*-----------------------------------------------------------------------------
	Reset macros.
	Use these to change the replication settings of an inherited property
-----------------------------------------------------------------------------*/

#define RESET_REPLIFETIME(c,v)  ResetReplicatedLifetimeProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v), COND_None, OutLifetimeProps);

#define RESET_REPLIFETIME_CONDITION(c,v,cond) ResetReplicatedLifetimeProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v), cond, OutLifetimeProps);

ENGINE_API void ResetReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, ELifetimeCondition LifetimeCondition, TArray< FLifetimeProperty >& OutLifetimeProps);

/*-----------------------------------------------------------------------------
	RPC Parameter Validation Helpers
-----------------------------------------------------------------------------*/

// This macro is for RPC parameter validation.
// It handles the details of what should happen if a validation expression fails
#define RPC_VALIDATE( expression )						\
	if ( !( expression ) )								\
	{													\
		UE_LOG( LogNet, Warning,						\
		TEXT("RPC_VALIDATE Failed: ")					\
		TEXT( PREPROCESSOR_TO_STRING( expression ) )	\
		TEXT(" File: ")									\
		TEXT( PREPROCESSOR_TO_STRING( __FILE__ ) )		\
		TEXT(" Line: ")									\
		TEXT( PREPROCESSOR_TO_STRING( __LINE__ ) ) );	\
		return false;									\
	}
