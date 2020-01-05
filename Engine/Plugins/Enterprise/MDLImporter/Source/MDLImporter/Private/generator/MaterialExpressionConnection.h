// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"

class UMaterialExpression;
class UTexture;

namespace Generator
{
	enum EConnectionType
	{
		Expression,
		Boolean,
		Float,
		Float2,
		Float3,
		Float4,
		Texture,
		TextureSelection
	};

	struct FMaterialExpressionConnection
	{
		struct FData
		{
			FData(UMaterialExpression* Expression, int32 Index, bool bIsDefault);

			bool operator!=(const FData& rhs);

			UMaterialExpression* Expression;
			int32                Index;
			bool                 bIsDefault;
		};

		FMaterialExpressionConnection();
		FMaterialExpressionConnection(UMaterialExpression* Expression, int32 OutputIndex = 0, bool bIsDefault = false);
		FMaterialExpressionConnection(bool bValue);
		FMaterialExpressionConnection(int Value);
		FMaterialExpressionConnection(float Value);
		FMaterialExpressionConnection(float Value0, float Value1);
		FMaterialExpressionConnection(float Value0, float Value1, float Value2);
		FMaterialExpressionConnection(float Value0, float Value1, float Value2, float Value3);
		FMaterialExpressionConnection(double Value);
		FMaterialExpressionConnection(UTexture* Texture);
		FMaterialExpressionConnection(const FData& Value, const FData& True, const FData& False);

		bool operator!=(const FMaterialExpressionConnection& rhs);

		EConnectionType ConnectionType;
		union {
			FData     ExpressionData;
			bool      bValue;
			float     Values[4];
			UTexture* Texture;
			FData     TextureSelectionData[3];
		};
	};
	struct FMaterialExpressionConnectionList
	{
		TArray<FMaterialExpressionConnection> Connections;

		bool IsUsed;

		FMaterialExpressionConnectionList()
			: IsUsed(false)
		{}

		FMaterialExpressionConnectionList(std::initializer_list<FMaterialExpressionConnection> Expressions)
			: IsUsed(false)
		{
			Connections = Expressions;
		}

		void Reserve(int32 Size)
		{
			Connections.Reserve(Size);
		}

		void SetNum(int32 Size)
		{
			Connections.SetNum(Size);
		}

		void Empty() 
		{
			Connections.Empty();
		}

		int32 Num() const
		{
			return Connections.Num();
		}

		void Add(const FMaterialExpressionConnection& Connection)
		{
			Connections.Add(Connection);
		}

		void Push(const FMaterialExpressionConnection&& Connection)
		{
			Connections.Push(Connection);
		}

		template <typename... ArgsType>
		FORCEINLINE int32 Emplace(ArgsType&&... Args)
		{
			return Connections.Emplace(Forward<ArgsType>(Args)...);
		}


		FMaterialExpressionConnection& operator[](int32 ConnectionIndex)
		{
			return Connections[ConnectionIndex];
		}

		const 	FMaterialExpressionConnection& operator[](int32 ConnectionIndex) const
		{
			return Connections[ConnectionIndex];
		}

		void Append(const FMaterialExpressionConnectionList& Other)
		{
			Connections.Append(Other.Connections);
		}

		template <typename Predicate>
		int32 FindLastByPredicate(Predicate Pred)
		{
			return Connections.FindLastByPredicate(Pred);
		}

		operator TArray<FMaterialExpressionConnection>()
		{
			return Connections;
		}


		void Reset()
		{
			Connections.SetNum(0);
			IsUsed = false;
		}


	};
		

	//

	inline FMaterialExpressionConnection::FMaterialExpressionConnection()
	    : ConnectionType(EConnectionType::Expression)
	    , ExpressionData(nullptr, 0, true)
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(UMaterialExpression* Expression,
	                                                                    int32                OutputIndex /*= 0*/,
	                                                                    bool                 bIsDefault /*= false*/)
	    : ConnectionType(EConnectionType::Expression)
	    , ExpressionData(Expression, OutputIndex, bIsDefault)
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(bool bValue)
	    : ConnectionType(EConnectionType::Boolean)
	    , bValue(bValue)
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(int Value)
	    : ConnectionType(EConnectionType::Float)
	    , Values {static_cast<float>(Value), 0.0f, 0.0f, 0.0f}
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(float Value)
	    : ConnectionType(EConnectionType::Float)
	    , Values {Value, 0.0f, 0.0f, 0.0f}
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(float Value0, float Value1)
	    : ConnectionType(EConnectionType::Float2)
	    , Values {Value0, Value1, 0.0f, 0.0f}
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(float Value0, float Value1, float Value2)
	    : ConnectionType(EConnectionType::Float3)
	    , Values {Value0, Value1, Value2, 0.0f}
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(float Value0, float Value1, float Value2, float Value3)
	    : ConnectionType(EConnectionType::Float4)
	    , Values {Value0, Value1, Value2, Value3}
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(double Value)
	    : ConnectionType(EConnectionType::Float)
	    , Values {static_cast<float>(Value), 0.0f, 0.0f, 0.0f}
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(UTexture* Texture)
	    : ConnectionType(EConnectionType::Texture)
	    , Texture(Texture)
	{
	}

	inline FMaterialExpressionConnection::FMaterialExpressionConnection(const FData& Value, const FData& True, const FData& False)
	    : ConnectionType(EConnectionType::TextureSelection)
	{
		TextureSelectionData[0] = Value;
		TextureSelectionData[1] = True;
		TextureSelectionData[2] = False;
	}

	inline bool FMaterialExpressionConnection::operator!=(const FMaterialExpressionConnection& rhs)
	{
		return (ConnectionType != rhs.ConnectionType) ||
		       ((ConnectionType == EConnectionType::Expression) && (ExpressionData != rhs.ExpressionData)) ||
		       ((ConnectionType == EConnectionType::Boolean) && (bValue != rhs.bValue)) ||
		       ((ConnectionType == EConnectionType::Float) && (Values[0] != rhs.Values[0])) ||
		       ((ConnectionType == EConnectionType::Float2) && ((Values[0] != rhs.Values[0]) || (Values[1] != rhs.Values[1]))) ||
		       ((ConnectionType == EConnectionType::Float3) &&
		        ((Values[0] != rhs.Values[0]) || (Values[1] != rhs.Values[1]) || (Values[2] != rhs.Values[2]))) ||
		       ((ConnectionType == EConnectionType::Float4) &&
		        ((Values[0] != rhs.Values[0]) || (Values[1] != rhs.Values[1]) || (Values[2] != rhs.Values[2]) || (Values[3] != rhs.Values[3]))) ||
		       ((ConnectionType == EConnectionType::Texture) && (Texture != rhs.Texture)) ||
		       ((ConnectionType == EConnectionType::TextureSelection) &&
		        ((TextureSelectionData[0] != rhs.TextureSelectionData[0]) || (TextureSelectionData[1] != rhs.TextureSelectionData[1]) ||
		         (TextureSelectionData[2] != rhs.TextureSelectionData[2])));
	}

	inline FMaterialExpressionConnection::FData::FData(UMaterialExpression* Expression, int32 Index, bool bIsDefault)
	    : Expression(Expression)
	    , Index(Index)
	    , bIsDefault(bIsDefault)
	{
	}

	inline bool FMaterialExpressionConnection::FData::operator!=(const FData& rhs)
	{
		return (Expression != rhs.Expression) || (Index != rhs.Index) || (bIsDefault != rhs.bIsDefault);
	}

}  // namespace Generator
