// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNode.h"

#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowProperty.h"

namespace Dataflow
{

	void FNode::AddProperty(FProperty* InPtr)
	{
		for (FProperty* In : Properties)
		{
			ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Property Failed: Existing Node property already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Properties.Add(InPtr);
	}

	void FNode::AddInput(FConnection* InPtr)
	{ 
		for (FConnection* In : Inputs)
		{
			ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Input Failed: Existing Node input already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Inputs.Add(InPtr); 
	}

	void FNode::AddOutput(FConnection* InPtr)
	{ 
		for (FConnection* Out : Outputs)
		{
			ensureMsgf(!Out->GetName().IsEqual(InPtr->GetName()), TEXT("Add Output Failed: Existing Node output already defined with name (%s)"), *InPtr->GetName().ToString());
		}
		Outputs.Add(InPtr);
	}



	TArray<FPin> FNode::GetPins() const
	{
		TArray<FPin> RetVal;
		for (FConnection* Con : Inputs)
			RetVal.Add({ FPin::EDirection::INPUT,Con->GetType(), Con->GetName() });
		for (FConnection* Con : Outputs)
			RetVal.Add({ FPin::EDirection::OUTPUT,Con->GetType(), Con->GetName() });
		return RetVal;
	}

	void FNode::InvalidateOutputs()
	{
		for(FConnection * Output :  Outputs)
		{
			Output->Invalidate();
		}
	}

	FConnection* FNode::FindInput(FName InName) const
	{
		for (FConnection* Input : Inputs)
		{
			if (Input->GetName().IsEqual(InName))
			{
				return Input;
			}
		}
		return nullptr;
	}

	FConnection* FNode::FindOutput(FName InName) const
	{
		for (FConnection* Output : Outputs)
		{
			if (Output->GetName().IsEqual(InName))
			{
				return Output;
			}
		}
		return nullptr;
	}

	TMap<FName, FProperty*> FNode::GetPropertyMap()
	{
		TMap<FName, FProperty*> Map;
		for (FProperty* Prop : GetProperties())
		{
			Map.Add(TTuple<FName, FProperty*>(Prop->GetName(),Prop));
		}
		return Map;
	}

	const TMap<FName, const FProperty*> FNode::GetPropertyMap() const
	{
		TMap<FName, const FProperty*> Map;
		for (FProperty* Prop : GetProperties())
		{
			Map.Add(TTuple<FName, const FProperty*>(Prop->GetName(), Prop));
		}
		return Map;
	}

	void FNode::SerializeInternal(FArchive& Ar)
	{
		TMap<FName, FProperty*> Map = GetPropertyMap();
		TArray< FProperty* >& CurrentProperties = GetProperties();
		uint8 NumProperties = CurrentProperties.Num();
		Ar << NumProperties;

		for (int32 i = NumProperties - 1; i >= 0; i--)
		{
			uint8 Type;
			FString StrName;
			int64 PropertySizeOf = 0, PropertyDataSize = 0;

			if (Ar.IsSaving())
			{
				FProperty* Prop = CurrentProperties[i];
				Type = (uint8)Prop->GetType();
				StrName = Prop->GetName().ToString();
				PropertySizeOf = Prop->SizeOf();
				Ar << Type << StrName << PropertySizeOf;

				// The following code serializes the size of the PropertyData to serialize so that when loading,
				// we can skip over it if the function could not be loaded.
				// Serialize a temporary value for the delta in order to end up with an archive of the right size.
				// Then serialize the PropertyData in order to get its size.
				const int64 PropertyDataSizePos = Ar.Tell();
				PropertyDataSize = 0;
				Ar << PropertyDataSize;

				const int64 PropBegin = Ar.Tell();
				Prop->Serialize(Ar);

				// Only go back and serialize the number of argument bytes if there is actually an underlying buffer to seek to.
				// Come back to the temporary value we wrote and overwrite it with the PropertyData size we just calculated.
				// And finally seek back to the end.
				if (PropertyDataSizePos != INDEX_NONE)
				{
					const int64 PropEnd = Ar.Tell();
					PropertyDataSize = (PropEnd - PropBegin);
					Ar.Seek(PropertyDataSizePos);
					Ar << PropertyDataSize;
					Ar.Seek(PropEnd);
				}
			}
			else if (Ar.IsLoading())
			{

				Ar << Type << StrName << PropertySizeOf << PropertyDataSize;
				FName PropName(StrName);

				if (FProperty* NewProperty = FProperty::NewProperty((FProperty::EType)Type, PropName))
				{
					if (Map.Contains(PropName) && Map[PropName]->GetType() == (FProperty::EType)Type)
					{
						Map[PropName]->Serialize(Ar);
						delete NewProperty;
					}
					else
					{
						NewProperty->Serialize(Ar);

						if (!Map.Contains(PropName))
						{
							AddProperty(NewProperty);
						}
						else
						{
							delete NewProperty;
						}
					}
				}
				else
				{
					Ar.Seek(Ar.Tell() + PropertyDataSize);
				}
			}
		}
	}

}

