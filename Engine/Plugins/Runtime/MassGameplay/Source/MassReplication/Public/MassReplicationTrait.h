// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassReplicationTrait.generated.h"


UCLASS(meta=(DisplayName="Replication"))
class MASSREPLICATION_API UMassReplicationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
