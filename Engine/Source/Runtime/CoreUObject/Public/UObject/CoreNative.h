// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CoreNative.h: Native function lookup table.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/Script.h"
#include "UObject/Object.h"

struct FFrame;

/** The type of a native function callable by script */
typedef void (*FNativeFuncPtr)(UObject* Context, FFrame& TheStack, RESULT_DECL);

// This class is deliberately simple (i.e. POD) to keep generated code size down.
struct FNameNativePtrPair
{
	const char* NameUTF8;
	FNativeFuncPtr Pointer;
};

extern COREUOBJECT_API FNativeFuncPtr GCasts[];
uint8 COREUOBJECT_API GRegisterCast( int32 CastCode, const FNativeFuncPtr& Func );


/** A struct that maps a string name to a native function */
struct FNativeFunctionRegistrar
{
	FNativeFunctionRegistrar(class UClass* Class, const ANSICHAR* InName, FNativeFuncPtr InPointer)
	{
		RegisterFunction(Class, InName, InPointer);
	}
	static COREUOBJECT_API void RegisterFunction(class UClass* Class, const ANSICHAR* InName, FNativeFuncPtr InPointer);
	// overload for types generated from blueprints, which can have unicode names:
	static COREUOBJECT_API void RegisterFunction(class UClass* Class, const WIDECHAR* InName, FNativeFuncPtr InPointer);

	static COREUOBJECT_API void RegisterFunctions(class UClass* Class, const FNameNativePtrPair* InArray, int32 NumFunctions);
};

// CoreUObject module. Handles UObject system pre-init (registers init function with Core callbacks).
class FCoreUObjectModule : public FDefaultModuleImpl
{
public:
	static void RouteRuntimeMessageToBP(ELogVerbosity::Type Verbosity, const ANSICHAR* FileName, int32 LineNumber, const FText& Message);

	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	static struct FPrecacheCallbackHandler* GetGlobalPrecacheHandler() { return GlobalPrecacheHandler; }

private:

	static struct FPrecacheCallbackHandler* GlobalPrecacheHandler;
};
