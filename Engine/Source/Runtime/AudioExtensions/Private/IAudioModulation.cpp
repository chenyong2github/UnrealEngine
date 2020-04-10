// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioModulation.h"

#include "UObject/Object.h"


namespace Audio
{
	FModulatorHandle::FModulatorHandle()
		: ParentId(INDEX_NONE)
		, ModulatorId(INDEX_NONE)
		, Modulation(nullptr)
	{
	}

	FModulatorHandle::FModulatorHandle(IAudioModulation& InModulation, uint32 InParentId, const USoundModulatorBase& InModulatorBase)
	{
		if (InModulation.RegisterModulator(InParentId, InModulatorBase))
		{
			ParentId = InParentId;
			ModulatorId = static_cast<Audio::FModulatorId>(InModulatorBase.GetUniqueID());
			Modulation = &InModulation;
		}
		else
		{
			ParentId = INDEX_NONE;
			ModulatorId = INDEX_NONE;
			Modulation = nullptr;
		}
	}

	FModulatorHandle::FModulatorHandle(const FModulatorHandle& InOther)
	{
		if (InOther.Modulation && InOther.Modulation->RegisterModulator(InOther.ParentId, InOther.ModulatorId))
		{
			ParentId = InOther.ParentId;
			ModulatorId = static_cast<Audio::FModulatorId>(InOther.ModulatorId);
			Modulation = InOther.Modulation;
		}
		else
		{
			ParentId = INDEX_NONE;
			ModulatorId = INDEX_NONE;
			Modulation = nullptr;
		}
	}

	FModulatorHandle::FModulatorHandle(FModulatorHandle&& InOther)
		: ParentId(InOther.ParentId)
		, ModulatorId(InOther.ModulatorId)
		, Modulation(InOther.Modulation)
	{
		// Move does not register as presumed already activated or
		// copying default handle, which is invalid. Removes data
		// from handle being moved to avoid double deactivation on
		// destruction.
		InOther.ParentId = INDEX_NONE;
		InOther.ModulatorId = INDEX_NONE;
		InOther.Modulation = nullptr;
	}

	FModulatorHandle::~FModulatorHandle()
	{
		if (Modulation)
		{
			Modulation->UnregisterModulator(*this);
		}
	}

	FModulatorHandle& FModulatorHandle::operator=(const FModulatorHandle& InOther)
	{
		if (InOther.Modulation && InOther.Modulation->RegisterModulator(InOther.ParentId, InOther.ModulatorId))
		{
			ParentId = InOther.ParentId;
			ModulatorId = static_cast<Audio::FModulatorId>(InOther.ModulatorId);
			Modulation = InOther.Modulation;
		}
		else
		{
			ParentId = INDEX_NONE;
			ModulatorId = INDEX_NONE;
			Modulation = nullptr;
		}

		return *this;
	}

	FModulatorHandle& FModulatorHandle::operator=(FModulatorHandle&& InOther)
	{
		// Move does not activate as presumed already activated or
		// copying default handle, which is invalid. Removes data
		// from handle being moved to avoid double deactivation on
		// destruction.
		ParentId = InOther.ParentId;
		ModulatorId = InOther.ModulatorId;
		Modulation = InOther.Modulation;

		InOther.ParentId	= INDEX_NONE;
		InOther.ModulatorId = INDEX_NONE;
		InOther.Modulation	= nullptr;

		return *this;
	}

	FModulatorId FModulatorHandle::GetId() const
	{
		return ModulatorId;
	}

	uint32 FModulatorHandle::GetParentId() const
	{
		return ParentId;
	}

	float FModulatorHandle::GetValue(const float InDefaultValue) const
	{
		if (IsValid())
		{
			float Value = InDefaultValue;
			if (Modulation->GetModulatorValue(*this, Value))
			{
				return Value;
			}
		}

		return InDefaultValue;
	}

	bool FModulatorHandle::IsValid() const
	{
		return Modulation && ModulatorId != INDEX_NONE;
	}
} // namespace Audio
