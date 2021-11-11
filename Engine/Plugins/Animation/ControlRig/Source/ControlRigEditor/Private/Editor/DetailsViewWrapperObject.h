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

	bool IsChildOf(const UStruct* InStruct) const
	{
		return GetWrappedStruct()->IsChildOf(InStruct);
	}

	template<typename T>
	bool IsChildOf() const
	{
		return IsChildOf(T::StaticStruct());
	}

	void SetContent(const uint8* InStructMemory, const UStruct* InStruct);
	void GetContent(uint8* OutStructMemory, const UStruct* InStruct) const;

	template<typename T>
	T GetContent() const
	{
		check(IsChildOf<T>());
		
		T Result;
		GetContent((uint8*)&Result, T::StaticStruct());
		return Result;
	}

	template<typename T>
	void SetContent(const T& InValue)
	{
		check(IsChildOf<T>());

		SetContent((const uint8*)&InValue, T::StaticStruct());
	}

	FWrappedPropertyChangedChainEvent& GetWrappedPropertyChangedChainEvent() { return WrappedPropertyChangedChainEvent; }
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

private:

	static void CopyPropertiesForUnrelatedStructs(uint8* InTargetMemory, const UStruct* InTargetStruct, const uint8* InSourceMemory, const UStruct* InSourceStruct);
	
	static TMap<UScriptStruct*, UClass*> StructToClass;
	static TMap<UClass*, UScriptStruct*> ClassToStruct;
	
	FWrappedPropertyChangedChainEvent WrappedPropertyChangedChainEvent;
};
