// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/Nodes/RigVMPrototypeNode.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/OutputDevice.h"
#include "Misc/DefaultValueHelper.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"

class FRigVMPinDefaultValueImportErrorContext : public FOutputDevice
{
public:

	int32 NumErrors;

	FRigVMPinDefaultValueImportErrorContext()
		: FOutputDevice()
		, NumErrors(0)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		NumErrors++;
	}
};

URigVMGraph* URigVMInjectionInfo::GetGraph() const
{
	return GetPin()->GetGraph();
}

URigVMPin* URigVMInjectionInfo::GetPin() const
{
	return CastChecked<URigVMPin>(GetOuter());
}

const URigVMPin::FPinOverrideMap URigVMPin::EmptyPinOverrideMap;
const URigVMPin::FPinOverride URigVMPin::EmptyPinOverride = URigVMPin::FPinOverride(FRigVMASTProxy(), EmptyPinOverrideMap);
const FString URigVMPin::OrphanPinPrefix = TEXT("Orphan::");

bool URigVMPin::SplitPinPathAtStart(const FString& InPinPath, FString& LeftMost, FString& Right)
{
	return InPinPath.Split(TEXT("."), &LeftMost, &Right, ESearchCase::IgnoreCase, ESearchDir::FromStart);
}

bool URigVMPin::SplitPinPathAtEnd(const FString& InPinPath, FString& Left, FString& RightMost)
{
	return InPinPath.Split(TEXT("."), &Left, &RightMost, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
}

bool URigVMPin::SplitPinPath(const FString& InPinPath, TArray<FString>& Parts)
{
	int32 OriginalPartsCount = Parts.Num();
	FString PinPathRemaining = InPinPath;
	FString Left, Right;
	while(SplitPinPathAtStart(PinPathRemaining, Left, Right))
	{
		Parts.Add(Left);
		Left.Empty();
		PinPathRemaining = Right;
	}

	if (!Right.IsEmpty())
	{
		Parts.Add(Right);
	}

	return Parts.Num() > OriginalPartsCount;
}

FString URigVMPin::JoinPinPath(const FString& Left, const FString& Right)
{
	ensure(!Left.IsEmpty() && !Right.IsEmpty());
	return Left + TEXT(".") + Right;
}

FString URigVMPin::JoinPinPath(const TArray<FString>& InParts)
{
	if (InParts.Num() == 0)
	{
		return FString();
	}

	FString Result = InParts[0];
	for (int32 PartIndex = 1; PartIndex < InParts.Num(); PartIndex++)
	{
		Result += TEXT(".") + InParts[PartIndex];
	}

	return Result;
}

TArray<FString> URigVMPin::SplitDefaultValue(const FString& InDefaultValue)
{
	TArray<FString> Parts;
	if (InDefaultValue.IsEmpty())
	{
		return Parts;
	}

	ensure(InDefaultValue[0] == TCHAR('('));
	ensure(InDefaultValue[InDefaultValue.Len() - 1] == TCHAR(')')); 

	FString Content = InDefaultValue.Mid(1, InDefaultValue.Len() - 2);
	int32 BraceCount = 0;
	int32 QuoteCount = 0;

	int32 LastPartStartIndex = 0;
	for (int32 CharIndex = 0; CharIndex < Content.Len(); CharIndex++)
	{
		TCHAR Char = Content[CharIndex];
		if (QuoteCount > 0)
		{
			if (Char == TCHAR('"'))
			{
				QuoteCount = 0;
			}
		}
		else if (Char == TCHAR('"'))
		{
			QuoteCount = 1;
		}

		if (Char == TCHAR('('))
		{
			if (QuoteCount == 0)
			{
				BraceCount++;
			}
		}
		else if (Char == TCHAR(')'))
		{
			if (QuoteCount == 0)
			{
				BraceCount--;
				BraceCount = FMath::Max<int32>(BraceCount, 0);
			}
		}
		else if (Char == TCHAR(',') && BraceCount == 0 && QuoteCount == 0)
		{
			// ignore whitespaces
			Parts.Add(Content.Mid(LastPartStartIndex, CharIndex - LastPartStartIndex).Replace(TEXT(" "), TEXT("")));
			LastPartStartIndex = CharIndex + 1;
		}
	}

	if (!Content.IsEmpty())
	{
		// ignore whitespaces
		Parts.Add(Content.Mid(LastPartStartIndex).Replace(TEXT(" "), TEXT("")));
	}
	return Parts;
}

URigVMPin::URigVMPin()
	: Direction(ERigVMPinDirection::Invalid)
	, bIsExpanded(false)
	, bIsConstant(false)
	, bRequiresWatch(false)
	, bIsDynamicArray(false)
	, CPPType(FString())
	, CPPTypeObject(nullptr)
	, CPPTypeObjectPath(NAME_None)
	, DefaultValue(FString())
	, BoundVariablePath()
{
#if UE_BUILD_DEBUG
	CachedPinPath = GetPinPath();
#endif
}

FString URigVMPin::GetPinPath(bool bUseNodePath) const
{
	FString PinPath;

	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin)
	{
		PinPath = FString::Printf(TEXT("%s.%s"), *ParentPin->GetPinPath(), *GetName());
	}
	else
	{
		URigVMNode* Node = GetNode();
		if (Node != nullptr)
		{
			PinPath = FString::Printf(TEXT("%s.%s"), *Node->GetNodePath(bUseNodePath), *GetName());
		}
	}
#if UE_BUILD_DEBUG
	if (!bUseNodePath)
	{
		CachedPinPath = PinPath;
	}
#endif
	return PinPath;
}

FString URigVMPin::GetSegmentPath() const
{
	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin)
	{
		FString ParentSegmentPath = ParentPin->GetSegmentPath();
		if (ParentSegmentPath.IsEmpty())
		{
			return GetName();
		}
		return FString::Printf(TEXT("%s.%s"), *ParentSegmentPath, *GetName());
	}
	return FString();
}

void URigVMPin::GetExposedPinChain(TArray<const URigVMPin*>& OutExposedPins) const
{
	// Variable nodes do not share the operand with their source link
	if ((GetNode()->IsA<URigVMVariableNode>() || GetNode()->IsA<URigVMParameterNode>()) && GetDirection() == ERigVMPinDirection::Input)
	{
		OutExposedPins.Add(this);
		return;
	}
	
	// Find the first pin in the chain (source)
	for (URigVMLink* Link : GetSourceLinks())
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		check(SourcePin != nullptr);
		
		// If the source is on an entry node, add the pin and make a recursive call on the collapse node pin
		if (URigVMFunctionEntryNode* EntryNode = Cast<URigVMFunctionEntryNode>(SourcePin->GetNode()))
		{
			URigVMGraph* Graph = EntryNode->GetGraph();
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Graph->GetOuter()))
			{
				if(URigVMPin* CollapseNodePin = CollapseNode->FindPin(SourcePin->GetName()))
				{
					CollapseNodePin->GetExposedPinChain(OutExposedPins);
				}
			}
		}
		else if (URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(SourcePin->GetNode()))
		{
			URigVMGraph* Graph = ReturnNode->GetGraph();
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Graph->GetOuter()))
			{
				if(URigVMPin* CollapseNodePin = CollapseNode->FindPin(SourcePin->GetName()))
				{
					CollapseNodePin->GetExposedPinChain(OutExposedPins);
				}
			}
		}
		// Variable nodes do not share the operand with their source link
		else if (SourcePin->GetNode()->IsA<URigVMVariableNode>() || SourcePin->GetNode()->IsA<URigVMParameterNode>())
		{
			continue;
		}
		else
		{
			SourcePin->GetExposedPinChain(OutExposedPins);
		}

		return;
	}

	// Add the pins in the OutExposedPins array in depth-first order
	TSet<const URigVMPin*> FoundPins;
	TArray<const URigVMPin*> ToProcess;
	ToProcess.Push(this);
	while (!ToProcess.IsEmpty())
	{
		const URigVMPin* Current = ToProcess.Pop();
		if (FoundPins.Contains(Current))
		{
			continue;
		}
		FoundPins.Add(Current);
		OutExposedPins.Add(Current);

		// Add target pins connected to the current pin
		for (URigVMLink* Link : Current->GetTargetLinks())
		{
			URigVMPin* TargetPin = Link->GetTargetPin();

			// Variable nodes do not share the operand with their source link
			if (TargetPin->GetNode()->IsA<URigVMVariableNode>() || TargetPin->GetNode()->IsA<URigVMParameterNode>())
			{
				continue;
			}
			ToProcess.Push(TargetPin);
		}

		// If pin is on a collapse node, add entry pin
		if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Current->GetNode()))
		{
			URigVMFunctionEntryNode* EntryNode = CollapseNode->GetEntryNode();
			URigVMPin* EntryPin = EntryNode->FindPin(Current->GetName());
			if (EntryPin)
			{
				ToProcess.Push(EntryPin);
			}
		}
		// If pin is on a return node, add parent pin on collapse node
		else if (URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(Current->GetNode()))
		{
			URigVMGraph* Graph = ReturnNode->GetGraph();
			if (URigVMCollapseNode* ParentNode = Cast<URigVMCollapseNode>(Graph->GetOuter()))
			{
				URigVMPin* CollapseNodePin = ParentNode->FindPin(Current->GetName());
				ToProcess.Push(CollapseNodePin);
			}
		}
	}		
}

FName URigVMPin::GetDisplayName() const
{
	if (DisplayName == NAME_None)
	{
		return GetFName();
	}

	if (InjectionInfos.Num() > 0)
	{
		FString ProcessedDisplayName = DisplayName.ToString();

		for (URigVMInjectionInfo* Injection : InjectionInfos)
		{
			if (TSharedPtr<FStructOnScope> DefaultStructScope = Injection->UnitNode->ConstructStructInstance())
			{
				FRigVMStruct* DefaultStruct = (FRigVMStruct*)DefaultStructScope->GetStructMemory();
				ProcessedDisplayName = DefaultStruct->ProcessPinLabelForInjection(ProcessedDisplayName);
			}
		}

		return *ProcessedDisplayName;
	}

	return DisplayName;
}

ERigVMPinDirection URigVMPin::GetDirection() const
{
	return Direction;
}

bool URigVMPin::IsExpanded() const
{
	return bIsExpanded;
}

bool URigVMPin::IsDefinedAsConstant() const
{
	if (IsArrayElement())
	{
		return GetParentPin()->IsDefinedAsConstant();
	}
	return bIsConstant;
}

bool URigVMPin::RequiresWatch(const bool bCheckExposedPinChain) const
{
	if (!bRequiresWatch && bCheckExposedPinChain)
	{
		TArray<const URigVMPin*> VirtualPins;
		GetExposedPinChain(VirtualPins);
		
		for (const URigVMPin* VirtualPin : VirtualPins)
		{
			if (VirtualPin->bRequiresWatch)
			{
				return true;
			}
		}		
	}
	
	return bRequiresWatch;
}

bool URigVMPin::IsStruct() const
{
	if (IsArray())
	{
		return false;
	}
	return GetScriptStruct() != nullptr;
}

bool URigVMPin::IsStructMember() const
{
	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin == nullptr)
	{
		return false;
	}
	return ParentPin->IsStruct();
}

bool URigVMPin::IsArray() const
{
	return CPPType.StartsWith(TEXT("TArray"));
}

bool URigVMPin::IsArrayElement() const
{
	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin == nullptr)
	{
		return false;
	}
	return ParentPin->IsArray();
}

bool URigVMPin::IsDynamicArray() const
{
	return bIsDynamicArray;
}

int32 URigVMPin::GetPinIndex() const
{
	int32 Index = INDEX_NONE;

	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin != nullptr)
	{
		ParentPin->GetSubPins().Find((URigVMPin*)this, Index);
	}
	else
	{
		URigVMNode* Node = GetNode();
		if (Node != nullptr)
		{
			Node->GetPins().Find((URigVMPin*)this, Index);
		}
	}
	return Index;
}

void URigVMPin::SetNameFromIndex()
{
	LowLevelRename(*FString::FormatAsNumber(GetPinIndex()));
}

int32 URigVMPin::GetArraySize() const
{
	return SubPins.Num();
}

FString URigVMPin::GetCPPType() const
{
	return CPPType;
}

FString URigVMPin::GetArrayElementCppType() const
{
	if (!IsArray())
	{
		return FString();
	}
	return CPPType.Mid(7, CPPType.Len() - 8);
}

bool URigVMPin::IsStringType() const
{
	return CPPType.Equals(TEXT("FString")) || CPPType.Equals(TEXT("FName"));
}

bool URigVMPin::IsExecuteContext() const
{
	if (UScriptStruct* ScriptStruct = GetScriptStruct())
	{
		if (ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
		{
			return true;
		}
	}
	return false;
}

FString URigVMPin::GetDefaultValue() const
{
	return GetDefaultValue(EmptyPinOverride);
}

FString URigVMPin::GetDefaultValue(const URigVMPin::FPinOverride& InOverride) const
{
	if (FPinOverrideValue const* OverrideValuePtr = InOverride.Value.Find(InOverride.Key.GetSibling((URigVMPin*)this)))
	{
		return OverrideValuePtr->DefaultValue;
	}

	if (IsArray())
	{
		if (SubPins.Num() > 0)
		{
			TArray<FString> ElementDefaultValues;
			for (URigVMPin* SubPin : SubPins)
			{
				FString ElementDefaultValue = SubPin->GetDefaultValue(InOverride);
				if (SubPin->IsStringType())
				{
					ElementDefaultValue = TEXT("\"") + ElementDefaultValue + TEXT("\"");
				}
				else if (ElementDefaultValue.IsEmpty())
				{
					continue;
				}
				ElementDefaultValues.Add(ElementDefaultValue);
			}
			if (ElementDefaultValues.Num() == 0)
			{
				return TEXT("()");
			}
			return FString::Printf(TEXT("(%s)"), *FString::Join(ElementDefaultValues, TEXT(",")));
		}

		return DefaultValue.IsEmpty() ? TEXT("()") : DefaultValue;
	}
	else if (IsStruct())
	{
		if (SubPins.Num() > 0)
		{
			TArray<FString> MemberDefaultValues;
			for (URigVMPin* SubPin : SubPins)
			{
				FString MemberDefaultValue = SubPin->GetDefaultValue(InOverride);
				if (SubPin->IsStringType())
				{
					MemberDefaultValue = TEXT("\"") + MemberDefaultValue + TEXT("\"");
				}
				else if (MemberDefaultValue.IsEmpty() || MemberDefaultValue == TEXT("()"))
				{
					continue;
				}
				MemberDefaultValues.Add(FString::Printf(TEXT("%s=%s"), *SubPin->GetName(), *MemberDefaultValue));
			}
			if (MemberDefaultValues.Num() == 0)
			{
				return TEXT("()");
			}
			return FString::Printf(TEXT("(%s)"), *FString::Join(MemberDefaultValues, TEXT(",")));
		}

		return DefaultValue.IsEmpty() ? TEXT("()") : DefaultValue;
	}

	return DefaultValue;
}

bool URigVMPin::IsValidDefaultValue(const FString& InDefaultValue) const
{
	TArray<FString> DefaultValues; 

	if (IsArray())
	{
		DefaultValues = URigVMPin::SplitDefaultValue(InDefaultValue);
	}
	else
	{
		DefaultValues.Add(InDefaultValue);
	}

	FString BaseCPPType = GetCPPType().Replace(TEXT("TArray<"), TEXT("")).Replace(TEXT(">"), TEXT(""));

	for (const FString& Value : DefaultValues)
	{
		// perform single value validation
		if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(GetCPPTypeObject()))
		{
			FRigVMPinDefaultValueImportErrorContext ErrorPipe;
			TArray<uint8> TempStructBuffer;
			TempStructBuffer.AddUninitialized(ScriptStruct->GetStructureSize());
			ScriptStruct->InitializeDefaultValue(TempStructBuffer.GetData());

			{
				// force logging to the error pipe for error detection
				LOG_SCOPE_VERBOSITY_OVERRIDE(LogExec, ELogVerbosity::Verbose); 
				ScriptStruct->ImportText(*Value, TempStructBuffer.GetData(), nullptr, PPF_None, &ErrorPipe, ScriptStruct->GetName()); 
			}

			ScriptStruct->DestroyStruct(TempStructBuffer.GetData());

			if (ErrorPipe.NumErrors > 0)
			{
				return false;
			}
		} 
		else if (UEnum* EnumType = Cast<UEnum>(GetCPPTypeObject()))
		{
			FName EnumName(EnumType->GenerateFullEnumName(*Value));
			if (!EnumType->IsValidEnumName(EnumName))
			{
				return false;
			}
			else
			{
				if (EnumType->HasMetaData(TEXT("Hidden"), EnumType->GetIndexByName(EnumName)))
				{
					return false;
				}
			}
		} 
		else if (BaseCPPType == TEXT("float"))
		{ 
			if (!FDefaultValueHelper::IsStringValidFloat(Value))
			{
				return false;
			}
		}
		else if (BaseCPPType == TEXT("int32"))
		{ 
			if (!FDefaultValueHelper::IsStringValidInteger(Value))
			{
				return false;
			}
		}
		else if (BaseCPPType == TEXT("bool"))
		{
			if (Value != TEXT("True") && Value != TEXT("False"))
			{
				return false;
			}
		}
		else if (BaseCPPType == TEXT("FString"))
		{ 
			// anything is allowed
		}
		else if (BaseCPPType == TEXT("FName"))
		{
			// anything is allowed
		}
	}

	return true;
}

FName URigVMPin::GetCustomWidgetName() const
{
	if (IsArrayElement())
	{
		return GetParentPin()->GetCustomWidgetName();
	}
	return CustomWidgetName;
}

FText URigVMPin::GetToolTipText() const
{
	if(URigVMNode* Node = GetNode())
	{
		return Node->GetToolTipTextForPin(this);
	}
	return FText();
}

UObject* URigVMPin::FindObjectFromCPPTypeObjectPath(const FString& InObjectPath)
{
	if (InObjectPath.IsEmpty())
	{
		return nullptr;
	}

	if (InObjectPath == FName(NAME_None).ToString())
	{
		return nullptr;
	}

	// we do this to avoid ambiguous searches for 
	// common names such as "transform" or "vector"
	UPackage* Package = ANY_PACKAGE;
	FString PackageName;
	FString CPPTypeObjectName = InObjectPath;
	if (InObjectPath.Split(TEXT("."), &PackageName, &CPPTypeObjectName))
	{
		Package = FindPackage(nullptr, *PackageName);
	}
	
	if (UObject* ObjectWithinPackage = FindObject<UObject>(Package, *CPPTypeObjectName))
	{
		return ObjectWithinPackage;
	}

	return FindObject<UObject>(ANY_PACKAGE, *InObjectPath);
}

// Returns the variable bound to this pin (or NAME_None)
const FString& URigVMPin::GetBoundVariablePath() const
{
	return GetBoundVariablePath(EmptyPinOverride);
}

// Returns the variable bound to this pin (or NAME_None)
const FString& URigVMPin::GetBoundVariablePath(const URigVMPin::FPinOverride& InOverride) const
{
	if (FPinOverrideValue const* OverrideValuePtr = InOverride.Value.Find(InOverride.Key.GetSibling((URigVMPin*)this)))
	{
		return OverrideValuePtr->BoundVariablePath;
	}
	return BoundVariablePath;
}

// Returns the variable bound to this pin (or NAME_None)
FString URigVMPin::GetBoundVariableName() const
{
	return GetBoundVariableName(EmptyPinOverride);
}

// Returns the variable bound to this pin (or NAME_None)
FString URigVMPin::GetBoundVariableName(const URigVMPin::FPinOverride& InOverride) const
{
	FString VariableName = GetBoundVariablePath(InOverride);
	BoundVariablePath.Split(TEXT("."), &VariableName, nullptr);
	return VariableName;
}

// Returns true if this pin is bound to a variable
bool URigVMPin::IsBoundToVariable() const
{
	return IsBoundToVariable(EmptyPinOverride);
}

// Returns true if this pin is bound to a variable
bool URigVMPin::IsBoundToVariable(const URigVMPin::FPinOverride& InOverride) const
{
	return !GetBoundVariablePath(InOverride).IsEmpty();
}

bool URigVMPin::CanBeBoundToVariable(const FRigVMExternalVariable& InExternalVariable, const FRigVMRegisterOffset& InOffset) const
{
	if (!InExternalVariable.IsValid(true))
	{
		return false;
	}

	if (bIsConstant)
	{
		return false;
	}

	// only allow to bind variables to input pins for now
	if (Direction == ERigVMPinDirection::Output)
	{
		return false;
	}

	// check type validity
	// in the future we need to allow arrays as well
	if (IsArray() && InOffset.IsValid())
	{
		return false;
	}
	if (IsArray() != InExternalVariable.bIsArray)
	{
		return false;
	}

	FString ExternalCPPType = InExternalVariable.TypeName.ToString();
	UObject* ExternalCPPTypeObject = InExternalVariable.TypeObject;

	if (InOffset.IsValid())
	{
		ExternalCPPType = InOffset.GetCPPType().ToString();
		ExternalCPPTypeObject = InOffset.GetScriptStruct();
	}

	if (GetCPPTypeObject() != nullptr)
	{
		if (GetCPPTypeObject() != ExternalCPPTypeObject)
		{
			return false;
		}
	}
	else
	{
		FString CPPBaseType = IsArray() ? GetArrayElementCppType() : GetCPPType();
		if (CPPBaseType != ExternalCPPType)
		{
			return false;
		}
	}

	return true;
}

bool URigVMPin::ShowInDetailsPanelOnly() const
{
#if WITH_EDITOR
	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(GetNode()))
	{
		if (GetParentPin() == nullptr)
		{
			if (UScriptStruct* ScriptStruct = UnitNode->GetScriptStruct())
			{
				if (FProperty* Property = ScriptStruct->FindPropertyByName(GetFName()))
				{
					if (Property->HasMetaData(FRigVMStruct::DetailsOnlyMetaName))
					{
						return true;
					}
				}
			}
		}
	}
#endif
	return false;
}

// Returns nullptr external variable matching this description
FRigVMExternalVariable URigVMPin::ToExternalVariable() const
{
	FRigVMExternalVariable ExternalVariable;

	FString VariableName = GetBoundVariableName();
	if (VariableName.IsEmpty())
	{
		FString NodeName, PinPath;
		if (!SplitPinPathAtStart(GetPinPath(), NodeName, VariableName))
		{
			return ExternalVariable;
		}

		VariableName = VariableName.Replace(TEXT("."), TEXT("_"));
	}
	ExternalVariable.Name = *VariableName;

	if (CPPType.StartsWith(TEXT("TArray<")))
	{
		ExternalVariable.bIsArray = true;
		ExternalVariable.TypeName = *CPPType.Mid(7, CPPType.Len() - 8);
		ExternalVariable.TypeObject = CPPTypeObject;
	}
	else
	{
		ExternalVariable.bIsArray = false;
		ExternalVariable.TypeName = *CPPType;
		ExternalVariable.TypeObject = CPPTypeObject;
	}

	ExternalVariable.bIsPublic = false;
	ExternalVariable.bIsReadOnly = false;
	ExternalVariable.Memory = nullptr;

	return ExternalVariable;
}

bool URigVMPin::IsOrphanPin() const
{
	if(URigVMPin* RootPin = GetRootPin())
	{
		if(RootPin != this)
		{
			return RootPin->IsOrphanPin();
		}
	}
	return GetNode()->OrphanedPins.Contains(this); 
}

void URigVMPin::UpdateCPPTypeObjectIfRequired() const
{
	if (CPPTypeObject == nullptr)
	{
		if (CPPTypeObjectPath != NAME_None)
		{
			URigVMPin* MutableThis = (URigVMPin*)this;
			MutableThis->CPPTypeObject = FindObjectFromCPPTypeObjectPath(CPPTypeObjectPath.ToString());
		}
	}
}

UObject* URigVMPin::GetCPPTypeObject() const
{
	UpdateCPPTypeObjectIfRequired();
	return CPPTypeObject;
}

UScriptStruct* URigVMPin::GetScriptStruct() const
{
	return Cast<UScriptStruct>(GetCPPTypeObject());
}

UEnum* URigVMPin::GetEnum() const
{
	return Cast<UEnum>(GetCPPTypeObject());
}

URigVMPin* URigVMPin::GetParentPin() const
{
	return Cast<URigVMPin>(GetOuter());
}

URigVMPin* URigVMPin::GetRootPin() const
{
	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin == nullptr)
	{
		return const_cast<URigVMPin*>(this);
	}
	return ParentPin->GetRootPin();
}

bool URigVMPin::IsRootPin() const
{
	return GetParentPin() == nullptr;
}

URigVMPin* URigVMPin::GetPinForLink() const
{
	URigVMPin* RootPin = GetRootPin();

	if (RootPin->InjectionInfos.Num() == 0)
	{
		return const_cast<URigVMPin*>(this);
	}

	URigVMPin* PinForLink =
		((Direction == ERigVMPinDirection::Input) || (Direction == ERigVMPinDirection::IO)) ?
		RootPin->InjectionInfos.Last()->InputPin :
		RootPin->InjectionInfos.Last()->OutputPin;

	if (RootPin != this)
	{
		FString SegmentPath = GetSegmentPath();
		return PinForLink->FindSubPin(SegmentPath);
	}

	return PinForLink;
}

URigVMPin* URigVMPin::GetOriginalPinFromInjectedNode() const
{
	if (URigVMInjectionInfo* Injection = GetNode()->GetInjectionInfo())
	{
		URigVMPin* RootPin = GetRootPin();
		URigVMPin* OriginalPin = nullptr;
		if (Injection->bInjectedAsInput && Injection->InputPin == RootPin)
		{
			TArray<URigVMPin*> LinkedPins = Injection->OutputPin->GetLinkedTargetPins();
			if (LinkedPins.Num() == 1)
			{
				OriginalPin = LinkedPins[0]->GetOriginalPinFromInjectedNode();
			}
		}
		else if (!Injection->bInjectedAsInput && Injection->OutputPin == RootPin)
		{
			TArray<URigVMPin*> LinkedPins = Injection->InputPin->GetLinkedSourcePins();
			if (LinkedPins.Num() == 1)
			{
				OriginalPin = LinkedPins[0]->GetOriginalPinFromInjectedNode();
			}
		}

		if (OriginalPin)
		{
			if (this != RootPin)
			{
				OriginalPin = OriginalPin->FindSubPin(GetSegmentPath());
			}
			return OriginalPin;
		}
	}

	return const_cast<URigVMPin*>(this);
}

const TArray<URigVMPin*>& URigVMPin::GetSubPins() const
{
	return SubPins;
}

URigVMPin* URigVMPin::FindSubPin(const FString& InPinPath) const
{
	FString Left, Right;
	if (!URigVMPin::SplitPinPathAtStart(InPinPath, Left, Right))
	{
		Left = InPinPath;
	}

	for (URigVMPin* Pin : SubPins)
	{
		if (Pin->GetName() == Left)
		{
			if (Right.IsEmpty())
			{
				return Pin;
			}
			return Pin->FindSubPin(Right);
		}
	}
	return nullptr;
}

bool URigVMPin::IsLinkedTo(URigVMPin* InPin) const
{
	for (URigVMLink* Link : Links)
	{
		if (Link->GetSourcePin() == InPin || Link->GetTargetPin() == InPin)
		{
			return true;
		}
	}
	return false;
}

const TArray<URigVMLink*>& URigVMPin::GetLinks() const
{
	return Links;
}

TArray<URigVMPin*> URigVMPin::GetLinkedSourcePins(bool bRecursive) const
{
	TArray<URigVMPin*> Pins;
	for (URigVMLink* Link : Links)
	{
		if (Link->GetTargetPin() == this)
		{
			Pins.AddUnique(Link->GetSourcePin());
		}
	}

	if (bRecursive)
	{
		for (URigVMPin* SubPin : SubPins)
		{
			Pins.Append(SubPin->GetLinkedSourcePins(bRecursive));
		}
	}

	return Pins;
}

TArray<URigVMPin*> URigVMPin::GetLinkedTargetPins(bool bRecursive) const
{
	TArray<URigVMPin*> Pins;
	for (URigVMLink* Link : Links)
	{
		if (Link->GetSourcePin() == this)
		{
			Pins.AddUnique(Link->GetTargetPin());
		}
	}

	if (bRecursive)
	{
		for (URigVMPin* SubPin : SubPins)
		{
			Pins.Append(SubPin->GetLinkedTargetPins(bRecursive));
		}
	}

	return Pins;
}

TArray<URigVMLink*> URigVMPin::GetSourceLinks(bool bRecursive) const
{
	TArray<URigVMLink*> Results;
	for (URigVMLink* Link : Links)
	{
		if (Link->GetTargetPin() == this)
		{
			Results.Add(Link);
		}
	}

	if (bRecursive)
	{
		for (URigVMPin* SubPin : SubPins)
		{
			Results.Append(SubPin->GetSourceLinks(bRecursive));
		}
	}

	return Results;
}

TArray<URigVMLink*> URigVMPin::GetTargetLinks(bool bRecursive) const
{
	TArray<URigVMLink*> Results;
	for (URigVMLink* Link : Links)
	{
		if (Link->GetSourcePin() == this)
		{
			Results.Add(Link);
		}
	}

	if (bRecursive)
	{
		for (URigVMPin* SubPin : SubPins)
		{
			Results.Append(SubPin->GetTargetLinks(bRecursive));
		}
	}

	return Results;
}

URigVMNode* URigVMPin::GetNode() const
{
	URigVMPin* ParentPin = GetParentPin();
	if (ParentPin)
	{
		return ParentPin->GetNode();
	}

	URigVMNode* Node = Cast<URigVMNode>(GetOuter());
	if(Node)
	{
		return Node;
	}

	return nullptr;
}

URigVMGraph* URigVMPin::GetGraph() const
{
	URigVMNode* Node = GetNode();
	if(Node)
	{
		return Node->GetGraph();
	}

	return nullptr;
}

bool URigVMPin::CanLink(URigVMPin* InSourcePin, URigVMPin* InTargetPin, FString* OutFailureReason, const FRigVMByteCode* InByteCode)
{
	if (InSourcePin == nullptr || InTargetPin == nullptr)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("One of the pins is nullptr.");
		}
		return false;
	}

	if (InSourcePin == InTargetPin)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Source and target pins are the same.");
		}
		return false;
	}
	
	URigVMNode* SourceNode = InSourcePin->GetNode();
	URigVMNode* TargetNode = InTargetPin->GetNode();
	if (SourceNode == TargetNode)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Source and target pins are on the same node.");
		}
		return false;
	}

	if (InSourcePin->GetGraph() != InTargetPin->GetGraph())
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Source and target pins are in different graphs.");
		}
		return false;
	}

	if (InSourcePin->Direction != ERigVMPinDirection::Output &&
		InSourcePin->Direction != ERigVMPinDirection::IO)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Source pin is not an output.");
		}
		return false;
	}

	if (InTargetPin->Direction != ERigVMPinDirection::Input &&
		InTargetPin->Direction != ERigVMPinDirection::IO)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Target pin is not an input.");
		}
		return false;
	}

	if (InTargetPin->IsDefinedAsConstant() && !InSourcePin->IsDefinedAsConstant())
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Cannot connect non-constants to constants.");
		}
		return false;
	}

	if (InSourcePin->CPPType != InTargetPin->CPPType)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Source and target pin types are not compatible.");
		}

		// check if this might be a prototype node
		if (InSourcePin->CPPType.IsEmpty())
		{
			if (URigVMPrototypeNode* PrototypeNode = Cast<URigVMPrototypeNode>(SourceNode))
			{
				if (PrototypeNode->SupportsType(InSourcePin, InTargetPin->CPPType))
				{
					if (OutFailureReason)
					{
						*OutFailureReason = FString();
					}
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else if (InTargetPin->CPPType.IsEmpty())
		{
			if (URigVMPrototypeNode* PrototypeNode = Cast<URigVMPrototypeNode>(TargetNode))
			{
				if (PrototypeNode->SupportsType(InTargetPin, InSourcePin->CPPType))
				{
					if (OutFailureReason)
					{
						*OutFailureReason = FString();
					}
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	if(!SourceNode->AllowsLinksOn(InSourcePin))
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Node doesn't allow links on this pin.");
		}
		return false;
	}

	if(!TargetNode->AllowsLinksOn(InTargetPin))
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Node doesn't allow links on this pin.");
		}
		return false;
	}

	// only allow to link to specified input / output pins on an injected node
	if (URigVMInjectionInfo* SourceInjectionInfo = SourceNode->GetInjectionInfo())
	{
		if (SourceInjectionInfo->OutputPin != InSourcePin->GetRootPin())
		{
			if (OutFailureReason)
			{
				*OutFailureReason = TEXT("Cannot link to a non-exposed pin on an injected node.");
			}
			return false;
		}
	}

	// only allow to link to specified input / output pins on an injected node
	if (URigVMInjectionInfo* TargetInjectionInfo = TargetNode->GetInjectionInfo())
	{
		if (TargetInjectionInfo->InputPin != InTargetPin->GetRootPin())
		{
			if (OutFailureReason)
			{
				*OutFailureReason = TEXT("Cannot link to a non-exposed pin on an injected node.");
			}
			return false;
		}
	}

	if (InSourcePin->IsLinkedTo(InTargetPin))
	{
		if (OutFailureReason)
		{
			*OutFailureReason = TEXT("Source and target pins are already connected.");
		}
		return false;
	}

	TArray<URigVMNode*> SourceNodes;
	SourceNodes.Add(SourceNode);

	if (InByteCode)
	{
		int32 TargetNodeInstructionIndex = InByteCode->GetFirstInstructionIndexForSubject(TargetNode);
		if (TargetNodeInstructionIndex != INDEX_NONE)
		{
			for (int32 SourceNodeIndex = 0; SourceNodeIndex < SourceNodes.Num(); SourceNodeIndex++)
			{
				bool bNodeCanLinkAnywhere = SourceNodes[SourceNodeIndex]->IsA<URigVMRerouteNode>();
				if (!bNodeCanLinkAnywhere)
				{
					if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(SourceNodes[SourceNodeIndex]))
					{
						// pure / immutable nodes can be connected to any input in any order.
						// since a new link is going to change the abstract syntax tree 
						if (!UnitNode->IsMutable())
						{
							bNodeCanLinkAnywhere = true;
						}
					}
				}

				if (!bNodeCanLinkAnywhere)
				{
					int32 SourceNodeInstructionIndex = InByteCode->GetFirstInstructionIndexForSubject(SourceNodes[SourceNodeIndex]);
					if (SourceNodeInstructionIndex != INDEX_NONE &&
						SourceNodeInstructionIndex > TargetNodeInstructionIndex)
					{
						return false;
					}
					SourceNodes.Append(SourceNodes[SourceNodeIndex]->GetLinkedSourceNodes());
				}
			}
		}
	}

	return true;
}
