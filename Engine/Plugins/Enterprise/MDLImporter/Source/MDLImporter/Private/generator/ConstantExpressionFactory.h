// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "generator/BaseExpressionFactory.h"
#include "generator/MaterialExpressionConnection.h"

namespace mi
{
	namespace neuraylib
	{
		class IValue;
		class ITransaction;
	}
}

namespace Generator
{
	class FMaterialTextureFactory;

	class FConstantExpressionFactory : public FBaseExpressionFactory
	{
	public:
		FConstantExpressionFactory();

		void SetTextureFactory(FMaterialTextureFactory* Factory);

		FMaterialExpressionConnectionList CreateExpression(mi::neuraylib::ITransaction& Transaction, const mi::neuraylib::IValue& MDLConstant);

	private:
		FMaterialTextureFactory* TextureFactory;
	};

	inline void FConstantExpressionFactory::SetTextureFactory(FMaterialTextureFactory* Factory)
	{
		TextureFactory = Factory;
	}

}  // namespace Generator
