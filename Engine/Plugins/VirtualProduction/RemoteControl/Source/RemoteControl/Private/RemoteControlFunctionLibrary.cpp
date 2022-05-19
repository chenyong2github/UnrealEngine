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
	if (!TargetObject)
	{
		return false;
	}

	FProperty* Property = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (const FStructProperty* ColorProperty = CastField<FStructProperty>(Property))
	{
		FLinearColor Color;
		ColorProperty->GetValue_InContainer(TargetObject, &Color);

		// Convert to HSV
		Color = Color.LinearRGBToHSV();

		FVector2D Position;

		if (Color.B > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			// Determine direction as a unit vector based on the calculated hue
			const double HueRadians = FMath::DegreesToRadians(Color.R);
			Position = FVector2D(FMath::Cos(HueRadians), FMath::Sin(HueRadians));
			
			// Multiply the unit vector by saturation to determine the current position in the color wheel
			Position *= Color.G;
		}
		else
		{
			// Color's value is too low to determine the hue and saturation. Fall back to the reference color's position
			Position = ReferenceColor.Position;
		}

		// Apply the delta to the position, then convert back to hue and saturation
		Position += FVector2D(DeltaValue.Position.X, DeltaValue.Position.Y);
		Color.R = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Position.Y, Position.X)) + 360.0, 360.0);
		Color.G = FMath::Clamp(Position.Length(), 0.0, 1.0);

		// Apply the value and alpha changes directly
		Color.B = FMath::Clamp(Color.B + DeltaValue.Value, 0.0, 1.0);
		Color.A = FMath::Clamp(Color.A + DeltaValue.Alpha, 0.0, 1.0);

		Color = Color.HSVToLinearRGB();

#if WITH_EDITOR
		TargetObject->PreEditChange(Property);
		TargetObject->Modify();
#endif

		ColorProperty->SetValue_InContainer(TargetObject, &Color);

#if WITH_EDITOR
		FPropertyChangedEvent ChangeEvent(Property, bIsInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet);
		TargetObject->PostEditChangeProperty(ChangeEvent);
#endif

		return true;
	}

	return false;
}
