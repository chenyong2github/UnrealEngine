// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlFunctionLibrary.h"

namespace RemoteControlFunctionLibrary
{
	FGuid GetOrCreateGroup(URemoteControlPreset* Preset, const FString& GroupName)
	{
		FGuid GroupId;
		if (GroupName.IsEmpty())
		{
			GroupId = Preset->Layout.GetDefaultGroup().Id;
		}
		else
		{
			if (FRemoteControlPresetGroup* Group = Preset->Layout.GetGroupByName(*GroupName))
			{
				GroupId = Group->Id;
			}
			else
			{
				GroupId = Preset->Layout.CreateGroup(*GroupName).Id;
			}
		}
		return GroupId;
	}

	/**
	 * Traverse the nested struct properties on an object to find the property with the given period-separated path.
	 * If found, return true and set OutProperty to the property matching the name and OutContainer to the object containing its value (within TargetObject).
	 */
	bool GetNestedPropertyAndContainerFromPath(UObject* Object, const FString& PropertyName, FProperty*& OutProperty, void*& OutContainer)
	{
		if (!Object)
		{
			return false;
		}

		// Split the property name up on dots so we can find nested struct properties
		TArray<FString> PropertyPathNames;
		PropertyName.ParseIntoArray(PropertyPathNames, TEXT("."));

		if (PropertyPathNames.IsEmpty())
		{
			return false;
		}

		void* Container = Object;
		UStruct* ContainerClass = Object->GetClass();
		if (!ContainerClass)
		{
			return false;
		}

		FProperty* Property = ContainerClass->FindPropertyByName(FName(*PropertyPathNames[0]));
		if (!Property)
		{
			return false;
		}

		// Walk through the chain of structs until we reach the end (or run out of matching struct properties)
		for (int32 PathIndex = 1; PathIndex < PropertyPathNames.Num(); ++PathIndex)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				ContainerClass = StructProperty->Struct;
				if (ContainerClass == nullptr)
				{
					return false;
				}

				Property = ContainerClass->FindPropertyByName(FName(*PropertyPathNames[PathIndex]));
				if (Property == nullptr)
				{
					return false;
				}

				Container = StructProperty->ContainerPtrToValuePtr<void>(Container);
			}
			else
			{
				return false;
			}
		}

		ensure(Container && Property);

		OutProperty = Property;
		OutContainer = Container;
		return true;
	}

	/**
	 * Apply a wheel-based color delta to an RGB linear color.
	 */
	void ApplyWheelColorBaseDelta(FLinearColor& InOutColor, const FColorWheelColorBase& DeltaValue, const FColorWheelColorBase& ReferenceColor, float MaxValue)
	{
		// Convert to HSV
		InOutColor = InOutColor.LinearRGBToHSV();

		FVector2D Position;

		if (InOutColor.B > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			// Determine direction as a unit vector based on the calculated hue
			const double HueRadians = FMath::DegreesToRadians(InOutColor.R);
			Position = FVector2D(FMath::Cos(HueRadians), FMath::Sin(HueRadians));

			// Multiply the unit vector by saturation to determine the current position in the color wheel
			Position *= InOutColor.G;
		}
		else
		{
			// Color's value is too low to determine the hue and saturation. Fall back to the reference color's position
			Position = ReferenceColor.Position;
		}

		// Apply the delta to the position, then convert back to hue and saturation
		Position += FVector2D(DeltaValue.Position.X, DeltaValue.Position.Y);
		InOutColor.R = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Position.Y, Position.X)) + 360.0, 360.0);
		InOutColor.G = FMath::Clamp(Position.Length(), 0.0, MaxValue);

		// Apply the value change directly
		InOutColor.B = FMath::Clamp(InOutColor.B + DeltaValue.Value, 0.0, MaxValue);

		InOutColor = InOutColor.HSVToLinearRGB();
	}

	/** Set a property on an object and fire relevant events. */
	void SetPropertyAndFireEvents(UObject* Object, FProperty* Property, void* Container, void* Value, bool bIsInteractive)
	{
#if WITH_EDITOR
		Object->PreEditChange(Property);
		Object->Modify();
#endif

		Property->SetValue_InContainer(Container, Value);

#if WITH_EDITOR
		FPropertyChangedEvent ChangeEvent(Property, bIsInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(ChangeEvent);
#endif
	}
}

bool URemoteControlFunctionLibrary::ExposeProperty(URemoteControlPreset* Preset, UObject* SourceObject, const FString& Property, FRemoteControlOptionalExposeArgs Args)
{
	if (Preset && SourceObject)
	{
		return Preset->ExposeProperty(SourceObject, Property, {Args.DisplayName, RemoteControlFunctionLibrary::GetOrCreateGroup(Preset, Args.GroupName)}).IsValid();
	}
	return false;
}

bool URemoteControlFunctionLibrary::ExposeFunction(URemoteControlPreset* Preset, UObject* SourceObject, const FString& Function, FRemoteControlOptionalExposeArgs Args)
{
	if (Preset && SourceObject)
	{
		if (UFunction* TargetFunction = SourceObject->FindFunction(*Function))
		{
			return Preset->ExposeFunction(SourceObject, TargetFunction, { Args.DisplayName, RemoteControlFunctionLibrary::GetOrCreateGroup(Preset, Args.GroupName) }).IsValid();
		}
	}
	return false;
}

bool URemoteControlFunctionLibrary::ExposeActor(URemoteControlPreset* Preset, AActor* Actor, FRemoteControlOptionalExposeArgs Args)
{
	if (Preset && Actor)
	{
		return Preset->ExposeActor(Actor, {Args.DisplayName, RemoteControlFunctionLibrary::GetOrCreateGroup(Preset, Args.GroupName)}).IsValid();
	}
	return false;
}

bool URemoteControlFunctionLibrary::ApplyColorWheelDelta(UObject* TargetObject, const FString& PropertyName, const FColorWheelColor& DeltaValue, const FColorWheelColor& ReferenceColor, bool bIsInteractive)
{
	FProperty* Property;
	void* Container;
	if (!RemoteControlFunctionLibrary::GetNestedPropertyAndContainerFromPath(TargetObject, PropertyName, Property, Container))
	{
		return false;
	}

	if (const FStructProperty* ColorProperty = CastField<FStructProperty>(Property))
	{
		if (ColorProperty->Struct != TBaseStructure<FLinearColor>::Get())
		{
			// This isn't a color
			return false;
		}

		FLinearColor Color;
		ColorProperty->GetValue_InContainer(Container, &Color);

		RemoteControlFunctionLibrary::ApplyWheelColorBaseDelta(Color, DeltaValue, ReferenceColor, 1.0);

		// Apply the alpha change directly
		Color.A = FMath::Clamp(Color.A + DeltaValue.Alpha, 0.0, 1.0);

		RemoteControlFunctionLibrary::SetPropertyAndFireEvents(TargetObject, Property, Container, &Color, bIsInteractive);

		return true;
	}

	return false;
}

bool URemoteControlFunctionLibrary::ApplyColorGradingWheelDelta(UObject* TargetObject, const FString& PropertyName, const FColorGradingWheelColor& DeltaValue, const FColorGradingWheelColor& ReferenceColor, bool bIsInteractive)
{
	FProperty* Property = nullptr;
	void* Container = nullptr;
	if (!RemoteControlFunctionLibrary::GetNestedPropertyAndContainerFromPath(TargetObject, PropertyName, Property, Container))
	{
		return false;
	}

	if (const FStructProperty* ColorProperty = CastField<FStructProperty>(Property))
	{
		if (ColorProperty->Struct != TBaseStructure<FVector4>::Get())
		{
			// This isn't an FVector4 representation of a color
			return false;
		}

		// Get the vector value and convert the color portion of it to HSV
		FVector4 VectorValue;
		ColorProperty->GetValue_InContainer(Container, &VectorValue);

		FLinearColor Color(VectorValue.X, VectorValue.Y, VectorValue.Z);
		RemoteControlFunctionLibrary::ApplyWheelColorBaseDelta(Color, DeltaValue, ReferenceColor, 2.0);

		// Apply the luminance change directly
		const double NewLuminance = VectorValue.W + DeltaValue.Luminance;
		VectorValue = FVector4(Color, NewLuminance);

		RemoteControlFunctionLibrary::SetPropertyAndFireEvents(TargetObject, Property, Container, &VectorValue, bIsInteractive);

		return true;
	}

	return false;
}
