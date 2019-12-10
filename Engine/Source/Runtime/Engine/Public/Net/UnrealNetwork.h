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

typedef TMap<FString, TArray<uint8>> FDemoFrameDataMap;
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnWriteGameSpecificFrameData, UWorld* /*World*/, float /*FrameTime*/, FDemoFrameDataMap& /*Data*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnProcessGameSpecificFrameData, UWorld* /*World*/, float /*FrameTime*/, const FDemoFrameDataMap& /*Data*/);

struct ENGINE_API FNetworkReplayDelegates
{
	/** global delegate called one time prior to scrubbing */
	static FPreReplayScrub OnPreScrub;

	/** Game specific demo headers */
	static FOnWriteGameSpecificDemoHeader OnWriteGameSpecificDemoHeader;
	static FOnProcessGameSpecificDemoHeader OnProcessGameSpecificDemoHeader;

	/** Game specific per frame data */
	static FOnWriteGameSpecificFrameData OnWriteGameSpecificFrameData;
	static FOnProcessGameSpecificFrameData OnProcessGameSpecificFrameData;
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

namespace NetworkingPrivate
{
	struct ENGINE_API FRepPropertyDescriptor
	{
		FRepPropertyDescriptor(const FProperty* Property)
			: PropertyName(VerifyPropertyAndGetName(Property))
			, RepIndex(Property->RepIndex)
			, ArrayDim(Property->ArrayDim)
		{
		}

		FRepPropertyDescriptor(const TCHAR* InPropertyName, const int32 InRepIndex, const int32 InArrayDim)
			: PropertyName(InPropertyName)
			, RepIndex(InRepIndex)
			, ArrayDim(InArrayDim)
		{
		}

		const TCHAR* PropertyName;
		const int32 RepIndex;
		const int32 ArrayDim;

	private:

		static const TCHAR* VerifyPropertyAndGetName(const FProperty* Property)
		{
			check(Property);
			return *(Property->GetName());
		}

		UE_NONCOPYABLE(FRepPropertyDescriptor);

		void* operator new(size_t) = delete;
		void* operator new[](size_t) = delete;
		void operator delete(void*) = delete;
		void operator delete[](void*) = delete;
	};

	struct ENGINE_API FRepClassDescriptor
	{
		FRepClassDescriptor(const TCHAR* InClassName, const int32 InStartRepIndex, const int32 InEndRepIndex)
			: ClassName(InClassName)
			, StartRepIndex(InStartRepIndex)
			, EndRepIndex(InEndRepIndex)
		{
		}

		const TCHAR* ClassName;
		const int32 StartRepIndex;
		const int32 EndRepIndex;

	private:

		UE_NONCOPYABLE(FRepClassDescriptor);

		void* operator new(size_t) = delete;
		void* operator new[](size_t) = delete;
		void operator delete(void*) = delete;
		void operator delete[](void*) = delete;
	};
}
/*-----------------------------------------------------------------------------
	Replication.
-----------------------------------------------------------------------------*/

static bool ValidateReplicatedClassInheritance(const UClass* CallingClass, const UClass* PropClass, const TCHAR* PropertyName)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!CallingClass->IsChildOf(PropClass))
	{
		UE_LOG(LogNet, Fatal, TEXT("Attempt to replicate property '%s.%s' in C++ but class '%s' is not a child of '%s'"), *PropClass->GetName(), PropertyName, *CallingClass->GetName(), *PropClass->GetName());
	}
#endif

	return true;
}

/** wrapper to find replicated properties that also makes sure they're valid */
static FProperty* GetReplicatedProperty(const UClass* CallingClass, const UClass* PropClass, const FName& PropName)
{
	ValidateReplicatedClassInheritance(CallingClass, PropClass, *PropName.ToString());

	FProperty* TheProperty = FindFieldChecked<FProperty>(PropClass, PropName);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!(TheProperty->PropertyFlags & CPF_Net))
	{
		UE_LOG(LogNet, Fatal,TEXT("Attempt to replicate property '%s' that was not tagged to replicate! Please use 'Replicated' or 'ReplicatedUsing' keyword in the UPROPERTY() declaration."), *TheProperty->GetFullName());
	}
#endif
	return TheProperty;
}

#define DOREPLIFETIME_WITH_PARAMS_FAST(c,v,params) \
{ \
	static const bool bIsValid_##c_##v = ValidateReplicatedClassInheritance(StaticClass(), c::StaticClass(), TEXT(#v)); \
	const TCHAR* DoRepPropertyName_##c_##v(TEXT(#v)); \
	const NetworkingPrivate::FRepPropertyDescriptor PropertyDescriptor_##c_##v(DoRepPropertyName_##c_##v, NetworkingPrivate::Net_##c::NETFIELD_##v, NetworkingPrivate::Net_##c::ARRAYDIM_##v); \
	RegisterReplicatedLifetimeProperty(PropertyDescriptor_##c_##v, OutLifetimeProps, params); \
}

#define DOREPLIFETIME_WITH_PARAMS(c,v,params) \
{ \
	FProperty* ReplicatedProperty = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v)); \
	RegisterReplicatedLifetimeProperty(ReplicatedProperty, OutLifetimeProps, params); \
}

#define DOREPLIFETIME(c,v) DOREPLIFETIME_WITH_PARAMS(c,v,FDoRepLifetimeParams())

/** This macro is used by nativized code (DynamicClasses), so the Property may be recreated. */
#define DOREPLIFETIME_DIFFNAMES(c,v, n) \
{ \
	static TWeakFieldPtr<FProperty> __swp##v{};							\
	const FProperty* sp##v = __swp##v.Get();								\
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


#define DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(c,v,active) \
{ \
	static const bool bIsValid_##c_##v = ValidateReplicatedClassInheritance(StaticClass(), c::StaticClass(), TEXT(#v)); \
	for (int32 i = 0; i < NetworkingPrivate::Net_##c::ARRAYDIM_##v; i++) \
	{ \
		ChangedPropertyTracker.SetCustomIsActiveOverride(NetworkingPrivate::Net_##c::NETFIELD_##v + i, active); \
	} \
}

#define DOREPLIFETIME_ACTIVE_OVERRIDE(c,v,active)	\
{													\
	static FProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v)); \
	for ( int32 i = 0; i < sp##v->ArrayDim; i++ )											\
	{																						\
		ChangedPropertyTracker.SetCustomIsActiveOverride( sp##v->RepIndex + i, active );	\
	}																						\
}

UE_DEPRECATED(4.24, "Please use the RESET_REPLIFETIME_CONDITION macro")
ENGINE_API void DeprecatedChangeCondition(FProperty* ReplicatedProperty, TArray< FLifetimeProperty >& OutLifetimeProps, ELifetimeCondition InCondition);


//~ This is already using a deprecated method, don't bother updating it.
#define DOREPLIFETIME_CHANGE_CONDITION(c,v,cond) \
{ \
	FProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v));			\
	DeprecatedChangeCondition(sp##v, OutLifetimeProps, cond);														\
}

UE_DEPRECATED(4.24, "Use RegisterReplicatedLifetimeProperty that takes FDoRepLifetimeParams.")
ENGINE_API void RegisterReplicatedLifetimeProperty(
	const FProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	ELifetimeCondition InCondition,
	ELifetimeRepNotifyCondition InRepNotifyCondition = REPNOTIFY_OnChanged);

ENGINE_API void RegisterReplicatedLifetimeProperty(
	const FProperty* ReplicatedProperty,
	TArray<FLifetimeProperty>& OutLifetimeProps,
	const FDoRepLifetimeParams& Params);

ENGINE_API void RegisterReplicatedLifetimeProperty(
	const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor,
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

#define DISABLE_REPLICATED_PROPERTY_FAST(c,v) \
{ \
	static const bool bIsValid_##c_##v = ValidateReplicatedClassInheritance(StaticClass(), c::StaticClass(), TEXT(#v)); \
	const TCHAR* DoRepPropertyName_##c_##v(TEXT(#v)); \
	const NetworkingPrivate::FRepPropertyDescriptor PropertyDescriptor_##c_##v(DoRepPropertyName_##c_##v, NetworkingPrivate::Net_##c::NETFIELD_##v, NetworkingPrivate::Net_##c::ARRAYDIM_##v); \
	DisableReplicatedLifetimeProperty(PropertyDescriptor_##c_##v, OutLifetimeProps); \
}

/** Use this macro in GetLifetimeReplicatedProps to flag all replicated properties of a class as not-replicated.
    Use the EFieldIteratorFlags enum to disable all inherited properties or only those of the class specified
*/
#define DISABLE_ALL_CLASS_REPLICATED_PROPERTIES(c, SuperClassBehavior) \
DisableAllReplicatedPropertiesOfClass(StaticClass(), c::StaticClass(), SuperClassBehavior, OutLifetimeProps);

#define DISABLE_ALL_CLASS_REPLICATED_PROPERTIES_FAST(c, SuperClassBehavior) \
{ \
	static const bool bIsValid_##c_##v = ValidateReplicatedClassInheritance(StaticClass(), c::StaticClass(), TEXT("DISABLE_ALL_CLASS_REPLICATED_PROPERTIES")); \
	const TCHAR* DoRepPropertyName_##c(TEXT(#c)); \
	const NetworkingPrivate::FRepClassDescriptor ClassDescriptor_##c(DoRepPropertyName_##c, NetworkingPrivate::Net_##c::NETFIELD_REP_START, NetworkingPrivate::Net_##c::NETFIELD_REP_END); \
	DisableAllReplicatedPropertiesOfClass(StaticClass(), c::StaticClass(), SuperClassBehavior, OutLifetimeProps); \
}	

ENGINE_API void DisableReplicatedLifetimeProperty(const UClass* ThisClass, const UClass* PropertyClass, FName PropertyName, TArray< FLifetimeProperty >& OutLifetimeProps);
ENGINE_API void DisableAllReplicatedPropertiesOfClass(const UClass* ThisClass, const UClass* ClassToDisable, EFieldIteratorFlags::SuperClassFlags SuperClassBehavior, TArray< FLifetimeProperty >& OutLifetimeProps);

ENGINE_API void DisableReplicatedLifetimeProperty(const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor, TArray<FLifetimeProperty>& OutLifetimeProps);
ENGINE_API void DisableAllReplicatedPropertiesOfClass(const NetworkingPrivate::FRepClassDescriptor& ClassDescriptor, EFieldIteratorFlags::SuperClassFlags SuperClassBehavior, TArray<FLifetimeProperty>& OutLifetimeProps);

/*-----------------------------------------------------------------------------
	Reset macros.
	Use these to change the replication settings of an inherited property
-----------------------------------------------------------------------------*/

#define RESET_REPLIFETIME_CONDITION(c,v,cond)  ResetReplicatedLifetimeProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v), cond, OutLifetimeProps);

#define RESET_REPLIFETIME(c,v) RESET_REPLIFETIME_CONDITION(c, v, COND_None)

#define RESET_REPLIFETIME_CONDITION_FAST(c,v,cond) \
{ \
	static const bool bIsValid_##c_##v = ValidateReplicatedClassInheritance(StaticClass(), c::StaticClass(), TEXT(#v)); \
	const TCHAR* DoRepPropertyName_##c_##v(TEXT(#v)); \
	const NetworkingPrivate::FRepPropertyDescriptor PropertyDescriptor_##c_##v(DoRepPropertyName_##c_##v, NetworkingPrivate::Net_##c::NETFIELD_##v, NetworkingPrivate::Net_##c::ARRAYDIM_##v); \
	ResetReplicatedLifetimeProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v), cond, OutLifetimeProps); \
}

#define RESET_REPLIFETIME_FAST(c,v) RESET_REPLIFETIME_CONDITION_FAST(c, v, COND_None)

ENGINE_API void ResetReplicatedLifetimeProperty(
	const UClass* ThisClass,
	const UClass* PropertyClass,
	FName PropertyName,
	ELifetimeCondition LifetimeCondition,
	TArray< FLifetimeProperty >& OutLifetimeProps);

ENGINE_API void ResetReplicatedLifetimeProperty(
	const NetworkingPrivate::FRepPropertyDescriptor& PropertyDescriptor,
	ELifetimeCondition LifetimeCondition,
	TArray<FLifetimeProperty>& OutLifetimeProps);

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
