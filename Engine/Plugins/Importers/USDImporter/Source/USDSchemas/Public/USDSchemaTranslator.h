// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "USDMemory.h"

#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/Optional.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
	#include "pxr/usd/usd/typed.h"
#include "USDIncludesEnd.h"

struct FUsdSchemaTranslationContext;
class FUsdSchemaTranslator;
class FUsdSchemaTranslatorTaskChain;
class ULevel;
class USceneComponent;
class UStaticMesh;

PXR_NAMESPACE_OPEN_SCOPE
	class TfToken;
	class UsdGeomPointInstancer;
	class UsdGeomXformable;
PXR_NAMESPACE_CLOSE_SCOPE

#endif //#if USE_USD_SDK

class USDSCHEMAS_API FUsdSchemaTranslatorRegistry
{
#if USE_USD_SDK
	using TCreateTranslator = TFunction< TSharedRef< FUsdSchemaTranslator >( TSharedRef< FUsdSchemaTranslationContext >, const pxr::UsdTyped& ) >;

public:
	TSharedPtr< FUsdSchemaTranslator > CreateTranslatorForSchema( TSharedRef< FUsdSchemaTranslationContext > InTranslationContext, const pxr::UsdTyped& InSchema );

	template< typename TSchemaTranslator >
	void Register( const FString& SchemaName )
	{
		auto CreateSchemaTranslator =
		[]( TSharedRef< FUsdSchemaTranslationContext > InContext, const pxr::UsdTyped& InSchema ) -> TSharedRef< FUsdSchemaTranslator >
		{
			return MakeShared< TSchemaTranslator >( InContext, InSchema );
		};

		Register( SchemaName, CreateSchemaTranslator );
	}

protected:
	void Register( const FString& SchemaName, TCreateTranslator CreateFunction );

	TArray< TPair< FString, TCreateTranslator > > CreationMethods;
#endif // #if USE_USD_SDK
};

#if USE_USD_SDK

struct USDSCHEMAS_API FUsdSchemaTranslationContext : public TSharedFromThis< FUsdSchemaTranslationContext >
{
	explicit FUsdSchemaTranslationContext( TMap< FString, UObject* >& InPrimPathsToAssets, TMap< FString, UObject* >& InAssetsCache )
		: PrimPathsToAssets( InPrimPathsToAssets )
		, AssetsCache( InAssetsCache )
	{
	}

	/** Level to spawn actors in */
	ULevel* Level = nullptr;

	/** Flags used when creating UObjects */
	EObjectFlags ObjectFlags;

	/** The parent component when translating children */
	USceneComponent* ParentComponent = nullptr;

	/** The time at which we are translating */
	float Time = 0.f;

	/** Map of translated UsdPrims to UAssets */
	TMap< FString, UObject* >& PrimPathsToAssets;

	TMap< FString, UObject* >& AssetsCache;

	FCriticalSection CriticalSection;

	bool IsValid() const
	{
		return Level != nullptr;
	}

	void CompleteTasks();

	TArray< TSharedPtr< FUsdSchemaTranslatorTaskChain > > TranslatorTasks;
};

enum class ESchemaTranslationStatus
{
	InProgress,
	Done
};

class FUsdSchemaTranslator
{
public:
	explicit FUsdSchemaTranslator( TSharedRef< FUsdSchemaTranslationContext > InContext, const pxr::UsdTyped& InSchema )
		: Context( InContext )
		, Schema( InSchema )
	{
	}

	virtual ~FUsdSchemaTranslator() = default;

	virtual void CreateAssets() {}

	virtual USceneComponent* CreateComponents() { return nullptr; }
	virtual void UpdateComponents( USceneComponent* SceneComponent ) {}

	virtual bool CollapsedHierarchy() const { return false; }

protected:
	TSharedRef< FUsdSchemaTranslationContext > Context;
	TUsdStore< pxr::UsdTyped > Schema;
};

struct FSchemaTranslatorTask
{
	explicit FSchemaTranslatorTask( bool bInAsync, TFunction< bool() > InCallable )
		: Callable( InCallable )
		, bAsync( bInAsync )
		, bIsDone( false )
	{
	}

	TFunction< bool() > Callable;
	TOptional< TFuture< bool > > Result;
	TSharedPtr< FSchemaTranslatorTask > Continuation;

	bool bAsync = false;
	FThreadSafeBool bIsDone;

	void Start();
	void StartIfAsync();

	bool IsStarted() const
	{
		return Result.IsSet() && Result->IsValid();
	}

	bool DoWork();

	bool IsDone() const
	{
		return bIsDone;
	}
};

class FUsdSchemaTranslatorTaskChain
{
public:
	FUsdSchemaTranslatorTaskChain& Do( bool bAsync, TFunction< bool() > Callable );
	FUsdSchemaTranslatorTaskChain& Then( bool bAsync, TFunction< bool() > Callable );

	ESchemaTranslationStatus Execute();

private:
	TSharedPtr< FSchemaTranslatorTask > CurrentTask;
};

#endif //#if USE_USD_SDK
