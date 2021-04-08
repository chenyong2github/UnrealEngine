// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApplySnapshotDataArchive.h"

#include "BaseObjectInfo.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsStats.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"

FApplySnapshotDataArchive FApplySnapshotDataArchive::MakeDeserializingIntoWorldObject(const FBaseObjectInfo& InObjectInfo, const FPropertySelection* InSelectedProperties)
{
	return FApplySnapshotDataArchive(InObjectInfo, InSelectedProperties);
}

FApplySnapshotDataArchive FApplySnapshotDataArchive::MakeForDeserializingTransientObject(const FBaseObjectInfo& InObjectInfo)
{
	return FApplySnapshotDataArchive(InObjectInfo);
}

bool FApplySnapshotDataArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ShouldSkipProperty_Loading"), STAT_ShouldSkipProperty_Loading, STATGROUP_LevelSnapshots);
	checkf(IsLoading(), TEXT("Should only be used to write data to objects"));
	
	bool bShouldSkipProperty = Super::ShouldSkipProperty(InProperty);
	if (!bShouldSkipProperty && SelectedProperties)
	{
		// We require the const_cast to correctly initialize the TFieldPath even though we are not actually modifying the property
		const TFieldPath<FProperty> PropertyPath(const_cast<FProperty*>(InProperty));
		
		bShouldSkipProperty = !SelectedProperties.GetValue()->SelectedPropertyPaths.Contains(PropertyPath);
		HandleHiddenCustomSerializedProperty(InProperty, bShouldSkipProperty);
	}

	return bShouldSkipProperty;
}

FApplySnapshotDataArchive::FApplySnapshotDataArchive(const FBaseObjectInfo& InObjectInfo, const FPropertySelection* InSelectedProperties)
        :
        SelectedProperties(InSelectedProperties ? InSelectedProperties : TOptional<const FPropertySelection*>())
{
	SetReadOnlyObjectInfo(InObjectInfo);
	
	SetWantBinaryPropertySerialization(false);
	SetIsTransacting(false);
	SetIsPersistent(true);
    
	SetIsLoading(true);			
}

void FApplySnapshotDataArchive::HandleHiddenCustomSerializedProperty(const FProperty* InProperty, bool& bShouldSkipProperty) const
{
	/* Edge case: Certain structs may implement a custom operator<< or Serialize function. These functions might push any properties (native or UProperty),
	 * that could NOT be discovered when SelectedProperties->SelectedPropertyPaths was built.
	 * ShouldSkipProperty must return false so these internal properties are also serialized. 
	 * 
	 * "Nice" example:
	 * 1. FNiagaraParameterStore::SortedParameterOffsets is an TArray<FNiagaraVariableWithOffset>.
	 * 2. ShouldSkipProperty returned false for SortedParameterOffsets, meaning SortedParameterOffsets is now getting serialized.
	 * 3. FNiagaraVariableWithOffset implements a custom Serialize function, which in turn does Ar << Handle, where Handle is of type FNiagaraTypeDefinitionHandle.
	 * 4. Ar << Handle causes this nice code to be executed:
	 * 
	 * FArchive& operator<<(FArchive& Ar, FNiagaraTypeDefinitionHandle& Handle)
	 * {
	 *		UScriptStruct* TypeDefStruct = FNiagaraTypeDefinition::StaticStruct();
	 *
	 *		if (Ar.IsSaving())
	 *		{
	 *			FNiagaraTypeDefinition TypeDef = *Handle;
	 *			TypeDefStruct->SerializeItem(Ar, &TypeDef, nullptr);
	 *		}
	 *		else if (Ar.IsLoading())
	 *		{
	 *			FNiagaraTypeDefinition TypeDef;
	 *			TypeDefStruct->SerializeItem(Ar, &TypeDef, nullptr);
	 *			Handle = FNiagaraTypeDefinitionHandle(TypeDef);
	 *		} 
	 * }
	 * 
	 * Result: ShouldSkipProperty is now called with the properties of FNiagaraTypeDefinition. 
	 * These properties are not in SelectedProperties->SelectedPropertyPaths but we MUST serialize them.
	 * 
	 * If you understood the above, congratulations; this is a complicated edge case.
	 */
	const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
	const bool bIsRootProperty = PropertyChain == nullptr || PropertyChain->GetNumProperties() == 0;
	// The last property in the chain is the last property on which ShouldSkipProperty was called, e.g. SortedParameterOffsets
	FProperty* LastPropertyInChain = bIsRootProperty ? GetSerializedProperty() : PropertyChain->GetPropertyFromStack(0);
	if (bShouldSkipProperty && LastPropertyInChain)
	{
		const bool bIsInternalProperty = [LastPropertyInChain, InProperty]()
		{
			FStructProperty* LastInChainAsStruct = CastField<FStructProperty>(LastPropertyInChain);
			if (!LastInChainAsStruct)
			{
				return false;
			}

			UStruct* CurrentStruct = LastInChainAsStruct->Struct;
			while (CurrentStruct)
			{
				FField* ChildProperty = CurrentStruct->ChildProperties;
				while (ChildProperty)
				{
					if (ChildProperty == InProperty)
					{
						return false;
					}
					ChildProperty = ChildProperty->Next;
				}
				CurrentStruct = CurrentStruct->GetSuperStruct();
			}
			return true;
		}();

		const bool bIsPropertyInCollection = [PropertyChain]()
		{
			const bool bCouldBeInCollection = PropertyChain && PropertyChain->GetNumProperties() > 1; 
			if (bCouldBeInCollection)
			{
				FProperty* PossibleCollection = PropertyChain->GetPropertyFromStack(1);
				return CastField<FArrayProperty>(PossibleCollection) || CastField<FSetProperty>(PossibleCollection) || CastField<FMapProperty>(PossibleCollection);
			}
			return false;
		}();
		// E.g. UPROPERTY() TArray<FFoo> -> PropertyChain->GetPropertyFromStack(0) = FStructProperty (FFoo) PropertyChain->GetPropertyFromStack(1) -> FArrayProperty
		// The user who built passed in selected properties may have forgotten to include these internal properties, so we handle it here.
		const TFieldPath<FProperty> PathOfLastPropertyInChain(bIsPropertyInCollection && ensure(PropertyChain) ? PropertyChain->GetPropertyFromStack(1) : LastPropertyInChain);

		// If InProperty is internal or in a collection, InProperty belongs to the last property in the chain. When deciding whether to skip, we need to check the parent property.
		const bool bIsSerializingPropertyOfPropertyWeCareAbout = (bIsInternalProperty || bIsPropertyInCollection) && SelectedProperties.GetValue()->SelectedPropertyPaths.Contains(PathOfLastPropertyInChain);
		bShouldSkipProperty = !bIsSerializingPropertyOfPropertyWeCareAbout;
	}
}