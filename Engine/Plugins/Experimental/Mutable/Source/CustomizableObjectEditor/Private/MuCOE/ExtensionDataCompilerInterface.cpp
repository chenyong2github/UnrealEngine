// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/ExtensionDataCompilerInterface.h"

#include "InstancedStruct.h"
#include "MuCO/CustomizableObjectStreamedExtensionData.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"
#include "MuR/ExtensionData.h"

FExtensionDataCompilerInterface::FExtensionDataCompilerInterface(FMutableGraphGenerationContext& InGenerationContext)
	: GenerationContext(InGenerationContext)
{
}

mu::ExtensionDataPtrConst FExtensionDataCompilerInterface::MakeStreamedExtensionData(UCustomizableObjectExtensionDataContainer*& OutContainer)
{
	mu::ExtensionDataPtr Result = new mu::ExtensionData;
	Result->Origin = mu::ExtensionData::EOrigin::ConstantStreamed;
	Result->Index = GenerationContext.StreamedExtensionData.Num();

	// Generate a deterministic name to help with deterministic cooking
	const FString ContainerName = FString::Printf(TEXT("Streamed_%d"), Result->Index);

	UObject* ExistingObject = FindObject<UObject>(GenerationContext.Object, *ContainerName);
	if (ExistingObject)
	{
		// This must have been left behind from a previous compilation and hasn't been deleted by 
		// GC yet.
		//
		// Move it into the transient package to get it out of the way.
		ExistingObject->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);

		check(!FindObject<UObject>(GenerationContext.Object, *ContainerName));
	}

	check(GenerationContext.Object);
	OutContainer = NewObject<UCustomizableObjectExtensionDataContainer>(
		GenerationContext.Object,
		FName(*ContainerName),
		RF_Public);

	GenerationContext.StreamedExtensionData.Add(OutContainer);

	return Result;
}

mu::ExtensionDataPtrConst FExtensionDataCompilerInterface::MakeAlwaysLoadedExtensionData(FInstancedStruct&& Data)
{
	mu::ExtensionDataPtr Result = new mu::ExtensionData;
	Result->Origin = mu::ExtensionData::EOrigin::ConstantAlwaysLoaded;
	Result->Index = GenerationContext.AlwaysLoadedExtensionData.Num();

	FCustomizableObjectExtensionData* CompileTimeExtensionData = &GenerationContext.AlwaysLoadedExtensionData.AddDefaulted_GetRef();
	CompileTimeExtensionData->Data = MoveTemp(Data);

	return Result;
}

UObject* FExtensionDataCompilerInterface::GetOuterForAlwaysLoadedObjects()
{
	check(GenerationContext.Object);
	return GenerationContext.Object;
}
