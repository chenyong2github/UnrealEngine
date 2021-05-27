// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/CurveTableFactory.h"
#include "Engine/CurveTable.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "CurveTableFactory"

UCurveTableFactory::UCurveTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCurveTable::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UCurveTableFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UCurveTable* CurveTable = nullptr;
	if (ensure(SupportedClass == Class))
	{
		ensure(0 != (RF_Public & Flags));
		CurveTable = MakeNewCurveTable(InParent, Name, Flags);
	}
	return CurveTable;
}

UCurveTable* UCurveTableFactory::MakeNewCurveTable(UObject* InParent, FName Name, EObjectFlags Flags)
{
	return NewObject<UCurveTable>(InParent, Name, Flags);
}

#undef LOCTEXT_NAMESPACE 
