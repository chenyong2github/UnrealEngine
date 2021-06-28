// Copyright Epic Games, Inc. All Rights Reserved.

#include "CalibrationPointComponent.h"

bool UCalibrationPointComponent::GetWorldLocation(const FString& InPointName, FVector& OutLocation) const
{
	FString ComponentName;
	FString SubpointName;

	const bool bWasNamespaced = InPointName.Split(TEXT("::"), &ComponentName, &SubpointName);

	if (bWasNamespaced)
	{
		// If it came namespaced, make sure the root name corresponds to this component's name.
		if (ComponentName != GetName())
		{
			return false;
		}
	}
	else
	{
		// If it wasn't namespaced, it will first try to match the name with the component name.
		if (InPointName == GetName())
		{
			OutLocation = GetComponentLocation();
			return true;
		}

		SubpointName = InPointName;
	}

	for (auto& Elem : SubPoints)
	{
		if (Elem.Key == SubpointName)
		{
			const FTransform SubPointTransform(FRotator(0, 0, 0), Elem.Value, FVector(1.0f));
			OutLocation = (SubPointTransform * GetComponentTransform()).GetLocation();
			return true;
		}
	}

	return false;
}

bool UCalibrationPointComponent::NamespacedSubpointName(const FString& InSubpointName, FString& OutNamespacedName) const
{
	OutNamespacedName = FString::Printf(TEXT("%s::%s"), *GetName(), *InSubpointName);

	return InSubpointName.Len() > 0;
}

void UCalibrationPointComponent::GetNamespacedPointNames(TArray<FString>& OutNamespacedNames) const
{
	OutNamespacedNames.Add(GetName());

	for (auto& Elem : SubPoints)
	{
		FString NamespacedName;
		if (NamespacedSubpointName(Elem.Key, NamespacedName))
		{
			OutNamespacedNames.Add(NamespacedName);
		}
	}
}
