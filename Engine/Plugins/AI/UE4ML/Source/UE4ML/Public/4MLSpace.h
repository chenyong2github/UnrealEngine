// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "4MLTypes.h"
#include "4MLJson.h"
#include "4MLSpace.generated.h"


UENUM()
enum class E4MLSpaceType : uint8
{
	Discrete,
	MultiDiscrete,
	Box,
	Tuple,
	MAX
};
DECLARE_ENUM_TO_STRING(E4MLSpaceType);


namespace F4ML
{
	struct UE4ML_API FSpace : public IJsonable, public TSharedFromThis<FSpace>
	{
		virtual ~FSpace() {}
		E4MLSpaceType Type = E4MLSpaceType::MAX;

		virtual FString ToJson() const { return TEXT("{\"InvalidFSpaceType\": 0}"); }
		virtual int32 Num() const { return 0; }
	};

	struct UE4ML_API FSpace_Discrete : public FSpace
	{
		uint32 Count;
		explicit FSpace_Discrete(uint32 InCount = 2);
		virtual FString ToJson() const override;
		virtual int32 Num() const override { return Count; }
	};

	/** Multiple options, each with separate discrete range */
	struct UE4ML_API FSpace_MultiDiscrete : public FSpace
	{
		TArray<uint32> Options;
		// simplified constructor creating InCount number of InValues-count options
		explicit FSpace_MultiDiscrete(uint32 InCount, uint32 InValues = 2); 
		// each element in InOptions defines number of possible values for each n-th option
		explicit FSpace_MultiDiscrete(std::initializer_list<uint32> InOptions);
		explicit FSpace_MultiDiscrete(const TArray<uint32>& InOptions);
		virtual FString ToJson() const override;
		virtual int32 Num() const override { return Options.Num(); }
	};

	struct UE4ML_API FSpace_Box : public FSpace
	{
		TArray<uint32> Shape;
		float Low = -1.f;
		float High = 1.f;
		FSpace_Box();
		FSpace_Box(std::initializer_list<uint32> InShape, float InLow = -1.f, float InHigh = 1.f);
		virtual FString ToJson() const override;
		virtual int32 Num() const override;

		static TSharedPtr<FSpace> Vector3D(float Low = -1.f, float High = 1.f) { return MakeShareable(new FSpace_Box({ 3 }, Low, High)); }
		static TSharedPtr<FSpace> Vector2D(float Low = -1.f, float High = 1.f) { return MakeShareable(new FSpace_Box({ 2 }, Low, High)); }
	};

	struct UE4ML_API FSpace_Dummy : public FSpace_Box
	{
		FSpace_Dummy() : FSpace_Box({ 0 }) {}
		virtual int32 Num() const override { return 0; }
	};

	struct UE4ML_API FSpace_Tuple : public FSpace
	{
		TArray<TSharedPtr<FSpace> > SubSpaces;
		FSpace_Tuple();
		FSpace_Tuple(std::initializer_list<TSharedPtr<FSpace> > InitList);
		FSpace_Tuple(TArray<TSharedPtr<FSpace> >& InSubSpaces);
		virtual FString ToJson() const override;
		virtual int32 Num() const override;
	};

	struct FSpaceSerializeGuard
	{
		const TSharedRef<F4ML::FSpace>& Space;
		F4MLMemoryWriter& Ar;
		const int64 Tell;
		const int ElementSize;
		FSpaceSerializeGuard(const TSharedRef<F4ML::FSpace>& InSpace, F4MLMemoryWriter& InAr,  const int InElementSize = sizeof(float))
			: Space(InSpace), Ar(InAr), Tell(InAr.Tell()), ElementSize(InElementSize)
		{}
		~FSpaceSerializeGuard()
		{
			ensure(FMath::Abs(Ar.Tell() - Tell) == Space->Num() * ElementSize);
		}
	};

}


struct F4MLDescription
{
	static bool FromJson(const FString& JsonString, F4MLDescription& OutInstance);
	FString ToJson() const;

	F4MLDescription& Add(FString Key, FString Element) { Data.Add(TPair<FString, FString>(Key, Element)); return(*this); }
	F4MLDescription& Add(FString Key, const F4MLDescription& Element) { Add(Key, Element.ToJson()); return(*this); }
	F4MLDescription& Add(FString Key, const int Element) { Add(Key, FString::FromInt(Element)); return(*this); }
	F4MLDescription& Add(FString Key, const float Element) { Add(Key, FString::SanitizeFloat(Element)); return(*this); }
	F4MLDescription& Add(const F4ML::FSpace& Space) { PrepData.Add(Space.ToJson()); return(*this); }

	/** Used in loops to optimize memory use */
	void Reset() { Data.Reset(); PrepData.Reset(); }

	bool IsEmpty() const { return (Data.Num() == 0); }
protected:
	TArray<TPair<FString, FString>> Data;
	TArray<FString> PrepData;
};


struct F4MLSpaceDescription
{
	typedef TPair<FString, F4MLDescription> ValuePair;
	FString ToJson() const;

	F4MLSpaceDescription& Add(FString Key, F4MLDescription&& Element) { Data.Add(ValuePair(Key, Element)); return(*this); }
	F4MLSpaceDescription& Add(FString Key, F4MLDescription Element) { Data.Add(ValuePair(Key, Element)); return(*this); }
protected:
	TArray<ValuePair> Data;
};
