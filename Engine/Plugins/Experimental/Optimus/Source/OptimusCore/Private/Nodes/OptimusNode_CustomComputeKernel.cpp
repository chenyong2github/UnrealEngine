// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_CustomComputeKernel.h"

#include "DataInterfaces/OptimusDataInterfaceRawBuffer.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusHelpers.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "OptimusObjectVersion.h"

static const FName DefaultKernelName = FName("MyKernel");
static const FName InputBindingsName= GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, InputBindingArray);
static const FName OutputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, OutputBindingArray);

UOptimusNode_CustomComputeKernel::UOptimusNode_CustomComputeKernel()
{
	EnableDynamicPins();
	UpdatePreamble();
	
	KernelName = DefaultKernelName;
}


FString UOptimusNode_CustomComputeKernel::GetKernelSourceText() const
{
	return GetCookedKernelSource(GetPathName(), ShaderSource.ShaderText, KernelName.ToString(), GroupSize);
}

#if WITH_EDITOR

FString UOptimusNode_CustomComputeKernel::GetNameForShaderTextEditor() const
{
	return KernelName.ToString();
}

FString UOptimusNode_CustomComputeKernel::GetDeclarations() const
{
	return ShaderSource.Declarations;
}

FString UOptimusNode_CustomComputeKernel::GetShaderText() const
{
	return ShaderSource.ShaderText;
}

void UOptimusNode_CustomComputeKernel::SetShaderText(const FString& NewText) 
{
	Modify();
	ShaderSource.ShaderText = NewText;
}

#endif // WITH_EDITOR

FString UOptimusNode_CustomComputeKernel::GetBindingDeclaration(
	FName BindingName
	) const
{
	auto ParameterBindingPredicate = [BindingName](const FOptimusParameterBinding& InBinding)
	{
		if (InBinding.Name == BindingName)
		{
			return true;	
		}
			
		return false;
	};

	if (const FOptimusParameterBinding* Binding = InputBindingArray.FindByPredicate(ParameterBindingPredicate))
	{
		return GetDeclarationForBinding(*Binding, true);
	}
	if (const FOptimusParameterBinding* Binding = OutputBindingArray.FindByPredicate(ParameterBindingPredicate))
	{
		return GetDeclarationForBinding(*Binding, false);
	}

	return FString();
}


bool UOptimusNode_CustomComputeKernel::CanAddPinFromPin(
	const UOptimusNodePin* InSourcePin,
	EOptimusNodePinDirection InNewPinDirection,
	FString* OutReason
	) const
{
	if (!CanConnect(InSourcePin, InNewPinDirection, OutReason))
	{
		return false;
	}
	
	if (InSourcePin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		if (InSourcePin->GetStorageType() != EOptimusNodePinStorageType::Resource)
		{
			if (OutReason)
			{
				*OutReason = TEXT("Can't add parameter pin as output");
			}

			return false;
		}
	}

	return true;
}


UOptimusNodePin* UOptimusNode_CustomComputeKernel::TryAddPinFromPin(
	UOptimusNodePin* InSourcePin,
	FName InNewPinName
	) 
{
	const EOptimusNodePinDirection NewPinDirection =
		InSourcePin->GetDirection() == EOptimusNodePinDirection::Input ?
				EOptimusNodePinDirection::Output : EOptimusNodePinDirection::Input;
	
	TArray<FOptimusParameterBinding>& BindingArray = 
		InSourcePin->GetDirection() == EOptimusNodePinDirection::Input ?
			OutputBindingArray.InnerArray : InputBindingArray.InnerArray;

	FOptimusParameterBinding Binding;
	Binding.Name = InNewPinName;
	Binding.DataType = {InSourcePin->GetDataType()};
	Binding.DataDomain = {InSourcePin->GetDataDomainLevelNames()};

	Modify();
	BindingArray.Add(Binding);
	UpdatePreamble();
	
	const FOptimusNodePinStorageConfig StorageConfig(Binding.DataDomain.LevelNames);
	UOptimusNodePin* NewPin	= AddPinDirect(InNewPinName, NewPinDirection, StorageConfig, Binding.DataType);

	return NewPin;
}


bool UOptimusNode_CustomComputeKernel::RemoveAddedPin(
	UOptimusNodePin* InAddedPinToRemove
	)
{
	TArray<FOptimusParameterBinding>& BindingArray = 
		InAddedPinToRemove->GetDirection() == EOptimusNodePinDirection::Input ?
			InputBindingArray.InnerArray : OutputBindingArray.InnerArray;

	Modify();
	BindingArray.RemoveAll(
		[InAddedPinToRemove](const FOptimusParameterBinding& Binding)
		{
			return Binding.Name == InAddedPinToRemove->GetFName(); 
		});
	UpdatePreamble();
	return RemovePinDirect(InAddedPinToRemove);
}


FName UOptimusNode_CustomComputeKernel::GetSanitizedNewPinName(
	FName InPinName
	)
{
	FName NewName = Optimus::GetSanitizedNameForHlsl(InPinName);

	NewName = Optimus::GetUniqueNameForScope(this, NewName);

	return NewName;
}


#if WITH_EDITOR
void UOptimusNode_CustomComputeKernel::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent
	)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName BasePropertyName = (PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None);
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
	{
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, KernelName))
		{
			SetDisplayName(FText::FromName(KernelName));
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusParameterBinding, Name))
		{
			if (BasePropertyName == InputBindingsName)
			{
				UpdatePinNames(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinNames(EOptimusNodePinDirection::Output);
			}
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusDataTypeRef, TypeName))
		{
			if (BasePropertyName == InputBindingsName)
			{
				UpdatePinTypes(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinTypes(EOptimusNodePinDirection::Output);
			}
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusParameterBinding, DataDomain))
		{
			if (BasePropertyName == InputBindingsName)
			{
				UpdatePinDataDomains(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinDataDomains(EOptimusNodePinDirection::Output);
			}
			UpdatePreamble();
		}
		else if (PropertyName == InputBindingsName || PropertyName == OutputBindingsName)
		{
			RefreshBindingPins(PropertyName);
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayAdd)
	{
		EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;
		FOptimusParameterBinding *Binding = nullptr;
		FName Name;
		FOptimusNodePinStorageConfig StorageConfig;

		if (BasePropertyName == InputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &InputBindingArray.Last();
			Name = FName("Input");

			StorageConfig = FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex});
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Output;
			Binding = &OutputBindingArray.Last();
			Name = FName("Output");

			StorageConfig = FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex});
		}

		if (Binding != nullptr)
		{
			Binding->Name = Optimus::GetUniqueNameForScope(this, Name);
			Binding->DataType = FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass());

			AddPin(Binding->Name, Direction, StorageConfig, Binding->DataType);

			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayRemove)
	{
		TMap<FName, UOptimusNodePin *> RemovedPins;
		if (BasePropertyName == InputBindingsName)
		{
			RemovedPins = GetNamedPinsByDirection(EOptimusNodePinDirection::Input);
			
			for (const FOptimusParameterBinding& Binding: InputBindingArray)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			RemovedPins = GetNamedPinsByDirection(EOptimusNodePinDirection::Output);
			
			for (const FOptimusParameterBinding& Binding: OutputBindingArray)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}

		if (RemovedPins.Num())
		{
			for (TMap<FName, UOptimusNodePin*>::TIterator It = RemovedPins.CreateIterator(); It; ++It)
			{
				RemovePin(It.Value());
			}
			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayClear)
	{
		ClearBindingPins(BasePropertyName);
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayMove)
	{
		RefreshBindingPins(BasePropertyName);
	}
}
#endif

void UOptimusNode_CustomComputeKernel::PostLoad()
{
	if (GetLinkerCustomVersion(FOptimusObjectVersion::GUID) < FOptimusObjectVersion::SwitchToParameterBindingArrayStruct)
	{
		Modify();
		InputBindingArray.InnerArray = InputBindings_DEPRECATED;
		OutputBindingArray.InnerArray = OutputBindings_DEPRECATED;
	}

	if (!Parameters_DEPRECATED.IsEmpty())
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS		
		TArray<FOptimusParameterBinding> ParameterInputBindings;

		for (const FOptimus_ShaderBinding& OldBinding: Parameters_DEPRECATED)
		{
			FOptimusParameterBinding NewBinding;
			NewBinding.Name = OldBinding.Name;
			NewBinding.DataType = OldBinding.DataType;
			NewBinding.DataDomain.LevelNames.Reset();
			
			ParameterInputBindings.Add(NewBinding);
		}
		
		InputBindingArray.InnerArray.Insert(ParameterInputBindings, 0);
		
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	
	Super::PostLoad();
}


void UOptimusNode_CustomComputeKernel::ConstructNode()
{
	// After a duplicate, the kernel node has no pins, so we need to reconstruct them from
	// the bindings. We can assume that all naming clashes have already been dealt with.
	for (const FOptimusParameterBinding& Binding: InputBindingArray)
	{
		const FOptimusNodePinStorageConfig StorageConfig(Binding.DataDomain.LevelNames);
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Input, StorageConfig, Binding.DataType);
	}
	for (const FOptimusParameterBinding& Binding: OutputBindingArray)
	{
		const FOptimusNodePinStorageConfig StorageConfig(Binding.DataDomain.LevelNames);
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Output, StorageConfig, Binding.DataType);
	}
}

void UOptimusNode_CustomComputeKernel::RefreshBindingPins(FName InBindingPropertyName)
{
	// This event can indicate that a reordering action was applied to an array
	if (!IsParameterBinding(InBindingPropertyName))
	{
		return;
	}
	
	TArray<FOptimusParameterBinding>* ParameterBindingArray = nullptr;
	EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Input;;
	if (InBindingPropertyName == InputBindingsName)
	{
		Direction = EOptimusNodePinDirection::Input;
		ParameterBindingArray = &InputBindingArray.InnerArray;
	}
	else /* if (InBindingPropertyName == OutputBindingsName) */
	{
		Direction = EOptimusNodePinDirection::Output;
		ParameterBindingArray = &OutputBindingArray.InnerArray;
	}

	const TMap<FName, UOptimusNodePin *> RemovedPins = GetNamedPinsByDirection(Direction);

	// Save the links and readd them later when new pins are created
	TMap<FName, TArray<UOptimusNodePin*>> ConnectedPinsMap;
	for (const TTuple<FName, UOptimusNodePin*>& Pin : RemovedPins.Array())
	{
		ConnectedPinsMap.FindOrAdd(Pin.Key) = Pin.Value->GetConnectedPins();
	}
	
	ClearBindingPins(InBindingPropertyName);

	TArray<UOptimusNodePin*> AddedPins;
	for (const FOptimusParameterBinding& Binding: *ParameterBindingArray)
	{
		AddedPins.Add(AddPin(Binding.Name, Direction, Binding.DataDomain.LevelNames, Binding.DataType, nullptr));
	}

	for (UOptimusNodePin* AddedPin : AddedPins)
	{
		if (TArray<UOptimusNodePin*>* ConnectedPins = ConnectedPinsMap.Find(AddedPin->GetFName()))
		{
			for (UOptimusNodePin* ConnectedPin : *ConnectedPins)
			{
				if (Direction == EOptimusNodePinDirection::Input)
				{
					GetOwningGraph()->AddLink(ConnectedPin, AddedPin);
				}
				else if (Direction == EOptimusNodePinDirection::Output)
				{
					GetOwningGraph()->AddLink(AddedPin, ConnectedPin);
				}
			}
		}
	}
		
	UpdatePreamble();
}

void UOptimusNode_CustomComputeKernel::ClearBindingPins(FName InBindingPropertyName)
{
	if (!IsParameterBinding(InBindingPropertyName))
	{
		return;
	}

	EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Input;;
	if (InBindingPropertyName == InputBindingsName)
	{
		Direction = EOptimusNodePinDirection::Input;
	}
	else /* if (InBindingPropertyName == OutputBindingsName) */
	{
		Direction = EOptimusNodePinDirection::Output;
	}


	const TMap<FName, UOptimusNodePin *> RemovedPins = GetNamedPinsByDirection(Direction);
	for (const TTuple<FName, UOptimusNodePin*>& Pin : RemovedPins.Array())
	{
		RemovePin(Pin.Value);
	}
		
	UpdatePreamble();
}


bool UOptimusNode_CustomComputeKernel::IsParameterBinding(FName InBindingPropertyName)
{
	return InBindingPropertyName == InputBindingsName || InBindingPropertyName == OutputBindingsName;
}


void UOptimusNode_CustomComputeKernel::UpdatePinTypes(
	EOptimusNodePinDirection InPinDirection
	)
{
	TArray<FOptimusDataTypeHandle> DataTypes;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimusParameterBinding& Binding: InputBindingArray)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimusParameterBinding& Binding: OutputBindingArray)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}	
	
	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> KernelPins = GetPinsByDirection(InPinDirection);

	if (ensure(DataTypes.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			if (KernelPins[Index]->GetDataType() != DataTypes[Index])
			{
				SetPinDataType(KernelPins[Index], DataTypes[Index]);
			}
		}
	}
}


void UOptimusNode_CustomComputeKernel::UpdatePinNames(
	EOptimusNodePinDirection InPinDirection
	)
{
	TArray<FName> Names;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimusParameterBinding& Binding: InputBindingArray)
		{
			Names.Add(Binding.Name);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimusParameterBinding& Binding: OutputBindingArray)
		{
			Names.Add(Binding.Name);
		}
	}	
	
	// Let's try and figure out which pin got changed.
	TArray<UOptimusNodePin*> KernelPins = GetPinsByDirection(InPinDirection);

	bool bNameChanged = false;
	if (ensure(Names.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			if (KernelPins[Index]->GetFName() != Names[Index])
			{
				FName NewName = Optimus::GetUniqueNameForScope(this, Names[Index]);

				SetPinName(KernelPins[Index], NewName);

				if (NewName != Names[Index])
				{
					bNameChanged = true;  
				}
			}
		}
	}

	if (bNameChanged)
	{
		if (InPinDirection == EOptimusNodePinDirection::Input)
		{
			for (int32 Index = 0; Index < Names.Num(); Index++)
			{
				InputBindingArray[Index].Name = Names[Index];
			}
		}
		else if (InPinDirection == EOptimusNodePinDirection::Output)
		{
			for (int32 Index = 0; Index < Names.Num(); Index++)
			{
				OutputBindingArray[Index].Name = Names[Index];
			}
		}
	}
}

void UOptimusNode_CustomComputeKernel::UpdatePinDataDomains(
	EOptimusNodePinDirection InPinDirection
	)
{
	TArray<TArray<FName>> PinDataDomains;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimusParameterBinding& Binding: InputBindingArray)
		{
			PinDataDomains.Add(Binding.DataDomain.LevelNames);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimusParameterBinding& Binding: OutputBindingArray)
		{
			PinDataDomains.Add(Binding.DataDomain.LevelNames);
		}
	}	
	
	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> KernelPins = GetPinsByDirection(InPinDirection);

	if (ensure(PinDataDomains.Num() == KernelPins.Num()))
	{
		for (int32 Index = 0; Index < KernelPins.Num(); Index++)
		{
			SetPinDataDomain(KernelPins[Index], PinDataDomains[Index]);
		}
	}
}


void UOptimusNode_CustomComputeKernel::UpdatePreamble()
{
	TSet<FString> StructsSeen;
	TArray<FString> Structs;

	auto CollectStructs = [&StructsSeen, &Structs](const auto& BindingArray)
	{
		for (const FOptimusParameterBinding &Binding: BindingArray)
		{
			const FShaderValueType& ValueType = *Binding.DataType->ShaderValueType;
			if (ValueType.Type == EShaderFundamentalType::Struct)
			{
				const FString StructName = ValueType.ToString();
				if (!StructsSeen.Contains(StructName))
				{
					Structs.Add(ValueType.GetTypeDeclaration() + TEXT("\n\n"));
					StructsSeen.Add(StructName);
				}
			}
		}
	};

	CollectStructs(InputBindingArray);
	CollectStructs(OutputBindingArray);
	
	TArray<FString> Declarations;

	// FIXME: Lump input/output functions together into single context.
	auto ContextsPredicate = [](const FOptimusParameterBinding& A, const FOptimusParameterBinding &B)
	{
		for (int32 Index = 0; Index < FMath::Min(A.DataDomain.LevelNames.Num(), B.DataDomain.LevelNames.Num()); Index++)
		{
			if (A.DataDomain.LevelNames[Index] != B.DataDomain.LevelNames[Index])
			{
				return FNameLexicalLess()(A.DataDomain.LevelNames[Index], B.DataDomain.LevelNames[Index]);
			}
		}
		return false;
	};
	
	TSet<TArray<FName>> SeenDataDomains;
	TArray<FOptimusParameterBinding> Bindings = InputBindingArray.InnerArray;
	Bindings.Sort(ContextsPredicate);

	auto AddCountFunctionIfNeeded = [&Declarations, &SeenDataDomains](const TArray<FName>& InContextNames)
	{
		if (!InContextNames.IsEmpty() && !SeenDataDomains.Contains(InContextNames))
		{
			FString CountNameInfix;

			for (FName ContextName: InContextNames)
			{
				CountNameInfix.Append(ContextName.ToString());
			}
			Declarations.Add(FString::Printf(TEXT("uint Get%sCount();"), *CountNameInfix));
			SeenDataDomains.Add(InContextNames);
		}
	};
	
	for (const FOptimusParameterBinding& Binding: Bindings)
	{
		AddCountFunctionIfNeeded(Binding.DataDomain.LevelNames);
		
		TArray<FString> Indexes;
		for (FString IndexName: GetIndexNamesFromDataDomainLevels(Binding.DataDomain.LevelNames))
		{
			Indexes.Add(FString::Printf(TEXT("uint %s"), *IndexName));
		}
		
		Declarations.Add(GetDeclarationForBinding(Binding, true));
	}

	Bindings = OutputBindingArray.InnerArray;
	Bindings.Sort(ContextsPredicate);
	for (const FOptimusParameterBinding& Binding: Bindings)
	{
		AddCountFunctionIfNeeded(Binding.DataDomain.LevelNames);
		
		TArray<FString> Indexes;
		for (FString IndexName: GetIndexNamesFromDataDomainLevels(Binding.DataDomain.LevelNames))
		{
			Indexes.Add(FString::Printf(TEXT("uint %s"), *IndexName));
		}
		
		Declarations.Add(GetDeclarationForBinding(Binding, false));
	}

	ShaderSource.Declarations.Reset();
	if (!Structs.IsEmpty())
	{
		ShaderSource.Declarations += TEXT("// Type declarations\n");
		ShaderSource.Declarations += FString::Join(Structs, TEXT("\n")) + TEXT("\n");
	}
	if (!Declarations.IsEmpty())
	{
		ShaderSource.Declarations += TEXT("// Parameters and resource read/write functions\n");
		ShaderSource.Declarations += FString::Join(Declarations, TEXT("\n"));
	}
	ShaderSource.Declarations += "\n// Resource Indexing\n";
	ShaderSource.Declarations += "uint Index;	// From SV_DispatchThreadID.x\n";
}

FString UOptimusNode_CustomComputeKernel::GetDeclarationForBinding(const FOptimusParameterBinding& Binding, bool bIsInput)
{
	TArray<FString> Indexes;
	for (FString IndexName: GetIndexNamesFromDataDomainLevels(Binding.DataDomain.LevelNames))
	{
		Indexes.Add(FString::Printf(TEXT("uint %s"), *IndexName));
	}

	if (bIsInput)
	{
		return FString::Printf(TEXT("%s Read%s(%s);"),
				*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", ")));
	}
	else
	{
		return FString::Printf(TEXT("void Write%s(%s, %s Value);"),
					*Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", ")), *Binding.DataType->ShaderValueType->ToString());
	}
}


TArray<UOptimusNodePin*> UOptimusNode_CustomComputeKernel::GetPinsByDirection(
	EOptimusNodePinDirection InPinDirection
	) const
{
	TArray<UOptimusNodePin*> FilteredPins;
	for (UOptimusNodePin* Pin : GetPins())
	{
		if (InPinDirection == EOptimusNodePinDirection::Unknown || Pin->GetDirection() == InPinDirection)
		{
			FilteredPins.Add(Pin);
		}
	}

	return FilteredPins;
}

TMap<FName, UOptimusNodePin*> UOptimusNode_CustomComputeKernel::GetNamedPinsByDirection(
	EOptimusNodePinDirection InDirection
	) const
{
	TMap<FName, UOptimusNodePin *> FilteredPins;
	for (UOptimusNodePin *Pin: GetPins())
	{
		if (Pin->GetDirection() == InDirection)
		{
			FilteredPins.Add(Pin->GetFName(), Pin);
		}
	}
	return FilteredPins;
}
