// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "NiagaraCommon.h"

#include "NiagaraEditorDataBase.generated.h"

class UNiagaraParameterDefinitionsBase;

USTRUCT()
struct FNiagaraGraphViewSettings
{
	GENERATED_BODY()
public:
	FNiagaraGraphViewSettings()
		: Location(FVector2D::ZeroVector)
		, Zoom(0.0f)
		, bIsValid(false)
	{
	}

	FNiagaraGraphViewSettings(const FVector2D& InLocation, float InZoom)
		: Location(InLocation)
		, Zoom(InZoom)
		, bIsValid(true)
	{
	}

	const FVector2D& GetLocation() const { return Location; }
	float GetZoom() const { return Zoom; }
	bool IsValid() const { return bIsValid; }

private:
	UPROPERTY()
	FVector2D Location;

	UPROPERTY()
	float Zoom;

	UPROPERTY()
	bool bIsValid;
};

/** A base class for editor only data which supports post loading from the runtime owner object. */
UCLASS(abstract, MinimalAPI)
class UNiagaraEditorDataBase : public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	virtual void PostLoadFromOwner(UObject* InOwner) { }

	NIAGARA_API FSimpleMulticastDelegate& OnPersistentDataChanged() { return PersistentDataChangedDelegate; }

private:
	FSimpleMulticastDelegate PersistentDataChangedDelegate;
#endif
};

/** A base class for editor only data which owns UNiagaraScriptVariables and supports synchronizing them with definitions. */
UCLASS(abstract, MinimalAPI)
class UNiagaraEditorParametersAdapterBase : public UObject
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
public:
	/** Synchronize all source script variables that have been changed or removed from the parameter definitions to all eligible destination script variables owned by the editor data.
	 * @param	ParameterDefinitions				The parameter definitions to synchronize owned UNiagaraScriptVariables with.
	 * @param	ParameterDefinitionsParameterIds	The unique ids of all parameters owned by parameter definitions assets subscribed to by the owning INiagaraParameterDefinitionsSubscriber.
	 *												Used to reconcile if a definition parameter has been removed and the subscribing UNiagaraSCriptVariable may mark itself as no longer subscribed.
	 * @param	Args								Top level arguments defining specific definitions or destination script vars to sync. See FSynchronizeWithParameterDefinitionsArgs for more info.
	 * @return										Returns an array of name pairs representing old names of script vars that were synced and the new names they inherited, respectively.
	 */
	virtual TArray<TTuple<FName /*SyncedOldName*/, FName /*SyncedNewName*/>> SynchronizeParametersWithParameterDefinitions(
		const TArray<UNiagaraParameterDefinitionsBase*> ParameterDefinitions,
		const TArray<FGuid>& ParameterDefinitionsParameterIds,
		const FSynchronizeWithParameterDefinitionsArgs& Args
	) {
		return TArray<TTuple<FName, FName>>();
	};
#endif
};
