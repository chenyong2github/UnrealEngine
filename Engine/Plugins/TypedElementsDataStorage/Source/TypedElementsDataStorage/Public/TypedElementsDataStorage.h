// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

struct FMassEntityManager;
class FReferenceCollector;
class ITypedElementDataStorageInterface;
class UTypedElementDatabase;
class UTypedElementDatabaseCompatibility;

class FTypedElementsDataStorageModule : public IModuleInterface, public FGCObject
{
public:
	~FTypedElementsDataStorageModule() override = default;

	//
	// IModuleInterface
	//

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	
	//
	// FGCObject
	//
	
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	FString GetReferencerName() const override;

private:
	TObjectPtr<UTypedElementDatabase> Database;
	TObjectPtr<UTypedElementDatabaseCompatibility> DatabaseCompatibility;
	bool bInitialized{ false };
};
