// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RemoteControlActor.h"
#include "RemoteControlPreset.h"
#include "RemoteControlBPFunctionLibrary.generated.h"

class URemoteControlPreset;
class FJsonObject;
class FJsonValue;
class AActor;

UENUM(BlueprintType)
enum class ERemoteControlPresetEntityType : uint8
{
	EntityType_Property		UMETA(DisplayName = "Property"),
	EntityType_Function		UMETA(DisplayName = "Function"),
	EntityType_Actor		UMETA(DisplayName = "Actor")
};

USTRUCT(BlueprintType)
struct FRemoteControlPropertyMinMax
{
	GENERATED_BODY()

	FRemoteControlPropertyMinMax() = default;
	FRemoteControlPropertyMinMax(FString inClampMin, FString inClampMax, FString inUIMin, FString inUIMax)
	{
		ClampMin = inClampMin;
		ClampMax = inClampMax;
		UIMin = inUIMin;
		UIMax = inUIMax;
	}

	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FString ClampMin;
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FString ClampMax;
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FString UIMin;
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FString UIMax;
};

USTRUCT(BlueprintType)
struct FRemoteControlJsonObject
{
	GENERATED_BODY()

	FRemoteControlJsonObject()
	{
		JsonObject = MakeShareable(new FJsonObject());
	}
	FRemoteControlJsonObject(TSharedPtr<FJsonObject> InJsonObject)
	{
		JsonObject = InJsonObject;
	}

	TSharedPtr<FJsonObject> JsonObject;
};

USTRUCT(BlueprintType)
struct FRemoteControlJsonValue
{
	GENERATED_BODY()

	FRemoteControlJsonValue()
	{
		JsonValue = MakeShareable(new FJsonValueNull());
	}
	FRemoteControlJsonValue(TSharedPtr<FJsonValue> InJsonValue)
	{
		JsonValue = InJsonValue;
	}
	FRemoteControlJsonValue(bool Value)
	{
		JsonValue = MakeShareable(new FJsonValueBoolean(Value));
	}
	FRemoteControlJsonValue(FString Value)
	{
		JsonValue = MakeShareable(new FJsonValueString(Value));
	}
	FRemoteControlJsonValue(float Value)
	{
		JsonValue = MakeShareable(new FJsonValueNumber(Value));
	}
	FRemoteControlJsonValue(FRemoteControlJsonObject Value)
	{
		JsonValue = MakeShareable(new FJsonValueObject(Value.JsonObject));
	}
	FRemoteControlJsonValue(TArray<FRemoteControlJsonValue> Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (FRemoteControlJsonValue& Value : Values)
		{
			JsonValues.Add(Value.JsonValue);
		}
		JsonValue = MakeShareable(new FJsonValueArray(JsonValues));
	}

	TSharedPtr<FJsonValue> JsonValue;
};

USTRUCT(BlueprintType)
struct FRemoteControlPresetEntity
{
	GENERATED_BODY()

	FRemoteControlPresetEntity() = default;
	FRemoteControlPresetEntity(TSharedPtr<FRemoteControlActor> Actor)
	{
		EntityType = ERemoteControlPresetEntityType::EntityType_Actor;
		RemoteControlActor = *Actor.Get();
	}
	FRemoteControlPresetEntity(TOptional<FRemoteControlField> RemoteControlField)
	{
		// if Property
		if (RemoteControlField->FieldType == EExposedFieldType::Property)
		{
			EntityType = ERemoteControlPresetEntityType::EntityType_Property;
			TOptional<FRemoteControlProperty> RCProperty = RemoteControlField->GetOwner()->GetProperty(RemoteControlField->GetLabel());
			RemoteControlProperty = *RCProperty;
		}
		// if Function
		else if (RemoteControlField->FieldType == EExposedFieldType::Function)
		{
			EntityType = ERemoteControlPresetEntityType::EntityType_Function;
			TOptional<FRemoteControlFunction> RCFunction = RemoteControlField->GetOwner()->GetFunction(RemoteControlField->GetLabel());
			RemoteControlFunction = *RCFunction;
		}
	}
	
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	ERemoteControlPresetEntityType EntityType;

	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FRemoteControlActor RemoteControlActor;
	
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FRemoteControlProperty RemoteControlProperty;
	
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FRemoteControlFunction RemoteControlFunction;

};

UCLASS()
class URemoteControlBPFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
	// Expose a Property to a RemoteControlPreset
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Layout")
	static void ExposeProperty(URemoteControlPreset* RemoteControlPreset, UObject* SourceObject, FString Property, FString DisplayName, FName GroupName);

	// Expose a Function to a RemoteControlPreset
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Layout")
	static void ExposeFunction(URemoteControlPreset* RemoteControlPreset, UObject* SourceObject, FString Function, FString DisplayName, FName GroupName);

	// Expose an Actor to a RemoteControlPreset
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Layout")
	static void ExposeActor(URemoteControlPreset* RemoteControlPreset, AActor* Actor, FString DisplayName, FName GroupName);

	// Add a Group to a RemoteControlPreset
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Layout")
	static void AddGroup(URemoteControlPreset* RemoteControlPreset, FName GroupName);

	static FRemoteControlPresetGroup* FindOrAddGroup(URemoteControlPreset* RemoteControlPreset, FName GroupName);

	// Return a list of Group names from a given RemoteControlPreset
	UFUNCTION(BlueprintPure, Category = "RemoteControlPreset")
	static TArray<FName> GetGroups(URemoteControlPreset* RemoteControlPreset);

	// Return a list of exposed entities from a given RemoteControlPreset
	UFUNCTION(BlueprintPure, Category = "RemoteControlPreset")
	static TArray<FRemoteControlPresetEntity> GetEntities (URemoteControlPreset* RemoteControlPreset, FName Group);

	UFUNCTION(BlueprintPure, Category = "RemoteControlPreset")
	static FString GetRemoteControlPropertyType(FRemoteControlProperty RemoteControlProperty);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset")
	static FRemoteControlPropertyMinMax GetRemoteControlPropertyMinMax(FRemoteControlProperty RemoteControlProperty);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static void BuildWebRemoteControl (URemoteControlPreset* RemoteControlPreset, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects, bool bLogJsonObject);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateTab(FString TabName, FString Icon, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateListWidgetElement(FString Label, FString Actor, FString Property, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateTabsWidgetElement(FString Label, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateListWidget(TArray<FRemoteControlJsonObject> RemoteControlJsonObjects);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateTabsWidget(TArray<FRemoteControlJsonObject> RemoteControlJsonObjects);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateActorWidget(FString WidgetType, FString Actor, FString Property, FString PropertyType);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateDropdownOptionsFromProperty(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlProperty RemoteControlProperty);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateDropdownOptionsFromActor(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlActor RemoteControlActor, FName Property);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateDropdownOptionsFromSkySwitcherActor(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlActor RemoteControlActor);
	
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject CreateEmptyRemoteControlJsonObject();

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonValue CreateRemoteControlJsonValueBool(bool Value);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonValue CreateRemoteControlJsonValueString(FString Value);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonValue CreateRemoteControlJsonValueNumber(float Value);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonValue CreateRemoteControlJsonValueObject(UPARAM(ref) FRemoteControlJsonObject& Value);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonValue CreateRemoteControlJsonValueArray(UPARAM(ref) TArray<FRemoteControlJsonValue>& Values);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject SetBoolField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, bool Value);

	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject SetNumberField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, float Value);
	
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject SetStringField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, FString Value);
	
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject SetObjectField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, UPARAM(ref) FRemoteControlJsonObject& Value);
	
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset | Web")
	static FRemoteControlJsonObject SetArrayField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, UPARAM(ref) FRemoteControlJsonValue& Values);

	static TSharedPtr<FJsonObject> CreateJsonObjectActorWidget(FString WidgetType, FString Actor, FString Property, FString PropertyType);
	static TSharedPtr<FJsonObject> CreateJsonObjectTabsWidgetElement(FString Label, TArray< TSharedPtr<FJsonValue> > Widgets);
	static TSharedPtr<FJsonObject> CreateJsonObjectTabsWidget(TArray< TSharedPtr<FJsonValue> > Widgets);
	static TSharedPtr<FJsonObject> CreateJsonObjectListWidgetElement(FString Label, FString Actor, FString Property, TArray< TSharedPtr<FJsonValue> > ActorWidgets);
	static TSharedPtr<FJsonObject> CreateJsonObjectListWidget(TArray< TSharedPtr<FJsonValue> > ListWidgetElements);
	static TSharedPtr<FJsonObject> CreateJsonObjectLabelWidget(FString Label);
	static TSharedPtr<FJsonObject> CreateJsonObjectStack(FString Name, FString Icon, TArray< TSharedPtr<FJsonValue> > Widgets);
	static TSharedPtr<FJsonObject> CreateJsonObjectDropdownOption(FString Value, FString Label);
	static TSharedPtr<FJsonObject> CreateJsonObjectDropdownOption(int32 Value, FString Label);

};
