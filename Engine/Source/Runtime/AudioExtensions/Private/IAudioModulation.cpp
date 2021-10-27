// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioModulation.h"

#include "UObject/Object.h"


namespace Audio
{
	FModulatorHandleId CreateModulatorHandleId()
	{
		static FModulatorHandleId NextHandleId = INDEX_NONE;
		return ++NextHandleId;
	}

	FModulationParameter::FModulationParameter()
		: MixFunction(GetDefaultMixFunction())
		, UnitFunction(GetDefaultUnitConversionFunction())
		, NormalizedFunction(GetDefaultNormalizedConversionFunction())
	{
	}

	FModulationParameter::FModulationParameter(FModulationParameter&& InParam)
		: ParameterName(MoveTemp(InParam.ParameterName))
		, DefaultValue(InParam.DefaultValue)
		, MinValue(InParam.MinValue)
		, MaxValue(InParam.MaxValue)
		, bRequiresConversion(InParam.bRequiresConversion)
	#if WITH_EDITORONLY_DATA
		, UnitDisplayName(MoveTemp(InParam.UnitDisplayName))
	#endif // WITH_EDITORONLY_DATA
		, MixFunction(MoveTemp(InParam.MixFunction))
		, UnitFunction(MoveTemp(InParam.UnitFunction))
		, NormalizedFunction(MoveTemp(InParam.NormalizedFunction))
	{
	}

	FModulationParameter::FModulationParameter(const FModulationParameter& InParam)
		: ParameterName(InParam.ParameterName)
		, DefaultValue(InParam.DefaultValue)
		, MinValue(InParam.MinValue)
		, MaxValue(InParam.MaxValue)
		, bRequiresConversion(InParam.bRequiresConversion)
#if WITH_EDITORONLY_DATA
		, UnitDisplayName(InParam.UnitDisplayName)
#endif // WITH_EDITORONLY_DATA
		, MixFunction(InParam.MixFunction)
		, UnitFunction(InParam.UnitFunction)
		, NormalizedFunction(InParam.NormalizedFunction)
	{
	}

	FModulationParameter& FModulationParameter::operator=(const FModulationParameter& InParam)
	{
		ParameterName = InParam.ParameterName;
		DefaultValue = InParam.DefaultValue;
		MinValue = InParam.MinValue;
		MaxValue = InParam.MaxValue;
		bRequiresConversion = InParam.bRequiresConversion;

#if WITH_EDITORONLY_DATA
		UnitDisplayName = InParam.UnitDisplayName;
#endif // WITH_EDITORONLY_DATA

		MixFunction = InParam.MixFunction;
		UnitFunction = InParam.UnitFunction;
		NormalizedFunction = InParam.NormalizedFunction;

		return *this;
	}

	FModulationParameter& FModulationParameter::operator=(FModulationParameter&& InParam)
	{
		ParameterName = MoveTemp(InParam.ParameterName);
		DefaultValue = InParam.DefaultValue;
		MinValue = InParam.MinValue;
		MaxValue = InParam.MaxValue;
		bRequiresConversion = InParam.bRequiresConversion;

	#if WITH_EDITORONLY_DATA
		UnitDisplayName = MoveTemp(InParam.UnitDisplayName);
	#endif // WITH_EDITORONLY_DATA

		MixFunction = MoveTemp(InParam.MixFunction);
		UnitFunction = MoveTemp(InParam.UnitFunction);
		NormalizedFunction = MoveTemp(InParam.NormalizedFunction);

		return *this;
	}

	const FModulationMixFunction& FModulationParameter::GetDefaultMixFunction()
	{
		static const FModulationMixFunction DefaultMixFunction = [](float& InOutValueA, float InValueB)
		{
			InOutValueA *= InValueB;
		};

		return DefaultMixFunction;
	}

	const FModulationUnitConversionFunction& FModulationParameter::GetDefaultUnitConversionFunction()
	{
		static const FModulationUnitConversionFunction ConversionFunction = [](float& InOutValue)
		{
		};

		return ConversionFunction;
	};

	const FModulationNormalizedConversionFunction& FModulationParameter::GetDefaultNormalizedConversionFunction()
	{
		static const FModulationNormalizedConversionFunction ConversionFunction = [](float& InOutValue)
		{
		};

		return ConversionFunction;
	};

	FModulatorHandle::FModulatorHandle(IAudioModulation& InModulation, const USoundModulatorBase* InModulatorBase, FName InParameterName)
	{
		HandleId = CreateModulatorHandleId();
		Modulation = &InModulation;
		Parameter.ParameterName = InParameterName;
		ModulatorTypeId = InModulation.RegisterModulator(HandleId, InModulatorBase, Parameter);

		if (ModulatorTypeId != INDEX_NONE)
		{
			ModulatorId = static_cast<Audio::FModulatorId>(InModulatorBase->GetUniqueID());
		}
	}

	FModulatorHandle::FModulatorHandle(const FModulatorHandle& InOther)
	{
		HandleId = CreateModulatorHandleId();

		if (InOther.Modulation)
		{
			InOther.Modulation->RegisterModulator(HandleId, InOther.ModulatorId);
			Parameter = InOther.Parameter;
			ModulatorId = InOther.ModulatorId;
			ModulatorTypeId = InOther.ModulatorTypeId;
			Modulation = InOther.Modulation;
		}
	}

	FModulatorHandle::FModulatorHandle(FModulatorHandle&& InOther)
		: Parameter(MoveTemp(InOther.Parameter))
		, HandleId(InOther.HandleId)
		, ModulatorTypeId(InOther.ModulatorTypeId)
		, ModulatorId(InOther.ModulatorId)
		, Modulation(InOther.Modulation)
	{
		// Move does not register as presumed already activated or
		// copying default handle, which is invalid. Removes data
		// from handle being moved to avoid double deactivation on
		// destruction.
		InOther.Parameter = FModulationParameter();
		InOther.HandleId = INDEX_NONE;
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
		Parameter = InOther.Parameter;

		if (InOther.Modulation)
		{
			HandleId = CreateModulatorHandleId();
			ModulatorId = InOther.ModulatorId;
			ModulatorTypeId = InOther.ModulatorTypeId;
			Modulation = InOther.Modulation;

			if (ModulatorId != INDEX_NONE)
			{
				Modulation->RegisterModulator(HandleId, ModulatorId);
			}
		}
		else
		{
			HandleId = INDEX_NONE;
			ModulatorId = INDEX_NONE;
			ModulatorTypeId = INDEX_NONE;
			Modulation = nullptr;
		}

		return *this;
	}

	FModulatorHandle& FModulatorHandle::operator=(FModulatorHandle&& InOther)
	{
		if (HandleId != INDEX_NONE)
		{
			if (ensureAlways(Modulation))
			{
				Modulation->UnregisterModulator(*this);
			}
		}

		// Move does not activate as presumed already activated or
		// copying default handle, which is invalid. Removes data
		// from handle being moved to avoid double deactivation on
		// destruction.
		Parameter = MoveTemp(InOther.Parameter);
		HandleId = InOther.HandleId;
		ModulatorId = InOther.ModulatorId;
		ModulatorTypeId = InOther.ModulatorTypeId;
		Modulation = InOther.Modulation;

		InOther.Parameter = FModulationParameter();
		InOther.HandleId = INDEX_NONE;
		InOther.ModulatorId = INDEX_NONE;
		InOther.ModulatorTypeId = INDEX_NONE;
		InOther.Modulation = nullptr;

		return *this;
	}

	FModulatorId FModulatorHandle::GetModulatorId() const
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

	uint32 FModulatorHandle::GetHandleId() const
	{
		return HandleId;
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
