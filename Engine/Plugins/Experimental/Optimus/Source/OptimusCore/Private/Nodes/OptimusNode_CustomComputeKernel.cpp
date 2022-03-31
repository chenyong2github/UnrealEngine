// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_CustomComputeKernel.h"

#include "OptimusObjectVersion.h"

#include "OptimusComputeDataInterface.h"
#include "OptimusNodePin.h"
#include "OptimusHelpers.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusCoreModule.h"
#include "OptimusNodeGraph.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"

static const FName ParametersName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, Parameters);;
static const FName InputBindingsName= GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, InputBindingArray);;
static const FName OutputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, OutputBindingArray);;

UOptimusNode_CustomComputeKernel::UOptimusNode_CustomComputeKernel()
{
	EnableDynamicPins();
	UpdatePreamble();
}


FString UOptimusNode_CustomComputeKernel::GetKernelName() const
{
	return KernelName;
}

FIntVector UOptimusNode_CustomComputeKernel::GetGroupSize() const
{
	return GroupSize;
}

FString UOptimusNode_CustomComputeKernel::GetKernelSourceText() const
{
	return GetCookedKernelSource(GetPathName(), ShaderSource.ShaderText, KernelName, GroupSize);
}


void UOptimusNode_CustomComputeKernel::SetCompilationDiagnostics(
	const TArray<FOptimusCompilerDiagnostic>& InDiagnostics
	)
{
	ShaderSource.Diagnostics = InDiagnostics;
	
	EOptimusDiagnosticLevel NodeLevel = EOptimusDiagnosticLevel::None;
	for (const FOptimusCompilerDiagnostic& Diagnostic: InDiagnostics)
	{
		if (Diagnostic.Level > NodeLevel)
		{
			NodeLevel = Diagnostic.Level;
		}
	}
	SetDiagnosticLevel(NodeLevel);

#if WITH_EDITOR
	FProperty* DiagnosticsProperty = FOptimusShaderText::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(FOptimusShaderText, Diagnostics));
	if (ensure(DiagnosticsProperty))
	{
		FPropertyChangedEvent PropertyChangedEvent(DiagnosticsProperty, EPropertyChangeType::ValueSet, {this});
		PostEditChangeProperty(PropertyChangedEvent);
	}

	OnDiagnosticsUpdatedEvent.Broadcast();
#endif
}

FString UOptimusNode_CustomComputeKernel::GetNameForShaderTextEditor() const
{
	return KernelName;
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

const TArray<FOptimusCompilerDiagnostic>& UOptimusNode_CustomComputeKernel::GetCompilationDiagnostics() const
{
	return ShaderSource.Diagnostics;
}


FString UOptimusNode_CustomComputeKernel::GetBindingDeclaration(FName BindingName) const
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
			SetDisplayName(FText::FromString(KernelName));
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimus_ShaderBinding, Name))
		{
			if (BasePropertyName == ParametersName || BasePropertyName == InputBindingsName)
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
			if (BasePropertyName == ParametersName || BasePropertyName == InputBindingsName)
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
			if (BasePropertyName == ParametersName || BasePropertyName == InputBindingsName)
			{
				UpdatePinDataDomains(EOptimusNodePinDirection::Input);
			}
			else if (BasePropertyName == OutputBindingsName)
			{
				UpdatePinDataDomains(EOptimusNodePinDirection::Output);
			}
			UpdatePreamble();
		}
		else if (PropertyName == ParametersName || PropertyName == InputBindingsName || PropertyName == OutputBindingsName)
		{
			RefreshBindingPins(PropertyName);
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayAdd)
	{
		EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;
		FOptimus_ShaderBinding *Binding = nullptr;
		FName Name;
		UOptimusNodePin *BeforePin = nullptr;
		FOptimusNodePinStorageConfig StorageConfig;

		if (BasePropertyName == ParametersName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &Parameters.Last();
			Name = FName("Param");
			StorageConfig = {};

			if (!InputBindingArray.IsEmpty())
			{
				BeforePin = GetPins()[Parameters.Num() - 1];
			}
		}
		else if (BasePropertyName == InputBindingsName)
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

		if (ensure(Binding))
		{
			Binding->Name = Optimus::GetUniqueNameForScope(this, Name);
			Binding->DataType = FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass());

			AddPin(Binding->Name, Direction, StorageConfig, Binding->DataType, BeforePin);

			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayRemove)
	{
		TMap<FName, UOptimusNodePin *> RemovedPins;
		if (BasePropertyName == ParametersName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Input, EOptimusNodePinStorageType::Value);
			
			for (const FOptimus_ShaderBinding& Binding: Parameters)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}
		else if (BasePropertyName == InputBindingsName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Input, EOptimusNodePinStorageType::Resource);
			
			for (const FOptimus_ShaderBinding& Binding: InputBindingArray)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Output, EOptimusNodePinStorageType::Resource);
			
			for (const FOptimus_ShaderBinding& Binding: OutputBindingArray)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}

		if (ensure(RemovedPins.Num() == 1))
		{
			RemovePin(RemovedPins.CreateIterator().Value());

			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayClear)
	{
		ClearBindingPins(BasePropertyName);
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::Unspecified)
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
	
	Super::PostLoad();
}


void UOptimusNode_CustomComputeKernel::ConstructNode()
{
	// After a duplicate, the kernel node has no pins, so we need to reconstruct them from
	// the bindings. We can assume that all naming clashes have already been dealt with.
	for (const FOptimus_ShaderBinding& Binding: Parameters)
	{
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Input, {}, Binding.DataType);
	}
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
	
	TArray<FOptimus_ShaderBinding>* ShaderBindingArray = nullptr;
	TArray<FOptimusParameterBinding>* ParameterBindingArray = nullptr;
	EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Input;;
	EOptimusNodePinStorageType StorageType = EOptimusNodePinStorageType::Value;;
	UOptimusNodePin* BeforePin = nullptr;
	if (InBindingPropertyName == ParametersName)
	{
		Direction = EOptimusNodePinDirection::Input;
		StorageType = EOptimusNodePinStorageType::Value;
		ShaderBindingArray = &Parameters;
		if (!InputBindingArray.IsEmpty())
		{
			BeforePin = GetPins()[Parameters.Num()];
		}	
	}
	else if (InBindingPropertyName == InputBindingsName)
	{
		Direction = EOptimusNodePinDirection::Input;
		StorageType = EOptimusNodePinStorageType::Resource;
		ParameterBindingArray = &InputBindingArray.InnerArray;
	}
	else if (InBindingPropertyName == OutputBindingsName)
	{
		Direction = EOptimusNodePinDirection::Output;
		StorageType = EOptimusNodePinStorageType::Resource;
		ParameterBindingArray = &OutputBindingArray.InnerArray;
	}

	const TMap<FName, UOptimusNodePin *> RemovedPins = GetFilteredPins(Direction, StorageType);

	// Save the links and readd them later when new pins are created
	TMap<FName, TArray<UOptimusNodePin*>> ConnectedPinsMap;
	for (const TTuple<FName, UOptimusNodePin*>& Pin : RemovedPins.Array())
	{
		ConnectedPinsMap.FindOrAdd(Pin.Key) = Pin.Value->GetConnectedPins();
	}
	
	ClearBindingPins(InBindingPropertyName);

	TArray<UOptimusNodePin*> AddedPins;
	if (ShaderBindingArray)
	{
		for (const FOptimus_ShaderBinding& Binding: *ShaderBindingArray)
		{
			AddedPins.Add(AddPin(Binding.Name, Direction, {}, Binding.DataType, BeforePin));
		}
	}
	else if (ParameterBindingArray)
	{
		for (const FOptimusParameterBinding& Binding: *ParameterBindingArray)
		{
			AddedPins.Add(AddPin(Binding.Name, Direction, Binding.DataDomain.LevelNames, Binding.DataType, nullptr));
		}
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
	EOptimusNodePinStorageType StorageType = EOptimusNodePinStorageType::Value;;
	if (InBindingPropertyName == ParametersName)
	{
		Direction = EOptimusNodePinDirection::Input;
		StorageType = EOptimusNodePinStorageType::Value;
	}
	else if (InBindingPropertyName == InputBindingsName)
	{
		Direction = EOptimusNodePinDirection::Input;
		StorageType = EOptimusNodePinStorageType::Resource;	
	}
	else if (InBindingPropertyName == OutputBindingsName)
	{
		Direction = EOptimusNodePinDirection::Output;
		StorageType = EOptimusNodePinStorageType::Resource;	
	}


	const TMap<FName, UOptimusNodePin *> RemovedPins = GetFilteredPins(Direction, StorageType);
	for (const TTuple<FName, UOptimusNodePin*>& Pin : RemovedPins.Array())
	{
		RemovePin(Pin.Value);
	}
		
	UpdatePreamble();
}

bool UOptimusNode_CustomComputeKernel::IsParameterBinding(FName InBindingPropertyName)
{
	if (InBindingPropertyName == ParametersName
	|| InBindingPropertyName == InputBindingsName
	|| InBindingPropertyName == OutputBindingsName)
	{
		return true;
	}
	return false;
}


void UOptimusNode_CustomComputeKernel::UpdatePinTypes(
	EOptimusNodePinDirection InPinDirection
	)
{
	TArray<FOptimusDataTypeHandle> DataTypes;

	if (InPinDirection == EOptimusNodePinDirection::Input)
	{
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
		for (const FOptimus_ShaderBinding& Binding: InputBindingArray)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderBinding& Binding: OutputBindingArray)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}	
	
	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> KernelPins = GetKernelPins(InPinDirection);

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
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			Names.Add(Binding.Name);
		}
		for (const FOptimus_ShaderBinding& Binding: InputBindingArray)
		{
			Names.Add(Binding.Name);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderBinding& Binding: OutputBindingArray)
		{
			Names.Add(Binding.Name);
		}
	}	
	
	// Let's try and figure out which pin got changed.
	TArray<UOptimusNodePin*> KernelPins = GetKernelPins(InPinDirection);

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
				if (Index < Parameters.Num())
				{
					Parameters[Index].Name = Names[Index];
				}
				else
				{
					InputBindingArray[Index - Parameters.Num()].Name = Names[Index];
				}
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
		for (const FOptimus_ShaderBinding& Binding: Parameters)
		{
			PinDataDomains.Add({});
		}
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
	const TArray<UOptimusNodePin *> KernelPins = GetKernelPins(InPinDirection);

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
		for (const FOptimus_ShaderBinding &Binding: BindingArray)
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

	CollectStructs(Parameters);
	CollectStructs(InputBindingArray);
	CollectStructs(OutputBindingArray);
	
	TArray<FString> Declarations;

	for (const FOptimus_ShaderBinding& Binding: Parameters)
	{
		Declarations.Add(GetDeclarationForBinding(Binding));
	}
	if (!Parameters.IsEmpty())
	{
		Declarations.AddDefaulted();
	}

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
		if (!SeenDataDomains.Contains(InContextNames))
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

TMap<FName, UOptimusNodePin*> UOptimusNode_CustomComputeKernel::GetFilteredPins(
	EOptimusNodePinDirection InDirection,
	EOptimusNodePinStorageType InStorageType) const
{
	TMap<FName, UOptimusNodePin *> FilteredPins;
	for (UOptimusNodePin *Pin: GetPins())
	{
		if (Pin->GetDirection() == InDirection && Pin->GetStorageType() == InStorageType)
		{
			FilteredPins.Add(Pin->GetFName(), Pin);
		}
	}
	return FilteredPins;
}

FString UOptimusNode_CustomComputeKernel::GetDeclarationForBinding(const FOptimus_ShaderBinding& Binding) 
{
	return FString::Printf(TEXT("%s Read%s();"),
				*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString());	
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
	return FString::Printf(TEXT("void Write%s(%s, %s Value);"),
				*Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", ")), *Binding.DataType->ShaderValueType->ToString());
}


TArray<UOptimusNodePin*> UOptimusNode_CustomComputeKernel::GetKernelPins(
	EOptimusNodePinDirection InPinDirection
	) const
{
	TArray<UOptimusNodePin*> KernelPins;
	for (UOptimusNodePin* Pin : GetPins())
	{
		if (InPinDirection == EOptimusNodePinDirection::Unknown || Pin->GetDirection() == InPinDirection)
		{
			KernelPins.Add(Pin);
		}
	}

	return KernelPins;
}
