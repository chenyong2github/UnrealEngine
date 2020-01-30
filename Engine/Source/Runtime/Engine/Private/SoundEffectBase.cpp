// Copyright Epic Games, Inc. All Rights Reserved.


#include "Sound/SoundEffectBase.h"


FSoundEffectBase::FSoundEffectBase()
	: bChanged(false)
	, bIsRunning(false)
	, bIsActive(false)
{}

FSoundEffectBase::~FSoundEffectBase()
{
}

bool FSoundEffectBase::IsActive() const
{
	return bIsActive;
}

void FSoundEffectBase::SetEnabled(const bool bInIsEnabled)
{
	bIsActive = bInIsEnabled;
}

void FSoundEffectBase::SetPreset(USoundEffectPreset* Inpreset)
{
	if (Preset != Inpreset)
	{
		ClearPreset();

		Preset = Inpreset;
		if (Preset.IsValid())
		{
			Preset->AddEffectInstance(this);
		}
	}

	// Anytime notification occurs that the preset has been modified,
	// flag for update.
	bChanged = true;
}

USoundEffectPreset* FSoundEffectBase::GetPreset()
{
	return Preset.Get();
}

void FSoundEffectBase::ClearPreset(bool bRemoveFromPreset)
{
	if (Preset.IsValid())
	{
		if (bRemoveFromPreset)
		{
			Preset->RemoveEffectInstance(this);
		}
		Preset.Reset();
	}
}

bool FSoundEffectBase::Update()
{
	PumpPendingMessages();

	if (bChanged && Preset.IsValid())
	{
		OnPresetChanged();
		bChanged = false;

		return true;
	}

	return false;
}

bool FSoundEffectBase::IsPreset(USoundEffectPreset* InPreset) const
{
	return Preset == InPreset;
}

void FSoundEffectBase::EffectCommand(TFunction<void()> Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}

void FSoundEffectBase::PumpPendingMessages()
{
	// Pumps the command queue
	TFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		Command();
	}
}
