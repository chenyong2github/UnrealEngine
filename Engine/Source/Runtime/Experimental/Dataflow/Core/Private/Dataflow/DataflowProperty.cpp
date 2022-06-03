// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowProperty.h"
#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	FProperty* FProperty::NewProperty(EType InType, FName InName, FNode* InNode)
	{
		switch (InType)
		{
		case EType::BOOL:
			return new TProperty<bool>({ InName,bool(), InNode });
		case EType::INT:
			return new TProperty<int32>({ InName, int32(), InNode });
		case EType::FLOAT:
			return new TProperty<float>({ InName, float(), InNode });
		case EType::DOUBLE:
			return new TProperty<double>({ InName, double(), InNode });
		case EType::STRING:
			return new TProperty<FString>({ InName, FString(), InNode });
		case EType::NAME:
			return new TProperty<FName>({ InName, FName(), InNode });
		default:
			return nullptr;
		}
	}

	void FProperty::BindProperty(FNode* InNode)
	{
		if (InNode)
		{
			InNode->AddProperty(this);
		}
	}
}

