// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedReferenceSubobjectHelper.h"
	
UObject* FInstancedPropertyPath::Resolve(const UObject* Container) const
{
	UStruct* CurrentContainerType = Container->GetClass();

	const TArray<FPropertyLink>& PropChainRef = PropertyChain;
	auto GetProperty = [&CurrentContainerType, &PropChainRef](int32 ChainIndex)->FProperty*
	{
		const FProperty* SrcProperty = PropChainRef[ChainIndex].PropertyPtr;
		return FindFProperty<FProperty>(CurrentContainerType, SrcProperty->GetFName());
	};

	auto GetArrayIndex = [&PropChainRef](int32 ChainIndex)->int32
	{
		return PropChainRef[ChainIndex].ArrayIndex == INDEX_NONE ? 0 : PropChainRef[ChainIndex].ArrayIndex;
	};

	const FProperty* CurrentProp = GetProperty(0);
	const uint8* ValuePtr = (CurrentProp) ? CurrentProp->ContainerPtrToValuePtr<uint8>(Container, GetArrayIndex(0)) : nullptr;

	for (int32 ChainIndex = 1; CurrentProp && ChainIndex < PropertyChain.Num(); ++ChainIndex)
	{
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentProp))
		{
			check(PropertyChain[ChainIndex].PropertyPtr == ArrayProperty->Inner);

			int32 TargetIndex = PropertyChain[ChainIndex].ArrayIndex;
			check(TargetIndex != INDEX_NONE);

			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			if (TargetIndex >= ArrayHelper.Num())
			{
				CurrentProp = nullptr;
				break;
			}

			CurrentProp = ArrayProperty->Inner;
			ValuePtr    = ArrayHelper.GetRawPtr(TargetIndex);
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(CurrentProp))
		{
			check(PropertyChain[ChainIndex].PropertyPtr == SetProperty->ElementProp);

			int32 TargetIndex = PropertyChain[ChainIndex].ArrayIndex;
			check(TargetIndex != INDEX_NONE);

			FScriptSetHelper SetHelper(SetProperty, ValuePtr);
			if (TargetIndex >= SetHelper.Num())
			{
				CurrentProp = nullptr;
				break;
			}

			CurrentProp = SetProperty->ElementProp;
			ValuePtr    = SetHelper.GetElementPtr(TargetIndex);
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(CurrentProp))
		{
			int32 TargetIndex = PropertyChain[ChainIndex].ArrayIndex;
			check(TargetIndex != INDEX_NONE);
				
			FScriptMapHelper MapHelper(MapProperty, ValuePtr);
			if(PropertyChain[ChainIndex].PropertyPtr == MapProperty->KeyProp)
			{
				ValuePtr = MapHelper.GetKeyPtr(TargetIndex);
			}
			else if(ensure(PropertyChain[ChainIndex].PropertyPtr == MapProperty->ValueProp))
			{
				ValuePtr = MapHelper.GetValuePtr(TargetIndex);
			}
			CurrentProp = PropertyChain[ChainIndex].PropertyPtr;
		}
		else
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(CurrentProp))
			{
				CurrentContainerType = StructProperty->Struct;
			}

			CurrentProp = GetProperty(ChainIndex);
			ValuePtr = (CurrentProp) ? CurrentProp->ContainerPtrToValuePtr<uint8>(ValuePtr, GetArrayIndex(ChainIndex)) : nullptr;
		}
	}

	const FObjectProperty* TargetProperty = CastField<FObjectProperty>(CurrentProp);
	if (TargetProperty && TargetProperty->HasAnyPropertyFlags(CPF_InstancedReference))
	{
		return TargetProperty->GetObjectPropertyValue(ValuePtr);
	}
	return nullptr;
}

void FFindInstancedReferenceSubobjectHelper::GetInstancedSubObjects_Inner(FInstancedPropertyPath& PropertyPath, const uint8* ContainerAddress, TFunctionRef<void(const FInstancedSubObjRef& Ref)> OutObjects)
{
	check(ContainerAddress);
	const FProperty* TargetProp = PropertyPath.Head();

	if (const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(TargetProp))
	{
		// Exit now if the array doesn't contain any instanced references.
		if (!ArrayProperty->HasAnyPropertyFlags(CPF_ContainsInstancedReference))
		{
			return;
		}

		FScriptArrayHelper ArrayHelper(ArrayProperty, ContainerAddress);
		for (int32 ElementIndex = 0; ElementIndex < ArrayHelper.Num(); ++ElementIndex)
		{
			const uint8* ValueAddress = ArrayHelper.GetRawPtr(ElementIndex);

			PropertyPath.Push(ArrayProperty->Inner, ElementIndex);
			GetInstancedSubObjects_Inner(PropertyPath, ValueAddress, OutObjects);
			PropertyPath.Pop();
		}
	}
	else if (const FMapProperty* MapProperty = CastField<const FMapProperty>(TargetProp))
	{
		// Exit now if the map doesn't contain any instanced references.
		if (!MapProperty->HasAnyPropertyFlags(CPF_ContainsInstancedReference))
		{
			return;
		}

		FScriptMapHelper MapHelper(MapProperty, ContainerAddress);
		for (int32 ElementIndex = 0; ElementIndex < MapHelper.Num(); ++ElementIndex)
		{
			const uint8* KeyAddress = MapHelper.GetKeyPtr(ElementIndex);
			const uint8* ValueAddress = MapHelper.GetValuePtr(ElementIndex);

			PropertyPath.Push(MapProperty->KeyProp, ElementIndex);
			GetInstancedSubObjects_Inner(PropertyPath, KeyAddress, OutObjects);
			PropertyPath.Pop();

			PropertyPath.Push(MapProperty->ValueProp, ElementIndex);
			GetInstancedSubObjects_Inner(PropertyPath, ValueAddress, OutObjects);
			PropertyPath.Pop();
		}
	}
	else if (const FSetProperty* SetProperty = CastField<const FSetProperty>(TargetProp))
	{
		// Exit now if the set doesn't contain any instanced references.
		if (!SetProperty->HasAnyPropertyFlags(CPF_ContainsInstancedReference))
		{
			return;
		}

		FScriptSetHelper SetHelper(SetProperty, ContainerAddress);
		for (int32 ElementIndex = 0; ElementIndex < SetHelper.Num(); ++ElementIndex)
		{
			const uint8* ValueAddress = SetHelper.GetElementPtr(ElementIndex);

			PropertyPath.Push(SetProperty->ElementProp, ElementIndex);
			GetInstancedSubObjects_Inner(PropertyPath, ValueAddress, OutObjects);
			PropertyPath.Pop();
		}
	}
	else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(TargetProp))
	{
		// Exit early if the struct does not contain any instanced references or if the struct is invalid.
		if (!StructProperty->HasAnyPropertyFlags(CPF_ContainsInstancedReference) || !StructProperty->Struct)
		{
			return;
		}

		for (FProperty* StructProp = StructProperty->Struct->RefLink; StructProp; StructProp = StructProp->NextRef)
		{
			for (int32 ArrayIdx = 0; ArrayIdx < StructProp->ArrayDim; ++ArrayIdx)
			{
				const uint8* ValueAddress = StructProp->ContainerPtrToValuePtr<uint8>(ContainerAddress, ArrayIdx);

				PropertyPath.Push(StructProp, ArrayIdx);
				GetInstancedSubObjects_Inner(PropertyPath, ValueAddress, OutObjects);
				PropertyPath.Pop();
			}
		}
	}
	else if (TargetProp->HasAllPropertyFlags(CPF_PersistentInstance))
	{
		ensure(TargetProp->HasAllPropertyFlags(CPF_InstancedReference));
		if (const FObjectProperty* ObjectProperty = CastField<const FObjectProperty>(TargetProp))
		{
			if (UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ContainerAddress))
			{
				// don't need to push to PropertyPath, since this property is already at its head
				OutObjects(FInstancedSubObjRef(ObjectValue, PropertyPath));
			}
		}
	}
}

void FFindInstancedReferenceSubobjectHelper::Duplicate(UObject* OldObject, UObject* NewObject, TMap<UObject*, UObject*>& ReferenceReplacementMap, TArray<UObject*>& DuplicatedObjects)
{
	if (OldObject->GetClass()->HasAnyClassFlags(CLASS_HasInstancedReference) &&
		NewObject->GetClass()->HasAnyClassFlags(CLASS_HasInstancedReference))
	{
		TArray<FInstancedSubObjRef> OldInstancedSubObjects;
		GetInstancedSubObjects(OldObject, OldInstancedSubObjects);
		if (OldInstancedSubObjects.Num() > 0)
		{
			TArray<FInstancedSubObjRef> NewInstancedSubObjects;
			GetInstancedSubObjects(NewObject, NewInstancedSubObjects);
			for (const FInstancedSubObjRef& Obj : NewInstancedSubObjects)
			{
				const bool bNewObjectHasOldOuter = (Obj->GetOuter() == OldObject);
				if (bNewObjectHasOldOuter)
				{
					const bool bKeptByOld = OldInstancedSubObjects.Contains(Obj);
					const bool bNotHandledYet = !ReferenceReplacementMap.Contains(Obj);
					if (bKeptByOld && bNotHandledYet)
					{
						UObject* NewEditInlineSubobject = StaticDuplicateObject(Obj, NewObject, Obj->GetFName());
						ReferenceReplacementMap.Add(Obj, NewEditInlineSubobject);

						// NOTE: we cannot patch OldObject's linker table here, since we don't 
						//       know the relation between the  two objects (one could be of a 
						//       super class, and the other a child)

						// We also need to make sure to fixup any properties here
						DuplicatedObjects.Add(NewEditInlineSubobject);
					}
				}
			}
		}
	}
}
