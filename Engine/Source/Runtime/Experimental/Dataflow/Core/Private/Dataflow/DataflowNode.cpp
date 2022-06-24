// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNode.h"

#include "ChaosLog.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowArchive.h"



//
// Inputs
//

void FDataflowNode::AddInput(FDataflowConnection* InPtr)
{
	if (InPtr)
	{
		for (TPair<uint32, FDataflowConnection*> Elem : Inputs)
		{
			FDataflowConnection* In = Elem.Value;
			ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Input Failed: Existing Node input already defined with name (%s)"), *InPtr->GetName().ToString());
		}

		if (!Inputs.Contains(InPtr->Property->GetOffset_ForInternal()))
		{
			Inputs.Add(InPtr->Property->GetOffset_ForInternal(), InPtr);
		}
		else
		{
			Inputs[InPtr->Property->GetOffset_ForInternal()] = InPtr;
		}
	}
}

FDataflowInput* FDataflowNode::FindInput(FName InName)
{
	for (TPair<uint32, FDataflowConnection*> Elem : Inputs)
	{
		FDataflowConnection* Con = Elem.Value;
		if (Con->GetName().IsEqual(InName))
		{
			return (FDataflowInput*)Con;
		}
	}
	return nullptr;
}


const FDataflowInput* FDataflowNode::FindInput(const void* Reference) const
{
	for (TPair<uint32, FDataflowConnection*> Elem : Inputs)
	{
		FDataflowConnection* Con = Elem.Value;
		if (Con->RealAddress() == (size_t)Reference)
		{
			return (FDataflowInput*)Con;
		}
	}
	return nullptr;
}

FDataflowInput* FDataflowNode::FindInput(void* Reference)
{
	for (TPair<uint32, FDataflowConnection*> Elem : Inputs)
	{
		FDataflowConnection* Con = Elem.Value;
		if (Con->RealAddress() == (size_t)Reference)
		{
			return (FDataflowInput*)Con;
		}
	}
	return nullptr;
}

TArray< FDataflowConnection* > FDataflowNode::GetInputs() const
{
	TArray< FDataflowConnection* > Result;
	Inputs.GenerateValueArray(Result);
	return Result;
}

void FDataflowNode::ClearInputs()
{
	for (TPair<uint32, FDataflowConnection*> Elem : Inputs)
	{
		FDataflowConnection* Con = Elem.Value;
		delete Con;
	}
	Inputs.Reset();
}


//
// Outputs
//


void FDataflowNode::AddOutput(FDataflowConnection* InPtr)
{
	if (InPtr)
	{
		for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
		{
			FDataflowConnection* Out = Elem.Value;
			ensureMsgf(!Out->GetName().IsEqual(InPtr->GetName()), TEXT("Add Output Failed: Existing Node output already defined with name (%s)"), *InPtr->GetName().ToString());
		}

		if (!Outputs.Contains(InPtr->Property->GetOffset_ForInternal()))
		{
			Outputs.Add(InPtr->Property->GetOffset_ForInternal(), InPtr);
		}
		else
		{
			Outputs[InPtr->Property->GetOffset_ForInternal()] = InPtr;
		}
	}
}



FDataflowOutput* FDataflowNode::FindOutput(FName InName)
{
	for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
	{
		FDataflowConnection* Con = Elem.Value;
		if (Con->GetName().IsEqual(InName))
		{
			return (FDataflowOutput*)Con;
		}
	}
	return nullptr;
}

const FDataflowOutput* FDataflowNode::FindOutput(const void* Reference) const
{
	for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
	{
		FDataflowConnection* Con = Elem.Value;
		if (Con->RealAddress() == (size_t)Reference)
		{
			return (FDataflowOutput*)Con;
		}
	}
	return nullptr;
}

FDataflowOutput* FDataflowNode::FindOutput(void* Reference)
{
	for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
	{
		FDataflowConnection* Con = Elem.Value;
		if (Con->RealAddress() == (size_t)Reference)
		{
			return (FDataflowOutput*)Con;
		}
	}
	return nullptr;
}

TArray< FDataflowConnection* > FDataflowNode::GetOutputs() const
{
	TArray< FDataflowConnection* > Result;
	Outputs.GenerateValueArray(Result);
	return Result;
}


void FDataflowNode::ClearOutputs()
{
	for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
	{
		FDataflowConnection* Con = Elem.Value;
		delete Con;
	}
	Outputs.Reset();
}


TArray<Dataflow::FPin> FDataflowNode::GetPins() const
{
	TArray<Dataflow::FPin> RetVal;
	for (TPair<uint32, FDataflowConnection*> Elem : Inputs)
	{
		FDataflowConnection* Con = Elem.Value;
		RetVal.Add({ Dataflow::FPin::EDirection::INPUT,Con->GetType(), Con->GetName() });
	}
	for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
	{
		FDataflowConnection* Con = Elem.Value;
		RetVal.Add({ Dataflow::FPin::EDirection::OUTPUT,Con->GetType(), Con->GetName() });
	}
	return RetVal;
}

void FDataflowNode::InvalidateOutputs()
{
	for (TPair<uint32, FDataflowConnection*> Elem : Outputs)
	{
		FDataflowConnection* Con = Elem.Value;
		Con->Invalidate();
	}
}

void FDataflowNode::RegisterInputConnection(const void* Data)
{
	if (TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope()))
	{
		if (const UStruct* Struct = ScriptOnStruct->GetStruct())
		{
			for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				size_t RealAddress = (size_t)this + Property->GetOffset_ForInternal();
				if (RealAddress == (size_t)Data)
				{
					FName PropName(Property->GetName());
					FName PropType(Property->GetCPPType());
					AddInput(new FDataflowInput({ PropType, PropName, this, Property }));
				}
			}
		}
	}
}

void FDataflowNode::RegisterOutputConnection(const void* Data)
{
	if (TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope()))
	{
		if (const UStruct* Struct = ScriptOnStruct->GetStruct())
		{
			for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				size_t RealAddress = (size_t)this + Property->GetOffset_ForInternal();
				if (RealAddress == (size_t)Data)
				{
					FName PropName(Property->GetName());
					FName PropType(Property->GetCPPType());
					AddOutput(new FDataflowOutput({ PropType, PropName, this, Property }));
				}
			}
		}
	}
}


bool FDataflowNode::ValidateConnections()
{
	bValid = true;
	if (const TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope()))
	{
		if (const UStruct* Struct = ScriptOnStruct->GetStruct())
		{
			for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				FName PropName(Property->GetName());
#if WITH_EDITORONLY_DATA
				if (Property->HasMetaData(TEXT("DataflowInput")))
				{
					if (!FindInput(PropName))
					{
						UE_LOG(LogChaos, Warning, TEXT("Missing dataflow RegisterInputConnection in constructor for (%s:%s)"), *GetName().ToString(), *PropName.ToString())
						bValid = false;
					}
				}
				if (Property->HasMetaData(TEXT("DataflowOutput")))
				{
					if(!FindOutput(PropName))
					{
						UE_LOG(LogChaos, Warning, TEXT("Missing dataflow RegisterOutputConnection in constructor for (%s:%s)"), *GetName().ToString(),*PropName.ToString());
						bValid = false;
					}
				}
#endif
			}
		}
	}
	return bValid;
}



