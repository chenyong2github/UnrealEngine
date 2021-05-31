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