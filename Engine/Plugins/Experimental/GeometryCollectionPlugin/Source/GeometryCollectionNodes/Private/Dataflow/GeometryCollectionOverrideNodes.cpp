// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionOverrideNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/Engine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionOverrideNodes)

namespace Dataflow
{
	void GeometryCollectionOverrideNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetFloatOverrideFromAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetIntOverrideFromAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetBoolOverrideFromAssetDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetStringOverrideFromAssetDataflowNode);

		// Override
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Override", FLinearColor(1.f, 0.4f, 0.4f), CDefaultNodeBodyTintColor);
	}
}


void FGetFloatOverrideFromAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Float))
	{
		float NewValue = FCString::Atof(*GetDefaultValue(Context));

		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			const FString ValueFromAsset = GetValueFromAsset(Context, EngineContext->Owner);

			if (!ValueFromAsset.IsEmpty() && ValueFromAsset.IsNumeric())
			{
				NewValue = FCString::Atof(*ValueFromAsset);
			}
		}

		SetValue(Context, NewValue, &Float);
	}
}

void FGetIntOverrideFromAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Int))
	{
		int32 NewValue = FCString::Atoi(*GetDefaultValue(Context));

		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			const FString ValueFromAsset = GetValueFromAsset(Context, EngineContext->Owner);

			if (!ValueFromAsset.IsEmpty() && ValueFromAsset.IsNumeric())
			{
				NewValue = FCString::Atoi(*ValueFromAsset);
			}
		}

		SetValue(Context, NewValue, &Int);
	}
}

static bool StringToBool(const FString& InString, bool InDefault)
{
	bool Result = InDefault;

	if (!InString.IsEmpty())
	{
		if (InString.IsNumeric())
		{
			Result = FCString::Atoi(*InString) == 0 ? false : true;
		}
		else
		{
			Result = !InString.Compare("false") ? false : !InString.Compare("true") ? true : InDefault;
		}
	}

	return Result;
}

void FGetBoolOverrideFromAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Bool))
	{
		bool NewValue = StringToBool(GetDefaultValue(Context), false);

		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			const FString ValueFromAsset = GetValueFromAsset(Context, EngineContext->Owner);

			if (!ValueFromAsset.IsEmpty())
			{
				NewValue = StringToBool(ValueFromAsset, false);
			}
		}

		SetValue(Context, NewValue, &Bool);
	}
}

void FGetStringOverrideFromAssetDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&String))
	{
		FString NewValue = GetDefaultValue(Context);

		if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			const FString ValueFromAsset = GetValueFromAsset(Context, EngineContext->Owner);

			if (!ValueFromAsset.IsEmpty())
			{
				NewValue = ValueFromAsset;
			}
		}

		SetValue(Context, NewValue, &String);
	}
}
