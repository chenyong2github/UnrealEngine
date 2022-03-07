// Copyright Epic Games, Inc. All Rights Reserved.

#include "FieldNotification/FieldMulticastDelegate.h"


namespace UE::FieldNotification
{

FDelegateHandle FFieldMulticastDelegate::Add(const UObject* InObject, FFieldId InFieldId, FDelegate InNewDelegate)
{
	FDelegateHandle Result = InNewDelegate.GetHandle();
	FInvocationKey InvocationKey{ InObject, InFieldId };
	if (DelegateLockCount > 0)
	{
		const int32 Index = Delegates.Emplace(FInvocationElement{ InvocationKey, MoveTemp(InNewDelegate) });
		AddedEmplaceAt = FMath::Min(AddedEmplaceAt, (uint16)Index);
	}
	else
	{
		const int32 FoundIndex = UpperBound(InvocationKey);
		Delegates.EmplaceAt(FoundIndex, FInvocationElement{ InvocationKey, MoveTemp(InNewDelegate) });
	}
	return Result;
}


FFieldMulticastDelegate::FRemoveResult FFieldMulticastDelegate::Remove(FDelegateHandle InDelegate)
{
	FRemoveResult Result;

	for (int32 Index = Delegates.Num() - 1; Index >= 0; --Index)
	{
		FInvocationElement& Element = Delegates[Index];
		if (Element.Delegate.GetHandle() == InDelegate)
		{
			Result.Object = Element.Key.Object.Get();
			Result.FieldId = Element.Key.Id;
			Result.bRemoved = Element.Delegate.IsBound();
			if (DelegateLockCount > 0)
			{
				Element.Delegate.Unbind();
				++CompactionCount;
			}
			else
			{
				Delegates.RemoveAt(Index);
			}
			break;
		}
	}

	if (Result.FieldId.IsValid())
	{
		// Search in the normal sorted list
		const int32 FoundIndex = UpperBound(Result.FieldId) - 1;
		if (Delegates.IsValidIndex(FoundIndex))
		{
			for (int32 Index = FoundIndex; Index >= 0; --Index)
			{
				const FInvocationElement& Element = Delegates[Index];
				if (Element.Key.Id != Result.FieldId)
				{
					break;
				}
				else if (Element.Key.Object == Result.Object && Element.Delegate.IsBound())
				{
					Result.bHasOtherBoundDelegates = true;
					break;
				}
			}
		}

		if (!Result.bHasOtherBoundDelegates)
		{
			for (int32 Index = AddedEmplaceAt; Index < Delegates.Num(); ++Index)
			{
				const FInvocationElement& Element = Delegates[Index];
				if (Element.Key.Id == Result.FieldId && Element.Key.Object == Result.Object && Element.Delegate.IsBound())
				{
					Result.bHasOtherBoundDelegates = true;
					break;
				}
			}
		}
	}

	return Result;
}


FFieldMulticastDelegate::FRemoveFromResult FFieldMulticastDelegate::RemoveFrom(const UObject* InObject, FFieldId InFieldId, FDelegateHandle InDelegate)
{
	bool bRemoved = false;
	bool bFieldPresent = false;

	FFieldMulticastDelegate* Self = this;
	auto RemoveOrUnbind = [Self, &InDelegate, &bRemoved, &bFieldPresent, InObject](FInvocationElement& Element, int32 Index)
	{
		if (Element.Delegate.GetHandle() == InDelegate)
		{
			if (Self->DelegateLockCount > 0)
			{
				Element.Delegate.Unbind();
				++Self->CompactionCount;
			}
			else
			{
				Self->Delegates.RemoveAt(Index);
			}
			bRemoved = true;
			return !bFieldPresent;
		}
		else if (Element.Key.Object == InObject && Element.Delegate.IsBound())
		{
			bFieldPresent = true;
			return !bRemoved;
		}
		return true;
	};

	// Search in the normal sorted list
	const int32 FoundIndex = UpperBound(InFieldId)-1;
	if (Delegates.IsValidIndex(FoundIndex))
	{
		for (int32 Index = FoundIndex; Index >= 0; --Index)
		{
			FInvocationElement& Element = Delegates[Index];
			if (Element.Key.Id != InFieldId)
			{
				break;
			}
			if (!RemoveOrUnbind(Element, Index))
			{
				break;
			}
		}
	}

	// The item may be at the end of the array, if added while broadcasting
	if (!bFieldPresent)
	{
		for (int32 Index = Delegates.Num() - 1; Index >= AddedEmplaceAt; --Index)
		{
			FInvocationElement& Element = Delegates[Index];
			if (Element.Key.Id == InFieldId)
			{
				if (!RemoveOrUnbind(Element, Index))
				{
					break;
				}
			}
		}
	}

	return FRemoveFromResult{ bRemoved, bFieldPresent };
}


FFieldMulticastDelegate::FRemoveAllResult FFieldMulticastDelegate::RemoveAll(const UObject* InObject, const void* InUserObject)
{
	FRemoveAllResult Result;

	auto SetHasFields = [&Result, InObject](FInvocationElement& Element)
	{
		Result.HasFields.PadToNum(Element.Key.Id.GetIndex() + 1, false);
		Result.HasFields[Element.Key.Id.GetIndex()] = true;
	};

	if (DelegateLockCount > 0)
	{
		for (FInvocationElement& Element : Delegates)
		{
			if (const IDelegateInstance* DelegateInstance = GetDelegateInstance(Element.Delegate))
			{
				if (Element.Key.Object == InObject)
				{
					if (DelegateInstance->HasSameObject(InUserObject))
					{
						Element.Delegate.Unbind();
						++CompactionCount;
						++Result.RemoveCount;
					}
					else if (!DelegateInstance->IsCompactable())
					{
						SetHasFields(Element);
					}
				}
			}
		}
	}
	else
	{
		for (int32 Index = Delegates.Num() - 1; Index >= 0; --Index)
		{
			FInvocationElement& Element = Delegates[Index];
			const IDelegateInstance* DelegateInstance = GetDelegateInstance(Element.Delegate);
			if (DelegateInstance == nullptr || DelegateInstance->IsCompactable())
			{
				Delegates.RemoveAt(Index);
			}
			else if (Element.Key.Object == InObject)
			{
				if (DelegateInstance->HasSameObject(InUserObject))
				{
					Delegates.RemoveAt(Index);
					++Result.RemoveCount;
				}
				else
				{
					SetHasFields(Element);
				}
			}
		}
		CompactionCount = 0;
	}

	return Result;
}


FFieldMulticastDelegate::FRemoveAllResult FFieldMulticastDelegate::RemoveAll(const UObject* InObject, FFieldId InFieldId, const void* InUserObject)
{
	FRemoveAllResult Result;

	auto SetHasFields = [&Result, InObject](FInvocationElement& Element)
	{
		Result.HasFields.PadToNum(Element.Key.Id.GetIndex() + 1, false);
		Result.HasFields[Element.Key.Id.GetIndex()] = true;
	};

	if (DelegateLockCount > 0)
	{
		for (FInvocationElement& Element : Delegates)
		{
			if (const IDelegateInstance* DelegateInstance = GetDelegateInstance(Element.Delegate))
			{
				if (Element.Key.Id == InFieldId && Element.Key.Object == InObject)
				{
					if (DelegateInstance->HasSameObject(InUserObject))
					{
						Element.Delegate.Unbind();
						++CompactionCount;
						++Result.RemoveCount;
					}
					else if (!DelegateInstance->IsCompactable())
					{
						SetHasFields(Element);
					}
				}
			}
		}
	}
	else
	{
		for (int32 Index = Delegates.Num() - 1; Index >= 0; --Index)
		{
			FInvocationElement& Element = Delegates[Index];
			const IDelegateInstance* DelegateInstance = GetDelegateInstance(Element.Delegate);
			if (DelegateInstance == nullptr || DelegateInstance->IsCompactable())
			{
				Delegates.RemoveAt(Index);
			}
			else if (Element.Key.Id == InFieldId && Element.Key.Object == InObject)
			{
				if (DelegateInstance->HasSameObject(InUserObject))
				{
					Delegates.RemoveAt(Index);
					++Result.RemoveCount;
				}
				else
				{
					SetHasFields(Element);
				}
			}
		}
		CompactionCount = 0;
	}

	return Result;
}


void FFieldMulticastDelegate::Broadcast(UObject* InObject, UE::FieldNotification::FFieldId InFieldId)
{
	++DelegateLockCount;

	// Search in the normal sorted list
	const int32 FoundIndex = UpperBound(InFieldId) - 1;
	if (Delegates.IsValidIndex(FoundIndex))
	{
		for (int32 Index = FoundIndex; Index >= 0; --Index)
		{
			FInvocationElement& Element = Delegates[Index];
			if (Element.Key.Id != InFieldId)
			{
				break;
			}
			if (Element.Key.Object == InObject)
			{
				Element.Delegate.ExecuteIfBound(InObject, InFieldId);
			}
		}
	}

	// The item may be at the end of the array, if added while broadcasting
	for (int32 Index = Delegates.Num() - 1; Index >= AddedEmplaceAt; --Index)
	{
		FInvocationElement& Element = Delegates[Index];
		if (Element.Key.Id == InFieldId && Element.Key.Object == InObject)
		{
			Element.Delegate.ExecuteIfBound(InObject, InFieldId);
		}
	}

	--DelegateLockCount;
	ExecuteLockOperations();
}


void FFieldMulticastDelegate::Reset()
{
	if (DelegateLockCount > 0)
	{
		for (FInvocationElement& Element : Delegates)
		{
			CompactionCount += Element.Delegate.IsBound() ? 1 : 0;
			Element.Delegate.Unbind();
		}
	}
	else
	{
		Delegates.Reset();
		DelegateLockCount = 0;
		CompactionCount = 0;
		AddedEmplaceAt = std::numeric_limits<uint16>::max();
	}
}


void FFieldMulticastDelegate::ExecuteLockOperations()
{
	if (DelegateLockCount <= 0)
	{
		// Remove items that got removed while broadcasting
		if (CompactionCount > 2)
		{
			for (int32 Index = Delegates.Num() - 1; Index >= 0; --Index)
			{
				FInvocationElement& Element = Delegates[Index];
				const IDelegateInstance* DelegateInstance = GetDelegateInstance(Element.Delegate);
				if (DelegateInstance == nullptr || DelegateInstance->IsCompactable())
				{
					Delegates.RemoveAt(Index);
				}
			}
			CompactionCount = 0;
		}

		// Sort item that were added while broadcasting
		for (; AddedEmplaceAt < Delegates.Num();)
		{
			if (Delegates[AddedEmplaceAt].Delegate.IsBound())
			{
				const int32 FoundIndex = UpperBound(Delegates[AddedEmplaceAt].Key);
				if (FoundIndex != AddedEmplaceAt)
				{
					FInvocationElement Element = MoveTemp(Delegates[AddedEmplaceAt]);
					Delegates.RemoveAtSwap(AddedEmplaceAt);
					Delegates.EmplaceAt(FoundIndex, MoveTemp(Element));
				}
				++AddedEmplaceAt;
			}
			else
			{
				Delegates.RemoveAtSwap(AddedEmplaceAt);
			}
		}
		AddedEmplaceAt = std::numeric_limits<uint16>::max();

		DelegateLockCount = 0;
	}
}

} //namespace