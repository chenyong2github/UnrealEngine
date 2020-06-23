// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "USDMemory.h"
#include "UnrealUSDWrapper.h"

#include "UsdWrappers/UsdTyped.h"

#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/Optional.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"

class FRegisteredSchemaTranslator;
struct FUsdSchemaTranslationContext;
class FUsdSchemaTranslator;
class FUsdSchemaTranslatorTaskChain;
class ULevel;
class USceneComponent;
class UStaticMesh;

class USDSCHEMAS_API FRegisteredSchemaTranslatorHandle
{
public:
	FRegisteredSchemaTranslatorHandle()
		: Id( CurrentSchemaTranslatorId++ )
	{
	}

	explicit FRegisteredSchemaTranslatorHandle( const FString& InSchemaName )
		: FRegisteredSchemaTranslatorHandle()
	{
		SchemaName = InSchemaName;
	}

	int32 GetId() const { return Id; }
	void SetId( int32 InId ) { Id = InId; }

	const FString& GetSchemaName() const { return SchemaName; }
	void SetSchemaName( const FString& InSchemaName ) { SchemaName = InSchemaName; }

private:
	static int32 CurrentSchemaTranslatorId;

	FString SchemaName;
	int32 Id;
};

class USDSCHEMAS_API FUsdSchemaTranslatorRegistry
{
	using FCreateTranslator = TFunction< TSharedRef< FUsdSchemaTranslator >( TSharedRef< FUsdSchemaTranslationContext >, const UE::FUsdTyped& ) >;
	using FSchemaTranslatorsStack = TArray< FRegisteredSchemaTranslator, TInlineAllocator< 1 > >;

public:
	TSharedPtr< FUsdSchemaTranslator > CreateTranslatorForSchema( TSharedRef< FUsdSchemaTranslationContext > InTranslationContext, const UE::FUsdTyped& InSchema );

	template< typename TSchemaTranslator >
	FRegisteredSchemaTranslatorHandle Register( const FString& SchemaName )
	{
		auto CreateSchemaTranslator =
		[]( TSharedRef< FUsdSchemaTranslationContext > InContext, const UE::FUsdTyped& InSchema ) -> TSharedRef< FUsdSchemaTranslator >
		{
			return MakeShared< TSchemaTranslator >( InContext, InSchema );
		};

		return Register( SchemaName, CreateSchemaTranslator );
	}

	void Unregister( const FRegisteredSchemaTranslatorHandle& TranslatorHandle );

protected:
	FRegisteredSchemaTranslatorHandle Register( const FString& SchemaName, FCreateTranslator CreateFunction );

	FSchemaTranslatorsStack* FindSchemaTranslatorStack( const FString& SchemaName );

	TArray< TPair< FString, FSchemaTranslatorsStack > > RegisteredSchemaTranslators;
};

class FRegisteredSchemaTranslator
{
	using FCreateTranslator = TFunction< TSharedRef< FUsdSchemaTranslator >( TSharedRef< FUsdSchemaTranslationContext >, const UE::FUsdTyped& ) >;

public:
	FRegisteredSchemaTranslatorHandle Handle;
	FCreateTranslator CreateFunction;
};

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

	/** We're only allowed to load prims with purposes that match these flags */
	EUsdPurpose PurposesToLoad;

	/** Map of translated UsdPrims to UAssets */
	TMap< FString, UObject* >& PrimPathsToAssets;

	TMap< FString, UObject* >& AssetsCache;

	FCriticalSection CriticalSection;

	bool bAllowCollapsing = true;

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

class USDSCHEMAS_API FUsdSchemaTranslator
{
public:
	explicit FUsdSchemaTranslator( TSharedRef< FUsdSchemaTranslationContext > InContext, const UE::FUsdTyped& InSchema )
		: Context( InContext )
		, Schema( InSchema )
	{
	}

	virtual ~FUsdSchemaTranslator() = default;

	virtual void CreateAssets() {}

	virtual USceneComponent* CreateComponents() { return nullptr; }
	virtual void UpdateComponents( USceneComponent* SceneComponent ) {}

	enum class ECollapsingType
	{
		Assets,
		Components
	};

	virtual bool CollapsesChildren( ECollapsingType CollapsingType ) const { return false; }

	bool IsCollapsed( ECollapsingType CollapsingType ) const;
	virtual bool CanBeCollapsed( ECollapsingType CollapsingType ) const { return false; }

protected:
	TSharedRef< FUsdSchemaTranslationContext > Context;
	UE::FUsdTyped Schema;
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
	virtual ~FUsdSchemaTranslatorTaskChain() = default;

	FUsdSchemaTranslatorTaskChain& Do( bool bAsync, TFunction< bool() > Callable );
	FUsdSchemaTranslatorTaskChain& Then( bool bAsync, TFunction< bool() > Callable );

	ESchemaTranslationStatus Execute();

private:
	TSharedPtr< FSchemaTranslatorTask > CurrentTask;
};
