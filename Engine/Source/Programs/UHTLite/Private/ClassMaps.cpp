// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassMaps.h"
#include "UnrealHeaderTool.h"
#include "UnrealTypeDefinitionInfo.h"

void FClassDeclarations::AddIfMissing(FName Name, TUniqueFunction<TSharedRef<FClassDeclarationMetaData>()>&& DeclConstructFunc)
{
	FRWScopeLock Lock(ClassDeclLock, SLT_Write);
	if (!ClassDeclarations.Contains(Name))
	{
		TSharedRef<FClassDeclarationMetaData> ClassDecl = DeclConstructFunc();
		ClassDeclarations.Add(Name, MoveTemp(ClassDecl));
	}
}

FClassDeclarationMetaData* FClassDeclarations::Find(FName Name) const
{
	FRWScopeLock Lock(ClassDeclLock, SLT_ReadOnly);
	if (const TSharedRef<FClassDeclarationMetaData>* ClassDecl = ClassDeclarations.Find(Name))
	{
		return &ClassDecl->Get();
	}
	return nullptr;
}

FClassDeclarationMetaData& FClassDeclarations::FindChecked(FName Name) const
{
	FRWScopeLock Lock(ClassDeclLock, SLT_ReadOnly);
	return ClassDeclarations.FindChecked(Name).Get();
}
