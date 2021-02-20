// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EngineElementsLibrary.generated.h"

class UObject;
struct FObjectElementData;

class AActor;
struct FActorElementData;

class UActorComponent;
struct FComponentElementData;

class UInstancedStaticMeshComponent;
struct FSMInstanceElementData;

struct FSMInstanceId;
struct FSMInstanceElementId;

class UWorld;
class UTypedElementList;

UCLASS()
class ENGINE_API UEngineElementsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UEngineElementsLibrary();

	static TTypedElementOwner<FObjectElementData> CreateObjectElement(const UObject* InObject);
	static void DestroyObjectElement(const UObject* InObject, TTypedElementOwner<FObjectElementData>& InOutObjectElement);
#if WITH_EDITOR
	static void CreateEditorObjectElement(const UObject* Object);
	static void DestroyEditorObjectElement(const UObject* Object);
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|Object", meta=(ScriptMethod="AcquireEditorElementHandle"))
	static FTypedElementHandle AcquireEditorObjectElementHandle(const UObject* Object, const bool bAllowCreate = true);
#endif

	static TTypedElementOwner<FActorElementData> CreateActorElement(const AActor* InActor);
	static void DestroyActorElement(const AActor* InActor, TTypedElementOwner<FActorElementData>& InOutActorElement);
#if WITH_EDITOR
	static void CreateEditorActorElement(const AActor* Actor);
	static void DestroyEditorActorElement(const AActor* Actor);
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Actor", meta=(ScriptMethod="AcquireEditorElementHandle"))
	static FTypedElementHandle AcquireEditorActorElementHandle(const AActor* Actor, const bool bAllowCreate = true);
#endif

	static TTypedElementOwner<FComponentElementData> CreateComponentElement(const UActorComponent* InComponent);
	static void DestroyComponentElement(const UActorComponent* InComponent, TTypedElementOwner<FComponentElementData>& InOutComponentElement);
#if WITH_EDITOR
	static void CreateEditorComponentElement(const UActorComponent* Component);
	static void DestroyEditorComponentElement(const UActorComponent* Component);
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|Component", meta=(ScriptMethod="AcquireEditorElementHandle"))
	static FTypedElementHandle AcquireEditorComponentElementHandle(const UActorComponent* Component, const bool bAllowCreate = true);
#endif

	static TTypedElementOwner<FSMInstanceElementData> CreateSMInstanceElement(const FSMInstanceId& InSMInstanceId);
	static TTypedElementOwner<FSMInstanceElementData> CreateSMInstanceElement(const FSMInstanceElementId& InSMInstanceElementId);
	static void DestroySMInstanceElement(const FSMInstanceElementId& InSMInstanceElementId, TTypedElementOwner<FSMInstanceElementData>& InOutSMInstanceElement);
#if WITH_EDITOR
	static void CreateEditorSMInstanceElement(const FSMInstanceId& SMInstanceId);
	static void DestroyEditorSMInstanceElement(const FSMInstanceElementId& SMInstanceElementId);
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|StaticMeshInstance", meta=(ScriptMethod="AcquireEditorElementHandle"))
	static FTypedElementHandle AcquireEditorSMInstanceElementHandle(const UInstancedStaticMeshComponent* ISMComponent, const int32 InstanceIndex, const bool bAllowCreate = true);
	static FTypedElementHandle AcquireEditorSMInstanceElementHandle(const FSMInstanceId& SMInstanceId, const bool bAllowCreate = true);
	static FTypedElementHandle AcquireEditorSMInstanceElementHandle(const FSMInstanceElementId& SMInstanceElementId, const bool bAllowCreate = true);
#endif

private:
#if WITH_EDITOR
	static void DestroyUnreachableEditorObjectElements();
	static void OnObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementObjects);
#endif
};
