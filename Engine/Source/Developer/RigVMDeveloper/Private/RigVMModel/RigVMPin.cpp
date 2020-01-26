// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

URigVMGraph* URigVMInjectionInfo::GetGraph() const
{
	return GetPin()->GetGraph();
}

URigVMPin* URigVMInjectionInfo::GetPin() const
{
	return CastChecked<URigVMPin>(GetOuter());
}

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

URigVMPin::URigVMPin()
	: Direction(ERigVMPinDirection::Invalid)
	, bIsExpanded(false)
	, bIsConstant(false)
	, bRequiresWatch(false)
	, CPPType(FString())
	, CPPTypeObject(nullptr)
	, CPPTypeObjectPath(NAME_None)
	, DefaultValue(FString())
{
}

FString URigVMPin::GetPinPath() const
{
	URigVMPin* ParentPin = GetParentPin();
	if(ParentPin)
	{
		return FString::Printf(TEXT("%s.%s"), *ParentPin->GetPinPath(), *GetName());
	}

	URigVMNode* Node = GetNode();
	if (Node == nullptr)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s.%s"), *Node->GetNodePath(), *GetName());
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
			if (TSharedPtr<FStructOnScope> DefaultStructScope = Injection->StructNode->ConstructStructInstance())
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

bool URigVMPin::IsConstant() const
{
	if (IsArrayElement())
	{
		return GetParentPin()->IsConstant();
	}
	return bIsConstant;
}

bool URigVMPin::RequiresWatch() const
{
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
	/*
	URigVMPin* PinForDefaultValue = URigVMCompiler::FollowPinForDefaultValue((URigVMPin*)this);
	if (PinForDefaultValue != this)
	{
		return PinForDefaultValue->GetDefaultValue();
	}
	*/

	if (IsArray())
	{
		if (SubPins.Num() > 0)
		{
			TArray<FString> ElementDefaultValues;
			for (URigVMPin* SubPin : SubPins)
			{
				FString ElementDefaultValue = SubPin->GetDefaultValue();
				if (SubPin->IsStringType())
				{
					ElementDefaultValue = TEXT("\"") + ElementDefaultValue + TEXT("\"");
				}
				else if (ElementDefaultValue.IsEmpty() || ElementDefaultValue == TEXT("()"))
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
				FString MemberDefaultValue = SubPin->GetDefaultValue();
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

bool URigVMPin::CanLink(URigVMPin* InSourcePin, URigVMPin* InTargetPin, FString* OutFailureReason)
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

	if (InSourcePin->GetNode() == InTargetPin->GetNode())
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

	if (InTargetPin->IsConstant() && !InSourcePin->IsConstant())
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
		return false;
	}

	// only allow to link to specified input / output pins on an injected node
	if (URigVMInjectionInfo* SourceInjectionInfo = InSourcePin->GetNode()->GetInjectionInfo())
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
	if (URigVMInjectionInfo* TargetInjectionInfo = InTargetPin->GetNode()->GetInjectionInfo())
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

	return true;
}
