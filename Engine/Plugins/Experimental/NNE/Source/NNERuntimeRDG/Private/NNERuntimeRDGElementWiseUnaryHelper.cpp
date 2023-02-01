// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGElementWiseUnaryHelper.h"
#include "Math/UnrealMathUtility.h"

namespace UE::NNERuntimeRDG::Private::ElementWiseUnaryCPUHelper
{
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Abs>(float X, float Alpha, float Beta, float Gamma) { return FMath::Abs(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Acos>(float X, float Alpha, float Beta, float Gamma) { return FMath::Acos(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Acosh>(float X, float Alpha, float Beta, float Gamma) {
		//https://mathworld.wolfram.com/InverseHyperbolicCosine.html
		float FloatNan = FMath::Sqrt(-1.0f);
		float yAboveOne = FMath::Loge(X + FMath::Sqrt(X + 1.0f) + FMath::Sqrt(X - 1.0f));
		return (X == 1.0f) ? 0.0f : (X >= 1.0f) ? yAboveOne : FloatNan;
	}
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Asin>(float X, float Alpha, float Beta, float Gamma) { return FMath::Asin(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Asinh>(float X, float Alpha, float Beta, float Gamma) {
		//https://mathworld.wolfram.com/InverseHyperbolicSine.html
		return FMath::Loge(X + FMath::Sqrt(1 + (X * X)));
	}
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Atan>(float X, float Alpha, float Beta, float Gamma) { return FMath::Atan(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Atanh>(float X, float Alpha, float Beta, float Gamma) {
		//https://mathworld.wolfram.com/InverseHyperbolicTangent.html
		return 0.5f * (FMath::Loge(1 + X) - FMath::Loge(1 - X));
	}

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Ceil>(float X, float Alpha, float Beta, float Gamma) { return FMath::CeilToFloat(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Cos>(float X, float Alpha, float Beta, float Gamma) { return FMath::Cos(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Cosh>(float X, float Alpha, float Beta, float Gamma) {
		//https://mathworld.wolfram.com/HyperbolicCosine.html
		return 0.5f * (FMath::Exp(X) - FMath::Exp(-X));
	}
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Elu>(float X, float Alpha, float Beta, float Gamma) {
		//https://github.com/onnx/onnx/blob/main/docs/Operators.md#elu
		float yNeg = Alpha * (FMath::Exp(X) - 1.0f);
		float yPosOrZero = X;
		return (X >= 0.0f) ? yPosOrZero : yNeg;
	}
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Exp>(float X, float Alpha, float Beta, float Gamma) { return FMath::Exp(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Floor>(float X, float Alpha, float Beta, float Gamma) { return FMath::Floor(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::IsInf>(float X, float Alpha, float Beta, float Gamma) { return !FMath::IsFinite(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::IsNan>(float X, float Alpha, float Beta, float Gamma) { return FMath::IsNaN(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::HardSigmoid>(float X, float Alpha, float Beta, float Gamma) {
		//https://github.com/onnx/onnx/blob/main/docs/Operators.md#hardSigmoid
		return FMath::Max(0.0f, FMath::Min(1.0f, Alpha * X + Beta));
	}

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::HardSwish>(float X, float Alpha, float Beta, float Gamma) {
		//https://github.com/onnx/onnx/blob/main/docs/Operators.md#hardSwish
		return Apply<NNECore::Internal::EElementWiseUnaryOperatorType::HardSigmoid>(X, 1.0f / 6.0f, 0.5f, Gamma);
	}

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::LeakyRelu>(float X, float Alpha, float Beta, float Gamma) { return (X >= 0.0f) ? X : Alpha * X; }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Log>(float X, float Alpha, float Beta, float Gamma) { return FMath::Loge(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Neg>(float X, float Alpha, float Beta, float Gamma) { return -X; }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Reciprocal>(float X, float Alpha, float Beta, float Gamma) { return 1.0f/X; }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Relu>(float X, float Alpha, float Beta, float Gamma) { return FMath::Max(X, 0.0f); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Round>(float X, float Alpha, float Beta, float Gamma) { return FMath::RoundToFloat(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Selu>(float X, float Alpha, float Beta, float Gamma) {
		//https://github.com/onnx/onnx/blob/main/docs/Operators.md#Selu
		float yNegOrZero = Gamma * (Alpha * FMath::Exp(X) - Alpha);
		float yPos = Gamma * X;
		return (X > 0.0f) ? yPos : yNegOrZero;
	}
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Sigmoid>(float X, float Alpha, float Beta, float Gamma) { return 1.0f / (1.0f + FMath::Exp(-X)); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Sign>(float X, float Alpha, float Beta, float Gamma) { return FMath::Sign(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Sin>(float X, float Alpha, float Beta, float Gamma) { return FMath::Sin(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Sinh>(float X, float Alpha, float Beta, float Gamma) { return FMath::Sinh(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Softplus>(float X, float Alpha, float Beta, float Gamma) { return FMath::Loge(FMath::Exp(X) + 1.0f); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Softsign>(float X, float Alpha, float Beta, float Gamma) { return X / (1.0f + FMath::Abs(X)); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Sqrt>(float X, float Alpha, float Beta, float Gamma) { return FMath::Sqrt(X); }
	
	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Tan>(float X, float Alpha, float Beta, float Gamma) { return FMath::Tan(X); }

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Tanh>(float X, float Alpha, float Beta, float Gamma) {
		//https://mathworld.wolfram.com/HyperbolicTangent.html
		return Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Sinh>(X, Alpha, Beta, Gamma) / Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Cosh>(X, Alpha, Beta, Gamma);
	}

	template<> float Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Erf>(float X, float Alpha, float Beta, float Gamma) {
		//https://aapt.scitation.org/doi/abs/10.1119/1.15018?journalCode=ajp
		float a = 167.0f / 148.0f;
		float b = 11.0f / 109.0f;
		float x3 = X * X * X;
		return Apply<NNECore::Internal::EElementWiseUnaryOperatorType::Tanh>(a * X + b * x3, Alpha, Beta, Gamma);
	}
	
} // UE::NNERuntimeRDG::Private::ElementWiseUnaryCPUHelper
