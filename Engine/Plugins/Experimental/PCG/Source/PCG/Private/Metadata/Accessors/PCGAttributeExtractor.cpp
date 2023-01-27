// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/Accessors/PCGAttributeExtractor.h"
#include "Metadata/Accessors/IPCGAttributeAccessorTpl.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

#include "Internationalization/Regex.h"

namespace PCGAttributeExtractor
{
	template <typename VectorType>
	double GetAt(const VectorType& Value, int32 Index)
	{
		check(Index >= 0 && Index < 4);
		return Value[Index];
	}

	template <>
	double GetAt<FQuat>(const FQuat& Value, int32 Index)
	{
		check(Index >= 0 && Index < 4);
		switch (Index)
		{
		case 0:
			return Value.X;
		case 1:
			return Value.Y;
		case 2:
			return Value.Z;
		default:
			return Value.W;
		}
	}

	template <typename VectorType>
	void SetAt(VectorType& Value, double In, int32 Index)
	{
		check(Index >= 0 && Index < 4);
		Value[Index] = In;
	}

	template <>
	void SetAt<FQuat>(FQuat& Value, double In, int32 Index)
	{
		check(Index >= 0 && Index < 4);
		switch (Index)
		{
		case 0:
			Value.X = In;
		case 1:
			Value.Y = In;
		case 2:
			Value.Z = In;
		default:
			Value.W = In;
		}
	}

	// Works for Vec2, Vec3, Vec4 and Quat (same as Vec4)
	template <typename VectorType>
	TUniquePtr<IPCGAttributeAccessor> CreateVectorExtractor(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess)
	{
		if ((Name == PCGAttributeExtractorConstants::VectorLength) || (Name == PCGAttributeExtractorConstants::VectorSize))
		{
			bOutSuccess = true;
			return MakeUnique<FPCGChainAccessor<double, VectorType>>(std::move(InAccessor),
				[](const VectorType& Value) -> double { return Value.Size(); });
		}

		FString MatchString;
		if constexpr (std::is_same_v<FVector2D, VectorType>)
		{
			MatchString = TEXT("[XY]{1,4}");
		}
		else if constexpr (std::is_same_v<FVector, VectorType>)
		{
			MatchString = TEXT("[XYZ]{1,4}");
		}
		else
		{
			MatchString = TEXT("[XYZW]{1,4}");
		}

		const FString NameStr = Name.ToString();

		const FRegexPattern RegexPattern(MatchString, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher RegexMatcher(RegexPattern, *NameStr);
		if (!RegexMatcher.FindNext())
		{
			// Failed
			bOutSuccess = false;
			return InAccessor;
		}

		TArray<int32, TInlineAllocator<4>> Indexes;
		for (const TCHAR Char : NameStr)
		{
			int32 Index = -1;

			if (Char == TEXT('w') || Char == TEXT('W'))
			{
				Index = 3;
			}
			else
			{
				Index = static_cast<int32>(Char) - static_cast<int32>(TEXT('x'));
				if (Index < 0)
				{
					Index = static_cast<int32>(Char) - static_cast<int32>(TEXT('X'));
				}
			}

			// Safeguard, should be caught by not matching the regex above
			if (!ensure(Index >= 0 && Index < 4))
			{
				bOutSuccess = false;
				return InAccessor;
			}

			Indexes.Add(Index);
		}

		bOutSuccess = true;

		if (Indexes.Num() == 1)
		{
			return MakeUnique<FPCGChainAccessor<double, VectorType>>(std::move(InAccessor),
				[Indexes](const VectorType& Value) -> double { return GetAt(Value, Indexes[0]); },
				[Indexes](VectorType& Value, const double& In) -> void { SetAt(Value, In, Indexes[0]); });
		}
		else if (Indexes.Num() == 2)
		{
			return MakeUnique<FPCGChainAccessor<FVector2D, VectorType>>(std::move(InAccessor),
				[Indexes](const VectorType& Value) -> FVector2D { return FVector2D(GetAt(Value, Indexes[0]), GetAt(Value, Indexes[1])); },
				[Indexes](VectorType& Value, const FVector2D& In) -> void { SetAt(Value, In.X, Indexes[0]); SetAt(Value, In.Y, Indexes[1]);});
		}
		else if (Indexes.Num() == 3)
		{
			return MakeUnique<FPCGChainAccessor<FVector, VectorType>>(std::move(InAccessor),
				[Indexes](const VectorType& Value) -> FVector { return FVector(GetAt(Value, Indexes[0]), GetAt(Value, Indexes[1]), GetAt(Value, Indexes[2])); },
				[Indexes](VectorType& Value, const FVector& In) -> void { SetAt(Value, In.X, Indexes[0]); SetAt(Value, In.Y, Indexes[1]); SetAt(Value, In.Z, Indexes[2]); });
		}
		else
		{
			return MakeUnique<FPCGChainAccessor<FVector4, VectorType>>(std::move(InAccessor),
				[Indexes](const VectorType& Value) -> FVector4 { return FVector4(GetAt(Value, Indexes[0]), GetAt(Value, Indexes[1]), GetAt(Value, Indexes[2]), GetAt(Value, Indexes[3])); },
				[Indexes](VectorType& Value, const FVector4& In) -> void { SetAt(Value, In.X, Indexes[0]); SetAt(Value, In.Y, Indexes[1]); SetAt(Value, In.Z, Indexes[2]); SetAt(Value, In.W, Indexes[3]); });
		}
	}

	TUniquePtr<IPCGAttributeAccessor> CreateRotatorExtractor(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess)
	{
		bOutSuccess = true;

		if (Name == PCGAttributeExtractorConstants::RotatorPitch)
		{
			return MakeUnique<FPCGChainAccessor<double, FRotator>>(std::move(InAccessor),
				[](const FRotator& Value) -> double { return Value.Pitch; },
				[](FRotator& Value, const double& In) -> void { Value.Pitch = In; });
		}

		if (Name == PCGAttributeExtractorConstants::RotatorRoll)
		{
			return MakeUnique<FPCGChainAccessor<double, FRotator>>(std::move(InAccessor),
				[](const FRotator& Value) -> double { return Value.Roll; },
				[](FRotator& Value, const double& In) -> void { Value.Roll = In; });
		}

		if (Name == PCGAttributeExtractorConstants::RotatorYaw)
		{
			return MakeUnique<FPCGChainAccessor<double, FRotator>>(std::move(InAccessor),
				[](const FRotator& Value) -> double { return Value.Yaw; },
				[](FRotator& Value, const double& In) -> void { Value.Yaw = In; });
		}

		bOutSuccess = false;
		return InAccessor;
	}

	TUniquePtr<IPCGAttributeAccessor> CreateTransformExtractor(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess)
	{
		bOutSuccess = true;

		if ((Name == PCGAttributeExtractorConstants::TransformLocation) || (Name == TEXT("Position")))
		{
			return MakeUnique<FPCGChainAccessor<FVector, FTransform>>(std::move(InAccessor),
				[](const FTransform& Value) -> FVector { return Value.GetLocation(); },
				[](FTransform& Value, const FVector& In) -> void { Value.SetLocation(In); });
		}

		if ((Name == PCGAttributeExtractorConstants::TransformScale) || (Name == TEXT("Scale3D")))
		{
			return MakeUnique<FPCGChainAccessor<FVector, FTransform>>(std::move(InAccessor),
				[](const FTransform& Value) -> FVector { return Value.GetScale3D(); },
				[](FTransform& Value, const FVector& In) -> void { Value.SetScale3D(In); });
		}

		if (Name == PCGAttributeExtractorConstants::TransformRotation)
		{
			return MakeUnique<FPCGChainAccessor<FQuat, FTransform>>(std::move(InAccessor),
				[](const FTransform& Value) -> FQuat { return Value.GetRotation(); },
				[](FTransform& Value, const FQuat& In) -> void { Value.SetRotation(In); });
		}

		bOutSuccess = false;
		return InAccessor;
	}

	// Template instantiation for all vectors types + quat
	template TUniquePtr<IPCGAttributeAccessor> CreateVectorExtractor<FVector2D>(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess);
	template TUniquePtr<IPCGAttributeAccessor> CreateVectorExtractor<FVector>(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess);
	template TUniquePtr<IPCGAttributeAccessor> CreateVectorExtractor<FVector4>(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess);
	template TUniquePtr<IPCGAttributeAccessor> CreateVectorExtractor<FQuat>(TUniquePtr<IPCGAttributeAccessor> InAccessor, FName Name, bool& bOutSuccess);
}
