// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlBPFunctionLibrary.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture.h"
#include "RemoteControlPreset.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RemoteControlActor.h"

void URemoteControlBPFunctionLibrary::ExposeProperty(URemoteControlPreset* RemoteControlPreset, UObject* SourceObject, FString Property, FString DisplayName, FName GroupName)
{
	if (RemoteControlPreset)
	{
		// Find or Add Group
		FRemoteControlPresetGroup* RCPGroup = FindOrAddGroup(RemoteControlPreset, GroupName);

		// Expose Property and Add Field to Group
		TMap<FName, FRemoteControlTarget>& RemoteControlTargets = RemoteControlPreset->GetRemoteControlTargets();
		if (DisplayName.IsEmpty())
		{
			DisplayName = Property;
		}

		if (FRemoteControlTarget* RemoteControlTarget = RemoteControlTargets.Find(FName(*UKismetSystemLibrary::GetDisplayName(SourceObject))))
		{
			FName ExistingLabel = RemoteControlTarget->FindFieldLabel(*Property);
			if (ExistingLabel == NAME_None)
			{
				RemoteControlTarget->ExposeProperty(FRCFieldPathInfo{ Property }, DisplayName, RCPGroup->Id);
				UE_LOG(LogTemp, Display, TEXT("Added Property: %s"), *Property);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("Property %s already exposed, skipping."), *Property);
			}
		}
		else
		{
			RemoteControlPreset->CreateAndGetTarget({ SourceObject }).ExposeProperty(FRCFieldPathInfo{ Property }, DisplayName, RCPGroup->Id);
			UE_LOG(LogTemp, Display, TEXT("Created RCTarget and added Property: %s"), *Property);
		}
	}
}

void URemoteControlBPFunctionLibrary::ExposeFunction(URemoteControlPreset* RemoteControlPreset, UObject* SourceObject, FString Function, FString DisplayName, FName GroupName)
{
	if (RemoteControlPreset)
	{
		// Find or Add Group
		FRemoteControlPresetGroup* RCPGroup = FindOrAddGroup(RemoteControlPreset, GroupName);

		// Expose Function and Add Field to Group
		TMap<FName, FRemoteControlTarget>& RemoteControlTargets = RemoteControlPreset->GetRemoteControlTargets();
		FString FunctionPath = SourceObject->GetPathName() + FString(".") + Function;
		FRCFieldPathInfo RCFieldPathInfo = FRCFieldPathInfo(FunctionPath);
		if (FRemoteControlTarget* RemoteControlTarget = RemoteControlTargets.Find(SourceObject->GetFName()))
		{
			FName ExistingLabel = RemoteControlTarget->FindFieldLabel(RCFieldPathInfo);
			UE_LOG(LogTemp, Display, TEXT("%s"), *ExistingLabel.ToString());
			if (ExistingLabel == NAME_None)
			{
				RemoteControlTarget->ExposeFunction(FunctionPath, DisplayName, RCPGroup->Id);
				UE_LOG(LogTemp, Display, TEXT("Added Function: %s"), *Function);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("Function %s already exposed, skipping."), *Function);
			}
		}
		else
		{
			RemoteControlPreset->CreateAndGetTarget({ SourceObject }).ExposeFunction(FunctionPath, DisplayName, RCPGroup->Id);
			UE_LOG(LogTemp, Display, TEXT("Created RCTarget and added Function: %s"), *Function);
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
	TSharedPtr<FJsonObject> JsonObjectActorWidget = MakeShareable(new FJsonObject);
	JsonObjectActorWidget->SetStringField(FString(TEXT("widget")), WidgetType);
	JsonObjectActorWidget->SetStringField(FString(TEXT("actor")), Actor);
	JsonObjectActorWidget->SetStringField(FString(TEXT("property")), Property);
	JsonObjectActorWidget->SetStringField(FString(TEXT("propertyType")), PropertyType);
	return JsonObjectActorWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectTabsWidgetElement(FString Label, TArray< TSharedPtr<FJsonValue> > Widgets)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidgetElement = MakeShareable(new FJsonObject);
	JsonObjectTabsWidgetElement->SetStringField(FString(TEXT("label")), Label);
	JsonObjectTabsWidgetElement->SetArrayField(FString(TEXT("widgets")), Widgets);
	return JsonObjectTabsWidgetElement;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectTabsWidget(TArray< TSharedPtr<FJsonValue> > Widgets)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidget = MakeShareable(new FJsonObject);
	JsonObjectTabsWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("Tabs")));
	JsonObjectTabsWidget->SetArrayField(FString(TEXT("tabs")), Widgets);
	return JsonObjectTabsWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectListWidgetElement(FString Label, FString Actor, FString Property, TArray< TSharedPtr<FJsonValue> > ActorWidgets)
{
	TSharedPtr<FJsonObject> JsonObjectListWidgetElementCheck = MakeShareable(new FJsonObject);
	JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("actor")), Actor);
	JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("property")), Property);
	TSharedPtr<FJsonObject> JsonObjectListWidgetElement = MakeShareable(new FJsonObject);
	JsonObjectListWidgetElement->SetStringField(FString(TEXT("label")), Label);
	JsonObjectListWidgetElement->SetObjectField(FString(TEXT("check")), JsonObjectListWidgetElementCheck);
	JsonObjectListWidgetElement->SetArrayField(FString(TEXT("widgets")), ActorWidgets);
	return JsonObjectListWidgetElement;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectListWidget(TArray< TSharedPtr<FJsonValue> > ListWidgetElements)
{
	TSharedPtr<FJsonObject> JsonObjectListWidget = MakeShareable(new FJsonObject);
	JsonObjectListWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("List")));
	JsonObjectListWidget->SetArrayField(FString(TEXT("items")), ListWidgetElements);
	return JsonObjectListWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectLabelWidget(FString Label)
{
	TSharedPtr<FJsonObject> JsonObjectLabelWidget = MakeShareable(new FJsonObject);
	JsonObjectLabelWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("Label")));
	JsonObjectLabelWidget->SetStringField(FString(TEXT("label")), Label);
	return JsonObjectLabelWidget;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectStack(FString Name, FString Icon, TArray< TSharedPtr<FJsonValue> > Widgets)
{
	TSharedPtr<FJsonObject> JsonObjectStack = MakeShareable(new FJsonObject);
	JsonObjectStack->SetStringField(FString(TEXT("name")), Name);
	JsonObjectStack->SetStringField(FString(TEXT("icon")), Icon);
	JsonObjectStack->SetStringField(FString(TEXT("layout")), FString(TEXT("Stack")));
	JsonObjectStack->SetArrayField(FString(TEXT("stack")), Widgets);
	return JsonObjectStack;
}
TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectDropdownOption(FString Value, FString Label)
{
	TSharedPtr<FJsonObject> JsonObjectDropdownOption = MakeShareable(new FJsonObject);
	JsonObjectDropdownOption->SetStringField(FString(TEXT("value")), Value);
	JsonObjectDropdownOption->SetStringField(FString(TEXT("label")), Label);
	return JsonObjectDropdownOption;
}

TSharedPtr<FJsonObject> URemoteControlBPFunctionLibrary::CreateJsonObjectDropdownOption(int32 Value, FString Label)
{
	TSharedPtr<FJsonObject> JsonObjectDropdownOption = MakeShareable(new FJsonObject);
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
		if (TOptional<FRemoteControlField> Field = RemoteControlPreset->GetField(RCPGroupGuid))
		{
			Entities.Add(FRemoteControlPresetEntity(Field));
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
	TSharedPtr<FJsonObject> JsonObjectMain = MakeShareable(new FJsonObject());
	TArray< TSharedPtr<FJsonValue> > Tabs;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Tab = MakeShareable(new FJsonValueObject(RemoteControlJsonObject.JsonObject));
		Tabs.Add(Tab);
	}
	JsonObjectMain->SetArrayField(FString(TEXT("tabs")), Tabs);

	FString JsonToMetadataString;
	TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&JsonToMetadataString);
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
	TSharedPtr<FJsonObject> JsonObjectStack = MakeShareable(new FJsonObject);
	JsonObjectStack->SetStringField(FString(TEXT("name")), TabName);
	JsonObjectStack->SetStringField(FString(TEXT("icon")), Icon);
	JsonObjectStack->SetStringField(FString(TEXT("layout")), FString(TEXT("Stack")));
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShareable(new FJsonValueObject(RemoteControlJsonObject.JsonObject));
		Widgets.Add(Widget);
	}
	JsonObjectStack->SetArrayField(FString(TEXT("stack")), Widgets);
	return FRemoteControlJsonObject(JsonObjectStack);
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateListWidgetElement(FString Label, FString Actor, FString Property, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
TSharedPtr<FJsonObject> JsonObjectListWidgetElementCheck = MakeShareable(new FJsonObject);
JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("actor")), Actor);
JsonObjectListWidgetElementCheck->SetStringField(FString(TEXT("property")), Property);
TSharedPtr<FJsonObject> JsonObjectListWidgetElement = MakeShareable(new FJsonObject);
JsonObjectListWidgetElement->SetStringField(FString(TEXT("label")), Label);
JsonObjectListWidgetElement->SetObjectField(FString(TEXT("check")), JsonObjectListWidgetElementCheck);
TArray<TSharedPtr<FJsonValue>> Widgets;
for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
{
	TSharedRef<FJsonValueObject> Widget = MakeShareable(new FJsonValueObject(RemoteControlJsonObject.JsonObject));
	Widgets.Add(Widget);
}
JsonObjectListWidgetElement->SetArrayField(FString(TEXT("widgets")), Widgets);
return FRemoteControlJsonObject(JsonObjectListWidgetElement);
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateListWidget(TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectListWidget = MakeShareable(new FJsonObject);
	JsonObjectListWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("List")));
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShareable(new FJsonValueObject(RemoteControlJsonObject.JsonObject));
		Widgets.Add(Widget);
	}
	JsonObjectListWidget->SetArrayField(FString(TEXT("items")), Widgets);
	return JsonObjectListWidget;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateTabsWidgetElement(FString Label, TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidgetElement = MakeShareable(new FJsonObject);
	JsonObjectTabsWidgetElement->SetStringField(FString(TEXT("label")), Label);
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShareable(new FJsonValueObject(RemoteControlJsonObject.JsonObject));
		Widgets.Add(Widget);
	}
	JsonObjectTabsWidgetElement->SetArrayField(FString(TEXT("widgets")), Widgets);
	return JsonObjectTabsWidgetElement;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateTabsWidget(TArray<FRemoteControlJsonObject> RemoteControlJsonObjects)
{
	TSharedPtr<FJsonObject> JsonObjectTabsWidget = MakeShareable(new FJsonObject);
	JsonObjectTabsWidget->SetStringField(FString(TEXT("widget")), FString(TEXT("Tabs")));
	TArray<TSharedPtr<FJsonValue>> Widgets;
	for (FRemoteControlJsonObject& RemoteControlJsonObject : RemoteControlJsonObjects)
	{
		TSharedRef<FJsonValueObject> Widget = MakeShareable(new FJsonValueObject(RemoteControlJsonObject.JsonObject));
		Widgets.Add(Widget);
	}
	JsonObjectTabsWidget->SetArrayField(FString(TEXT("tabs")), Widgets);
	return JsonObjectTabsWidget;
}
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateActorWidget(FString WidgetType, FString Actor, FString Property, FString PropertyType)
{
	TSharedPtr<FJsonObject> JsonObjectActorWidget = MakeShareable(new FJsonObject);
	JsonObjectActorWidget->SetStringField(FString(TEXT("widget")), WidgetType);
	JsonObjectActorWidget->SetStringField(FString(TEXT("actor")), Actor);
	JsonObjectActorWidget->SetStringField(FString(TEXT("property")), Property);
	JsonObjectActorWidget->SetStringField(FString(TEXT("propertyType")), PropertyType);
	return FRemoteControlJsonObject(JsonObjectActorWidget);
}
FString URemoteControlBPFunctionLibrary::GetRemoteControlPropertyType(FRemoteControlProperty RemoteControlProperty)
{
	TOptional<FExposedProperty> UnderlyingProperty = RemoteControlProperty.GetOwner()->ResolveExposedProperty(RemoteControlProperty.GetLabel());
	if (!UnderlyingProperty.IsSet())
	{
		return FString();
	}

	const FProperty* ValueProperty = UnderlyingProperty->Property;
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
FRemoteControlJsonObject URemoteControlBPFunctionLibrary::CreateDropdownOptionsFromProperty(FRemoteControlJsonObject& RemoteControlJsonObject, FRemoteControlProperty RemoteControlProperty)
{
	TArray< TSharedPtr<FJsonValue> > DropdownOptions;
	TOptional<FExposedProperty> UnderlyingProperty = RemoteControlProperty.GetOwner()->ResolveExposedProperty(RemoteControlProperty.GetLabel());
	if (!UnderlyingProperty.IsSet())
	{
		return RemoteControlJsonObject;
	}

	UEnum* Enum = nullptr;
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(UnderlyingProperty->Property))
	{
		Enum = ByteProperty->Enum;
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(UnderlyingProperty->Property))
	{
		Enum = EnumProperty->GetEnum();
	}
	if (Enum)
	{
		for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
		{
			TSharedRef<FJsonValueObject> DropdownOption = MakeShareable(new FJsonValueObject(CreateJsonObjectDropdownOption(Enum->GetNameStringByIndex(i), Enum->GetDisplayNameTextByIndex(i).ToString())));
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
				TSharedRef<FJsonValueObject> DropdownOption = MakeShareable(new FJsonValueObject(CreateJsonObjectDropdownOption(Enum->GetNameStringByIndex(i), Enum->GetDisplayNameTextByIndex(i).ToString())));
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
					TSharedRef<FJsonValueObject> DropdownOption = MakeShareable(new FJsonValueObject(CreateJsonObjectDropdownOption(Index + 1, (*Textures)[Index]->GetOutermost()->GetPathName())));
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
	TOptional<FExposedProperty> UnderlyingProperty = RemoteControlProperty.GetOwner()->ResolveExposedProperty(RemoteControlProperty.GetLabel());
	if (!UnderlyingProperty.IsSet())
	{
		return FRemoteControlPropertyMinMax();
	}

	return FRemoteControlPropertyMinMax(
		UnderlyingProperty->Property->GetMetaData(TEXT("ClampMin")),
		UnderlyingProperty->Property->GetMetaData(TEXT("ClampMax")), 
		UnderlyingProperty->Property->GetMetaData(TEXT("UIMin")), 
		UnderlyingProperty->Property->GetMetaData(TEXT("UIMax"))) ;
}