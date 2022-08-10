// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_EDITOR
#include "Tests/Determinism/PCGDeterminismNativeTests.h"

#include "PCGSettings.h"

#include "Elements/PCGDifferenceElement.h"
#include "Tests/Determinism/PCGDifferenceDeterminismTest.h"

namespace PCGDeterminismTests
{
	TFunction<bool()> GetNativeTestIfExists(const UPCGSettings* PCGSettings)
	{
		// This way we only build it if we ever need it
		static TMap<UClass*, TFunction<bool()>> NativeTestMapping;

		if (NativeTestMapping.IsEmpty())
		{
			NativeTestMapping =
			{
				{UPCGDifferenceSettings::StaticClass(), PCGDeterminismTests::DifferenceElement::RunTestSuite}
				// TODO: Append other native tests here
			};
		}

		if (TFunction<bool()>* TestFunction = NativeTestMapping.Find(PCGSettings->GetClass()))
		{
			return *TestFunction;
		}

		return nullptr;
	}
}

#endif