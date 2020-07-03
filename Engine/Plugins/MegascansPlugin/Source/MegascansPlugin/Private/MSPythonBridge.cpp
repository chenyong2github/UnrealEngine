// Copyright Epic Games, Inc. All Rights Reserved.
#include "MSPythonBridge.h"

UMSPythonBridge* UMSPythonBridge::Get()
{
	TArray<UClass*> PythonBridgeClasses;
	GetDerivedClasses(UMSPythonBridge::StaticClass(), PythonBridgeClasses);
	int32 NumClasses = PythonBridgeClasses.Num();
	if (NumClasses > 0)
	{
		return Cast<UMSPythonBridge>(PythonBridgeClasses[NumClasses - 1]->GetDefaultObject());
	}
	return nullptr;
};

void UMSPythonBridge::CalledFromPython(FString InputString)
{
	UE_LOG(LogTemp, Error, TEXT("Hello from Python"));
}