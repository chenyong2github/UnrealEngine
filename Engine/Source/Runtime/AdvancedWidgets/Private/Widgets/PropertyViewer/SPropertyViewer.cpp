// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/PropertyViewer/SPropertyViewer.h"
#include "Widgets/PropertyViewer/SPropertyViewerImpl.h"

#define LOCTEXT_NAMESPACE "SPropertyViewer"

namespace UE::PropertyViewer
{

void SPropertyViewer::ConstructInternal(const FArguments& InArgs)
{
	ChildSlot
	[
		Implementation->Construct(InArgs)
	];
}

void SPropertyViewer::Construct(const FArguments& InArgs)
{
	Implementation = MakeShared<Private::FPropertyViewerImpl>(InArgs);
	ConstructInternal(InArgs);
}
void SPropertyViewer::Construct(const FArguments& InArgs, const UScriptStruct* Struct)
{
	Implementation = MakeShared<Private::FPropertyViewerImpl>(InArgs);
	Implementation->AddContainer(MakeContainerIdentifier(), Struct);
	ConstructInternal(InArgs);
}
void SPropertyViewer::Construct(const FArguments& InArgs, const UScriptStruct* Struct, void* InData)
{
	Implementation = MakeShared<Private::FPropertyViewerImpl>(InArgs);
	Implementation->AddContainerInstance(MakeContainerIdentifier(), Struct, InData);
	ConstructInternal(InArgs);
}
void SPropertyViewer::Construct(const FArguments& InArgs, const UClass* Class)
{
	Implementation = MakeShared<Private::FPropertyViewerImpl>(InArgs);
	Implementation->AddContainer(MakeContainerIdentifier(), Class);
	ConstructInternal(InArgs);
}
void SPropertyViewer::Construct(const FArguments& InArgs, UObject* ObjectInstance)
{
	Implementation = MakeShared<Private::FPropertyViewerImpl>(InArgs);
	Implementation->AddContainerInstance(MakeContainerIdentifier(), ObjectInstance);
	ConstructInternal(InArgs);
}
void SPropertyViewer::Construct(const FArguments& InArgs, const UFunction* Function)
{
	Implementation = MakeShared<Private::FPropertyViewerImpl>(InArgs);
	Implementation->AddContainer(MakeContainerIdentifier(), Function);
	ConstructInternal(InArgs);
}


SPropertyViewer::FHandle SPropertyViewer::AddContainer(const UScriptStruct* Struct)
{
	SPropertyViewer::FHandle Result = MakeContainerIdentifier();
	Implementation->AddContainer(Result, Struct);
	return Result;
}
SPropertyViewer::FHandle SPropertyViewer::AddContainer(const UClass* Class)
{
	SPropertyViewer::FHandle Result = MakeContainerIdentifier();
	Implementation->AddContainer(Result, Class);
	return Result;
}
SPropertyViewer::FHandle SPropertyViewer::AddContainer(const UFunction* Function)
{
	SPropertyViewer::FHandle Result = MakeContainerIdentifier();
	Implementation->AddContainer(Result, Function);
	return Result;
}


SPropertyViewer::FHandle SPropertyViewer::AddInstance(const UScriptStruct* Struct, void* InData)
{
	check(InData);
	SPropertyViewer::FHandle Result = MakeContainerIdentifier();
	Implementation->AddContainerInstance(Result, Struct, InData);
	return Result;
}
SPropertyViewer::FHandle SPropertyViewer::AddInstance(UObject* ObjectInstance)
{
	SPropertyViewer::FHandle Result = MakeContainerIdentifier();
	Implementation->AddContainerInstance(Result, ObjectInstance);
	return Result;
}


void SPropertyViewer::Remove(FHandle Identifier)
{
	Implementation->Remove(Identifier);
}


void SPropertyViewer::RemoveAll()
{

	Implementation->RemoveAll();
}


SPropertyViewer::FHandle SPropertyViewer::MakeContainerIdentifier()
{
	static int32 IdentifierGenerator = 0;
	SPropertyViewer::FHandle Result;
	++IdentifierGenerator;
	Result.Id = IdentifierGenerator;
	return Result;
}

} //namespace

#undef LOCTEXT_NAMESPACE
