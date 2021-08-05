// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetailsViewWrapperObject.generated.h"

class UDetailsViewWrapperObject;

DECLARE_EVENT_ThreeParams(UDetailsViewWrapperObject, FWrappedPropertyChangedChainEvent, UDetailsViewWrapperObject*, const FString&, FPropertyChangedChainEvent&);

UCLASS()
class UDetailsViewWrapperObject : public UObject
{
public:
	GENERATED_BODY()

	static UClass* GetClassForStruct(UScriptStruct* InStruct, bool bCreateIfNeeded = true);
	static UDetailsViewWrapperObject* MakeInstance(UScriptStruct* InStruct, uint8* InStructMemory, UObject* InOuter = nullptr);
	UScriptStruct* GetWrappedStruct() const;
	FStructProperty* GetStructProperty() const;

	void SetContent(const uint8* InStructMemory, int32 InStructSize);
	const uint8* GetContent();
	void GetContent(uint8* OutStructMemory, int32 InStructSize);

	template<typename T>
	T* GetContent()
	{
		return (T*)GetStoredStructPtr();
	}

	template<typename T>
	void SetContent(const T* InStructMemory)
	{
		SetCOntent((const uint8*)InStructMemory, T::StaticStruct()->GetStructureSize());
	}

	FWrappedPropertyChangedChainEvent& GetWrappedPropertyChangedChainEvent() { return WrappedPropertyChangedChainEvent; }
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

private:

	uint8* GetStoredStructPtr();
	
	static TMap<UScriptStruct*, UClass*> StructToClass;
	static TMap<UClass*, UScriptStruct*> ClassToStruct;
	
	FWrappedPropertyChangedChainEvent WrappedPropertyChangedChainEvent;
};
