// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/LightWeightInstanceStaticMeshManager.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityTemplate.h"
#include "MassLWIStaticMeshManager.generated.h"


struct MASSLWI_API FMassLWIManagerRegistrationHandle
{
	explicit FMassLWIManagerRegistrationHandle(const int32 InRegisteredIndex = INDEX_NONE)
		: RegisteredIndex(InRegisteredIndex)
	{}

	operator int32() const { return RegisteredIndex; }
	bool IsValid() const { return RegisteredIndex != INDEX_NONE; }

private:
	const int32 RegisteredIndex = INDEX_NONE;
};

UCLASS()
class MASSLWI_API AMassLWIStaticMeshManager : public ALightWeightInstanceStaticMeshManager
{
	GENERATED_BODY()

public:
	bool IsRegisteredWithMass() const { return MassRegistrationHandle.IsValid(); }

	void TransferDataToMass(FMassEntityManager& EntityManager);
	FMassLWIManagerRegistrationHandle GetMassRegistrationHandle() const { return MassRegistrationHandle; }
	void MarkRegisteredWithMass(const FMassLWIManagerRegistrationHandle RegistrationIndex);
	void MarkUnregisteredWithMass();

protected:
	// UObject API
	virtual void PostLoad() override;
	// End UObject API

	// AActor API
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// End AActor API

	virtual void CreateMassTemplate(FMassEntityManager& EntityManager);


	FMassLWIManagerRegistrationHandle MassRegistrationHandle;
	FMassEntityTemplateID MassTemplateID;
	TSharedPtr<FMassEntityTemplate> FinalizedTemplate;
	FMassArchetypeHandle TargetArchetype;
	TArray<FMassEntityHandle> Entities;
};
