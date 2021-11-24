// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMArrayNode.h"
#include "RigVMModel/RigVMGraph.h"

const FString URigVMArrayNode::ArrayName = TEXT("Array");
const FString URigVMArrayNode::NumName = TEXT("Num");
const FString URigVMArrayNode::IndexName = TEXT("Index");
const FString URigVMArrayNode::ElementName = TEXT("Element");
const FString URigVMArrayNode::SuccessName = TEXT("Success");
const FString URigVMArrayNode::OtherName = TEXT("Other");
const FString URigVMArrayNode::CloneName = TEXT("Clone");
const FString URigVMArrayNode::CountName = TEXT("Count");
const FString URigVMArrayNode::RatioName = TEXT("Ratio");
const FString URigVMArrayNode::ResultName = TEXT("Result");
const FString URigVMArrayNode::ContinueName = TEXT("Continue");
const FString URigVMArrayNode::CompletedName = TEXT("Completed");

URigVMArrayNode::URigVMArrayNode()
: OpCode(ERigVMOpCode::Invalid)
{
}

FString URigVMArrayNode::GetNodeTitle() const
{
	return GetNodeTitle(OpCode);
}

FString URigVMArrayNode::GetNodeTitle(ERigVMOpCode InOpCode)
{
	if(InOpCode == ERigVMOpCode::ArrayGetAtIndex)
	{
		static const FString ArrayGetAtIndexNodeTitle = TEXT("At"); 
		return ArrayGetAtIndexNodeTitle;
	}
	else if(InOpCode == ERigVMOpCode::ArraySetAtIndex)
	{
		static const FString ArraySetAtIndexNodeTitle = TEXT("Set At"); 
		return ArraySetAtIndexNodeTitle;
	}
	else if(InOpCode == ERigVMOpCode::ArrayGetNum)
	{
		static const FString ArrayGetNumNodeTitle = TEXT("Num"); 
		return ArrayGetNumNodeTitle;
	}
	else if(InOpCode == ERigVMOpCode::ArrayIterator)
	{
		static const FString ArrayIteratorNodeTitle = TEXT("For Each"); 
		return ArrayIteratorNodeTitle;
	}

	FString OpCodeString = StaticEnum<ERigVMOpCode>()->GetDisplayNameTextByValue((int64)InOpCode).ToString();
	OpCodeString.RemoveFromStart(URigVMArrayNode::ArrayName);
	return OpCodeString;
}

FText URigVMArrayNode::GetToolTipText() const
{
	switch(OpCode)
	{
		case ERigVMOpCode::ArrayReset:
		{
			return FText::FromString(TEXT("Removes all elements from an array.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayGetNum:
		{
			return FText::FromString(TEXT("Returns the number of elements of an array"));
		} 
		case ERigVMOpCode::ArraySetNum:
		{
			return FText::FromString(TEXT("Sets the numbers of elements of an array.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayGetAtIndex:
		{
			return FText::FromString(TEXT("Returns an element of an array by index."));
		}  
		case ERigVMOpCode::ArraySetAtIndex:
		{
			return FText::FromString(TEXT("Sets an element of an array by index.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayAdd:
		{
			return FText::FromString(TEXT("Adds an element to an array and returns the new element's index.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayInsert:
		{
			return FText::FromString(TEXT("Inserts an element into an array at a given index.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayRemove:
		{
			return FText::FromString(TEXT("Removes an element from an array by index.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayFind:
		{
			return FText::FromString(TEXT("Searchs a potential element in an array and returns its index."));
		}
		case ERigVMOpCode::ArrayAppend:
		{
			return FText::FromString(TEXT("Appends the another array to the main one.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayClone:
		{
			return FText::FromString(TEXT("Clones an array and returns a duplicate."));
		}
		case ERigVMOpCode::ArrayIterator:
		{
			return FText::FromString(TEXT("Loops over the elements in an array."));
		}
		case ERigVMOpCode::ArrayUnion:
		{
			return FText::FromString(TEXT("Merges two arrays while ensuring unique elements.\nModifies the input array."));
		}
		case ERigVMOpCode::ArrayDifference:
		{
			return FText::FromString(TEXT("Creates a new array containing the difference between the two input arrays.\nDifference here means elements that are only contained in either A or B."));
		}
		case ERigVMOpCode::ArrayIntersection:
		{
			return FText::FromString(TEXT("Creates a new array containing the intersection between the two input arrays.\nDifference here means elements that are contained in both A and B."));
		}
		case ERigVMOpCode::ArrayReverse:
		{
			return FText::FromString(TEXT("Reverses the order of the elements of an array.\nModifies the input array."));
		}
		default:
		{
			ensure(false);
			break;
		}
	}
	return Super::GetToolTipText();
}

bool URigVMArrayNode::IsLoopNode() const
{
	return GetOpCode() == ERigVMOpCode::ArrayIterator;
}

FText URigVMArrayNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	switch(OpCode)
	{
		case ERigVMOpCode::ArrayReset:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to be cleared."));
			}
			break;
		}
		case ERigVMOpCode::ArrayGetNum:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to retrieve the size for."));
			}
			else if(InPin->GetName() == URigVMArrayNode::NumName)
			{
				return FText::FromString(TEXT("The size of the input array."));
			}
			break;
		} 
		case ERigVMOpCode::ArraySetNum:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to set the size for."));
			}
			else if(InPin->GetName() == URigVMArrayNode::NumName)
			{
				return FText::FromString(TEXT("The new size of the array."));
			}
			break;
		}
		case ERigVMOpCode::ArrayGetAtIndex:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to retrieve an element from."));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index of the element to retrieve."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ElementName)
			{
				return FText::FromString(TEXT("The element at the given index."));
			}
			break;
		}  
		case ERigVMOpCode::ArraySetAtIndex:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to set an element for."));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index of the element to set."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ElementName)
			{
				return FText::FromString(TEXT("The new value for element to set."));
			}
			break;
		}
		case ERigVMOpCode::ArrayAdd:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to add an element to."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ElementName)
			{
				return FText::FromString(TEXT("The element to add to the array."));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index of the newly added element."));
			}
			break;
		}
		case ERigVMOpCode::ArrayInsert:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to insert an element into."));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index at which to insert the element."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ElementName)
			{
				return FText::FromString(TEXT("The element to insert into the array."));
			}
			break;
		}
		case ERigVMOpCode::ArrayRemove:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to remove an element from."));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index at which to remove the element."));
			}
			break;
		}
		case ERigVMOpCode::ArrayFind:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to search within."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ElementName)
			{
				return FText::FromString(TEXT("The element to look for."));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index of the found element (or -1)."));
			}
			else if(InPin->GetName() == URigVMArrayNode::SuccessName)
			{
				return FText::FromString(TEXT("True if the element has been found."));
			}
			break;
		}
		case ERigVMOpCode::ArrayAppend:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to append the other array to."));
			}
			else if(InPin->GetName() == URigVMArrayNode::OtherName)
			{
				return FText::FromString(TEXT("The second array to append to the first one."));
			}
			break;
		}
		case ERigVMOpCode::ArrayClone:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to clone"));
			}
			else if(InPin->GetName() == URigVMArrayNode::CloneName)
			{
				return FText::FromString(TEXT("The duplicate of the input array."));
			}
			break;
		}
		case ERigVMOpCode::ArrayIterator:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to iterate over"));
			}
			else if(InPin->GetName() == URigVMArrayNode::IndexName)
			{
				return FText::FromString(TEXT("The index of the current element."));
			}
			else if(InPin->GetName() == URigVMArrayNode::CountName)
			{
				return FText::FromString(TEXT("The count of all elements in the array."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ElementName)
			{
				return FText::FromString(TEXT("The current element."));
			}
			else if(InPin->GetName() == URigVMArrayNode::RatioName)
			{
				return FText::FromString(TEXT("A float ratio from 0.0 (first element) to 1.0 (last element)."));
			}
			else if(InPin->GetName() == URigVMArrayNode::CompletedName)
			{
				return FText::FromString(TEXT("The execute block to run once the loop has completed."));
			}
			break;
		}
		case ERigVMOpCode::ArrayUnion:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array to merge the other array with."));
			}
			else if(InPin->GetName() == URigVMArrayNode::OtherName)
			{
				return FText::FromString(TEXT("The second array to merge to the first one."));
			}
			break;
		}
		case ERigVMOpCode::ArrayDifference:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The first array to compare the other array with."));
			}
			else if(InPin->GetName() == URigVMArrayNode::OtherName)
			{
				return FText::FromString(TEXT("The second array to compare the other array with."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ResultName)
			{
				return FText::FromString(TEXT("The resulting difference array."));
			}
			break;
		}
		case ERigVMOpCode::ArrayIntersection:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The first array to compare the other array with."));
			}
			else if(InPin->GetName() == URigVMArrayNode::OtherName)
			{
				return FText::FromString(TEXT("The second array to compare the other array with."));
			}
			else if(InPin->GetName() == URigVMArrayNode::ResultName)
			{
				return FText::FromString(TEXT("The resulting intersecting array."));
			}
			break;
		}
		case ERigVMOpCode::ArrayReverse:
		{
			if(InPin->GetName() == URigVMArrayNode::ArrayName)
			{
				return FText::FromString(TEXT("The array reverse."));
			}
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
	return Super::GetToolTipTextForPin(InPin);
}

FLinearColor URigVMArrayNode::GetNodeColor() const
{
	return FLinearColor::White;
}

ERigVMOpCode URigVMArrayNode::GetOpCode() const
{
	return OpCode;
}

FString URigVMArrayNode::GetCPPType() const
{
	URigVMPin* ArrayPin = FindPin(ArrayName);
	if (ArrayPin == nullptr)
	{
		return FString();
	}
	return RigVMTypeUtils::BaseTypeFromArrayType(ArrayPin->GetCPPType());
}

UObject* URigVMArrayNode::GetCPPTypeObject() const
{
	URigVMPin* ArrayPin = FindPin(ArrayName);
	if (ArrayPin == nullptr)
	{
		return nullptr;
	}
	return ArrayPin->GetCPPTypeObject();
}
