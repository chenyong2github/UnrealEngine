// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UnrealUSDWrapper.h"
#include "USDAssetCache.h"
#include "USDMemory.h"
#include "USDSkeletalDataConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdTyped.h"

#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/Optional.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtr.h"

class FRegisteredSchemaTranslator;
class FUsdSchemaTranslator;
class FUsdSchemaTranslatorTaskChain;
class ULevel;
class USceneComponent;
class UStaticMesh;
struct FUsdBlendShape;
struct FUsdSchemaTranslationContext;

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
	/**
	 * Returns the translator to use for InSchema
	 */
	TSharedPtr< FUsdSchemaTranslator > CreateTranslatorForSchema( TSharedRef< FUsdSchemaTranslationContext > InTranslationContext, const UE::FUsdTyped& InSchema );

	/**
	 * Registers SchemaTranslatorType to translate schemas of type SchemaName.
	 * Registration order is important as the last to register for a given schema will be the one handling it.
	 * Thus, you will want to register base schemas before the more specialized ones.
	 */
	template< typename SchemaTranslatorType >
	FRegisteredSchemaTranslatorHandle Register( const FString& SchemaName )
	{
		auto CreateSchemaTranslator =
		[]( TSharedRef< FUsdSchemaTranslationContext > InContext, const UE::FUsdTyped& InSchema ) -> TSharedRef< FUsdSchemaTranslator >
		{
			return MakeShared< SchemaTranslatorType >( InContext, InSchema );
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

class USDSCHEMAS_API FUsdRenderContextRegistry
{
public:
	FUsdRenderContextRegistry();

	void Register( const FName& RenderContextToken ) { RegisteredRenderContexts.Add( RenderContextToken ); }
	void Unregister( const FName& RenderContextToken ) { RegisteredRenderContexts.Remove( RenderContextToken ); }

	const TSet< FName >& GetRenderContexts() const { return RegisteredRenderContexts; }
	const FName& GetUniversalRenderContext() const { return UniversalRenderContext; }

protected:
	TSet< FName > RegisteredRenderContexts;
	FName UniversalRenderContext;
};

struct USDSCHEMAS_API FUsdSchemaTranslationContext : public TSharedFromThis< FUsdSchemaTranslationContext >
{
	explicit FUsdSchemaTranslationContext( const UE::FUsdStage& InStage, UUsdAssetCache& InAssetCache );

	/** pxr::UsdStage we're translating from */
	UE::FUsdStage Stage;

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

	/** The render context to use when translating materials */
	FName RenderContext;

	/** Where the translated assets will be stored */
	TStrongObjectPtr< UUsdAssetCache > AssetCache;


	/** Where we place imported blend shapes, if available */
	UsdUtils::FBlendShapeMap* BlendShapesByPath = nullptr;

	/**
	 * When parsing materials, we keep track of which primvar we mapped to which UV channel.
	 * When parsing meshes later, we use this data to place the correct primvar values in each UV channel.
	 */
	TMap< FString, TMap< FString, int32 > >* MaterialToPrimvarToUVIndex = nullptr;

	bool bAllowCollapsing = true;

	/**
	 * If true, prims with a "LOD" variant set, and "LOD0", "LOD1", etc. variants containing each
	 * a prim can be parsed into a single UStaticMesh asset with multiple LODs
	 */
	bool bAllowInterpretingLODs = true;

	/** If true, we will also try creating UAnimSequence skeletal animation assets when parsing SkelRoot prims */
	bool bAllowParsingSkeletalAnimations = true;

	bool IsValid() const
	{
		return Level != nullptr;
	}

	void CompleteTasks();

	TArray< TSharedPtr< FUsdSchemaTranslatorTaskChain > > TranslatorTasks;
};

enum class ESchemaTranslationStatus
{
	Pending,
	InProgress,
	Done
};

enum class ESchemaTranslationLaunchPolicy
{
	/**
	 * Task will run on main thread, with the guarantee that no other tasks are being run concurrently to it.
	 * Note: This is slow, and should not be used for realtime workflows (i.e. USDStage editor)
	 */
	ExclusiveSync,

	/** Task will run on main thread, while other tasks may be running concurrently */
	Sync,

	/** Task may run on another thread, while other tasks may be running concurrently */
	Async
};

class USDSCHEMAS_API FUsdSchemaTranslator
{
public:
	explicit FUsdSchemaTranslator( TSharedRef< FUsdSchemaTranslationContext > InContext, const UE::FUsdTyped& InSchema )
		: PrimPath( InSchema.GetPrim().GetPrimPath() )
		, Context( InContext )
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

	UE::FUsdPrim GetPrim() const { return Context->Stage.GetPrimAtPath(PrimPath); }

protected:
	UE::FSdfPath PrimPath;
	TSharedRef< FUsdSchemaTranslationContext > Context;
};

struct FSchemaTranslatorTask
{
	explicit FSchemaTranslatorTask( ESchemaTranslationLaunchPolicy InPolicy, TFunction< bool() > InCallable )
		: Callable( InCallable )
		, LaunchPolicy( InPolicy )
		, bIsDone( false )
	{
	}

	TFunction< bool() > Callable;
	TOptional< TFuture< bool > > Result;
	TSharedPtr< FSchemaTranslatorTask > Continuation;

	ESchemaTranslationLaunchPolicy LaunchPolicy;

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

	FUsdSchemaTranslatorTaskChain& Do( ESchemaTranslationLaunchPolicy Policy, TFunction< bool() > Callable );
	FUsdSchemaTranslatorTaskChain& Then( ESchemaTranslationLaunchPolicy Policy, TFunction< bool() > Callable );

	ESchemaTranslationStatus Execute(bool bExclusiveSyncTasks = false);

private:
	TSharedPtr< FSchemaTranslatorTask > CurrentTask;
};
