// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/MatrixH.h"

namespace CADKernel
{
	class FLayerData;
	class FRGBColorData;
	class FRGBAColorData;

	template<typename ValueType>
	class TMetadata;

	class CADKERNEL_API FMetadata
	{
	public:
		FMetadata() = default;

		template<typename ValueType>
		static TSharedRef<FMetadata> Create(const ValueType& Value)
		{
			return MakeShared<TMetadata<ValueType>>(Value);
		}

		static TSharedRef<FMetadata> CreateLayer(const int32 LayerId, const FString& LayerName, const int32 LayerFlag)
		{
			TSharedRef<FLayerData> Layer = MakeShared<FLayerData>(LayerId, LayerName, LayerFlag);
			return StaticCastSharedRef<FMetadata>(Layer);
		}

		static TSharedRef<FMetadata> CreateRGBColor(const double RedValue, const double GreenValue, const double BlueValue)
		{
			TSharedRef<FRGBColorData> Layer = MakeShared<FRGBColorData>(RedValue, GreenValue, BlueValue);
			return StaticCastSharedRef<FMetadata>(Layer);
		}

		static TSharedRef<FMetadata> CreateRGBAColor(const double RedValue, const double GreenValue, const double BlueValue, const double AlphaValue)
		{
			TSharedRef<FRGBAColorData> Layer = MakeShared<FRGBAColorData>(RedValue, GreenValue, BlueValue, AlphaValue);
			return StaticCastSharedRef<FMetadata>(Layer);
		}
	};

	template<typename ValueType>
	class TMetadata : public FMetadata
	{
	protected:
		ValueType Value;

	public:
		TMetadata(const ValueType& InValue)
			: Value(InValue)
		{
		}
		~TMetadata() = default;

		const ValueType& GetValue()
		{
			return Value;
		}
	};

	class CADKERNEL_API FRGBColorData : public FMetadata
	{
	protected:
		uint8 Red;
		uint8 Green;
		uint8 Blue;

	public:

		FRGBColorData(const double RedValue, const double GreenValue, const double BlueValue)
			: Red(ToUInt8(RedValue))
			, Green(ToUInt8(GreenValue))
			, Blue(ToUInt8(BlueValue))
		{
		}

		FRGBColorData(const uint8 RedValue, const uint8 GreenValue, const uint8 BlueValue)
			: Red(RedValue)
			, Green(GreenValue)
			, Blue(BlueValue)
		{
		}
	};

	class CADKERNEL_API FRGBAColorData : public FRGBColorData
	{
	protected:
		uint8 Alpha;

	public:

		FRGBAColorData(const double RedValue, const double GreenValue, const double BlueValue, const double AlphaValue)
			: FRGBColorData(RedValue, GreenValue, BlueValue)
			, Alpha(ToUInt8(AlphaValue))
		{
		}

		FRGBAColorData(const uint8 RedValue, const uint8 GreenValue, const uint8 BlueValue, const uint8 AlphaValue)
			: FRGBColorData(RedValue, GreenValue, BlueValue)
			, Alpha(AlphaValue)
		{
		}
	};

	class CADKERNEL_API FLayerData : public FMetadata
	{
	protected:
		int32 Id;
		FString Name;
		int32 Flag;

	public:

		FLayerData(const int32 InLayerId, const FString& InLayerName, const int32 InLayerFlag)
			: Id(InLayerId)
			, Name(InLayerName)
			, Flag(InLayerFlag)
		{
		}

		const int32 GetId()
		{
			return Id;
		}

		const FString& GetName()
		{
			return Name;
		}

		const int32 GetFlag()
		{
			return Flag;
		}

	};
}


