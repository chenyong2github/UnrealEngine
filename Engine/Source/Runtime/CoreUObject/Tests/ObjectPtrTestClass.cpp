// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "ObjectPtrTestClass.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

//can not put the #if inside as the expansion of IMPLEMENT_CORE_INTRINSIC_CLASS fails
#if WITH_EDITORONLY_DATA
IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrTestClass, UObject,
	{
		auto MetaData = Class->GetOutermost()->GetMetaData();
		if (MetaData)
		{
			MetaData->SetValue(Class, TEXT("LoadBehavior"), TEXT("LazyOnDemand"));
		}
	}
);
#else
IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrTestClass, UObject, 
	{ 
	});
#endif


#if WITH_EDITORONLY_DATA
IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrTestClassWithRef, UObject,
	{
		//add reflection info for the two properties
		{
			UECodeGen_Private::FObjectPropertyParams Params = { };
			Params.NameUTF8 = "ObjectPtr";
			Params.Offset = STRUCT_OFFSET(UObjectPtrTestClassWithRef, ObjectPtr);
			Params.PropertyFlags = CPF_None;
			Params.ObjectFlags = RF_Public | RF_Transient | RF_MarkAsNative;
			Params.ClassFunc = nullptr;
			auto Property = new FObjectPtrProperty(Class, Params );
			Property->PropertyClass = UObjectPtrTestClass::StaticClass();
		}

		{

			UECodeGen_Private::FObjectPropertyParams Params = { };
			Params.NameUTF8 = "ObjectPtrNonNullable";
			Params.Offset = STRUCT_OFFSET(UObjectPtrTestClassWithRef, ObjectPtrNonNullable);
			Params.PropertyFlags = EPropertyFlags::CPF_NonNullable;
			Params.ObjectFlags = RF_Public | RF_Transient | RF_MarkAsNative;
			Params.ClassFunc = nullptr;
			auto Property = new FObjectPtrProperty(Class, Params);
			Property->PropertyClass = UObjectPtrTestClass::StaticClass();
		}


		{
			UECodeGen_Private::FArrayPropertyParams Params = { };
			Params.NameUTF8 = "ArrayObjPtr";
			Params.Offset = STRUCT_OFFSET(UObjectPtrTestClassWithRef, ArrayObjPtr);
			Params.PropertyFlags = CPF_None;
			Params.ObjectFlags = RF_Public | RF_Transient | RF_MarkAsNative;
			auto Property = new FArrayProperty(Class, Params);
			auto InnerProperty = new FObjectPtrProperty(Property, TEXT("Inner"), EObjectFlags::RF_NoFlags);
			InnerProperty->PropertyClass = UObjectPtrTestClass::StaticClass();
			Property->AddCppProperty(InnerProperty);
		}

		auto MetaData = Class->GetOutermost()->GetMetaData();
		if (MetaData)
		{
			MetaData->SetValue(Class, TEXT("LoadBehavior"), TEXT("LazyOnDemand"));
		}
	}
);
#else
IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrTestClassWithRef, UObject,
	{
		//add reflection info for the two properties
		auto Property1 = new FObjectPtrProperty(Class, TEXT("ObjectPtr"), EObjectFlags::RF_NoFlags);
		Property1->PropertyClass = UObjectPtrTestClass::StaticClass();
		Class->AddCppProperty(Property1);

		auto Property2 = new FObjectPtrProperty(Class, TEXT("ObjectPtrNonNullable"), EObjectFlags::RF_NoFlags);
		Property2->PropertyClass = UObjectPtrTestClass::StaticClass();
		Property2->SetPropertyFlags(EPropertyFlags::CPF_NonNullable); //make non nullable
		Class->AddCppProperty(Property2);
	});
#endif

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectWithClassProperty, UObject,
	{
		auto Property1 = new FClassPtrProperty(Class, TEXT("ClassPtr"), EObjectFlags::RF_NoFlags);
		Property1->PropertyClass = UClass::StaticClass();
		Class->AddCppProperty(Property1);
	}
);

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectWithRawProperty, UObject,
	{
		auto Property1 = new FObjectProperty(Class, TEXT("ObjectPtr"), EObjectFlags::RF_NoFlags);
		Property1->PropertyClass = UClass::StaticClass();
		Class->AddCppProperty(Property1);

		auto Property2 = new FObjectPtrProperty(Class, TEXT("ObjectPtrNonNullable"), EObjectFlags::RF_NoFlags);
		Property2->PropertyClass = UObjectPtrTestClass::StaticClass();
		Property2->SetPropertyFlags(EPropertyFlags::CPF_NonNullable); //make non nullable
		Class->AddCppProperty(Property2);
	}
);

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrDerrivedTestClass, UObjectPtrTestClass, {});

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrNotLazyTestClass, UObject, {});

#endif