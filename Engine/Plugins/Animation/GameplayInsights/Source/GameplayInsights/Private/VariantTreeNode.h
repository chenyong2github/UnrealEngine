// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimationProvider.h"

/** A wrapper around a variant value, used to display collections of values in a tree */
struct FVariantTreeNode : TSharedFromThis<FVariantTreeNode>
{
	FVariantTreeNode(const FText& InName, const FVariantValue& InValue)
		: Name(InName)
		, Value(InValue)
	{}

	const TSharedRef<FVariantTreeNode>& AddChild(const TSharedRef<FVariantTreeNode>& InChild)
	{
		check(!InChild->Parent.IsValid());
		InChild->Parent = SharedThis(this);
		return Children.Add_GetRef(InChild);
	}

	FText GetName() const { return Name; }

	const FVariantValue& GetValue() const { return Value; }

	TSharedPtr<FVariantTreeNode> GetParent() const { return Parent.Pin(); }

	const TArray<TSharedRef<FVariantTreeNode>>& GetChildren() const { return Children; }

	static TSharedRef<FVariantTreeNode> MakeHeader(const FText& InName)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::String;
		Value.String.Value = TEXT("");

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeBool(const FText& InName, bool InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Bool;
		Value.Bool.bValue = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeInt32(const FText& InName, int32 InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Int32;
		Value.Int32.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeFloat(const FText& InName, float InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Float;
		Value.Float.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeVector2D(const FText& InName, const FVector2D& InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Vector2D;
		Value.Vector2D.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}


	static TSharedRef<FVariantTreeNode> MakeVector(const FText& InName, const FVector& InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Vector;
		Value.Vector.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeString(const FText& InName, const TCHAR* InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::String;
		Value.String.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeObject(const FText& InName, uint64 InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Object;
		Value.Object.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

	static TSharedRef<FVariantTreeNode> MakeClass(const FText& InName, uint64 InValue)
	{
		FVariantValue Value;
		Value.Type = EAnimNodeValueType::Class;
		Value.Class.Value = InValue;

		return MakeShared<FVariantTreeNode>(InName, Value);
	}

private:
	FText Name;
	FVariantValue Value;
	TWeakPtr<FVariantTreeNode> Parent;
	TArray<TSharedRef<FVariantTreeNode>> Children;
};