// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererComponents.h"
#include "NiagaraConstants.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraRendererComponents.h"

DECLARE_CYCLE_STAT(TEXT("Component renderer bind data"), STAT_NiagaraComponentRendererBind, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Component renderer update data"), STAT_NiagaraComponentRendererUpdate, STATGROUP_Niagara);

//////////////////////////////////////////////////////////////////////////

TArray<FString> FNiagaraRendererComponents::SetterPrefixes;

FNiagaraRendererComponents::FNiagaraRendererComponents(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
{
	if (SetterPrefixes.Num() == 0)
	{
		SetterPrefixes.Add(FString("Set"));
		SetterPrefixes.Add(FString("K2_Set"));
	}
}

template<typename T>
void SetValueWithAccessor(FNiagaraVariable& DataVariable, FNiagaraDataSet& Data, int ParticleIndex)
{
	DataVariable.SetValue<T>(FNiagaraDataSetAccessor<T>::CreateReader(Data, DataVariable.GetName()).Get(ParticleIndex));
}

void SetVariableByType(FNiagaraVariable& DataVariable, FNiagaraDataSet& Data, int ParticleIndex)
{
	const FNiagaraTypeDefinition& VarType = DataVariable.GetType();
	if (VarType == FNiagaraTypeDefinition::GetFloatDef()) { SetValueWithAccessor<float>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetIntDef()) { SetValueWithAccessor<int32>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetBoolDef()) { SetValueWithAccessor<FNiagaraBool>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetVec2Def()) {	SetValueWithAccessor<FVector2D>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetVec3Def()) { SetValueWithAccessor<FVector>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetVec4Def()) {	SetValueWithAccessor<FVector4>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetColorDef()) { SetValueWithAccessor<FLinearColor>(DataVariable, Data, ParticleIndex); }
	else if (VarType == FNiagaraTypeDefinition::GetQuatDef()) { SetValueWithAccessor<FQuat>(DataVariable, Data, ParticleIndex); }
}

void ConvertVariableToType(const FNiagaraVariable& SourceVariable, FNiagaraVariable& TargetVariable)
{
	FNiagaraTypeDefinition SourceType = SourceVariable.GetType();
	FNiagaraTypeDefinition TargetType = TargetVariable.GetType();
	
	if (SourceType == FNiagaraTypeDefinition::GetVec3Def() && TargetType == UNiagaraComponentRendererProperties::GetFColorDef()) 
	{
		FVector Data = SourceVariable.GetValue<FVector>();
		FColor ColorData((uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.X * 255.f), 0, 255),
			(uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.Y * 255.f), 0, 255),
			(uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.Z * 255.f), 0, 255));
		TargetVariable.SetValue<FColor>(ColorData);
	}
	else if (SourceType == FNiagaraTypeDefinition::GetVec4Def() && TargetType == UNiagaraComponentRendererProperties::GetFColorDef())
	{
		FVector4 Data = SourceVariable.GetValue<FVector4>();
		FColor ColorData((uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.X * 255.f), 0, 255),
			(uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.Y * 255.f), 0, 255),
			(uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.Z * 255.f), 0, 255),
			(uint8)FMath::Clamp<int32>(FMath::TruncToInt(Data.W * 255.f), 0, 255));
		TargetVariable.SetValue<FColor>(ColorData);
	}
	else if (SourceType == FNiagaraTypeDefinition::GetColorDef() && TargetType == UNiagaraComponentRendererProperties::GetFColorDef())
	{
		FLinearColor Data = SourceVariable.GetValue<FLinearColor>();
		TargetVariable.SetValue<FColor>(Data.Quantize());
	}
	else if (SourceType == FNiagaraTypeDefinition::GetVec3Def() && TargetType == UNiagaraComponentRendererProperties::GetFRotatorDef())
	{
		FVector Data = SourceVariable.GetValue<FVector>();
		FRotator Rotator(Data.X, Data.Y, Data.Z);
		TargetVariable.SetValue<FRotator>(Rotator);
	}
	else if (SourceType == FNiagaraTypeDefinition::GetQuatDef() && TargetType == UNiagaraComponentRendererProperties::GetFRotatorDef())
	{
		FQuat Data = SourceVariable.GetValue<FQuat>();
		TargetVariable.SetValue<FRotator>(Data.Rotator());
	}
}

void InvokeSetterFunction(UObject* InRuntimeObject, UFunction* Setter, const uint8* InData, int32 DataSize)
{
	int32 ParmsSize = Setter->ParmsSize;
	check(InRuntimeObject && Setter && ParmsSize > 0);

	uint8* Params = const_cast<uint8*>(InData);
	if (Setter->ReturnValueOffset != MAX_uint16 || Setter->NumParms > 1)
	{
		// Function has a return value or multiple parameters, we need to initialize memory for the entire parameter pack
		// We use alloca here (as in UObject::ProcessEvent) to avoid a heap allocation. Alloca memory survives the current function's stack frame.
		Params = reinterpret_cast<uint8*>(FMemory_Alloca(ParmsSize));

		bool bFirstProperty = true;
		for (FProperty* Property = Setter->PropertyLink; Property; Property = Property->PropertyLinkNext)
		{
			// Initialize the parameter pack with any param properties that reside in the container
			if (Property->IsInContainer(ParmsSize))
			{
				Property->InitializeValue_InContainer(Params);

				// The first encountered property is assumed to be the input value so initialize this with the user-specified value from InPropertyValue
				if (Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm) && bFirstProperty)
				{
					const bool bIsValid = ensureMsgf(DataSize == Property->ElementSize, TEXT("Property type does not match for setter function %s::%s (%ibytes != %ibytes"), *InRuntimeObject->GetName(), *Setter->GetName(), DataSize, Property->ElementSize);
					if (bIsValid)
					{
						Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Params), InData);
					}
					else
					{
						return;
					}
				}
				bFirstProperty = false;
			}
		}
	}

	// Now we have the parameters set up correctly, call the function
	InRuntimeObject->ProcessEvent(Setter, Params);
}

void FNiagaraRendererComponents::Initialize(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent)
{
	const UNiagaraComponentRendererProperties* Properties = CastChecked<const UNiagaraComponentRendererProperties>(InProperties);
	if (!Properties || !Properties->TemplateComponent)
	{
		return;
	}

	// Search for a setter function if not already done before
	for (const FNiagaraComponentPropertyBinding& PropertyBinding : Properties->PropertyBindings)
	{
		if (!SetterFunctionMapping.Contains(PropertyBinding.PropertyName))
		{
			UFunction* SetterFunction = nullptr;

			// we first check if the property has some metadata that explicitly mentions the setter to use
			if (!PropertyBinding.MetadataSetterName.IsNone())
			{
				SetterFunction = Properties->TemplateComponent->FindFunction(PropertyBinding.MetadataSetterName);
			}

			if (!SetterFunction)
			{
				// the setter was not specified, so we try to find one that fits the name
				FString PropertyName = PropertyBinding.PropertyName.ToString();
				if (PropertyBinding.PropertyType == FNiagaraTypeDefinition::GetBoolDef())
				{
					PropertyName.RemoveFromStart("b", ESearchCase::CaseSensitive);
				}
				for (const FString& Prefix : FNiagaraRendererComponents::SetterPrefixes)
				{
					FName SetterFunctionName = FName(Prefix + PropertyName);
					SetterFunction = Properties->TemplateComponent->FindFunction(SetterFunctionName);
					if (SetterFunction)
					{
						break;
					}
				}
			}

			FNiagaraPropertySetter Setter;
			Setter.Function = SetterFunction;

			// Okay, so there is a special case where the *property* of an object has one type, but the *setter* has another type
			// that either doesn't need to be converted (e.g. the color property on a light component) or doesn't fit the converted value.
			// If we detect such a case we adapt the binding to either ignore the conversion or we discard the setter completely.
			if (SetterFunction)
			{
				for (FProperty* Property = SetterFunction->PropertyLink; Property; Property = Property->PropertyLinkNext)
				{
					if (Property->IsInContainer(SetterFunction->ParmsSize) && Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
					{
						FNiagaraTypeDefinition FieldType = UNiagaraComponentRendererProperties::ToNiagaraType(Property);
						if (FieldType != PropertyBinding.PropertyType && FieldType == PropertyBinding.AttributeBinding.GetType())
						{
							// we can use the original Niagara value with the setter instead of converting it
							Setter.bIgnoreConversion = true;
						} else if (FieldType != PropertyBinding.PropertyType)
						{
							// setter is completely unusable
							Setter.Function = nullptr;
						}
						break;
					}
				}
			}
			SetterFunctionMapping.Add(PropertyBinding.PropertyName, Setter);
		}
	}
}

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase* FNiagaraRendererComponents::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentRendererBind);

	FNiagaraSystemInstance* SystemInstance = Emitter->GetParentSystemInstance();

	//Bail if we don't have the required attributes to render this emitter.
	const UNiagaraComponentRendererProperties* Properties = CastChecked<const UNiagaraComponentRendererProperties>(InProperties);
	if (!SystemInstance || !Properties || SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return nullptr;
	}
	FNiagaraDataSet& Data = Emitter->GetData();
	FNiagaraDataBuffer& ParticleData = Data.GetCurrentDataChecked();
	FNiagaraDataSetReaderInt32<FNiagaraBool> EnabledAccessor = FNiagaraDataSetAccessor<FNiagaraBool>::CreateReader(Data, Properties->EnabledBinding.GetDataSetBindableVariable().GetName());
	FNiagaraDataSetReaderInt32<int32> UniqueIDAccessor = FNiagaraDataSetAccessor<int32>::CreateReader(Data, FName("UniqueID"));
	TSet<int32> ParticlesWithComponents = Properties->bOnlyCreateComponentsOnParticleSpawn ? SystemInstance->GetParticlesWithActiveComponents(Properties->TemplateComponent) : TSet<int32>();

	int32 TaskLimitLeft = Properties->ComponentCountLimit;
	int32 SmallestID = INT_MAX;
	for (uint32 ParticleIndex = 0; ParticleIndex < ParticleData.GetNumInstances() && TaskLimitLeft > 0; ParticleIndex++)
	{
		int32 ParticleID = -1;
		if (Properties->bAssignComponentsOnParticleID)
		{
			ParticleID = UniqueIDAccessor.GetSafe(ParticleIndex, -1);
			SmallestID = FMath::Min<int32>(ParticleID, SmallestID);
		}
		if (!EnabledAccessor.GetSafe(ParticleIndex, true))
		{
			continue;
		}
		if (Properties->bAssignComponentsOnParticleID && Properties->bOnlyCreateComponentsOnParticleSpawn)
		{
			bool bIsNewlySpawnedParticle = ParticleIndex >= ParticleData.GetNumInstances() - ParticleData.GetNumSpawnedInstances();
			if (!bIsNewlySpawnedParticle && !ParticlesWithComponents.Contains(ParticleID))
			{
				continue;
			}
		}

		TArray<FNiagaraComponentPropertyBinding> BindingsCopy = Properties->PropertyBindings;
		for (FNiagaraComponentPropertyBinding& PropertyBinding : BindingsCopy)
		{
			const FNiagaraPropertySetter* PropertySetter = SetterFunctionMapping.Find(PropertyBinding.PropertyName);
			if (!PropertySetter)
			{
				// it's possible that Initialize wasn't called or the bindings changed in the meantime
				continue;
			}
			PropertyBinding.SetterFunction = PropertySetter->Function;

			FNiagaraVariable& DataVariable = PropertyBinding.WritableValue;
			const FNiagaraVariableBase& FoundVar = PropertyBinding.AttributeBinding.GetDataSetBindableVariable();
			DataVariable.SetType(FoundVar.GetType());
			DataVariable.SetName(FoundVar.GetName());
			DataVariable.ClearData();
			if (!DataVariable.IsValid() || !Data.HasVariable(DataVariable))
			{
				continue;
			}
			
			SetVariableByType(DataVariable, Data, ParticleIndex);
			if (PropertyBinding.PropertyType.IsValid() && DataVariable.GetType() != PropertyBinding.PropertyType && !PropertySetter->bIgnoreConversion)
			{
				FNiagaraVariable TargetVariable(PropertyBinding.PropertyType, DataVariable.GetName());
				ConvertVariableToType(DataVariable, TargetVariable);
				DataVariable = TargetVariable;
			}
		}
		FNiagaraComponentUpdateTask UpdateTask;
		UpdateTask.TemplateObject = Properties->TemplateComponent;
#if WITH_EDITORONLY_DATA
		UpdateTask.bVisualizeComponents = Properties->bVisualizeComponents;
#endif
		UpdateTask.ParticleID = ParticleID;
		UpdateTask.SmallestID = SmallestID;
		UpdateTask.UpdateCallback = [BindingsCopy](USceneComponent* SceneComponent, FNiagaraComponentRenderPoolEntry& PoolEntry)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentRendererUpdate);

			for (const FNiagaraComponentPropertyBinding& PropertyBinding : BindingsCopy)
			{
				const FNiagaraVariable& DataVariable = PropertyBinding.WritableValue;
				if (!DataVariable.IsDataAllocated())
				{
					continue;
				}
				
				UFunction* SetterFunction = PropertyBinding.SetterFunction;
				if (SetterFunction && SetterFunction->NumParms >= 1)
				{
					// if we have a setter function we invoke it instead of setting the property directly, because then the object gets a chance to react to the new value
					InvokeSetterFunction(SceneComponent, SetterFunction, DataVariable.GetData(), DataVariable.GetSizeInBytes());
				}
				else
				{
					// no setter found, just slam the value in the object memory and hope for the best
					if (!PoolEntry.PropertyAddressMapping.Contains(PropertyBinding.PropertyName))
					{
						PoolEntry.PropertyAddressMapping.Add(PropertyBinding.PropertyName, FNiagaraRendererComponents::FindProperty(*SceneComponent, PropertyBinding.PropertyName.ToString()));
					}
					FComponentPropertyAddress PropertyAddress = PoolEntry.PropertyAddressMapping[PropertyBinding.PropertyName];
					if (!PropertyAddress.GetProperty())
					{
						continue;
					}
					uint8* Dest = (uint8*)SceneComponent + PropertyAddress.GetProperty()->GetOffset_ForInternal();
					DataVariable.CopyTo(Dest);
				}
			}
		};

		SystemInstance->EnqueueComponentUpdateTask(UpdateTask);
		TaskLimitLeft--;
	}

	return nullptr;
}

FComponentPropertyAddress FNiagaraRendererComponents::FindPropertyRecursive(void* BasePointer, UStruct* InStruct, TArray<FString>& InPropertyNames, uint32 Index)
{
	FComponentPropertyAddress NewAddress;
	FProperty* Property = FindFProperty<FProperty>(InStruct, *InPropertyNames[Index]);

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		NewAddress.Property = StructProp;
		NewAddress.Address = BasePointer;

		if (InPropertyNames.IsValidIndex(Index + 1))
		{
			void* StructContainer = StructProp->ContainerPtrToValuePtr<void>(BasePointer);
			return FindPropertyRecursive(StructContainer, StructProp->Struct, InPropertyNames, Index + 1);
		}
		else
		{
			check(StructProp->GetName() == InPropertyNames[Index]);
		}
	}
	else if (Property)
	{
		NewAddress.Property = Property;
		NewAddress.Address = BasePointer;
	}

	return NewAddress;
}

FComponentPropertyAddress FNiagaraRendererComponents::FindProperty(const UObject& InObject, const FString& InPropertyPath)
{
	TArray<FString> PropertyNames;
	InPropertyPath.ParseIntoArray(PropertyNames, TEXT("."), true);

	if (IsValid(&InObject) && PropertyNames.Num() > 0)
	{
		return FindPropertyRecursive((void*)&InObject, InObject.GetClass(), PropertyNames, 0);
	}
	return FComponentPropertyAddress();
}
