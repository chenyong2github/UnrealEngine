// Copyright Epic Games, Inc. All Rights Reserved.


#include "Field/FieldSystemActor.h"

#include "Field/FieldSystemComponent.h"

DEFINE_LOG_CATEGORY_STATIC(AFA_Log, NoLogging, All);

AFieldSystemActor::AFieldSystemActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UE_LOG(AFA_Log, Log, TEXT("AFieldSystemActor::AFieldSystemActor()"));

	FieldSystemComponent = CreateDefaultSubobject<UFieldSystemComponent>(TEXT("FieldSystemComponent"));
	RootComponent = FieldSystemComponent;
}


void AFieldSystemActor::OnConstruction(const FTransform& Transform)
{
	if (UFieldSystemComponent* Component = FieldSystemComponent)
	{
		if (UFieldSystem* Asset = FieldSystemComponent->FieldSystem)
		{

			if (Asset->Commands.Num() != Component->BlueprintBufferedCommands.Num())
			{
				if (!Component->BlueprintBufferedCommands.Num())
				{
					Asset->Modify();
					Asset->Commands.Reset();
				}
				else
				{
					Asset->Modify();
					Asset->Commands = Component->BlueprintBufferedCommands;
				}
			}
			else
			{
				bool bEqual = true;
				for (int i = 0; i < Asset->Commands.Num() && bEqual; i++)
				{
					bEqual &= Asset->Commands[i] == Component->BlueprintBufferedCommands[i];
				}
				if (!bEqual)
				{
					Asset->Modify();
					Asset->Commands = Component->BlueprintBufferedCommands;
				}
			}
		}
	}
}



