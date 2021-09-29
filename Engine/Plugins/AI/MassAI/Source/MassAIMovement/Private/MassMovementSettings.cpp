// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassMovementSettings.h"
#include "MassMovementDelegates.h"
#include "MassAIMovementTypes.h"

//----------------------------------------------------------------------//
// UMassMovementSettings
//----------------------------------------------------------------------//

void UMassMovementSettings::UpdateConfigs()
{
	// Update cached values on configs.
	for (FMassMovementConfig& Config : MovementConfigs)
	{
		Config.Update();
	}
}

void UMassMovementSettings::PostInitProperties()
{
	Super::PostInitProperties();
	UpdateConfigs();
}

#if WITH_EDITOR
void UMassMovementSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMassMovementSettings, MovementStyles))
		{
			const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());

			// Ensure unique ID on duplicated items.
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
			{
				if (MovementStyles.IsValidIndex(ArrayIndex))
				{
					MovementStyles[ArrayIndex].ID = FGuid::NewGuid();
					MovementStyles[ArrayIndex].Name = FName(TEXT("Movement Style"));
				}
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
			{
				if (MovementStyles.IsValidIndex(ArrayIndex))
				{
					MovementStyles[ArrayIndex].ID = FGuid::NewGuid();
					MovementStyles[ArrayIndex].Name = FName(MovementStyles[ArrayIndex].Name.ToString() + TEXT(" Duplicate"));
				}
			}

			UpdateConfigs();
			UE::MassMovement::Delegates::OnMassMovementNamesChanged.Broadcast();
		}

		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMassMovementSettings, MovementConfigs))
		{
			const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());

			// Ensure unique ID on duplicated items.
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
			{
				if (MovementConfigs.IsValidIndex(ArrayIndex))
				{
					MovementConfigs[ArrayIndex].ID = FGuid::NewGuid();
					MovementConfigs[ArrayIndex].Name = FName(TEXT("Movement Config"));
				}
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
			{
				if (MovementConfigs.IsValidIndex(ArrayIndex))
				{
					MovementConfigs[ArrayIndex].ID = FGuid::NewGuid();
					MovementConfigs[ArrayIndex].Name = FName(MovementConfigs[ArrayIndex].Name.ToString() + TEXT(" Duplicate"));
				}
			}

			UpdateConfigs();
			UE::MassMovement::Delegates::OnMassMovementNamesChanged.Broadcast();
		}
	}
}
#endif // WITH_EDITOR

const FMassMovementStyle* UMassMovementSettings::GetMovementStyleByID(const FGuid ID) const
{
	return MovementStyles.FindByPredicate([ID](const FMassMovementStyle& Style) { return Style.ID == ID; });
}
	
const FMassMovementConfig* UMassMovementSettings::GetMovementConfigByID(const FGuid ID) const
{
	return MovementConfigs.FindByPredicate([ID](const FMassMovementConfig& Config) { return Config.ID == ID; });
}

FMassMovementConfigHandle UMassMovementSettings::GetMovementConfigHandleByID(const FGuid ID) const
{
	const int32 Index = MovementConfigs.IndexOfByPredicate([ID](const FMassMovementConfig& Config) { return Config.ID == ID; });
	return Index != INDEX_NONE ? FMassMovementConfigHandle((uint8)Index) : FMassMovementConfigHandle();
}
