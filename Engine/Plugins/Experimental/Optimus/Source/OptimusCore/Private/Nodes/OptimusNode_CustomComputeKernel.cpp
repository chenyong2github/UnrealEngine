// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_CustomComputeKernel.h"

#include "OptimusComputeDataInterface.h"
#include "OptimusNodePin.h"
#include "OptimusHelpers.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusCoreModule.h"
#include "OptimusNodeGraph.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"


UOptimusNode_CustomComputeKernel::UOptimusNode_CustomComputeKernel()
{
	EnableDynamicPins();
	UpdatePreamble();
}


FString UOptimusNode_CustomComputeKernel::GetKernelName() const
{
	return KernelName;
}

FString UOptimusNode_CustomComputeKernel::GetKernelSourceText() const
{
	return GetCookedKernelSource(ShaderSource.ShaderText, KernelName, ThreadCount);
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
#endif
}


#if WITH_EDITOR
void UOptimusNode_CustomComputeKernel::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent
	)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	static const FName ParametersName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, Parameters);
	static const FName InputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, InputBindings);
	static const FName OutputBindingsName = GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, OutputBindings);

	const FName BasePropertyName = (PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None);
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
	{
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, KernelName))
		{
			SetDisplayName(FText::FromString(KernelName));
			UpdatePreamble();
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UOptimusNode_CustomComputeKernel, ThreadCount))
		{
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
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusMultiLevelDataDomain, LevelNames))
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

			if (!InputBindings.IsEmpty())
			{
				BeforePin = GetPins()[Parameters.Num() - 1];
			}
		}
		else if (BasePropertyName == InputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Input;
			Binding = &InputBindings.Last();
			Name = FName("Input");

			StorageConfig = FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex});
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			Direction = EOptimusNodePinDirection::Output;
			Binding = &OutputBindings.Last();
			Name = FName("Output");

			StorageConfig = FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex});
		}

		if (ensure(Binding))
		{
			Binding->Name = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodePin::StaticClass(), Name);
			Binding->DataType = FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass());

			AddPin(Binding->Name, Direction, StorageConfig, Binding->DataType, BeforePin);

			UpdatePreamble();
		}
	}
	else if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ArrayRemove)
	{
		auto GetFilteredPins = [this](EOptimusNodePinDirection InDirection, EOptimusNodePinStorageType InStorageType)
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
		};

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
			
			for (const FOptimus_ShaderBinding& Binding: InputBindings)
			{
				RemovedPins.Remove(Binding.Name);
			}
		}
		else if (BasePropertyName == OutputBindingsName)
		{
			RemovedPins = GetFilteredPins(EOptimusNodePinDirection::Output, EOptimusNodePinStorageType::Resource);
			
			for (const FOptimus_ShaderBinding& Binding: OutputBindings)
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
}
#endif

void UOptimusNode_CustomComputeKernel::PostLoad()
{
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
	for (const FOptimusParameterBinding& Binding: InputBindings)
	{
		const FOptimusNodePinStorageConfig StorageConfig(Binding.DataDomain.LevelNames);
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Input, StorageConfig, Binding.DataType);
	}
	for (const FOptimusParameterBinding& Binding: OutputBindings)
	{
		const FOptimusNodePinStorageConfig StorageConfig(Binding.DataDomain.LevelNames);
		AddPinDirect(Binding.Name, EOptimusNodePinDirection::Output, StorageConfig, Binding.DataType);
	}
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
		for (const FOptimus_ShaderBinding& Binding: InputBindings)
		{
			DataTypes.Add(Binding.DataType.Resolve());
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderBinding& Binding: OutputBindings)
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
		for (const FOptimus_ShaderBinding& Binding: InputBindings)
		{
			Names.Add(Binding.Name);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimus_ShaderBinding& Binding: OutputBindings)
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
				FName NewName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodePin::StaticClass(), Names[Index]);

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
					InputBindings[Index - Parameters.Num()].Name = Names[Index];
				}
			}
		}
		else if (InPinDirection == EOptimusNodePinDirection::Output)
		{
			for (int32 Index = 0; Index < Names.Num(); Index++)
			{
				OutputBindings[Index].Name = Names[Index];
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
		for (const FOptimusParameterBinding& Binding: InputBindings)
		{
			PinDataDomains.Add(Binding.DataDomain.LevelNames);
		}
	}
	else if (InPinDirection == EOptimusNodePinDirection::Output)
	{
		for (const FOptimusParameterBinding& Binding: OutputBindings)
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
	CollectStructs(InputBindings);
	CollectStructs(OutputBindings);
	
	TArray<FString> Declarations;

	for (const FOptimus_ShaderBinding& Binding: Parameters)
	{
		Declarations.Add(FString::Printf(TEXT("%s Read%s();"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString()));
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
	TArray<FOptimusParameterBinding> Bindings = InputBindings;
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
		
		Declarations.Add(FString::Printf(TEXT("%s Read%s(%s);"),
			*Binding.DataType->ShaderValueType->ToString(), *Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", "))));
	}

	Bindings = OutputBindings;
	Bindings.Sort(ContextsPredicate);
	for (const FOptimusParameterBinding& Binding: Bindings)
	{
		AddCountFunctionIfNeeded(Binding.DataDomain.LevelNames);
		
		TArray<FString> Indexes;
		for (FString IndexName: GetIndexNamesFromDataDomainLevels(Binding.DataDomain.LevelNames))
		{
			Indexes.Add(FString::Printf(TEXT("uint %s"), *IndexName));
		}
		
		Declarations.Add(FString::Printf(TEXT("void Write%s(%s, %s Value);"),
			*Binding.Name.ToString(), *FString::Join(Indexes, TEXT(", ")), *Binding.DataType->ShaderValueType->ToString()));
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
