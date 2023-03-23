// Copyright Epic Games, Inc. All Rights Reserved.
#include "ObjectColumn.h"
#include "ChooserPropertyAccess.h"

#if WITH_EDITOR
	#include "IPropertyAccessEditor.h"
#endif

bool FObjectContextProperty::GetValue(const UObject* ContextObject, FSoftObjectPath& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;

	if (UE::Chooser::ResolvePropertyChain(Container, StructType, Binding.PropertyBindingChain))
	{
		if (const FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(StructType, Binding.PropertyBindingChain.Last()))
		{
			// If the property has a value, then create a soft object path from it;
			// otherwise, it could be a weak pointer to something that is not loaded.
			const UObject* LoadedObject = ObjectProperty->GetObjectPropertyValue_InContainer(Container);
			if (LoadedObject != nullptr)
			{
				OutResult = LoadedObject;
				return true;
			}

			if (ObjectProperty->IsA<FSoftObjectProperty>())
			{
				const FSoftObjectPtr& SoftObjectPtr = *ObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
				OutResult = SoftObjectPtr.ToSoftObjectPath();
				return true;
			}
		}
	}

	return false;
}

#if WITH_EDITOR

void FObjectContextProperty::SetBinding(const TArray<FBindingChainElement>& InBindingChain)
{
	const UClass* PreviousClass = Binding.AllowedClass;
	Binding.AllowedClass = nullptr;

	UE::Chooser::CopyPropertyChain(InBindingChain, Binding.PropertyBindingChain);

	const FField* Field = InBindingChain.Last().Field.ToField();
	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Field))
	{
		Binding.AllowedClass = ObjectProperty->PropertyClass;
	}
}

#endif // WITH_EDITOR

FObjectColumn::FObjectColumn()
{
#if WITH_EDITOR
	InputValue.InitializeAs(FObjectContextProperty::StaticStruct());
#endif
}

bool FChooserObjectRowData::Evaluate(const FSoftObjectPath& LeftHandSide) const
{
	switch (Comparison)
	{
		case EObjectColumnCellValueComparison::MatchEqual:
			return LeftHandSide == Value.ToSoftObjectPath();

		case EObjectColumnCellValueComparison::MatchNotEqual:
			return LeftHandSide != Value.ToSoftObjectPath();

		case EObjectColumnCellValueComparison::MatchAny:
			return true;

		default:
			return false;
	}
}

void FObjectColumn::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) const
{
	FSoftObjectPath Result;
	if (ContextObject != nullptr &&
		InputValue.IsValid() &&
		InputValue.Get<FChooserParameterObjectBase>().GetValue(ContextObject, Result))
	{
		for (const uint32 Index : IndexListIn)
		{
			if (RowValues.IsValidIndex(Index))
			{
				const FChooserObjectRowData& RowValue = RowValues[Index];
				if (RowValue.Evaluate(Result))
				{
					IndexListOut.Emplace(Index);
				}
			}
		}
	}
	else
	{
		// passthrough fallback (behaves better during live editing)
		IndexListOut = IndexListIn;
	}
}
