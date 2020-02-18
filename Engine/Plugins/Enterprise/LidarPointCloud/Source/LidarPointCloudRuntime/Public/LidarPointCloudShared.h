// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LidarPointCloudShared.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLidarPointCloud, Log, All);
DECLARE_STATS_GROUP(TEXT("Lidar Point Cloud"), STATGROUP_LidarPointCloud, STATCAT_Advanced);

#define PC_LOG(Format, ...) UE_LOG(LogLidarPointCloud, Log, TEXT(Format), ##__VA_ARGS__)
#define PC_WARNING(Format, ...) UE_LOG(LogLidarPointCloud, Warning, TEXT(Format), ##__VA_ARGS__)
#define PC_ERROR(Format, ...) UE_LOG(LogLidarPointCloud, Error, TEXT(Format), ##__VA_ARGS__)

#if !CPP
USTRUCT(noexport)
struct FDoubleVector
{
	UPROPERTY()
	double X;

	UPROPERTY()
	double Y;

	UPROPERTY()
	double Z;
};
#endif

struct LIDARPOINTCLOUDRUNTIME_API FDoubleVector
{
	double X;
	double Y;
	double Z;

	/** A zero vector (0,0,0) */
	static const FDoubleVector ZeroVector;

	/** One vector (1,1,1) */
	static const FDoubleVector OneVector;

	/** World up vector (0,0,1) */
	static const FDoubleVector UpVector;

	/** Unreal forward vector (1,0,0) */
	static const FDoubleVector ForwardVector;

	/** Unreal right vector (0,1,0) */
	static const FDoubleVector RightVector;

	FORCEINLINE FDoubleVector() {}

	explicit FORCEINLINE FDoubleVector(double InD) : X(InD), Y(InD), Z(InD) {}

	FORCEINLINE FDoubleVector(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

	FORCEINLINE FDoubleVector(const FVector& V) : X(V.X), Y(V.Y), Z(V.Z) {}

	FORCEINLINE FDoubleVector operator-() const
	{
		return FDoubleVector(-X, -Y, -Z);
	}

	FORCEINLINE FDoubleVector operator+(const FDoubleVector& V) const
	{
		return FDoubleVector(X + V.X, Y + V.Y, Z + V.Z);
	}

	FORCEINLINE FDoubleVector operator-(const FDoubleVector& V) const
	{
		return FDoubleVector(X - V.X, Y - V.Y, Z - V.Z);
	}

	FORCEINLINE FDoubleVector operator+(const FVector& V) const
	{
		return FDoubleVector(X + V.X, Y + V.Y, Z + V.Z);
	}

	FORCEINLINE FDoubleVector operator-(const FVector& V) const
	{
		return FDoubleVector(X - V.X, Y - V.Y, Z - V.Z);
	}

	FORCEINLINE FDoubleVector operator+=(const FDoubleVector& V)
	{
		X += V.X; Y += V.Y; Z += V.Z;
		return *this;
	}

	FORCEINLINE FDoubleVector operator-=(const FDoubleVector& V)
	{
		X -= V.X; Y -= V.Y; Z -= V.Z;
		return *this;
	}

	FORCEINLINE FDoubleVector operator+=(const FVector& V)
	{
		X += V.X; Y += V.Y; Z += V.Z;
		return *this;
	}

	FORCEINLINE FDoubleVector operator-=(const FVector& V)
	{
		X -= V.X; Y -= V.Y; Z -= V.Z;
		return *this;
	}

	FORCEINLINE FDoubleVector operator*=(double Scale)
	{
		X *= Scale; Y *= Scale; Z *= Scale;
		return *this;
	}

	FORCEINLINE FDoubleVector operator*(double Scale) const
	{
		return FDoubleVector(X * Scale, Y * Scale, Z * Scale);
	}

	FORCEINLINE FDoubleVector operator*(const FVector& V) const
	{
		return FDoubleVector(X * V.X, Y * V.Y, Z * V.Z);
	}

	FORCEINLINE FDoubleVector operator*(const FDoubleVector& V) const
	{
		return FDoubleVector(X * V.X, Y * V.Y, Z * V.Z);
	}

	FORCEINLINE FDoubleVector operator*(const FIntVector& V) const
	{
		return FDoubleVector(X * V.X, Y * V.Y, Z * V.Z);
	}

	FORCEINLINE FDoubleVector operator/(int32 Scale) const
	{
		return FDoubleVector(X / Scale, Y / Scale, Z / Scale);
	}

	FORCEINLINE FDoubleVector operator/(double Scale) const
	{
		return FDoubleVector(X / Scale, Y / Scale, Z / Scale);
	}

	FORCEINLINE bool Equals(const FDoubleVector& V, float Tolerance = KINDA_SMALL_NUMBER) const
	{
		return FMath::Abs(X - V.X) <= Tolerance && FMath::Abs(Y - V.Y) <= Tolerance && FMath::Abs(Z - V.Z) <= Tolerance;
	}

	FORCEINLINE bool IsZero(float Tolerance = KINDA_SMALL_NUMBER) const
	{
		return Equals(FDoubleVector::ZeroVector, Tolerance);
	}

	FORCEINLINE bool IsNearlyZero(float Tolerance) const
	{
		return FMath::Abs(X) <= Tolerance && FMath::Abs(Y) <= Tolerance && FMath::Abs(Z) <= Tolerance;
	}

	FORCEINLINE FVector ToVector() const { return FVector(X, Y, Z); }
	FORCEINLINE FIntVector ToIntVector() const { return FIntVector(X, Y, Z); }
};

#pragma pack(push)
#pragma pack(1)
USTRUCT(BlueprintType)
struct LIDARPOINTCLOUDRUNTIME_API FLidarPointCloudPoint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar Point Cloud Point")
	FVector Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar Point Cloud Point")
	FColor Color;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar Point Cloud Point")
	uint8 bVisible : 1;

	/** Valid range is 0 - 31. */
	uint8 ClassificationID : 5;

private:
	uint8 bSelected : 1;
	uint8 bMarkedForDeletion : 1;

public:
	FLidarPointCloudPoint()
		: Location(FVector::ZeroVector)
		, Color(FColor::White)
		, bVisible(true)
		, ClassificationID(0)
		, bSelected(false)
		, bMarkedForDeletion(false)
	{
	}
	FLidarPointCloudPoint(const float& X, const float& Y, const float& Z)
		: FLidarPointCloudPoint()
	{
		Location.X = X;
		Location.Y = Y;
		Location.Z = Z;
	}
	FLidarPointCloudPoint(const float& X, const float& Y, const float& Z, const float& I)
		: FLidarPointCloudPoint(X, Y, Z)
	{
		Color.A = FMath::FloorToInt(FMath::Clamp(I, 0.0f, 1.0f) * 255.999f);
	}
	FLidarPointCloudPoint(const float& X, const float& Y, const float& Z, const float& R, const float& G, const float& B, const float& A = 1.0f)
		: FLidarPointCloudPoint(X, Y, Z)
	{
		Color = FLinearColor(R, G, B, A).ToFColor(false);
	}
	FLidarPointCloudPoint(const FVector& Location) : FLidarPointCloudPoint(Location.X, Location.Y, Location.Z) {}
	FLidarPointCloudPoint(const FVector& Location, const float& R, const float& G, const float& B, const float& A = 1.0f)
		: FLidarPointCloudPoint(Location)
	{
		Color = FLinearColor(R, G, B, A).ToFColor(false);
	}
	FLidarPointCloudPoint(const FVector& Location, const float& R, const float& G, const float& B, const float& A, const uint8& ClassificationID)
		: FLidarPointCloudPoint(Location, R, G, B, A)
	{
		this->ClassificationID = ClassificationID;
	}
	FLidarPointCloudPoint(const FVector& Location, const FColor& Color, const bool& bVisible, const uint8& ClassificationID)
		: FLidarPointCloudPoint(Location)
	{
		this->Color = Color;
		this->bVisible = bVisible;
		this->ClassificationID = ClassificationID;
	}
	FLidarPointCloudPoint(const FLidarPointCloudPoint& Other)
		: FLidarPointCloudPoint(Other.Location, Other.Color, Other.bVisible, Other.ClassificationID)
	{
	}

	FORCEINLINE void CopyFrom(const FLidarPointCloudPoint& Other)
	{
		Location = Other.Location;
		Color = Other.Color;
		bVisible = Other.bVisible;
		ClassificationID = Other.ClassificationID;
	}

	bool operator==(const FLidarPointCloudPoint& P) const { return Location == P.Location && Color == P.Color && bVisible == P.bVisible && ClassificationID == P.ClassificationID; }

	friend FArchive& operator<<(FArchive& Ar, FLidarPointCloudPoint& P);
	friend class FLidarPointCloudOctree;
	friend class FLidarPointCloudInstanceBuffer;
#if WITH_EDITOR
	friend class FLidarPointCloudEditor;
#endif
};
#pragma pack(pop)

/** Used in blueprint latent function execution */
UENUM(BlueprintType)
enum class ELidarPointCloudAsyncMode : uint8
{
	Success,
	Failure,
	Progress
};

/** Used for Raycasting */
struct FLidarPointCloudRay
{
public:
	FVector Origin;

private:
	FVector Direction;
	FVector InversedDirection;

public:
	FLidarPointCloudRay() : FLidarPointCloudRay(FVector::ZeroVector, FVector::ForwardVector) {}
	FLidarPointCloudRay(const FVector& Origin, const FVector& Direction) : Origin(Origin)
	{
		SetDirection(Direction);
	}

	FLidarPointCloudRay& TransformBy(FTransform Transform)
	{
		Origin = Transform.TransformPosition(Origin);
		SetDirection(Transform.TransformVector(Direction));
		return *this;
	}

	FORCEINLINE FVector GetDirection() const { return Direction; }
	FORCEINLINE void SetDirection(const FVector& NewDirection)
	{
		Direction = NewDirection;
		InversedDirection = FVector::OneVector / Direction;
	}

	/** An Efficient and Robust Ray-Box Intersection Algorithm. Amy Williams et al. 2004. */
	FORCEINLINE bool Intersects(const FBox& Box) const
	{
		float tmin, tmax, tymin, tymax, tzmin, tzmax;

		tmin = ((InversedDirection.X < 0 ? Box.Max.X : Box.Min.X) - Origin.X) * InversedDirection.X;
		tmax = ((InversedDirection.X < 0 ? Box.Min.X : Box.Max.X) - Origin.X) * InversedDirection.X;
		tymin = ((InversedDirection.Y < 0 ? Box.Max.Y : Box.Min.Y) - Origin.Y) * InversedDirection.Y;
		tymax = ((InversedDirection.Y < 0 ? Box.Min.Y : Box.Max.Y) - Origin.Y) * InversedDirection.Y;

		if ((tmin > tymax) || (tymin > tmax))
		{
			return false;
		}

		if (tymin > tmin)
		{
			tmin = tymin;
		}

		if (tymax < tmax)
		{
			tmax = tymax;
		}

		tzmin = ((InversedDirection.Z < 0 ? Box.Max.Z : Box.Min.Z) - Origin.Z) * InversedDirection.Z;
		tzmax = ((InversedDirection.Z < 0 ? Box.Min.Z : Box.Max.Z) - Origin.Z) * InversedDirection.Z;

		if ((tmin > tzmax) || (tzmin > tmax))
		{
			return false;
		}

		return true;
	}
	FORCEINLINE bool Intersects(const FLidarPointCloudPoint* Point, const float& RadiusSq) const
	{
		const FVector L = Point->Location - Origin;
		const float tca = FVector::DotProduct(L, Direction);
		const float d2 = FVector::DotProduct(L, L) - tca * tca;

		return d2 <= RadiusSq;
	}
};

struct FBenchmarkTimer
{
	static void Reset()
	{
		Time = FPlatformTime::Seconds();
	}
	static double Split(uint8 Decimal = 2)
	{
		double Now = FPlatformTime::Seconds();
		double Delta = Now - Time;
		Time = Now;

		uint32 Multiplier = FMath::Pow(10, Decimal);

		return FMath::RoundToDouble(Delta * Multiplier * 1000) / Multiplier;
	}
	static void Log(FString Text, uint8 Decimal = 2)
	{
		const double SplitTime = Split(Decimal);
		PC_LOG("%s: %f ms", *Text, SplitTime);
	}

private:
	static double Time;
};

struct FScopeBenchmarkTimer
{
public:
	bool bActive;

private:
	double Time;
	FString Label;
	float* OutTimer;

public:
	FScopeBenchmarkTimer(const FString& Label)
		: bActive(true)
		, Time(FPlatformTime::Seconds())
		, Label(Label)
		, OutTimer(nullptr)
	{
	}
	FScopeBenchmarkTimer(float* OutTimer)
		: bActive(true)
		, Time(FPlatformTime::Seconds())
		, OutTimer(OutTimer)
	{
	}
	~FScopeBenchmarkTimer()
	{
		if (bActive)
		{
			float Delta = FMath::RoundToDouble((FPlatformTime::Seconds() - Time) * 100000) * 0.01;

			if (OutTimer)
			{
				*OutTimer += Delta;
			}
			else
			{
				PC_LOG("%s: %f ms", *Label, Delta);
			}
		}
	}
};