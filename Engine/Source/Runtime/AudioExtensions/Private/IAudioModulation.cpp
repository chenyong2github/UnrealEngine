// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioModulation.h"

#include "UObject/Object.h"


namespace Audio
{
	FModulationParameter::FModulationParameter()
		: MixFunction(GetDefaultMixFunction())
	{
	}

	const FModulationMixFunction& FModulationParameter::GetDefaultMixFunction()
	{
		static const FModulationMixFunction DefaultMixFunction = [](float* RESTRICT OutValueBuffer, const float* RESTRICT InValueBuffer, int32 InNumSamples)
		{
			if (InNumSamples % 4 == 0)
			{
				Audio::MultiplyBuffersInPlace(InValueBuffer, OutValueBuffer, InNumSamples);
			}
			else
			{
				for (int32 i = 0; i < InNumSamples; ++i)
				{
					OutValueBuffer[i] *= InValueBuffer[i];
				}
			}
		};

		return DefaultMixFunction;
	}
	FModulatorHandle::FModulatorHandle(IAudioModulation& InModulation, uint32 InParentId, const USoundModulatorBase& InModulatorBase, FName InParameterName)
	{
		Parameter.ParameterName = InParameterName;
		ModulatorTypeId = InModulation.RegisterModulator(InParentId, InModulatorBase, Parameter);
		if (ModulatorTypeId != INDEX_NONE)
		{
			ParentId = InParentId;
			ModulatorId = static_cast<Audio::FModulatorId>(InModulatorBase.GetUniqueID());
			Modulation = &InModulation;
		}
	}

	FModulatorHandle::FModulatorHandle(const FModulatorHandle& InOther)
	{
		if (InOther.Modulation)
		{
			InOther.Modulation->RegisterModulator(InOther.ParentId, InOther.ModulatorId);
			Parameter = InOther.Parameter;
			ParentId = InOther.ParentId;
			ModulatorId = InOther.ModulatorId;
			ModulatorTypeId = InOther.ModulatorTypeId;
			Modulation = InOther.Modulation;
		}
	}

	FModulatorHandle::FModulatorHandle(FModulatorHandle&& InOther)
		: Parameter(InOther.Parameter)
		, ParentId(InOther.ParentId)
		, ModulatorTypeId(InOther.ModulatorTypeId)
		, ModulatorId(InOther.ModulatorId)
		, Modulation(InOther.Modulation)
	{
		// Move does not register as presumed already activated or
		// copying default handle, which is invalid. Removes data
		// from handle being moved to avoid double deactivation on
		// destruction.
		InOther.Parameter = FModulationParameter();
		InOther.ParentId = INDEX_NONE;
		InOther.ModulatorTypeId = INDEX_NONE;
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
		if (InOther.Modulation)
		{
			InOther.Modulation->RegisterModulator(InOther.ParentId, InOther.ModulatorId);
			Parameter = InOther.Parameter;
			ParentId = InOther.ParentId;
			ModulatorId = InOther.ModulatorId;
			ModulatorTypeId = InOther.ModulatorTypeId;
			Modulation = InOther.Modulation;
		}
		else
		{
			Parameter = FModulationParameter();
			ParentId = INDEX_NONE;
			ModulatorId = INDEX_NONE;
			ModulatorTypeId = INDEX_NONE;
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
		Parameter = InOther.Parameter;
		ParentId = InOther.ParentId;
		ModulatorId = InOther.ModulatorId;
		ModulatorTypeId = InOther.ModulatorTypeId;
		Modulation = InOther.Modulation;

		InOther.Parameter = FModulationParameter();
		InOther.ParentId	= INDEX_NONE;
		InOther.ModulatorId = INDEX_NONE;
		InOther.ModulatorTypeId = INDEX_NONE;
		InOther.Modulation	= nullptr;

		return *this;
	}

	FModulatorId FModulatorHandle::GetId() const
	{
		return ModulatorId;
	}

	const FModulationParameter& FModulatorHandle::GetParameter() const
	{
		return Parameter;
	}

	FModulatorTypeId FModulatorHandle::GetTypeId() const
	{
		return ModulatorTypeId;
	}

	uint32 FModulatorHandle::GetParentId() const
	{
		return ParentId;
	}

	bool FModulatorHandle::GetValue(float& OutValue) const
	{
		check(IsValid());

		OutValue = 1.0f;
		return Modulation->GetModulatorValue(*this, OutValue);
	}

	bool FModulatorHandle::IsValid() const
	{
		return ModulatorId != INDEX_NONE;
	}
} // namespace Audio
