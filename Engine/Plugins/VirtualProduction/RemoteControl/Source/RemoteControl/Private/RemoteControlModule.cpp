// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRemoteControlModule.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"

#include "StructSerializer.h"
#include "StructDeserializer.h"
#include "IStructSerializerBackend.h"

#if WITH_EDITOR
	#include "ScopedTransaction.h"
#endif

DEFINE_LOG_CATEGORY(LogRemoteControl);
#define LOCTEXT_NAMESPACE "RemoteControl"

namespace RemoteControlUtil
{
	const FName NAME_DisplayName(TEXT("DisplayName"));
	const FName NAME_ScriptName(TEXT("ScriptName"));
	const FName NAME_ScriptNoExport(TEXT("ScriptNoExport"));
	const FName NAME_DeprecatedFunction(TEXT("DeprecatedFunction"));
	const FName NAME_BlueprintGetter(TEXT("BlueprintGetter"));
	const FName NAME_BlueprintSetter(TEXT("BlueprintSetter"));

	bool CompareFunctionName(const FString& FunctionName, const FString& ScriptName)
	{
		int32 SemiColonIndex = INDEX_NONE;
		if (ScriptName.FindChar(TEXT(';'), SemiColonIndex))
		{
			return FCString::Strncmp(*ScriptName, *FunctionName, SemiColonIndex) == 0;
		}
		return ScriptName.Equals(FunctionName);
	}

	UFunction* FindFunctionByNameOrMetaDataName(UObject* Object, const FString& FunctionName)
	{
		UFunction* Function = Object->FindFunction(FName(*FunctionName));
		// if the function wasn't found through the function map, try finding it through its `ScriptName` or `DisplayName` metadata
		if (Function == nullptr)
		{
			for (TFieldIterator<UFunction> FuncIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); FuncIt; ++FuncIt)
			{
				if (FuncIt->HasMetaData(NAME_ScriptName) && CompareFunctionName(FunctionName, FuncIt->GetMetaData(NAME_ScriptName)))
				{
					Function = *FuncIt;
					break;
				}
				else if (FuncIt->HasMetaData(NAME_DisplayName) && CompareFunctionName(FunctionName, FuncIt->GetMetaData(NAME_DisplayName)))
				{
					Function = *FuncIt;
					break;
				}
			}
		}
		return Function;
	}

	bool IsPropertyAllowed(const FProperty* InProperty, ERCAccess InAccessType, bool bObjectInGamePackage)
	{

		// The property is allowed to be accessed if it exists...
		return InProperty && 
			// it doesn't have exposed getter/setter that should be used instead
#if WITH_EDITOR
			(!InProperty->HasMetaData(RemoteControlUtil::NAME_BlueprintGetter) || !InProperty->HasMetaData(RemoteControlUtil::NAME_BlueprintSetter)) &&
#endif
			// it isn't private or protected
			!InProperty->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected | CPF_NativeAccessSpecifierPrivate) &&
			// and it's either blueprint visible if in game or editable if in editor and it isn't read only if the access type is write
			(bObjectInGamePackage ?
				InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible) && (InAccessType == ERCAccess::READ_ACCESS || !InProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly)) :
				InProperty->HasAnyPropertyFlags(CPF_Edit) && (InAccessType == ERCAccess::READ_ACCESS || !InProperty->HasAnyPropertyFlags(CPF_EditConst)));
	};
}

/**
 * Implementation of the RemoteControl interface
 */
class FRemoteControlModule : public IRemoteControlModule
{
public:

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}

	virtual bool ResolveCall(const FString& ObjectPath, const FString& FunctionName, FRCCallReference& OutCallRef, FString* OutErrorText) override
	{
		bool bSuccess = true;
		FString ErrorText;
		if (!GIsSavingPackage && !IsGarbageCollecting())
		{
			// Resolve the object
			UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (Object)
			{
				// Find the function to call
				UFunction* Function = RemoteControlUtil::FindFunctionByNameOrMetaDataName(Object, FunctionName);

				if (!Function)
				{
					ErrorText = FString::Printf(TEXT("function: %s does not exist on object: %s"), *ObjectPath, *FunctionName);
					bSuccess = false;
				}
				else if (!Function->HasAllFunctionFlags(FUNC_BlueprintCallable | FUNC_Public) ||
					Function->HasMetaData(RemoteControlUtil::NAME_DeprecatedFunction) ||
					Function->HasMetaData(RemoteControlUtil::NAME_ScriptNoExport))
				{
					ErrorText = FString::Printf(TEXT("function: %s is deprecated or unavailable remotely on object: %s"), *ObjectPath, *FunctionName);
					bSuccess = false;
				}
				else
				{
					OutCallRef.Object = Object;
					OutCallRef.Function = Function;
				}
			}
			else
			{
				ErrorText = FString::Printf(TEXT("object: %s does not exists."), *ObjectPath);
				bSuccess = false;
			}
		}
		else
		{
			ErrorText = FString::Printf(TEXT("can't resolve object: %s while saving or garbage collecting."), *ObjectPath);
			bSuccess = false;
		}

		if (OutErrorText && !ErrorText.IsEmpty())
		{
			*OutErrorText = MoveTemp(ErrorText);
		}
		return bSuccess;
	}

	virtual bool InvokeCall(FRCCall& InCall) override
	{
		if (InCall.IsValid())
		{
#if WITH_EDITOR
			FScopedTransaction Transaction(LOCTEXT("RemoteCallTransaction", "Remote Call Transaction Wrap"), InCall.bGenerateTransaction);
#endif
			if (InCall.bGenerateTransaction)
			{
				InCall.CallRef.Object->Modify();
			}

			FEditorScriptExecutionGuard ScriptGuard;
			InCall.CallRef.Object->ProcessEvent(InCall.CallRef.Function.Get(), InCall.ParamStruct.GetStructMemory());
			return true;
		}
		return false;
	}

	virtual bool ResolveObject(ERCAccess AccessType, const FString& ObjectPath, const FString& PropertyName, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) override
	{
		bool bSuccess = true;
		FString ErrorText;
		if (!GIsSavingPackage && !IsGarbageCollecting())
		{
			// Resolve the object
			UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (Object)
			{
				bool bObjectInGame = !GIsEditor || Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
				if (!PropertyName.IsEmpty())
				{
					FProperty* Property = Object->GetClass()->FindPropertyByName(*PropertyName);
					if (Property && RemoteControlUtil::IsPropertyAllowed(Property, AccessType, bObjectInGame))
					{
						OutObjectRef.Object = Object;
						OutObjectRef.Property = Property;
						OutObjectRef.Access = AccessType;
					}
					else
					{
						ErrorText = FString::Printf(TEXT("object property: %s is unavailable remotely on object: %s"), *PropertyName, *ObjectPath);
						bSuccess = false;
					}
				}
				else
				{
					OutObjectRef.Object = Object;
					OutObjectRef.Access = AccessType;
				}
			}
			else
			{
				ErrorText = FString::Printf(TEXT("object: %s does not exists when trying to resolve property: %s"), *ObjectPath, *PropertyName);
				bSuccess = false;
			}
		}
		else
		{
			ErrorText = FString::Printf(TEXT("can't resolve object: %s while saving or garbage collecting."), *ObjectPath);
			bSuccess = false;
		}

		if (OutErrorText && !ErrorText.IsEmpty())
		{
			*OutErrorText = MoveTemp(ErrorText);
		}

		return bSuccess;
	}

	virtual bool GetObjectProperties(const FRCObjectReference& ObjectAccess, IStructSerializerBackend& Backend) override
	{
		if (ObjectAccess.IsValid() && ObjectAccess.Access == ERCAccess::READ_ACCESS)
		{
			UObject* Object = ObjectAccess.Object.Get();
			FStructSerializerPolicies Policies;
			if (ObjectAccess.Property.IsValid())
			{
				Policies.PropertyFilter = [&ObjectAccess](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return CurrentProp == ObjectAccess.Property || ParentProp != nullptr;
				};
			}
			else
			{
				bool bObjectInGame = !GIsEditor || Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
				Policies.PropertyFilter = [&ObjectAccess, bObjectInGame](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return RemoteControlUtil::IsPropertyAllowed(CurrentProp, ObjectAccess.Access, bObjectInGame) || ParentProp != nullptr;
				};
			}

			FStructSerializer::Serialize((const void*)Object, *Object->GetClass(), Backend, Policies);
			return true;
		}
		return false;
	}

	virtual bool SetObjectProperties(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend) override
	{
		if (ObjectAccess.IsValid() && (ObjectAccess.Access == ERCAccess::WRITE_ACCESS || ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS))
		{
			bool bGenerateTransaction = ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
#if WITH_EDITOR
			FScopedTransaction Transaction(LOCTEXT("RemoteSetPropertyTransaction", "Remote Set Object Property"), bGenerateTransaction);
#endif
			UObject* Object = ObjectAccess.Object.Get();
			if (bGenerateTransaction)
			{
				Object->Modify();
				Object->PreEditChange(ObjectAccess.Property.Get());
			}

			FStructDeserializerPolicies Policies;
			if (ObjectAccess.Property.IsValid())
			{
				Policies.PropertyFilter = [&ObjectAccess](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return CurrentProp == ObjectAccess.Property || ParentProp != nullptr;
				};
			}
			else
			{
				bool bObjectInGame = !GIsEditor || Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
				Policies.PropertyFilter = [&ObjectAccess, bObjectInGame](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return RemoteControlUtil::IsPropertyAllowed(CurrentProp, ObjectAccess.Access, bObjectInGame) || ParentProp != nullptr;
				};
			}
			bool bSuccess = FStructDeserializer::Deserialize((void*)Object, *Object->GetClass(), Backend, Policies);

			// if we are generating a transaction, also generate post edit property event, event if the change ended up unsuccessful
			// this is to match the pre edit change call that can unregister components for example
			if (bGenerateTransaction)
			{
				FPropertyChangedEvent PropertyEvent(ObjectAccess.Property.Get());
				Object->PostEditChangeProperty(PropertyEvent);
			}
			return bSuccess;
		}
		return false;
	}

	virtual bool ResetObjectProperties(const FRCObjectReference& ObjectAccess) override
	{
		if (ObjectAccess.IsValid() && (ObjectAccess.Access == ERCAccess::WRITE_ACCESS || ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS))
		{
			bool bGenerateTransaction = ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
#if WITH_EDITOR
			FScopedTransaction Transaction(LOCTEXT("RemoteResetPropertyTransaction", "Remote Reset Object Property"), bGenerateTransaction);
#endif
			UObject* Object = ObjectAccess.Object.Get();
			if (bGenerateTransaction)
			{
				Object->Modify();
				Object->PreEditChange(ObjectAccess.Property.Get());
			}
					
			ObjectAccess.Property->InitializeValue(ObjectAccess.Property->template ContainerPtrToValuePtr<void>(ObjectAccess.Object.Get()));

			// if we are generating a transaction, also generate post edit property event, event if the change ended up unsuccessful
			// this is to match the pre edit change call that can unregister components for example
			if (bGenerateTransaction)
			{
				FPropertyChangedEvent PropertyEvent(ObjectAccess.Property.Get());
				Object->PostEditChangeProperty(PropertyEvent);
			}
			return true;
		}
		return false;
	}


private:
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRemoteControlModule, RemoteControl);
