// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassMaps.h"
#include "UnrealHeaderTool.h"
#include "UnrealTypeDefinitionInfo.h"

FUnrealSourceFiles GUnrealSourceFilesMap;
FTypeDefinitionInfoMap GTypeDefinitionInfoMap;
TMap<const UPackage*, TArray<UField*>> GPackageSingletons;
FCriticalSection GPackageSingletonsCriticalSection;
FPublicSourceFileSet GPublicSourceFileSet;
TMap<FProperty*, FString> GArrayDimensions;
TMap<UPackage*,  const FManifestModule*> GPackageToManifestModuleMap;
TMap<void*, uint32> GGeneratedCodeHashes;
FRWLock GGeneratedCodeHashesLock;
TMap<UEnum*,  EUnderlyingEnumType> GEnumUnderlyingTypes;
TMap<FName, TSharedRef<FClassDeclarationMetaData> > GClassDeclarations;
TSet<FProperty*> GUnsizedProperties;
TSet<UField*> GEditorOnlyDataTypes;
TMap<UStruct*, TTuple<TSharedRef<FUnrealSourceFile>, int32>> GStructToSourceLine;
TMap<UClass*, FArchiveTypeDefinePair> GClassSerializerMap;
TSet<FProperty*> GPropertyUsesMemoryImageAllocator;
