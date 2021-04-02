// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApplySnapshotDataArchiveV2.h"

#include "LevelSnapshotsStats.h"
#include "PropertySelection.h"
#include "WorldSnapshotData.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

namespace
{
	/* Always serialize all struct child properties if struct itself is in selection set.
	 * This is a temp fix because selection sets currently don't hold struct property path information.
	 */
	void AllowAllStructSubpropertiesIfParentStructWasSelected_TempFix(const FArchive& Archive, bool& bShouldSkipProperty)
	{
		const FArchiveSerializedPropertyChain* PropertyChain = Archive.GetSerializedPropertyChain();
		const bool bIsRootProperty = PropertyChain == nullptr || PropertyChain->GetNumProperties() <= 1;
		if (bShouldSkipProperty && !bIsRootProperty)
		{
			for (int32 i = 0; i < PropertyChain->GetNumProperties() - 1; ++i)
			{
				const FProperty* CurrentProperty = PropertyChain->GetPropertyFromRoot(i);
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(CurrentProperty))
				{
					// ShouldSkipProperty has returned false on all properties from i = 0 to i = NumProperty - 1
					// Include all struct properties
					bShouldSkipProperty = false;
					return;
				}
			}
		}
	}

	void HandleHiddenCustomSerializedProperty(const FArchive& Archive, const FProperty* InProperty, const FPropertySelection& SelectionSet, bool& bShouldSkipProperty)
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
		const FArchiveSerializedPropertyChain* PropertyChain = Archive.GetSerializedPropertyChain();
		const bool bIsRootProperty = PropertyChain == nullptr || PropertyChain->GetNumProperties() == 0;
		// The last property in the chain is the last property on which ShouldSkipProperty was called, e.g. SortedParameterOffsets
		const FProperty* LastPropertyInChain = bIsRootProperty ? Archive.GetSerializedProperty() : PropertyChain->GetPropertyFromStack(0);
		if (bShouldSkipProperty && LastPropertyInChain)
		{
			const bool bIsInternalProperty = [LastPropertyInChain, InProperty]()
			{
				const FStructProperty* LastInChainAsStruct = CastField<FStructProperty>(LastPropertyInChain);
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
			const FProperty* PropertyToCheck = bIsPropertyInCollection && ensure(PropertyChain) ? PropertyChain->GetPropertyFromStack(1) : LastPropertyInChain;

			// If InProperty is internal or in a collection, InProperty belongs to the last property in the chain. When deciding whether to skip, we need to check the parent property.
			const bool bIsSerializingPropertyOfPropertyWeCareAbout = (bIsInternalProperty || bIsPropertyInCollection) && SelectionSet.IsPropertySelected(PropertyToCheck);
			bShouldSkipProperty = !bIsSerializingPropertyOfPropertyWeCareAbout;
		}
	}
	
	class FCopyProperties : public FObjectWriter
	{
		using Super = FObjectWriter;

		const FPropertySelection& PropertiesToSerialize;
				
	public:

		FCopyProperties(TArray<uint8>& SaveLocation, const FPropertySelection& PropertiesToSerialize)
			:
			Super(SaveLocation),
			PropertiesToSerialize(PropertiesToSerialize)
		{
			ArNoDelta = true;
		}

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
		{
			bool bShouldSkipProperty = !PropertiesToSerialize.IsPropertySelected(InProperty);
			AllowAllStructSubpropertiesIfParentStructWasSelected_TempFix(*this, bShouldSkipProperty);
			HandleHiddenCustomSerializedProperty(*this, InProperty, PropertiesToSerialize, bShouldSkipProperty);
			return bShouldSkipProperty;
		}
	};
}

void FApplySnapshotDataArchiveV2::ApplyToWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, UObject* InDeserializedVersion, const FPropertySelection& InSelectionSet)
{
	// ObjectData only contains properties that were different from the CDO at the time of saving. This archive may skip many properties.
	// Hence, we serialise in two steps:

	
	// Step 1: Serialise any properties that were different from CDO at time of snapshotting and that are different now
	// Most UObject references will be handled here:
		// - Subobject references are handled here
		// - References to other actors in the world are handled here
	FApplySnapshotDataArchiveV2 ApplySavedData(InObjectData, InSharedData, InOriginalObject, InSelectionSet);
	InOriginalObject->Serialize(ApplySavedData);


	
	// Step 2: Serialise any remaining properties that were not covered.
	// Most UObject references were covered in step 1.
		// - CDO was nullptr and level property is non-nullptr
		// - CDO was asset reference and level property now has different asset reference
	const FPropertySelection& PropertiesLeftToSerialise = ApplySavedData.PropertiesLeftToSerialize;
	if (PropertiesLeftToSerialise.SelectedPropertyPaths.Num() > 0)
	{
		TArray<uint8> CopiedPropertyData;
		FCopyProperties CopySimpleProperties(CopiedPropertyData, PropertiesLeftToSerialise);
		InDeserializedVersion->Serialize(CopySimpleProperties);
		
		FObjectReader PasteSimpleProperties(InOriginalObject, CopiedPropertyData);
	}
}

bool FApplySnapshotDataArchiveV2::ShouldSkipProperty(const FProperty* InProperty) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ShouldSkipProperty_Loading"), STAT_ShouldSkipProperty_Loading, STATGROUP_LevelSnapshots);
	
	bool bShouldSkipProperty = Super::ShouldSkipProperty(InProperty);
	if (!bShouldSkipProperty)
	{
		bShouldSkipProperty = !SelectionSet.IsPropertySelected(InProperty);
		AllowAllStructSubpropertiesIfParentStructWasSelected_TempFix(*this, bShouldSkipProperty);
		HandleHiddenCustomSerializedProperty(*this, InProperty, SelectionSet, bShouldSkipProperty);
	}
	
	return bShouldSkipProperty;
}

void FApplySnapshotDataArchiveV2::PushSerializedProperty(FProperty* InProperty, const bool bIsEditorOnlyProperty)
{
	Super::PushSerializedProperty(InProperty, bIsEditorOnlyProperty);

#if WITH_EDITOR
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PreEditChange"), STAT_PreEditChange, STATGROUP_LevelSnapshots);
	if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		// TODO: chain event
	}
	else
	{
		OriginalObject->PreEditChange(InProperty);
	}
#endif
	
	PropertiesLeftToSerialize.RemoveProperty(InProperty);
}

void FApplySnapshotDataArchiveV2::PopSerializedProperty(FProperty* InProperty, const bool bIsEditorOnlyProperty)
{
	Super::PopSerializedProperty(InProperty, bIsEditorOnlyProperty);

#if WITH_EDITOR
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PostEditChange"), STAT_PostEditChange, STATGROUP_LevelSnapshots);
	if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		// TODO: chain event
	}
	else
	{
		FPropertyChangedEvent ChangeEvent(InProperty);
		OriginalObject->PostEditChangeProperty(ChangeEvent);
	}
#endif
}

FApplySnapshotDataArchiveV2::FApplySnapshotDataArchiveV2(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, const FPropertySelection& InSelectionSet)
        :
        Super(InObjectData, InSharedData, true),
        SelectionSet(InSelectionSet),
		PropertiesLeftToSerialize(InSelectionSet),
        OriginalObject(InOriginalObject)
{
	bShouldLoadObjectDependenciesForTempWorld = false;
}
