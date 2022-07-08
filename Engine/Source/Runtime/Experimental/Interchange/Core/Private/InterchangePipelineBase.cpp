// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangePipelineBase.h"

#include "CoreMinimal.h"
#include "InterchangeLogPrivate.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

namespace UE
{
	namespace Interchange
	{
		namespace PipelinePrivate
		{
			FString CreateConfigSectionName(const FName PipelineStackName, UClass* PipelineClass)
			{
				FString Section = TEXT("Interchange_StackName__") + PipelineStackName.ToString() + TEXT("__PipelineClassName_") + PipelineClass->GetName();
				return Section;
			}
		}
	}
}

void UInterchangePipelineBase::LoadSettings(const FName PipelineStackName)
{
	LoadSettingsInternal(PipelineStackName, GEditorPerProjectIni, LockedProperties);
}

void UInterchangePipelineBase::SaveSettings(const FName PipelineStackName)
{
	SaveSettingsInternal(PipelineStackName, GEditorPerProjectIni);
}

bool UInterchangePipelineBase::SetLockedPropertyStatus(const FName PropertyPath, bool bLocked)
{
	ensure(bAllowLockedPropertiesEdition);

	if (!bLocked)
	{
		LockedProperties.Remove(PropertyPath);
	}
	else
	{
		bool& bLockStatus = LockedProperties.FindOrAdd(PropertyPath);
		bLockStatus = bLocked;
	}

	return true;
}

bool UInterchangePipelineBase::GetLockedPropertyStatus(const FName PropertyPath) const
{
	if (const bool* bLockStatus = LockedProperties.Find(PropertyPath))
	{
		return *bLockStatus;
	}
	return false;
}

FName UInterchangePipelineBase::GetLockedPropertiesPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UInterchangePipelineBase, LockedProperties);
}

void UInterchangePipelineBase::LoadSettingsInternal(const FName PipelineStackName, const FString& ConfigFilename, TMap<FName, bool>& ParentLockedProperties)
{
	int32 PortFlags = 0;
	UClass* Class = this->GetClass();
	for (FProperty* Property = Class->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		//Do not load a transient property
		if (Property->HasAnyPropertyFlags(CPF_Transient))
		{
			continue;
		}

		FString SectionName = UE::Interchange::PipelinePrivate::CreateConfigSectionName(PipelineStackName, Class);
		FString Key = Property->GetName();
		const FName PropertyName = Property->GetFName();
		const FName PropertyPath = FName(Property->GetPathName());
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UInterchangePipelineBase, LockedProperties))
		{
			continue;
		}

		if (const bool* LockStatus = ParentLockedProperties.Find(PropertyPath))
		{
			if (*LockStatus)
			{
				//Skip this locked property
				continue;
			}
		}

		const bool bIsPropertyInherited = Property->GetOwnerClass() != Class;
		UObject* SuperClassDefaultObject = Class->GetSuperClass()->GetDefaultObject();

		FObjectProperty* SubObject = CastField<FObjectProperty>(Property);
		FArrayProperty* Array = CastField<FArrayProperty>(Property);
		if (Array)
		{
			const bool bForce = false;
			const bool bConst = true;
			FConfigSection* Section = GConfig->GetSectionPrivate(*SectionName, bForce, bConst, ConfigFilename);
			if (Section != nullptr)
			{
				TArray<FConfigValue> List;
				const FName KeyName(*Key, FNAME_Find);
				Section->MultiFind(KeyName, List);

				FScriptArrayHelper_InContainer ArrayHelper(Array, this);
				// Only override default properties if there is something to override them with.
				if (List.Num() > 0)
				{
					ArrayHelper.EmptyAndAddValues(List.Num());
					for (int32 i = List.Num() - 1, c = 0; i >= 0; i--, c++)
					{
						Array->Inner->ImportText_Direct(*List[i].GetValue(), ArrayHelper.GetRawPtr(c), this, PortFlags);
					}
				}
				else
				{
					int32 Index = 0;
					const FConfigValue* ElementValue = nullptr;
					do
					{
						// Add array index number to end of key
						FString IndexedKey = FString::Printf(TEXT("%s[%i]"), *Key, Index);

						// Try to find value of key
						const FName IndexedName(*IndexedKey, FNAME_Find);
						if (IndexedName == NAME_None)
						{
							break;
						}
						ElementValue = Section->Find(IndexedName);

						// If found, import the element
						if (ElementValue != nullptr)
						{
							// expand the array if necessary so that Index is a valid element
							ArrayHelper.ExpandForIndex(Index);
							Array->Inner->ImportText_Direct(*ElementValue->GetValue(), ArrayHelper.GetRawPtr(Index), this, PortFlags);
						}

						Index++;
					} while (ElementValue || Index < ArrayHelper.Num());
				}
			}
		}
		else if (UInterchangePipelineBase* SubPipeline = SubObject ? Cast<UInterchangePipelineBase>(SubObject->GetObjectPropertyValue_InContainer(this)) : nullptr)
		{
			// Load the settings if the referenced pipeline is a subobject of ours
			if (SubPipeline->IsInOuter(this))
			{
				SubPipeline->LoadSettingsInternal(PipelineStackName, ConfigFilename, ParentLockedProperties);
			}
		}
		else
		{
			for (int32 i = 0; i < Property->ArrayDim; i++)
			{
				if (Property->ArrayDim != 1)
				{
					Key = FString::Printf(TEXT("%s[%i]"), *Property->GetName(), i);
				}

				FString Value;
				bool bFoundValue = GConfig->GetString(*SectionName, *Key, Value, ConfigFilename);

				if (bFoundValue)
				{
					if (Property->ImportText_Direct(*Value, Property->ContainerPtrToValuePtr<uint8>(this, i), this, PortFlags) == NULL)
					{
						// this should be an error as the properties from the .ini / .int file are not correctly being read in and probably are affecting things in subtle ways
						UE_LOG(LogInterchangeCore, Error, TEXT("UInterchangePipeline (class:%s) failed to load settings. Property: %s Value: %s"), *this->GetClass()->GetName(), *Property->GetName(), *Value);
					}
				}
			}
		}
	}
}

void UInterchangePipelineBase::SaveSettingsInternal(const FName PipelineStackName, const FString& ConfigFilename)
{
	int32 PortFlags = 0;
	UClass* Class = this->GetClass();
	for (FProperty* Property = Class->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		//Do not save a transient property
		if (Property->HasAnyPropertyFlags(CPF_Transient))
		{
			continue;
		}

		FString SectionName = UE::Interchange::PipelinePrivate::CreateConfigSectionName(PipelineStackName, Class);
		FString Key = Property->GetName();
		const FName PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UInterchangePipelineBase, LockedProperties))
		{
			continue;
		}

		const bool bIsPropertyInherited = Property->GetOwnerClass() != Class;
		UObject* SuperClassDefaultObject = Class->GetSuperClass()->GetDefaultObject();
		FObjectProperty* SubObject = CastField<FObjectProperty>(Property);
		FArrayProperty* Array = CastField<FArrayProperty>(Property);
		if (Array)
		{
			const bool bForce = true;
			const bool bConst = false;
			FConfigSection* Section = GConfig->GetSectionPrivate(*SectionName, bForce, bConst, ConfigFilename);
			check(Section);
			Section->Remove(*Key);

			FScriptArrayHelper_InContainer ArrayHelper(Array, this);
			for (int32 i = 0; i < ArrayHelper.Num(); i++)
			{
				FString	Buffer;
				Array->Inner->ExportTextItem_Direct(Buffer, ArrayHelper.GetRawPtr(i), ArrayHelper.GetRawPtr(i), this, PortFlags);
				Section->Add(*Key, *Buffer);
			}
		}
		else if (UInterchangePipelineBase* SubPipeline = SubObject ? Cast<UInterchangePipelineBase>(SubObject->GetObjectPropertyValue_InContainer(this)) : nullptr)
		{
			// Save the settings if the referenced pipeline is a subobject of ours
			if (SubPipeline->IsInOuter(this))
			{
				SubPipeline->SaveSettingsInternal(PipelineStackName, ConfigFilename);
			}
		}
		else
		{
			TCHAR TempKey[MAX_SPRINTF] = TEXT("");
			for (int32 Index = 0; Index < Property->ArrayDim; Index++)
			{
				if (Property->ArrayDim != 1)
				{
					FCString::Sprintf(TempKey, TEXT("%s[%i]"), *Property->GetName(), Index);
					Key = TempKey;
				}

				FString	Value;
				Property->ExportText_InContainer(Index, Value, this, this, this, PortFlags);
				GConfig->SetString(*SectionName, *Key, *Value, ConfigFilename);
			}
		}
	}
	GConfig->Flush(0);
}
