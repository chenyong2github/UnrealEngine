// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGMetadataAttributeTraits.generated.h"

UENUM(BlueprintType)
enum class EPCGMetadataTypes : uint8
{
	Float = 0,
	Double,
	Integer32,
	Integer64,
	Vector,
	Vector4,
	Quaternion,
	Transform,
	String,
	Boolean,
	Rotator,
	Name,
	Unknown = 255
};

namespace PCG
{
	namespace Private
	{
		template<typename T>
		struct MetadataTypes
		{
			enum { Id = static_cast<uint16>(EPCGMetadataTypes::Unknown) + sizeof(T) };
		};

#define PCGMetadataGenerateDataTypes(Type, TypeEnum) template<> struct MetadataTypes<Type>{ enum { Id = static_cast<uint16>(EPCGMetadataTypes::TypeEnum)}; }

		PCGMetadataGenerateDataTypes(float, Float);
		PCGMetadataGenerateDataTypes(double, Double);
		PCGMetadataGenerateDataTypes(int32, Integer32);
		PCGMetadataGenerateDataTypes(int64, Integer64);
		PCGMetadataGenerateDataTypes(FVector, Vector);
		PCGMetadataGenerateDataTypes(FVector4, Vector4);
		PCGMetadataGenerateDataTypes(FQuat, Quaternion);
		PCGMetadataGenerateDataTypes(FTransform, Transform);
		PCGMetadataGenerateDataTypes(FString, String);
		PCGMetadataGenerateDataTypes(bool, Boolean);
		PCGMetadataGenerateDataTypes(FRotator, Rotator);
		PCGMetadataGenerateDataTypes(FName, Name);

#undef PCGMetadataGenerateDataTypes

		template<typename T>
		struct DefaultOperationTraits
		{
			enum { CanSubAdd = true };
			enum { CanMulDiv = true };
			
			static bool Equal(const T& A, const T& B)
			{
				return A == B;
			}

			static T Sub(const T& A, const T& B)
			{
				return A - B;
			}

			static T Add(const T& A, const T& B)
			{
				return A + B;
			}

			static T Mul(const T& A, const T& B)
			{
				return A * B;
			}

			static T Div(const T& A, const T& B)
			{
				return A / B;
			}
		};

		template<typename T>
		struct DefaultWeightedSumTraits
		{
			enum { CanInterpolate = true };

			static T WeightedSum(const T& A, const T& B, float Weight)
			{
				return A + B * Weight;
			}

			static T ZeroValue()
			{
				return T{};
			}
		};

		// Common traits for int32, int64, float, double
		template<typename T>
		struct MetadataTraits : DefaultOperationTraits<T>, DefaultWeightedSumTraits<T>
		{
			enum { CompressData = false };
			enum { CanMinMax = true };

			static T Min(const T& A, const T& B)
			{
				return FMath::Min(A, B);
			}

			static T Max(const T& A, const T& B)
			{
				return FMath::Max(A, B);
			}
		};

		template<>
		struct MetadataTraits<bool>
		{
			enum { CompressData = false };
			enum { CanMinMax = true };
			enum { CanSubAdd = true };
			enum { CanMulDiv = false };
			enum { CanInterpolate = false };

			static bool Equal(const bool& A, const bool& B)
			{
				return A == B;
			}

			static bool Min(const bool& A, const bool& B)
			{
				return A && B;
			}

			static bool Max(const bool& A, const bool& B)
			{
				return A || B;
			}

			static bool Add(const bool& A, const bool& B)
			{
				return A || B;
			}

			static bool Sub(const bool& A, const bool& B)
			{
				return A && !B;
			}
		};

		// Vector types
		template<>
		struct MetadataTraits<FVector> : DefaultOperationTraits<FVector>
		{
			enum { CompressData = false };
			enum { CanMinMax = true };
			enum { CanInterpolate = true };

			static FVector Min(const FVector& A, const FVector& B) 
			{
				return FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
			}

			static FVector Max(const FVector& A, const FVector& B)
			{
				return FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
			}

			static FVector WeightedSum(const FVector& A, const FVector& B, float Weight)
			{
				return A + B * Weight;
			}

			static FVector ZeroValue()
			{
				return FVector::ZeroVector;
			}
		};

		template<>
		struct MetadataTraits<FVector4> : DefaultOperationTraits<FVector4>
		{
			enum { CompressData = false };
			enum { CanMinMax = true };
			enum { CanInterpolate = true };

			static FVector4 Min(const FVector4& A, const FVector4& B)
			{
				return FVector4(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z), FMath::Min(A.W, B.W));
			}

			static FVector4 Max(const FVector4& A, const FVector4& B)
			{
				return FVector4(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z), FMath::Max(A.W, B.W));
			}

			static FVector4 WeightedSum(const FVector4& A, const FVector4& B, float Weight)
			{
				return A + B * Weight;
			}

			static FVector4 ZeroValue()
			{
				return FVector4::Zero();
			}
		};

		// Quaternion
		template<>
		struct MetadataTraits<FQuat>
		{
			enum { CompressData = false };
			enum { CanMinMax = false };
			enum { CanSubAdd = true };
			enum { CanMulDiv = true };
			enum { CanInterpolate = true };

			static bool Equal(const FQuat& A, const FQuat& B)
			{
				return A == B;
			}

			static FQuat Add(const FQuat& A, const FQuat& B)
			{
				return A * B;
			}

			static FQuat Sub(const FQuat& A, const FQuat& B)
			{
				return A * B.Inverse();
			}

			static FQuat Mul(const FQuat& A, const FQuat& B)
			{
				return A * B;
			}

			static FQuat Div(const FQuat& A, const FQuat& B)
			{
				return A * B.Inverse();
			}

			static FQuat WeightedSum(const FQuat& A, const FQuat& B, float Weight)
			{
				// WARNING: the quaternion won't be normalized
				FQuat BlendQuat = B * Weight;

				if ((A | BlendQuat) >= 0.0f)
					return A + BlendQuat;
				else
					return A - BlendQuat;
			}

			static FQuat ZeroValue()
			{
				return FQuat::Identity;
			}
		};

		// Rotator
		template<>
		struct MetadataTraits<FRotator>
		{
			enum { CompressData = false };
			enum { CanMinMax = false };
			enum { CanSubAdd = true };
			enum { CanMulDiv = true };
			enum { CanInterpolate = true };

			static bool Equal(const FRotator& A, const FRotator& B)
			{
				return A == B;
			}

			static FRotator Add(const FRotator& A, const FRotator& B)
			{
				return A + B;
			}

			static FRotator Sub(const FRotator& A, const FRotator& B)
			{
				return A - B;
			}

			static FRotator Mul(const FRotator& A, const FRotator& B)
			{
				return A + B;
			}

			static FRotator Div(const FRotator& A, const FRotator& B)
			{
				return A - B;
			}

			static FRotator WeightedSum(const FRotator& A, const FRotator& B, float Weight)
			{
				// TODO review this, should we use TCustomLerp<UE::Math::TRotator<T>> ?
				return A + (B * Weight);
			}

			static FRotator ZeroValue()
			{
				return FRotator::ZeroRotator;
			}
		};

		// Transform
		template<>
		struct MetadataTraits<FTransform>
		{
			enum { CompressData = false };
			enum { CanMinMax = false };
			enum { CanSubAdd = true };
			enum { CanMulDiv = true };
			enum { CanInterpolate = true };

			static bool Equal(const FTransform& A, const FTransform& B)
			{
				return A.GetLocation() == B.GetLocation() &&
					A.GetRotation() == B.GetRotation() &&
					A.GetScale3D() == B.GetScale3D();
			}

			static FTransform Add(const FTransform& A, const FTransform& B)
			{
				return A * B;
			}

			static FTransform Sub(const FTransform& A, const FTransform& B)
			{
				return A * B.Inverse();
			}

			static FTransform Mul(const FTransform& A, const FTransform& B)
			{
				return A * B;
			}

			static FTransform Div(const FTransform& A, const FTransform& B)
			{
				return A * B.Inverse();
			}

			static FQuat WeightedQuatSum(const FQuat& Q, const FQuat& V, float Weight)
			{
				FQuat BlendQuat = V * Weight;

				if ((Q | BlendQuat) >= 0.0f)
					return Q + BlendQuat;
				else
					return Q - BlendQuat;
			}

			static FTransform WeightedSum(const FTransform& A, const FTransform& B, float Weight)
			{
				// WARNING: the rotation won't be normalized
				return FTransform(
					WeightedQuatSum(A.GetRotation(), B.GetRotation(), Weight),
					A.GetLocation() + B.GetLocation() * Weight,
					A.GetScale3D() + B.GetScale3D() * Weight);
			}

			static FTransform ZeroValue()
			{
				return FTransform::Identity;
			}
		};

		// Strings
		template<>
		struct MetadataTraits<FString>
		{
			enum { CompressData = true };
			enum { CanMinMax = false };
			enum { CanSubAdd = false };
			enum { CanMulDiv = false };
			enum { CanInterpolate = false };

			static bool Equal(const FString& A, const FString& B)
			{
				return A == B;
			}
		};

		template<>
		struct MetadataTraits<FName>
		{
			enum { CompressData = true };
			enum { CanMinMax = false };
			enum { CanSubAdd = false };
			enum { CanMulDiv = false };
			enum { CanInterpolate = false };

			static bool Equal(const FName& A, const FName& B)
			{
				return A == B;
			}
		};
	}
}