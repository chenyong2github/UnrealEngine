// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMModel/RigVMGraphUtils.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMEditorModule.h"
#include "UObject/PropertyPortFlags.h"

FRigVMCompileSettings::FRigVMCompileSettings()
	: SurpressInfoMessage(true)
	, SurpressWarnings(false)
	, SurpressErrors(false)
{
}

URigVMCompiler::URigVMCompiler()
{
}

bool URigVMCompiler::Compile(URigVMGraph* InGraph, URigVM* OutVM)
{
	if (InGraph == nullptr)
	{
		ReportError(TEXT("Provided graph is nullptr."));
		return false;
	}
	if (OutVM == nullptr)
	{
		ReportError(TEXT("Provided vm is nullptr."));
		return false;
	}

	OutVM->Reset();

	FRigVMGraphUtils Utils(InGraph);

	TArray<URigVMNode*> Nodes, Cycle;
	if (!Utils.TopologicalSort(Nodes, Cycle))
	{
		if (Cycle.Num() > 0)
		{
			ReportErrorf(TEXT("Cycle detected!"));
		}
		return false;
	}

	FRigVMByteCode& ByteCode = OutVM->ByteCode;

	TMap<FString, FRigVMOperand> PinHashToOperand;
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(Node))
		{
			for (URigVMPin* Pin : Node->GetPins())
			{
				FRigVMOperand Operand = AddRegisterForPin(Pin, OutVM);
				PinHashToOperand.Add(GetPinHash(Pin), Operand);
			}
		}
		else if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
		{
			URigVMPin* ValuePin = Node->FindPin(TEXT("Value"));
			FString PinHash = GetPinHash(ValuePin);
			if (!PinHashToOperand.Contains(PinHash))
			{
				ERigVMParameterType ParameterType = ParameterNode->IsInput() ? ERigVMParameterType::Input : ERigVMParameterType::Output;
				FRigVMParameter Parameter;

				FString BaseCPPType = ValuePin->IsArray() ? ValuePin->GetArrayElementCppType() : ValuePin->GetCPPType();
				UScriptStruct* ScriptStruct = ParameterNode->GetScriptStruct();
				if (ScriptStruct == nullptr)
				{
					ScriptStruct = GetScriptStructForCPPType(BaseCPPType);
				}


				TArray<FString> DefaultValues;
				if (ValuePin->IsArray())
				{
					DefaultValues = URigVMController::SplitDefaultValue(ValuePin->GetDefaultValue());
				}
				else
				{
					DefaultValues.Add(ValuePin->GetDefaultValue());
				}

				if (ScriptStruct != nullptr)
				{
					TArray<uint8> Data;
					Data.AddUninitialized(ScriptStruct->GetStructureSize() * DefaultValues.Num());
					uint8* Ptr = Data.GetData();

					for (FString DefaultValue : DefaultValues)
					{
						ScriptStruct->InitializeStruct(Ptr, 1);
						if (!DefaultValue.IsEmpty())
						{
							ScriptStruct->ImportText(*DefaultValue, Ptr, nullptr, PPF_None, nullptr, ScriptStruct->GetName());
						}
						Ptr = Ptr + ScriptStruct->GetStructureSize();
					}

					Parameter = OutVM->AddStructParameter(ParameterType, ParameterNode->GetParameterName(), ScriptStruct, Data.GetData(), DefaultValues.Num());
					ScriptStruct->DestroyStruct(Data.GetData(), DefaultValues.Num());
				}
				else
				{
					//Parameter = OutVM->AddPlainParameter(ParameterType, ParameterNode->GetParameterName(), BaseCPPType, nullptr, GetElementSizeFromCPPType(BaseCPPType, ScriptStruct));
					if (BaseCPPType == TEXT("bool"))
					{
						TArray<bool> Values;
						for (FString DefaultValue : DefaultValues)
						{
							Values.Add((DefaultValue == TEXT("True")) || (DefaultValue == TEXT("true")) || (DefaultValue == TEXT("1")));
						}
						Parameter = OutVM->AddPlainParameter<bool>(ParameterType, ParameterNode->GetParameterName(), BaseCPPType, Values);
					}
					else if (BaseCPPType == TEXT("int32"))
					{
						TArray<int32> Values;
						for (FString DefaultValue : DefaultValues)
						{
							if (DefaultValue.IsEmpty())
							{
								Values.Add(0);
							}
							else
							{
								Values.Add(FCString::Atoi(*DefaultValue));
							}
						}
						Parameter = OutVM->AddPlainParameter<int32>(ParameterType, ParameterNode->GetParameterName(), BaseCPPType, Values);
					}
					else if (BaseCPPType == TEXT("float"))
					{
						TArray<float> Values;
						for (FString DefaultValue : DefaultValues)
						{
							if (DefaultValue.IsEmpty())
							{
								Values.Add(0.f);
							}
							else
							{
								Values.Add(FCString::Atof(*DefaultValue));
							}
						}
						Parameter = OutVM->AddPlainParameter<float>(ParameterType, ParameterNode->GetParameterName(), BaseCPPType, Values);
					}
					else if (BaseCPPType == TEXT("FName"))
					{
						TArray<FName> Values;
						for (FString DefaultValue : DefaultValues)
						{
							Values.Add(*DefaultValue);
						}
						Parameter = OutVM->AddPlainParameter<FName>(ParameterType, ParameterNode->GetParameterName(), BaseCPPType, Values);
					}
					else if (BaseCPPType == TEXT("FString"))
					{
						Parameter = OutVM->AddPlainParameter<FString>(ParameterType, ParameterNode->GetParameterName(), BaseCPPType, DefaultValues);
					}
					else
					{
						ensure(false);
					}
				}

				FRigVMOperand Operand = OutVM->WorkMemory.GetOperand(Parameter.GetRegisterIndex());
				PinHashToOperand.Add(PinHash, Operand);
			}
		}
		else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			URigVMPin* ValuePin = Node->FindPin(TEXT("Value"));
			if (ValuePin)
			{
				FString PinHash = GetPinHash(ValuePin);
				if (!PinHashToOperand.Contains(PinHash))
				{
					FRigVMOperand Operand = AddRegisterForPin(ValuePin, OutVM);
					PinHashToOperand.Add(PinHash, Operand);
				}
			}

			URigVMPin* ExecutePin = Node->FindPin(TEXT("Execute"));
			if (ExecutePin)
			{
				FString PinHash = GetPinHash(ExecutePin);
				if (!PinHashToOperand.Contains(PinHash))
				{
					FRigVMOperand Operand = AddRegisterForPin(ExecutePin, OutVM);
					PinHashToOperand.Add(PinHash, Operand);
				}
			}
		}
		else
		{
			ReportErrorf(TEXT("Unsupported node found: '%s'"), *Node->GetClass()->GetName());
		}
	}

	for (URigVMNode* Node : Nodes)
	{
		if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(Node))
		{
			int32 FunctionIndex = OutVM->AddRigVMFunction(StructNode->GetScriptStruct(), StructNode->GetMethodName());
			if (FunctionIndex == INDEX_NONE)
			{
				ReportErrorf(TEXT("Function cannot be found for '%s', method '%s'."), *StructNode->GetScriptStruct()->GetName(), *StructNode->GetMethodName().ToString());
			}

			TArray<FRigVMOperand> Operands;
			for (URigVMPin* Pin : Node->GetPins())
			{
				FRigVMOperand Operand = PinHashToOperand.FindChecked(GetPinHash(Pin));
				Operands.Add(Operand);
			}

			ByteCode.AddExecuteOp(FunctionIndex, Operands);
		}

		TArray<URigVMPin*> AllPins = Node->GetAllPinsRecursively();
		for (URigVMPin* Pin : AllPins)
		{
			for (URigVMLink* Link : Pin->GetLinks())
			{
				if (Link->GetSourcePin() != Pin)
				{
					continue;
				}

				URigVMPin* SourcePin = Link->GetSourcePin();
				URigVMPin* TargetPin = Link->GetTargetPin();
				URigVMPin* SourceRootPin = SourcePin->GetRootPin();
				URigVMPin* TargetRootPin = TargetPin->GetRootPin();

				if (!PinHashToOperand.Contains(GetPinHash(SourceRootPin)))
				{
					ensure(false);
					continue;
				}
				if (!PinHashToOperand.Contains(GetPinHash(TargetRootPin)))
				{
					ensure(false);
					continue;
				}

				FRigVMOperand SourceOperand = PinHashToOperand.FindChecked(GetPinHash(SourceRootPin));
				FRigVMOperand TargetOperand = PinHashToOperand.FindChecked(GetPinHash(TargetRootPin));

				FString SourceSegmentPath = SourcePin->GetSegmentPath();
				FString TargetSegmentPath = TargetPin->GetSegmentPath();
				int32 SourceArrayIndex = 0;
				int32 TargetArrayIndex = 0;

				if (!SourceSegmentPath.IsEmpty())
				{
					if (SourceRootPin->IsArray())
					{
						FString Left, Right;
						if (!SourceSegmentPath.Split(TEXT("."), &Left, &Right))
						{
							Left = SourceSegmentPath;
						}
						SourceArrayIndex = FCString::Atoi(*Left);
						SourceSegmentPath = Right;
					}

					// todo: map to different memories
					SourceOperand = OutVM->WorkMemory.GetOperand(SourceOperand.GetRegisterIndex(), SourceSegmentPath, SourceArrayIndex);
				}

				if (!TargetSegmentPath.IsEmpty())
				{
					if (TargetRootPin->IsArray())
					{
						FString Left, Right;
						if (!TargetSegmentPath.Split(TEXT("."), &Left, &Right))
						{
							Left = TargetSegmentPath;
						}
						TargetArrayIndex = FCString::Atoi(*Left);
						TargetSegmentPath = Right;
					}

					// todo: map to different memories
					TargetOperand = OutVM->WorkMemory.GetOperand(TargetOperand.GetRegisterIndex(), TargetSegmentPath, TargetArrayIndex);
				}

				ByteCode.AddCopyOp(SourceOperand, TargetOperand);
			}
		}
	}

	ByteCode.AddExitOp();

	//URigVMGraph
	// d) find all literals based on defaults and define them (for now flatten them - no sharing)
	// g) define all work state (for now no sharing)
	// h) build out the byte code the the required instructions
	// i) fix up the jump instructions for all blocks

	return true;
}

FString URigVMCompiler::GetPinHash(URigVMPin* InPin)
{
	URigVMNode* Node = InPin->GetNode();
	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value"))
		{
			return FString::Printf(TEXT("Parameter::%s"), *ParameterNode->GetParameterName().ToString());
		}
	}
	else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value"))
		{
			return FString::Printf(TEXT("Variable::%s"), *VariableNode->GetVariableName().ToString());
		}
	}
	return InPin->GetPinPath();
}

UScriptStruct* URigVMCompiler::GetScriptStructForCPPType(const FString& InCPPType)
{
	if (InCPPType == TEXT("FRotator"))
	{
		return TBaseStructure<FRotator>::Get();
	}
	if (InCPPType == TEXT("FQuat"))
	{
		return TBaseStructure<FQuat>::Get();
	}
	if (InCPPType == TEXT("FTransform"))
	{
		return TBaseStructure<FTransform>::Get();
	}
	if (InCPPType == TEXT("FLinearColor"))
	{
		return TBaseStructure<FLinearColor>::Get();
	}
	if (InCPPType == TEXT("FColor"))
	{
		return TBaseStructure<FColor>::Get();
	}
	if (InCPPType == TEXT("FPlane"))
	{
		return TBaseStructure<FPlane>::Get();
	}
	if (InCPPType == TEXT("FVector"))
	{
		return TBaseStructure<FVector>::Get();
	}
	if (InCPPType == TEXT("FVector2D"))
	{
		return TBaseStructure<FVector2D>::Get();
	}
	if (InCPPType == TEXT("FVector4"))
	{
		return TBaseStructure<FVector4>::Get();
	}
	return nullptr;
}

uint16 URigVMCompiler::GetElementSizeFromCPPType(const FString& InCPPType, UScriptStruct* InScriptStruct)
{
	if (InScriptStruct == nullptr)
	{
		InScriptStruct = GetScriptStructForCPPType(InCPPType);
	}
	if (InScriptStruct != nullptr)
	{
		return InScriptStruct->GetStructureSize();
	}
	if (InCPPType == TEXT("bool"))
	{
		return sizeof(bool);
	}
	else if (InCPPType == TEXT("int32"))
	{
		return sizeof(int32);
	}
	if (InCPPType == TEXT("float"))
	{
		return sizeof(float);
	}
	if (InCPPType == TEXT("FName"))
	{
		return sizeof(FName);
	}
	if (InCPPType == TEXT("FString"))
	{
		return sizeof(FString);
	}

	ensure(false);
	return 0;
}

FRigVMOperand URigVMCompiler::AddRegisterForPin(URigVMPin* InPin, URigVM* OutVM)
{
	FString BaseCPPType = InPin->IsArray() ? InPin->GetArrayElementCppType() : InPin->GetCPPType();
	FString Hash = GetPinHash(InPin);
	FName HashName = *Hash;

	TArray<FString> DefaultValues;
	if (InPin->IsArray())
	{
		DefaultValues = URigVMController::SplitDefaultValue(InPin->GetDefaultValue());
	}
	else
	{
		DefaultValues.Add(InPin->GetDefaultValue());
	}

	UScriptStruct* ScriptStruct = InPin->GetScriptStruct();
	if (ScriptStruct == nullptr)
	{
		ScriptStruct = GetScriptStructForCPPType(BaseCPPType);
	}

	FRigVMMemoryContainer& WorkMemory = OutVM->WorkMemory;
	
	if (ScriptStruct)
	{
		TArray<uint8> Data;
		Data.AddUninitialized(ScriptStruct->GetStructureSize() * DefaultValues.Num());
		uint8* Ptr = Data.GetData();

		for (FString DefaultValue : DefaultValues)
		{
			ScriptStruct->InitializeStruct(Ptr, 1);
			if (!DefaultValue.IsEmpty())
			{
				ScriptStruct->ImportText(*DefaultValue, Ptr, nullptr, PPF_None, nullptr, ScriptStruct->GetName());
			}
			Ptr = Ptr + ScriptStruct->GetStructureSize();
		}

		int32 Register = WorkMemory.AddStructArray(HashName, ScriptStruct, DefaultValues.Num(), Data.GetData(), 1);
		ScriptStruct->DestroyStruct(Data.GetData(), DefaultValues.Num());

		return WorkMemory.GetOperand(Register);
	}

	if (BaseCPPType == TEXT("bool"))
	{
		TArray<bool> Values;
		for (FString DefaultValue : DefaultValues)
		{
			Values.Add((DefaultValue == TEXT("True")) || (DefaultValue == TEXT("true")) || (DefaultValue == TEXT("1")));
		}
		int32 Register = WorkMemory.AddPlainArray<bool>(HashName, Values, 1);
		return WorkMemory.GetOperand(Register);
	}
	else if (BaseCPPType == TEXT("int32"))
	{
		TArray<int32> Values;
		for (FString DefaultValue : DefaultValues)
		{
			if (DefaultValue.IsEmpty())
			{
				Values.Add(0);
			}
			else
			{
				Values.Add(FCString::Atoi(*DefaultValue));
			}
		}
		int32 Register = WorkMemory.AddPlainArray<int32>(HashName, Values, 1);
		return WorkMemory.GetOperand(Register);
	}
	if (BaseCPPType == TEXT("float"))
	{
		TArray<float> Values;
		for (FString DefaultValue : DefaultValues)
		{
			if (DefaultValue.IsEmpty())
			{
				Values.Add(0.f);
			}
			else
			{
				Values.Add(FCString::Atof(*DefaultValue));
			}
		}
		int32 Register = WorkMemory.AddPlainArray<float>(HashName, Values, 1);
		return WorkMemory.GetOperand(Register);
	}
	if (BaseCPPType == TEXT("FName"))
	{
		TArray<FName> Values;
		for (FString DefaultValue : DefaultValues)
		{
			Values.Add(*DefaultValue);
		}
		int32 Register = WorkMemory.AddPlainArray<FName>(Values, 1);
		return WorkMemory.GetOperand(Register);
	}
	if (BaseCPPType == TEXT("FString"))
	{
		int32 Register = WorkMemory.AddPlainArray<FString>(HashName, DefaultValues, 1);
		return WorkMemory.GetOperand(Register);
	}

	ensure(false);
	return FRigVMOperand();
}

void URigVMCompiler::ReportInfo(const FString& InMessage)
{
	if (!Settings.SurpressInfoMessage)
	{
		UE_LOG(LogRigVMEditor, Display, TEXT("%s"), *InMessage);
	}
}

void URigVMCompiler::ReportWarning(const FString& InMessage)
{
	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *InMessage, *FString());
}

void URigVMCompiler::ReportError(const FString& InMessage)
{
	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *InMessage, *FString());
}
