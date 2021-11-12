// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "InstancedStruct.h"
#include "MassTranslatorRegistry.generated.h"

class UScriptStruct;
class AActor;
class UActorComponent;
class UMassProcessor;
class UMassTranslator;
class UMassObjectFragmentCreator;
class UMassFragmentInitializer;
class UMassFragmentDestructor;

/** @todo rename the class, no longer hosting Translators, just Initializers and Deinitializers/Destructors
 *  @todo brief description of the class
 */
UCLASS()
class MASSSPAWNER_API UMassTranslatorRegistry : public UObject
{
	GENERATED_BODY()
public:
	UMassTranslatorRegistry();

	static UMassTranslatorRegistry& GetMutable() { return *GetMutableDefault<UMassTranslatorRegistry>(); }
	static const UMassTranslatorRegistry& Get() { return *GetDefault<UMassTranslatorRegistry>(); }

	/** UMassFragmentInitializer - given a UScriptStruct sets its initial values to some defaults or randomized 
	 *  values(implementation - specific). It's a kind of a MassProcessor */
	void RegisterFragmentInitializer(const UScriptStruct& FragmentType, TSubclassOf<UMassFragmentInitializer> FragmentInitializerClass);
	const UMassFragmentInitializer* GetFragmentInitializer(const UScriptStruct& FragmentType) const;

	/** UMassFragmentDestructor - given a UScriptStruct destroys values and do cleanup */
	void RegisterFragmentDestructor(const UScriptStruct& FragmentType, TSubclassOf<UMassFragmentDestructor> FragmentDestructorClass);
	const UMassFragmentDestructor* GetFragmentDestructor(const UScriptStruct& FragmentType) const;

protected:

	UPROPERTY()
	TMap<const UScriptStruct*, TSubclassOf<UMassFragmentInitializer>> FragmentInitializersMap;

	UPROPERTY()
	TMap<const UScriptStruct*, TSubclassOf<UMassFragmentDestructor>> FragmentDestructorsMap;
};
