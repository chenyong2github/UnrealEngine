// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlBPFunctionLibrary.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "RemoteControlActor.h"
#include "RemoteControlPreset.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void URemoteControlBPFunctionLibrary::ExposeProperty(URemoteControlPreset* RemoteControlPreset, UObject* SourceObject, FString Property, FString DisplayName, FName GroupName)
{
	if (RemoteControlPreset)
	{
		// Find or Add Group
		FRemoteControlPresetGroup* RCPGroup = FindOrAddGroup(RemoteControlPreset, GroupName);

		if (DisplayName.IsEmpty())
		{
			DisplayName = Property;
		}

		TArray<TWeakPtr<FRemoteControlProperty>> ExposedProperties = RemoteControlPreset->GetExposedEntities<FRemoteControlProperty>();
		bool bAlreadyExposed = false;

		for (const TWeakPtr<FRemoteControlProperty>& WeakProperty : ExposedProperties)
		{
			if (TSharedPtr<FRemoteControlProperty> ExposedProperty = WeakProperty.Pin())
			{
				const bool bContainsProperty = ExposedProperty->Bindings.ContainsByPredicate([SourceObject](const TWeakObjectPtr<URemoteControlBinding>& Binding){ return Binding.IsValid() && Binding->IsBound(SourceObject); });
					
				if (ExposedProperty->FieldName == *Property
					&& bContainsProperty) 
				{
					bAlreadyExposed = true;
					break;
				}
			}
		}

		if (!bAlreadyExposed)
		{
			RemoteControlPreset->ExposeProperty(SourceObject, Property, FRemoteControlPresetExposeArgs{DisplayName, RCPGroup->Id});
			UE_LOG(LogTemp, Display, TEXT("Added Property: %s"), *Property);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Property %s already exposed, skipping."), *Property);
		}
	}
}

void URemoteControlBPFunctionLibrary::ExposeFunction(URemoteControlPreset* RemoteControlPreset, UObject* SourceObject, FString Function, FString DisplayName, FName GroupName)
{
	if (RemoteControlPreset && SourceObject)
	{
		// Find or Add Group
		FRemoteControlPresetGroup* RCPGroup = FindOrAddGroup(RemoteControlPreset, GroupName);
		UFunction* ObjectFunction = SourceObject->GetClass()->FindFunctionByName(*Function);
		if (!ObjectFunction)
		{
			return;
		}
		
		TArray<TWeakPtr<FRemoteControlFunction>> ExposedFunctions = RemoteControlPreset->GetExposedEntities<FRemoteControlFunction>();
		bool bAlreadyExposed = false;
		
		for (const TWeakPtr<FRemoteControlFunction>& WeakFunction : ExposedFunctions)
		{
			if (TSharedPtr<FRemoteControlFunction> ExposedFunction = WeakFunction.Pin())
			{
				const bool bContainsFunction = ExposedFunction->Bindings.ContainsByPredicate([SourceObject](const TWeakObjectPtr<const URemoteControlBinding>& Binding){ return Binding.IsValid() && Binding->IsBound(SourceObject); });
				if (ExposedFunction->FieldName == *Function
                    && bContainsFunction) 
				{
					bAlreadyExposed = true;
					break;
				}
			}
		}

		if (!bAlreadyExposed)
		{
			RemoteControlPreset->ExposeFunction(SourceObject, ObjectFunction, FRemoteControlPresetExposeArgs{DisplayName, RCPGroup->Id});
			UE_LOG(LogTemp, Display, TEXT("Added Function: %s"), *Function);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Function %s already exposed, skipping."), *Function);
		}
	}
}

void URemoteControlBPFunctionLibrary::ExposeActor(URemoteControlPreset* RemoteControlPreset, AActor* Actor, FString DisplayName, FName GroupName)
{
	if (RemoteControlPreset)
	{
		// Find or Add Group
		FRemoteControlPresetGroup* RCPGroup = FindOrAddGroup(RemoteControlPreset, GroupName);

		// Expose Actor and Add Field to Group
		if (!RemoteControlPreset->IsExposed(RemoteControlPreset->GetExposedEntityId(FName(DisplayName))))
		{
			FRemoteControlPresetExposeArgs Args;
			Args.Label = DisplayName;
			Args.GroupId = RCPGroup->Id;
			RemoteControlPreset->Expose(Actor, Args);
			UE_LOG(LogTemp, Display, TEXT("Added Actor: %s"), *DisplayName);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Actor %s already exposed, skipping."), *DisplayName);
		}
	}
}

void URemoteControlBPFunctionLibrary::AddGroup(URemoteControlPreset* RemoteControlPreset, FName GroupName)
{
	FindOrAddGroup(RemoteControlPreset, GroupName);
}

FRemoteControlPresetGroup* URemoteControlBPFunctionLibrary::FindOrAddGroup(URemoteControlPreset* RemoteControlPreset, FName GroupName)
{
	FRemoteControlPresetGroup* RCPGroup = nullptr;
	if (RemoteControlPreset)
	{
		// Find or Add Group
		RCPGroup = RemoteControlPreset->Layout.GetGroupByName(GroupName);
		if (!RCPGroup)
		{
			RCPGroup = &RemoteControlPreset->Layout.CreateGroup(GroupName);
		}
	}
	return RCPGroup;
}

TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectActorWidget(FString WidgetType, FString Actor, FString Property, FString PropertyType)
{
	TSharedPtr<FJsonObject> JsonObjectActorWidget = MakeShared<FJsonObject>();
	JsonObjectActorWidget->SetStringField(FString(TEXT("widget")), WidgetType);
	JsonObjectActorWidget->SetStringField(FString(TEXT("actor")), Actor);
	JsonObjectActorWidget->SetStringField(FString(TEXT("property")), Property);
	JsonObjectActorWidget->SetStringField(FString(TEXT("propertyType")), PropertyType);
	return JsonObjectActorWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectTabsWidgetElement(FString Label, TArray< TSharedPtr<FJsonValue> > Widgets)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidgetElement = MakeShared<FJsonObject>();
	JsonObjectTabsWidgetElement->SetStringField(FString(TEXT("label")), Label);
	JsonObjectTabsWidgetElement->SetArrayField(FString(TEXT("widgets")), Widgets);
	return JsonObjectTabsWidgetElement;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectTabsWidget(TArray< TSharedPtr<FJsonValue> > Widgets)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidget = MakeShared<FJsonObject>();
	JsonObjectTabsWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("Tabs")));
	JsonObjectTabsWidget->SetArrayField(FString(TEXT("tabs")), Widgets);
	return JsonObjectTabsWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectListWidgetElement(FString Label, FString Actor, FString Property, TArray< TSharedPtr<FJsonValue> > ActorWidgets)
{
	TSharedPtr<FJsonObject> JsonObjectListWidgetElementCheck = MakeShared<FJsonObject>();
	JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("actor")), Actor);
	JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("property")), Property);
	TSharedPtr<FJsonObject> JsonObjectListWidgetElement = MakeShared<FJsonObject>();
	JsonObjectListWidgetElement->SetStringField(FString(TEXT("label")), Label);
	JsonObjectListWidgetElement->SetObjectField(FString(TEXT("check")), JsonObjectListWidgetElementCheck);
	JsonObjectListWidgetElement->SetArrayField(FString(TEXT("widgets")), ActorWidgets);
	return JsonObjectListWidgetElement;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectListWidget(TArray< TSharedPtr<FJsonValue> > ListWidgetElements)
{
	TSharedPtr<FJsonObject> JsonObjectListWidget = MakeShared<FJsonObject>();
	JsonObjectListWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("List")));
	JsonObjectListWidget->SetArrayField(FString(TEXT("items")), ListWidgetElements);
	return JsonObjectListWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectLabelWidget(FString Label)
{
	TSharedPtr<FJsonObject> JsonObjectLabelWidget = MakeShared<FJsonObject>();
	JsonObjectLabelWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("Label")));
	JsonObjectLabelWidget->SetStringField(FString(TEXT("label")), Label);
	return JsonObjectLabelWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectStack(FString Name, FString Icon, TArray< TSharedPtr<FJsonValue> > Widgets)
{
	TSharedPtr<FJsonObject> JsonObjectStack = MakeShared<FJsonObject>();
	JsonObjectStack->SetStringField(FString(TEXT("name")), Name);
	JsonObjectStack->SetStringField(FString(TEXT("icon")), Icon);
	JsonObjectStack->SetStringField(FString(TEXT("layout")), FString(TEXT("Stack")));
	JsonObjectStack->SetArrayField(FString(TEXT("stack")), Widgets);
	return JsonObjectStack;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectDropdownOption(FString Value, FString Label)
{
	TSharedPtr<FJsonObject> JsonObjectDropdownOption = MakeShared<FJsonObject>();
	JsonObjectDropdownOption->SetStringField(FString(TEXT("value")), Value);
	JsonObjectDropdownOption->SetStringField(FString(TEXT("label")), Label);
	return JsonObjectDropdownOption;
}

TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectDropdownOption(int32 Value, FString Label)
{
	TSharedPtr<FJsonObject> JsonObjectDropdownOption = MakeShared<FJsonObject>();
	JsonObjectDropdownOption->SetNumberField(FString(TEXT("value")), Value);
	JsonObjectDropdownOption->SetStringField(FString(TEXT("label")), Label);
	return JsonObjectDropdownOption;
}

TArray<FName> URemoteControlBPFunctionLibrary::GetGroups(URemoteControlPreset* RemoteControlPreset)
{
	TArray<FName> Groups;
	TArray<FRemoteControlPresetGroup> RCPGroups = RemoteControlPreset->Layout.GetGroups();
	for (FRemoteControlPresetGroup& RCPGroup : RCPGroups)
	{
		Groups.Add(RCPGroup.Name);
	}
	return Groups;
}
TArray<FRemoteControlPresetEntity> URemoteControlBPFunctionLibrary::GetEntities(URemoteControlPreset* RemoteControlPreset, FName Group)
{
	TArray<FRemoteControlPresetEntity> Entities;
	FRemoteControlPresetGroup* RCPGroup = RemoteControlPreset->Layout.GetGroupByName(Group);
	TArray<FGuid> RCPGroupGuids = RCPGroup->GetFields();
	for (FGuid& RCPGroupGuid : RCPGroupGuids)
	{
		if (TSharedPtr<FRemoteControlField> Field = RemoteControlPreset->GetExposedEntity<FRemoteControlField>(RCPGroupGuid).Pin())
		{
			Entities.Add(FRemoteControlPresetEntity(TOptional<FRemoteControlField>{*Field}));
		}
		// Check if there is an exposed Entity
		if (RemoteControlPreset->IsExposed(RCPGroupGuid))
		{
			TWeakPtr<FRemoteControlActor> Entity = RemoteControlPreset->GetExposedEntity<FRemoteControlActor>(RCPGroupGuid);
			if (TSharedPtr<FRemoteControlActor> EntityPin = Entity.Pin())
			{
				Entities.Add(FRemoteControlPresetEntity(EntityPin));
			}
		}
	}
	return Entities;
}
void URemoteControlBPFunctionLibrary::BuildWebRemoteControl(URemoteControlPreset* RemoteControlPreset, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects, bool bLogJsonObject)
{
	TSharedPtr<FJsonObject> JsonObjectMain = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Tabs;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Tab = MakeShared<FJsonValueObject>(RemoteControlJsonObject.JsonObject);
		Tabs.Add(Tab);
	}
	JsonObjectMain->SetArrayField(FString(TEXT("tabs")), Tabs);

	FString JsonToMetadataString;
	
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonToMetadataString);
	FJsonSerializer::Serialize(JsonObjectMain.ToSharedRef(), Writer);

	if (bLogJsonObject) 
	{ 
		UE_LOG(LogTemp, Display, TEXT("%s"), *JsonToMetadataString); 
	}

	RemoteControlPreset->Modify();
	RemoteControlPreset->Metadata.Add(FString(TEXT("view")), JsonToMetadataString);
	RemoteControlPreset->PostEditChange();
	RemoteControlPreset->OnMetadataModified().Broadcast(RemoteControlPreset);
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateTab(FString TabName, FString Icon, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectStack = MakeShared<FJsonObject>();
	JsonObjectStack->SetStringField(FString(TEXT("name")), TabName);
	JsonObjectStack->SetStringField(FString(TEXT("icon")), Icon);
	JsonObjectStack->SetStringField(FString(TEXT("layout")), FString(TEXT("Stack")));
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShared<FJsonValueObject>(RemoteControlJsonObject.JsonObject);
		Widgets.Add(Widget);
	}
	JsonObjectStack->SetArrayField(FString(TEXT("stack")), Widgets);
	return FRemoteControlJsonObject(JsonObjectStack);
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateListWidgetElement(FString Label, FString Actor, FString Property, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
TSharedPtr<FJsonObject> JsonObjectListWidgetElementCheck = MakeShared<FJsonObject>();
JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("actor")), Actor);
JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("property")), Property);
TSharedPtr<FJsonObject> JsonObjectListWidgetElement = MakeShared<FJsonObject>();
JsonObjectListWidgetElement->SetStringField(FString(TEXT("label")), Label);
JsonObjectListWidgetElement->SetObjectField(FString(TEXT("check")), JsonObjectListWidgetElementCheck);
TArray<TSharedPtr<FJsonValue>> Widgets;
for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
{
	TSharedRef<FJsonValueObject> Widget = MakeShared<FJsonValueObject>(RemoteControlJsonObject.JsonObject);
	Widgets.Add(Widget);
}
JsonObjectListWidgetElement->SetArrayField(FString(TEXT("widgets")), Widgets);
return FRemoteControlJsonObject(JsonObjectListWidgetElement);
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateListWidget(TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectListWidget = MakeShared<FJsonObject>();
	JsonObjectListWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("List")));
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShared<FJsonValueObject>(RemoteControlJsonObject.JsonObject);
		Widgets.Add(Widget);
	}
	JsonObjectListWidget->SetArrayField(FString(TEXT("items")), Widgets);
	return JsonObjectListWidget;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateTabsWidgetElement(FString Label, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidgetElement = MakeShared<FJsonObject>();
	JsonObjectTabsWidgetElement->SetStringField(FString(TEXT("label")), Label);
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShared<FJsonValueObject>(RemoteControlJsonObject.JsonObject);
		Widgets.Add(Widget);
	}
	JsonObjectTabsWidgetElement->SetArrayField(FString(TEXT("widgets")), Widgets);
	return JsonObjectTabsWidgetElement;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateTabsWidget(TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidget = MakeShared<FJsonObject>();
	JsonObjectTabsWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("Tabs")));
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShared<FJsonValueObject>(RemoteControlJsonObject.JsonObject);
		Widgets.Add(Widget);
	}
	JsonObjectTabsWidget->SetArrayField(FString(TEXT("tabs")), Widgets);
	return JsonObjectTabsWidget;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateActorWidget(FString WidgetType, FString Actor, FString Property, FString PropertyType)
{
	TSharedPtr<FJsonObject> JsonObjectActorWidget = MakeShared<FJsonObject>();
	JsonObjectActorWidget->SetStringField(FString(TEXT("widget")), WidgetType);
	JsonObjectActorWidget->SetStringField(FString(TEXT("actor")), Actor);
	JsonObjectActorWidget->SetStringField(FString(TEXT("property")), Property);
	JsonObjectActorWidget->SetStringField(FString(TEXT("propertyType")), PropertyType);
	return FRemoteControlJsonObject(JsonObjectActorWidget);
}
FString URemoteControlBPFunctionLibrary::GetRemoteControlPropertyType(FRemoteControlProperty RemoteControlProperty)
{
	if (const FProperty* ValueProperty = RemoteControlProperty.GetProperty())
	{
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ValueProperty))
		{
			ValueProperty = ArrayProperty->Inner;
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(ValueProperty))
		{
			ValueProperty = SetProperty->ElementProp;
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(ValueProperty))
		{
			ValueProperty = MapProperty->ValueProp;
		}
		return ValueProperty->GetCPPType();
	}
	return FString();
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateDropdownOptionsFromProperty(FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlProperty RemoteControlProperty)
{
	TArray< TSharedPtr<FJsonValue> > DropdownOptions;
	if (!RemoteControlProperty.GetProperty())
	{
		return RemoteControlJsonObject;
	}

	UEnum* Enum = nullptr;
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(RemoteControlProperty.GetProperty()))
	{
		Enum = ByteProperty->Enum;
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(RemoteControlProperty.GetProperty()))
	{
		Enum = EnumProperty->GetEnum();
	}
	if (Enum)
	{
		for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
		{
			TSharedRef<FJsonValueObject> DropdownOption = MakeShared<FJsonValueObject>(CreateJsonObjectDropdownOption(Enum->GetNameStringByIndex(i), Enum->GetDisplayNameTextByIndex(i).ToString()));
			DropdownOptions.Add(DropdownOption);
		}
		RemoteControlJsonObject.JsonObject->SetArrayField(FString(TEXT("options")), DropdownOptions);
	}
	return RemoteControlJsonObject;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateDropdownOptionsFromActor(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlActor RemoteControlActor, FName Property)
{
	if (AActor* Actor = RemoteControlActor.GetActor())
	{
		TArray< TSharedPtr<FJsonValue> > DropdownOptions;
		FProperty* FoundProperty = Actor->GetClass()->FindPropertyByName(Property);
		UEnum* Enum = nullptr;
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(FoundProperty))
		{
			Enum = ByteProperty->Enum;
		}
		else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(FoundProperty))
		{
			Enum = EnumProperty->GetEnum();
		}
		if (Enum)
		{
			for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
			{
				TSharedRef<FJsonValueObject> DropdownOption = MakeShared<FJsonValueObject>(CreateJsonObjectDropdownOption(Enum->GetNameStringByIndex(i), Enum->GetDisplayNameTextByIndex(i).ToString()));
				DropdownOptions.Add(DropdownOption);
			}
			RemoteControlJsonObject.JsonObject->SetArrayField(FString(TEXT("options")), DropdownOptions);
		}
	}
	return RemoteControlJsonObject;
}

FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateDropdownOptionsFromSkySwitcherActor(FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlActor RemoteControlActor)
{
	TArray<TSharedPtr<FJsonValue>> DropdownOptions;

	if (AActor* Actor = RemoteControlActor.GetActor())
	{
		if (FProperty* TexturesProperty = Actor->GetClass()->FindPropertyByName("SkyTexture"))
		{
			if (TArray<UTexture*>* Textures = TexturesProperty->ContainerPtrToValuePtr<TArray<UTexture*>>(Actor))
			{
				for (int32 Index = 0; Index < Textures->Num(); Index++)
				{
					TSharedRef<FJsonValueObject> DropdownOption = MakeShared<FJsonValueObject>(CreateJsonObjectDropdownOption(Index + 1, (*Textures)[Index]->GetOutermost()->GetPathName()));
					DropdownOptions.Add(DropdownOption);
				}
			}
		}
		
		RemoteControlJsonObject.JsonObject->SetArrayField(FString(TEXT("options")), DropdownOptions);
	}
	return RemoteControlJsonObject;
}

FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateEmptyRemoteControlJsonObject()
{
	return FRemoteControlJsonObject();
}
FRemoteControlJsonValue URemoteControlBPFunctionLibrary::CreateRemoteControlJsonValueBool(bool Value)
{
	return FRemoteControlJsonValue(Value);
}
FRemoteControlJsonValue URemoteControlBPFunctionLibrary::CreateRemoteControlJsonValueString(FString Value)
{
	return FRemoteControlJsonValue(Value);
}
FRemoteControlJsonValue URemoteControlBPFunctionLibrary::CreateRemoteControlJsonValueNumber(float Value)
{
	return FRemoteControlJsonValue(Value);
}
FRemoteControlJsonValue URemoteControlBPFunctionLibrary::CreateRemoteControlJsonValueObject(UPARAM(ref) FRemoteControlJsonObject& Value)
{
	return FRemoteControlJsonValue(Value);
}
FRemoteControlJsonValue URemoteControlBPFunctionLibrary::CreateRemoteControlJsonValueArray(UPARAM(ref) TArray<FRemoteControlJsonValue>& Values)
{
	return FRemoteControlJsonValue(Values);
}

FRemoteControlJsonObject URemoteControlBPFunctionLibrary::SetBoolField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, bool Value)
{
	RemoteControlJsonObject.JsonObject->SetBoolField(Field, Value);
	return RemoteControlJsonObject;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::SetNumberField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, float Value)
{
	RemoteControlJsonObject.JsonObject->SetNumberField(Field, Value);
	return RemoteControlJsonObject;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::SetStringField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, FString Value)
{
	RemoteControlJsonObject.JsonObject->SetStringField(Field, Value);
	return RemoteControlJsonObject;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::SetObjectField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, UPARAM(ref) FRemoteControlJsonObject& Value)
{
	RemoteControlJsonObject.JsonObject->SetObjectField(Field, Value.JsonObject);
	return RemoteControlJsonObject;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::SetArrayField(UPARAM(ref) FRemoteControlJsonObject& RemoteControlJsonObject, FString Field, UPARAM(ref) FRemoteControlJsonValue& Values)
{
	TArray<TSharedPtr<FJsonValue>> JsonValues;
	for (const FRemoteControlJsonValue& Value : Values.JsonValue->AsArray())
	{
		JsonValues.Add(Value.JsonValue);
	}
	RemoteControlJsonObject.JsonObject->SetArrayField(Field, JsonValues);
	return RemoteControlJsonObject;
}

FRemoteControlPropertyMinMax URemoteControlBPFunctionLibrary::GetRemoteControlPropertyMinMax(FRemoteControlProperty RemoteControlProperty)
{
	if (FProperty* Property = RemoteControlProperty.GetProperty())
	{
		return FRemoteControlPropertyMinMax(
	    Property->GetMetaData(TEXT("ClampMin")),
	    Property->GetMetaData(TEXT("ClampMax")), 
	    Property->GetMetaData(TEXT("UIMin")), 
	    Property->GetMetaData(TEXT("UIMax")));
	}
	else
	{
		return FRemoteControlPropertyMinMax();	
	}
}