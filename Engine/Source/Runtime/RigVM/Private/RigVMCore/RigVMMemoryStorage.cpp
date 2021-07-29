// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMMemoryStorage.h"
#include "RigVMModule.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

const FRigVMPropertyPath FRigVMMemoryHandle::EmptyPropertyPath;

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const FString FRigVMPropertyDescription::ArrayPrefix = TEXT("TArray<");
const FString FRigVMPropertyDescription::MapPrefix = TEXT("TMap<");
const FString FRigVMPropertyDescription::SetPrefix = TEXT("TSet<");
const FString FRigVMPropertyDescription::ContainerSuffix = TEXT(">");

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FRigVMPropertyDescription::FRigVMPropertyDescription(const FProperty* InProperty, const FString& InDefaultValue, const FName& InName)
	: Name(InName)
	, Property(InProperty)
	, CPPType()
	, CPPTypeObject(nullptr)
	, Containers()
	, DefaultValue(InDefaultValue)
{
	SanitizeName();

	const FProperty* ChildProperty = InProperty;
	do
	{
		if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ChildProperty))
		{
			Containers.Add(EPinContainerType::Array);
			ChildProperty = ArrayProperty->Inner;
		}
		else if(const FSetProperty* SetProperty = CastField<FSetProperty>(ChildProperty))
		{
			Containers.Add(EPinContainerType::Set);
			ChildProperty = SetProperty->ElementProp;
		}
		else if(const FMapProperty* MapProperty = CastField<FMapProperty>(ChildProperty))
		{
			Containers.Add(EPinContainerType::Map);
			ChildProperty = MapProperty->ValueProp;
		}
		else
		{
			ChildProperty = nullptr;
		}
	}
	while (ChildProperty);
}

FRigVMPropertyDescription::FRigVMPropertyDescription(const FName& InName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue)
	: Name(InName)
	, Property(nullptr)
	, CPPType(InCPPType)
	, CPPTypeObject(InCPPTypeObject)
	, Containers()
	, DefaultValue(InDefaultValue)
{
	SanitizeName();

	FString BaseCPPType = CPPType;

	do
	{
		if(BaseCPPType.RemoveFromStart(ArrayPrefix))
		{
			Containers.Add(EPinContainerType::Array);
			check(BaseCPPType.RemoveFromEnd(ContainerSuffix));
		}
		else if(BaseCPPType.RemoveFromStart(MapPrefix))
		{
			Containers.Add(EPinContainerType::Map);
			check(BaseCPPType.RemoveFromEnd(ContainerSuffix));
		}
		else if(BaseCPPType.RemoveFromStart(SetPrefix))
		{
			Containers.Add(EPinContainerType::Set);
			check(BaseCPPType.RemoveFromEnd(ContainerSuffix));
		}
		else
		{
			break;
		}
	}
	while(!BaseCPPType.IsEmpty());
}

FName FRigVMPropertyDescription::SanitizeName(const FName& InName)
{
	FString NameString = InName.ToString();

	// Sanitize the name
	for (int32 i = 0; i < NameString.Len(); ++i)
	{
		TCHAR& C = NameString[i];

		const bool bGoodChar = FChar::IsAlpha(C) ||							// Any letter
			(C == '_') || 													// _ anytime
			((i > 0) && (FChar::IsDigit(C)));								// 0-9 after the first character

		if (!bGoodChar)
		{
			C = '_';
		}
	}

	if(NameString != InName.ToString())
	{
		return *NameString;
	}

	return InName;
}

void FRigVMPropertyDescription::SanitizeName()
{
	Name = SanitizeName(Name);
}

FString FRigVMPropertyDescription::GetBaseCPPType() const
{
	FString BaseCPPType = CPPType;

	for(EPinContainerType Container : Containers)
	{
		switch(Container)
		{
			case EPinContainerType::Array:
			{
				check(BaseCPPType.RemoveFromStart(ArrayPrefix))
				check(BaseCPPType.RemoveFromEnd(ContainerSuffix));
				break;
			}		
			case EPinContainerType::Map:
			{
				check(BaseCPPType.RemoveFromStart(MapPrefix))
				check(BaseCPPType.RemoveFromEnd(ContainerSuffix));
				break;
			}		
			case EPinContainerType::Set:
			{
				check(BaseCPPType.RemoveFromStart(SetPrefix))
				check(BaseCPPType.RemoveFromEnd(ContainerSuffix));
				break;
			}		
			case EPinContainerType::None:
			default:
			{
				break;
			}
		}
	}
	
	return BaseCPPType;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void URigVMMemoryStorageGeneratorClass::PurgeClass(bool bRecompilingOnLoad)
{
	Super::PurgeClass(bRecompilingOnLoad);
	LinkedProperties.Reset();
	PropertyPaths.Reset();
	PropertyPathDescriptions.Reset();
}

void URigVMMemoryStorageGeneratorClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	// Force assembly of the reference token stream so that we can be properly handled by the
	// garbage collector.
	AssembleReferenceTokenStream(/*bForce=*/true);

	// Setup the LinkedProperties
	LinkedProperties.Reset();
	const FProperty* Property = CastField<FProperty>(ChildProperties);
	while(Property)
	{
		LinkedProperties.Add(Property);
		Property = CastField<FProperty>(Property->Next);
	}

	RefreshPropertyPaths();
}

void URigVMMemoryStorageGeneratorClass::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if(Ar.IsLoading() || Ar.IsSaving())
	{
		Ar << PropertyPathDescriptions;
		Ar << MemoryType;
	}
}

FString URigVMMemoryStorageGeneratorClass::GetClassName(ERigVMMemoryType InMemoryType)
{
	return TEXT("RigVMMemory_") + StaticEnum<ERigVMMemoryType>()->GetDisplayNameTextByValue((int64)InMemoryType).ToString();
}

URigVMMemoryStorageGeneratorClass* URigVMMemoryStorageGeneratorClass::GetStorageClass(UObject* InOuter, ERigVMMemoryType InMemoryType)
{
	check(InOuter);
	UPackage* Package = InOuter->GetOutermost();

	const FString ClassName = GetClassName(InMemoryType);
	return FindObject<URigVMMemoryStorageGeneratorClass>(Package, *ClassName);
}

URigVMMemoryStorageGeneratorClass* URigVMMemoryStorageGeneratorClass::CreateStorageClass(
	UObject* InOuter,
	ERigVMMemoryType InMemoryType,
	const TArray<FRigVMPropertyDescription>& InProperties,
	const TArray<FRigVMPropertyPathDescription>& InPropertyPaths)
{
	check(InOuter);
	UPackage* Package = InOuter->GetOutermost(); 
	UClass *SuperClass = URigVMMemoryStorage::StaticClass();

	const FString ClassName = GetClassName(InMemoryType);
	
	URigVMMemoryStorageGeneratorClass* OldClass = FindObject<URigVMMemoryStorageGeneratorClass>(Package, *ClassName);
	if(OldClass)
	{
		OldClass->RemoveFromRoot();
		OldClass->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		OldClass->MarkPendingKill();
	}

	URigVMMemoryStorageGeneratorClass* Class = NewObject<URigVMMemoryStorageGeneratorClass>(
		Package,
		*ClassName,
		RF_Standalone | RF_Public
	);

	Class->AddToRoot();

	Class->PurgeClass(false);
	Class->SetSuperStruct(SuperClass);
	Class->PropertyLink = SuperClass->PropertyLink;
	Class->ClassWithin = UObject::StaticClass();
	Class->ClassFlags |= CLASS_NotPlaceable;
	Class->MemoryType = InMemoryType;

	// Generate properties
	FField** LinkToProperty = &Class->ChildProperties;

	for(const FRigVMPropertyDescription& PropertyDescription : InProperties)
	{
		FProperty* CachedProperty = AddProperty(Class, PropertyDescription, LinkToProperty);
		check(CachedProperty);
	}

	// Store the property path descriptions
	Class->PropertyPathDescriptions = InPropertyPaths;

	// Update the class
	Class->Bind();
	Class->StaticLink(true);
	
	// Create default object
	URigVMMemoryStorage* CDO = Cast<URigVMMemoryStorage>(Class->GetDefaultObject(true));

	// and store default values.
	const TArray<const FProperty*>& LinkedProperties = CDO->GetProperties();
	for(int32 PropertyIndex = 0; PropertyIndex < LinkedProperties.Num(); PropertyIndex++)
	{
		const FString& DefaultValue = InProperties[PropertyIndex].DefaultValue;
		if(DefaultValue.IsEmpty())
		{
			continue;
		}

		const FProperty* Property = LinkedProperties[PropertyIndex];
		uint8* ValuePtr = Property->ContainerPtrToValuePtr<uint8>(CDO);

		Property->ImportText(*DefaultValue, ValuePtr, EPropertyPortFlags::PPF_None, nullptr);
	}

	/*
	if(OldClass)
	{
		UEngine::FCopyPropertiesForUnrelatedObjectsParams Params;
		Params.bAggressiveDefaultSubobjectReplacement = false;
		Params.bDoDelta = false;
		Params.bCopyDeprecatedProperties = true;
		Params.bSkipCompilerGeneratedDefaults = true;
		Params.bNotifyObjectReplacement = true;

		Params.bClearReferences = true;
		UEngine::CopyPropertiesForUnrelatedObjects(OldClass->GetDefaultObject(), CDO, Params);

		// we might need to run this again on the old instances,
		// but the VM compiler should take care of it anyway
	}
	*/
	
	return Class;
}

FProperty* URigVMMemoryStorageGeneratorClass::AddProperty(URigVMMemoryStorageGeneratorClass* InClass, const FRigVMPropertyDescription& InProperty, FField** LinkToProperty)
{
	UClass *SuperClass = URigVMMemoryStorage::StaticClass();
	
	check(InClass);
	check(InClass->GetSuperClass() == SuperClass);

	if(LinkToProperty == nullptr)
	{
		LinkToProperty = &InClass->ChildProperties;
	}
	while (*LinkToProperty != nullptr)
	{
		LinkToProperty = &(*LinkToProperty)->Next;
	}

	FProperty* Result = nullptr;
	if(InProperty.Property)
	{
		FProperty* NewProperty = CastFieldChecked<FProperty>(FField::Duplicate(InProperty.Property, InClass, InProperty.Name));
		check(NewProperty);

		Result = NewProperty;
		*LinkToProperty = NewProperty;
	}
	else
	{
		FFieldVariant PropertyOwner = InClass;
		// FProperty** KeyPropertyPtr = nullptr;
		FProperty** ValuePropertyPtr = &Result;

		for(EPinContainerType Container : InProperty.Containers)
		{
			switch(Container)
			{
				case EPinContainerType::Array:
				{
					FArrayProperty* ArrayProperty = new FArrayProperty(PropertyOwner, InProperty.Name, RF_Public);
					*ValuePropertyPtr = ArrayProperty;
					ValuePropertyPtr = &ArrayProperty->Inner;
					PropertyOwner = ArrayProperty;
					break;
				}
				case EPinContainerType::Map:
				{
					checkNoEntry(); // this is not implemented yet
					FMapProperty* MapProperty = new FMapProperty(PropertyOwner, InProperty.Name, RF_Public);
					*ValuePropertyPtr = MapProperty;
					// KeyPropertyPtr = &MapProperty->KeyProp;
					ValuePropertyPtr = &MapProperty->ValueProp;
					PropertyOwner = MapProperty;
					break;
				}
				case EPinContainerType::Set:
				{
					FSetProperty* SetProperty = new FSetProperty(PropertyOwner, InProperty.Name, RF_Public);
					*ValuePropertyPtr = SetProperty;
					ValuePropertyPtr = &SetProperty->ElementProp;
					PropertyOwner = SetProperty;
					break;
				}
				case EPinContainerType::None:
				default:
				{
					break;
				}
			}
		}

		if(InProperty.CPPTypeObject != nullptr)
		{
			if(UEnum* Enum = Cast<UEnum>(InProperty.CPPTypeObject))
			{
				FByteProperty* EnumProperty = new FByteProperty(PropertyOwner, InProperty.Name, RF_Public);
				EnumProperty->Enum = Enum;
				(*ValuePropertyPtr) = EnumProperty;
			}
			else if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InProperty.CPPTypeObject))
			{
				FStructProperty* StructProperty = new FStructProperty(PropertyOwner, InProperty.Name, RF_Public);
				StructProperty->Struct = ScriptStruct;
				(*ValuePropertyPtr) = StructProperty;
			}
			else if(UClass* PropertyClass = Cast<UClass>(InProperty.CPPTypeObject))
			{
				FObjectProperty* ObjectProperty = new FObjectProperty(PropertyOwner, InProperty.Name, RF_Public);
				ObjectProperty->SetPropertyClass(PropertyClass);
				(*ValuePropertyPtr) = ObjectProperty;
			}
			else
			{
				checkNoEntry();
			}
		}
		else // take care of default types...
		{
			static FString BoolString = TEXT("bool");
			static FString Int32String = TEXT("int32");
			static FString IntString = TEXT("int");
			static FString FloatString = TEXT("float");
			static FString DoubleString = TEXT("double");
			static FString StringString = TEXT("FString");
			static FString NameString = TEXT("FName");

			const FString BaseCPPType = InProperty.GetBaseCPPType();
			if(BaseCPPType.Equals(BoolString, ESearchCase::IgnoreCase))
			{
				(*ValuePropertyPtr) = new FBoolProperty(PropertyOwner, InProperty.Name, RF_Public);;
			}
			else if(BaseCPPType.Equals(Int32String, ESearchCase::IgnoreCase) ||
				BaseCPPType.Equals(IntString, ESearchCase::IgnoreCase))
			{
				(*ValuePropertyPtr) = new FIntProperty(PropertyOwner, InProperty.Name, RF_Public);;
			}
			else if(BaseCPPType.Equals(FloatString, ESearchCase::IgnoreCase))
			{
				(*ValuePropertyPtr) = new FFloatProperty(PropertyOwner, InProperty.Name, RF_Public);;
			}
			else if(BaseCPPType.Equals(DoubleString, ESearchCase::IgnoreCase))
			{
				(*ValuePropertyPtr) = new FDoubleProperty(PropertyOwner, InProperty.Name, RF_Public);;
			}
			else if(BaseCPPType.Equals(StringString, ESearchCase::IgnoreCase))
			{
				(*ValuePropertyPtr) = new FStrProperty(PropertyOwner, InProperty.Name, RF_Public);;
			}
			else if(BaseCPPType.Equals(NameString, ESearchCase::IgnoreCase))
			{
				(*ValuePropertyPtr) = new FNameProperty(PropertyOwner, InProperty.Name, RF_Public);;
			}
			else
			{
				checkNoEntry();
			}
		}

		Result->SetPropertyFlags(CPF_Edit | CPF_NonTransactional);
		(*LinkToProperty) = Result;
	}

	return Result;
}

void URigVMMemoryStorageGeneratorClass::RefreshPropertyPaths()
{
	// Update the property paths based on the descriptions
	PropertyPaths.SetNumZeroed(PropertyPathDescriptions.Num());
	for(int32 PropertyPathIndex = 0; PropertyPathIndex < PropertyPaths.Num(); PropertyPathIndex++)
	{
		PropertyPaths[PropertyPathIndex] = FRigVMPropertyPath();

		const int32 PropertyIndex = PropertyPathDescriptions[PropertyPathIndex].PropertyIndex;
		if(LinkedProperties.IsValidIndex(PropertyIndex))
		{
			PropertyPaths[PropertyPathIndex] = FRigVMPropertyPath(
				LinkedProperties[PropertyIndex],
				PropertyPathDescriptions[PropertyPathIndex].SegmentPath);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<const FProperty*> URigVMMemoryStorage::EmptyProperties;
const TArray<FRigVMPropertyPath> URigVMMemoryStorage::EmptyPropertyPaths;

FString URigVMMemoryStorage::GetDataAsString(int32 InPropertyIndex)
{
	check(IsValidIndex(InPropertyIndex));
	const uint8* Data = GetData<uint8>(InPropertyIndex);

	FString Value;
	GetProperties()[InPropertyIndex]->ExportTextItem(Value, Data, nullptr, nullptr, PPF_None);
	return Value;
}

class FRigVMMemoryStorageImportErrorContext : public FOutputDevice
{
public:

	int32 NumErrors;

	FRigVMMemoryStorageImportErrorContext()
		: FOutputDevice()
		, NumErrors(0)
	{
	}

	FORCEINLINE_DEBUGGABLE void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
#if WITH_EDITOR
		UE_LOG(LogRigVM, Display, TEXT("Skipping Importing To MemoryStorage: %s"), V);
#else
		UE_LOG(LogRigVM, Error, TEXT("Error Importing To MemoryStorage: %s"), V);
#endif
		NumErrors++;
	}
};

bool URigVMMemoryStorage::SetDataFromString(int32 InPropertyIndex, const FString& InValue)
{
	check(IsValidIndex(InPropertyIndex));
	uint8* Data = GetData<uint8>(InPropertyIndex);
	
	FRigVMMemoryStorageImportErrorContext ErrorPipe;
	GetProperties()[InPropertyIndex]->ImportText(*InValue, Data, EPropertyPortFlags::PPF_None, nullptr, &ErrorPipe);
	return ErrorPipe.NumErrors == 0;
}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

FRigVMMemoryHandle URigVMMemoryStorage::GetHandle(int32 InPropertyIndex, const FRigVMPropertyPath* InPropertyPath)
{
	check(IsValidIndex(InPropertyIndex));

	const FProperty* Property = GetProperties()[InPropertyIndex];
	uint8* Data = GetData<uint8>(InPropertyIndex);

	return FRigVMMemoryHandle(Data, Property, InPropertyPath);
}

#endif

bool URigVMMemoryStorage::CopyProperty(
	const FProperty* InTargetProperty,
	uint8* InTargetPtr,
	const FProperty* InSourceProperty,
	const uint8* InSourcePtr)
{
	check(InTargetProperty != nullptr);
	check(InSourceProperty != nullptr);
	check(InTargetPtr != nullptr);
	check(InSourcePtr != nullptr);

	if(!ensure(InTargetProperty->SameType(InSourceProperty)))
	{
		return false;
	}

	InTargetProperty->CopyCompleteValue(InTargetPtr, InSourcePtr);
	return true;
}

bool URigVMMemoryStorage::CopyProperty(
	const FProperty* InTargetProperty,
	uint8* InTargetPtr,
	const FRigVMPropertyPath& InTargetPropertyPath,
	const FProperty* InSourceProperty,
	const uint8* InSourcePtr,
	const FRigVMPropertyPath& InSourcePropertyPath)
{
	check(InTargetProperty != nullptr);
	check(InSourceProperty != nullptr);
	check(InTargetPtr != nullptr);
	check(InSourcePtr != nullptr);

	auto TraversePropertyPath = [](const FProperty*& Property, uint8*& MemoryPtr, const FRigVMPropertyPath& PropertyPath)
	{
		if(PropertyPath.IsEmpty())
		{
			return;
		}

		MemoryPtr = PropertyPath.GetData<uint8>(MemoryPtr, Property);
		Property = PropertyPath.GetTargetProperty();
	};

	uint8* SourcePtr = (uint8*)InSourcePtr;
	TraversePropertyPath(InTargetProperty, InTargetPtr, InTargetPropertyPath);
	TraversePropertyPath(InSourceProperty, SourcePtr, InSourcePropertyPath);

	return CopyProperty(InTargetProperty, InTargetPtr, InSourceProperty, SourcePtr);
}

bool URigVMMemoryStorage::CopyProperty(
	URigVMMemoryStorage* InTargetStorage,
	int32 InTargetPropertyIndex,
	const FRigVMPropertyPath& InTargetPropertyPath,
	URigVMMemoryStorage* InSourceStorage,
	int32 InSourcePropertyIndex,
	const FRigVMPropertyPath& InSourcePropertyPath)
{
	check(InTargetStorage != nullptr);
	check(InSourceStorage != nullptr);

	const FProperty* TargetProperty = InTargetStorage->GetProperties()[InTargetPropertyIndex];
	const FProperty* SourceProperty = InSourceStorage->GetProperties()[InSourcePropertyIndex];
	uint8* TargetPtr = TargetProperty->ContainerPtrToValuePtr<uint8>(InTargetStorage);
	uint8* SourcePtr = SourceProperty->ContainerPtrToValuePtr<uint8>(InSourceStorage);

	return CopyProperty(TargetProperty, TargetPtr, InTargetPropertyPath, SourceProperty, SourcePtr, InSourcePropertyPath);
}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

bool URigVMMemoryStorage::CopyProperty(
	FRigVMMemoryHandle& TargetHandle,
	FRigVMMemoryHandle& SourceHandle)
{
	return CopyProperty(
		TargetHandle.GetProperty(),
		TargetHandle.GetData(false),
		TargetHandle.GetPropertyPathRef(),
		SourceHandle.GetProperty(),
		SourceHandle.GetData(false),
		SourceHandle.GetPropertyPathRef());
}

#endif

const TArray<const FProperty*>& URigVMMemoryStorage::GetProperties() const
{
	if(const URigVMMemoryStorageGeneratorClass* Class = Cast<URigVMMemoryStorageGeneratorClass>(GetClass()))
	{
		return Class->GetProperties();
	}
	return EmptyProperties;
}

const TArray<FRigVMPropertyPath>& URigVMMemoryStorage::GetPropertyPaths() const
{
	if(const URigVMMemoryStorageGeneratorClass* Class = Cast<URigVMMemoryStorageGeneratorClass>(GetClass()))
	{
		return Class->GetPropertyPaths();
	}
	return EmptyPropertyPaths;
}

int32 URigVMMemoryStorage::GetPropertyIndex(const FProperty* InProperty) const
{
	return GetProperties().Find(InProperty);
}

int32 URigVMMemoryStorage::GetPropertyIndexByName(const FName& InName) const
{
	const FProperty* Property = FindPropertyByName(InName);
	return GetPropertyIndex(Property);
}

const FProperty* URigVMMemoryStorage::FindPropertyByName(const FName& InName) const
{
	const FName SanitizedName = FRigVMPropertyDescription::SanitizeName(InName);
	return GetClass()->FindPropertyByName(SanitizedName);
}
