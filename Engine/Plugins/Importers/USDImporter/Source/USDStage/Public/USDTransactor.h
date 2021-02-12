// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ITransaction.h"
#include "UObject/WeakObjectPtr.h"

#include "USDValueConversion.h"

#include "UsdWrappers/VtValue.h"

#include "USDTransactor.generated.h"

class AUsdStageActor;

namespace UsdUtils
{
	/**
	 * Maps from prim property paths to the values stored by those fields. "/" signifies the property is actually stage metadata, like metersPerUnit or upAxis.
	 * Note that for consistency we *always* have a field token at the end (almost always ".default", but can be "variability", "timeSamples", etc.).
	 * Example keys: "/Root/MyPrim.some_field.default", "/Root/Parent/SomePrim.kind.default", "/.metersPerUnit.default", "/.upAxis.default", etc.
	 */
	using FUsdFieldValueMap = TMap<FString, UE::FVtValue>;
	using FConvertedFieldValueMap = TMap<FString, UsdUtils::FConvertedVtValue>;

	class FUsdTransactorImpl;
}

/**
 * Class that allows us to log prim attribute changes into the unreal transaction buffer.
 * The AUsdStageActor owns one of these, and whenever a USD notice is fired this class transacts and serializes
 * the notice data with itself. When undo/redoing it applies the old/new values to the, AUsdStageActors' current stage.
 *
 * Additionally this class naturally allows multi-user (ConcertSync) support for USD stage interactions, by letting
 * these notice data to be mirrored on other clients.
 */
UCLASS()
class USDSTAGE_API UUsdTransactor : public UObject
{
	GENERATED_BODY()

public:
	// Boilerplate for Pimpl usage with UObject
	UUsdTransactor();
	UUsdTransactor( FVTableHelper& Helper );
	~UUsdTransactor();

	void Initialize( AUsdStageActor* InStageActor );
	void Update( const UsdUtils::FUsdFieldValueMap& InOldValues, const UsdUtils::FUsdFieldValueMap& InNewValues );

    // Begin UObject interface
	virtual void Serialize( FArchive& Ar ) override;
#if WITH_EDITOR
	virtual void PreEditUndo();
	virtual void PostEditUndo();
#endif // WITH_EDITOR
    //~ End UObject interface

private:
	TWeakObjectPtr<AUsdStageActor> StageActor;

	// On each USD object change notice we store both the old values of the changed attributes as well as the new ones.
	// This is what allows us to undo/redo them later, regardless of what happened between subsequent recorded transactions
	UsdUtils::FConvertedFieldValueMap OldValues;
	UsdUtils::FConvertedFieldValueMap NewValues;

	TUniquePtr< UsdUtils::FUsdTransactorImpl > Impl;
};