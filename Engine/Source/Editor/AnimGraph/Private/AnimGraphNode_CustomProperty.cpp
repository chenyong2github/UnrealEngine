// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_CustomProperty.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "DetailLayoutBuilder.h"
#include "AnimationGraphSchema.h"

#define LOCTEXT_NAMESPACE "CustomPropNode"

void UAnimGraphNode_CustomProperty::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UAnimBlueprint* AnimBP = CastChecked<UAnimBlueprint>(GetBlueprint());

	UObject* OriginalNode = MessageLog.FindSourceObject(this);

	// Check we have a class set
	UClass* TargetClass = GetTargetClass();
	if(!TargetClass)
	{
		MessageLog.Error(TEXT("Sub instance node @@ has no valid instance class to spawn."), this);
	}
}

void UAnimGraphNode_CustomProperty::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);

	UClass* TargetClass = GetTargetClass();

	if(!TargetClass)
	{
		// Nothing to search for properties
		return;
	}

	// Need the schema to extract pin types
	const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(GetSchema());

	// Default anim schema for util funcions
	const UAnimationGraphSchema* AnimGraphDefaultSchema = GetDefault<UAnimationGraphSchema>();

	// Grab the list of properties we can expose
	TArray<UProperty*> ExposablePropeties;
	GetExposableProperties(ExposablePropeties);

	// We'll track the names we encounter by removing from this list, if anything remains the properties
	// have been removed from the target class and we should remove them too
	TArray<FName> BeginExposableNames = KnownExposableProperties;

	for(UProperty* Property : ExposablePropeties)
	{
		FName PropertyName = Property->GetFName();
		BeginExposableNames.Remove(PropertyName);

		if(!KnownExposableProperties.Contains(PropertyName))
		{
			// New property added to the target class
			KnownExposableProperties.Add(PropertyName);
		}

		if(ExposedPropertyNames.Contains(PropertyName) && FBlueprintEditorUtils::PropertyStillExists(Property))
		{
			FEdGraphPinType PinType;

			verify(Schema->ConvertPropertyToPinType(Property, PinType));

			UEdGraphPin* NewPin = CreatePin(EEdGraphPinDirection::EGPD_Input, PinType, Property->GetFName());
			NewPin->PinFriendlyName = Property->GetDisplayNameText();

			// Need to grab the default value for the property from the target generated class CDO
			FString CDODefaultValueString;
			uint8* ContainerPtr = reinterpret_cast<uint8*>(TargetClass->GetDefaultObject());

			if(FBlueprintEditorUtils::PropertyValueToString(Property, ContainerPtr, CDODefaultValueString, this))
			{
				// If we successfully pulled a value, set it to the pin
				Schema->TrySetDefaultValue(*NewPin, CDODefaultValueString);
			}

			CustomizePinData(NewPin, PropertyName, INDEX_NONE);
		}
	}

	// Remove any properties that no longer exist on the target class
	for(FName& RemovedPropertyName : BeginExposableNames)
	{
		KnownExposableProperties.Remove(RemovedPropertyName);
	}
}

void UAnimGraphNode_CustomProperty::GetInstancePinProperty(const UClass* InOwnerInstanceClass, UEdGraphPin* InInputPin, UProperty*& OutProperty)
{
	// The actual name of the instance property
	FString FullName = GetPinTargetVariableName(InInputPin);

	if(UProperty* Property = FindField<UProperty>(InOwnerInstanceClass, *FullName))
	{
		OutProperty = Property;
	}
	else
	{
		OutProperty = nullptr;
	}
}

FString UAnimGraphNode_CustomProperty::GetPinTargetVariableName(const UEdGraphPin* InPin) const
{
	return TEXT("__CustomProperty_") + InPin->PinName.ToString() + TEXT("_") + NodeGuid.ToString();
}

FText UAnimGraphNode_CustomProperty::GetPropertyTypeText(UProperty* Property)
{
	FText PropertyTypeText;

	if(UStructProperty* StructProperty = Cast<UStructProperty>(Property))
	{
		PropertyTypeText = StructProperty->Struct->GetDisplayNameText();
	}
	else if(UObjectProperty* ObjectProperty = Cast<UObjectProperty>(Property))
	{
		PropertyTypeText = ObjectProperty->PropertyClass->GetDisplayNameText();
	}
	else if(UClass* PropClass = Property->GetClass())
	{
		PropertyTypeText = PropClass->GetDisplayNameText();
	}
	else
	{
		PropertyTypeText = LOCTEXT("PropertyTypeUnknown", "Unknown");
	}
	
	return PropertyTypeText;
}

void UAnimGraphNode_CustomProperty::RebuildExposedProperties(UClass* InNewClass)
{
	ExposedPropertyNames.Empty();
	KnownExposableProperties.Empty();
	if(InNewClass)
	{
		TArray<UProperty*> ExposableProperties;
		GetExposableProperties(ExposableProperties);

		for(UProperty* Property : ExposableProperties)
		{
			KnownExposableProperties.Add(Property->GetFName());
		}
	}
}

ECheckBoxState UAnimGraphNode_CustomProperty::IsPropertyExposed(FName PropertyName) const
{
	return ExposedPropertyNames.Contains(PropertyName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void UAnimGraphNode_CustomProperty::OnPropertyExposeCheckboxChanged(ECheckBoxState NewState, FName PropertyName)
{
	if(NewState == ECheckBoxState::Checked)
	{
		ExposedPropertyNames.AddUnique(PropertyName);
	}
	else if(NewState == ECheckBoxState::Unchecked)
	{
		ExposedPropertyNames.Remove(PropertyName);
	}

	ReconstructNode();
}

void UAnimGraphNode_CustomProperty::OnInstanceClassChanged(IDetailLayoutBuilder* DetailBuilder)
{
	if(DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

UObject* UAnimGraphNode_CustomProperty::GetJumpTargetForDoubleClick() const
{
	UClass* InstanceClass = GetTargetClass();
	
	if(InstanceClass)
	{
		return InstanceClass->ClassGeneratedBy;
	}

	return nullptr;
}

bool UAnimGraphNode_CustomProperty::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const
{
	UClass* InstanceClassToUse = GetTargetClass();

	// Add our instance class... If that changes we need a recompile
	if(InstanceClassToUse && OptionalOutput)
	{
		OptionalOutput->AddUnique(InstanceClassToUse);
	}

	bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return InstanceClassToUse || bSuperResult;
}

void UAnimGraphNode_CustomProperty::GetExposableProperties( TArray<UProperty*>& OutExposableProperties) const
{
	OutExposableProperties.Empty();

	UClass* TargetClass = GetTargetClass();

	if(TargetClass)
	{
		const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(GetSchema());

		for(TFieldIterator<UProperty> It(TargetClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UProperty* CurProperty = *It;
			FEdGraphPinType PinType;

			if(CurProperty->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) && CurProperty->HasAllFlags(RF_Public) && Schema->ConvertPropertyToPinType(CurProperty, PinType))
			{
				OutExposableProperties.Add(CurProperty);
			}
		}
	}
}

void UAnimGraphNode_CustomProperty::AddSourceTargetProperties(const FName& InSourcePropertyName, const FName& InTargetPropertyName)
{
	FAnimNode_CustomProperty* CustomPropAnimNode = GetInternalNode();
	if (CustomPropAnimNode)
	{
		CustomPropAnimNode->SourcePropertyNames.Add(InSourcePropertyName);
		CustomPropAnimNode->DestPropertyNames.Add(InTargetPropertyName);
	}
}

UClass* UAnimGraphNode_CustomProperty::GetTargetClass() const
{
	const FAnimNode_CustomProperty* CustomPropAnimNode = GetInternalNode();
	if (CustomPropAnimNode)
	{
		return CustomPropAnimNode->GetTargetClass();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE