// Copyright Epic Games, Inc. All Rights Reserved.


#include "ExpressionEvaluator.h"

#include <limits>

namespace CurveExpression::Evaluator
{

FEngine::FEngine(
	TMap<FName, float>&& InConstants
	) :
	Constants(MoveTemp(InConstants))
{
}


void FEngine::UpdateConstantValues(
	const TMap<FName, float>& InConstants
	)
{
	for (const TTuple<FName, float>& NewConstantItem: InConstants)
	{
		if (float* OldConstant = Constants.Find(NewConstantItem.Key))
		{
			*OldConstant = NewConstantItem.Value; 
		}
	}
}

// We can't use FChar::IsDigit, since it obeys locale, which would allow all sorts of non-ASCII
// numeric digits from Unicode.
static bool IsASCIIDigit(TCHAR InCh)
{
	return InCh >= '0' && InCh <= '9';
}

TVariant<FEngine::FToken, FParseError> FEngine::ParseTokenError(
	FString&& InError,
	const TCHAR* InCurrentPos,
	FStringView InExpression)
{
	return TVariant<FToken, FParseError>(TInPlaceType<FParseError>(),
		static_cast<int32>(InCurrentPos - InExpression.GetData()), MoveTemp(InError));
}


FString FEngine::TokenToString(
	const FEngine::FToken& InToken
	)
{
	const TCHAR* TypeStr = TEXT("<unknown>");
	FString ValueStr;
	
	if (const EOperatorToken* TokenKind = InToken.TryGet<EOperatorToken>())
	{
		TypeStr = TEXT("Token");
		switch(*TokenKind)
		{
		case EOperatorToken::Negate:		ValueStr = TEXT("<->"); break;
		case EOperatorToken::Add:			ValueStr = TEXT("+"); break;
		case EOperatorToken::Subtract:		ValueStr = TEXT("-"); break;
		case EOperatorToken::Multiply:		ValueStr = TEXT("*"); break;
		case EOperatorToken::Divide:		ValueStr = TEXT("/"); break;
		case EOperatorToken::Modulo:		ValueStr = TEXT("%"); break;
		case EOperatorToken::Power:			ValueStr = TEXT("**"); break;
		case EOperatorToken::FloorDivide:	ValueStr = TEXT("//"); break;
		case EOperatorToken::ParenOpen:		ValueStr = TEXT("("); break;
		case EOperatorToken::ParenClose:	ValueStr = TEXT(")"); break;
		}
	}
	else if (const FName* Identifier = InToken.TryGet<FName>())
	{
		TypeStr = TEXT("Identifier");
		ValueStr = Identifier->ToString();
	}
	else if (const float* Value = InToken.TryGet<float>())
	{
		TypeStr = TEXT("Value");
		ValueStr = FString::Printf(TEXT("%g"), *Value);
	}

	return FString::Printf(TEXT("%s[%s]"), TypeStr, *ValueStr);
}


TVariant<FEngine::FToken, FParseError> FEngine::ParseFloat(
	FStringView& InOutParseRange,
	FStringView InExpression
	)
{
	// We only parse at most 11 significant digits, since 9 is the maximum decimal precision 
	// a float will hold. We store 2 extra digits to make sure we round properly on the 
	// last digit bit.
	constexpr int32 MaxPrecision = 11;

	// Count the number of digits in the mantissa and, optionally, locate the decimal point
	// and the start of significant digits.
	int32 DecimalPointPos = -1;
	int32 MantissaLen = 0;
	int32 SignificantDigitStart = -1;
	for (; MantissaLen < InOutParseRange.Len(); MantissaLen++)
	{
		const TCHAR Ch = InOutParseRange[MantissaLen];
		if (!IsASCIIDigit(Ch))
		{
			// If the current character is neither a digit or period, or if we've already
			// found a period, stop counting.
			if (Ch != '.' || DecimalPointPos >= 0)
			{
				break;
			}
			DecimalPointPos = MantissaLen;
		}
		else if (Ch != '0' && SignificantDigitStart < 0)
		{
			SignificantDigitStart = MantissaLen;
		}
	}

	// Remember the position for the exponent parsing.
	int32 LastPos = MantissaLen;
	int32 SignificantDigitCount = SignificantDigitStart >= 0 ? MantissaLen - SignificantDigitStart : 0;

	// If no decimal point was given, set the position of it after the mantissa. Otherwise
	// remove one from the mantissa length, since that would be the decimal point.
	if (DecimalPointPos < 0)
	{
		DecimalPointPos = MantissaLen;
	}
	else
	{
		MantissaLen--;		

		// If the significant digit starts before the decimal point, remove one to account
		// for the decimal point.
		if (SignificantDigitStart < DecimalPointPos)
		{
			SignificantDigitCount--;
		}
	}
	
	if (MantissaLen == 0)
	{
		// We only parsed the period. That's not a valid float.
		return ParseTokenError(TEXT("Invalid floating point value"), InOutParseRange.GetData(), InExpression);
	}

	// Adjustment for the exponent based on number of significant digits in the fraction part.
	int32 FractionExponent;
	if (SignificantDigitCount > MaxPrecision)
	{
		FractionExponent = DecimalPointPos - MaxPrecision;
		SignificantDigitCount = MaxPrecision;
	}
	else
	{
		FractionExponent = DecimalPointPos - MantissaLen;
	}

	// Copy the integer and fraction digits, up to the maximum count.
	uint64 IntegerValue = 0;
	const TCHAR *Digits = InOutParseRange.GetData() + SignificantDigitStart;
	for (int32 DigitIndex = 0; DigitIndex < SignificantDigitCount; DigitIndex++)
	{
		TCHAR Ch = *Digits++;
		if (Ch == '.')
		{
			Ch = *Digits++;
		}
		IntegerValue = 10 * IntegerValue + (Ch - '0');
	}
	double Fraction = static_cast<double>(IntegerValue);

	// Now that we have the fraction part, parse the exponent, if any.
	int32 Exponent = 0;
	bool IsExponentNegative = false;

	// We need at least _two_ characters here ('e' + digit)
	if ((LastPos + 1) < InOutParseRange.Len() &&
		(InOutParseRange[LastPos] == 'e' || InOutParseRange[LastPos] == 'E'))
	{
		int32 ExponentPos = LastPos + 1;
		if (InOutParseRange[ExponentPos] == '+')
		{
			ExponentPos++;
		}
		else if (InOutParseRange[ExponentPos] == '-')
		{
			IsExponentNegative = true;
			ExponentPos++;
		}

		int32 DigitCount = 0;
		while(ExponentPos < InOutParseRange.Len() && IsASCIIDigit(InOutParseRange[ExponentPos]))
		{
			// We don't care past 2 digits. 38 is the maximum exponent we care about for floats.
			if (DigitCount < 2)
			{
				Exponent = Exponent * 10 + (InOutParseRange[ExponentPos] - '0');
			}
			ExponentPos++;
			DigitCount++;
		}

		// Only if we parsed some digits do we consider the exponent parsed at all. Otherwise
		// we leave the last parse at where the 'e' started.
		if (DigitCount > 0)
		{
			LastPos = ExponentPos;
		}
	}

	// Adjust the exponent to deal with the decimal point location.
	if (IsExponentNegative)
	{
		Exponent = FractionExponent - Exponent;
	}
	else
	{
		Exponent = FractionExponent + Exponent;
	}

	// The maximum exponent for floats is 38. 
	if (Exponent < 0) 
	{
		IsExponentNegative = true;
		Exponent = -Exponent;
	} 
	else 
	{
		IsExponentNegative = false;
	}

	Exponent = FMath::Min(Exponent, std::numeric_limits<float>::max_exponent10);
	double DoubleExponent = 1.0;

	for(const double PowerOf2Exp: {1.0e1, 1.0e2, 1.0e4, 1.0e8, 1.0e16, 1.0e32})
	{
		if (Exponent == 0)
		{
			break;
		}
		if (Exponent & 1)
		{
			DoubleExponent *= PowerOf2Exp;
		}
		Exponent >>= 1;
	}

	if (IsExponentNegative)
	{
		Fraction /= DoubleExponent;
	}
	else
	{
		Fraction *= DoubleExponent;
	}

	InOutParseRange = InOutParseRange.Mid(LastPos);
	
	return TVariant<FToken, FParseError>(
		TInPlaceType<FToken>(), FToken(TInPlaceType<float>(), Fraction));
}


TVariant<FEngine::FToken, FParseError> FEngine::ParseIdentifier(
	FStringView& InOutParseRange,
	FStringView InExpression
	) const
{
	// FIXME: We need quote escaping.
	bool bStopAtSingleQuote = false;
	int32 ParseIndex = 0;
	if (InOutParseRange[0] == '\'')
	{
		bStopAtSingleQuote = true;
		ParseIndex++;
		if (ParseIndex >= InOutParseRange.Len())
		{
			return ParseTokenError(TEXT("Missing end quote for constant"), InOutParseRange.GetData(), InExpression);
		}
	}
	// Unquoted constants have to start with an alphabetic character.
	else if (!FChar::IsAlpha(InOutParseRange[0]) && InOutParseRange[0] != '_')
	{
		return ParseTokenError(TEXT("Unexpected character"), InOutParseRange.GetData(), InExpression);
	}

	const int32 ConstantStart = ParseIndex;
	int32 ConstantEnd;

	ParseIndex++;

	if (bStopAtSingleQuote)
	{
		while(ParseIndex < InOutParseRange.Len() && InOutParseRange[ParseIndex] != '\'')
		{
			ParseIndex++;
		}

		if (ParseIndex == InOutParseRange.Len())
		{
			return ParseTokenError(TEXT("Missing end quote for constant"), InOutParseRange.GetData(), InExpression);
		}

		ConstantEnd = ParseIndex;
		ParseIndex++;
	}
	else
	{
		while(ParseIndex < InOutParseRange.Len() &&
			  (FChar::IsAlnum(InOutParseRange[ParseIndex]) || InOutParseRange[ParseIndex] == '_'))
		{
			ParseIndex++;
		}
		
		ConstantEnd = ParseIndex;
	}

	// Look up this identifier. We don't want to create a new FName, to avoid polluting the
	// FNameEntry cache.
	const FStringView Identifier(InOutParseRange.SubStr(ConstantStart, ConstantEnd - ConstantStart));
	const FName Name(Identifier.Len(), Identifier.GetData(), FNAME_Find);
	
	InOutParseRange = InOutParseRange.Mid(ParseIndex); 

	if (Constants.Contains(Name))
	{
		return TVariant<FToken, FParseError>(
			TInPlaceType<FToken>(), FToken(TInPlaceType<FName>(), Name));
	}
	else
	{
		// If the constant value is not found, it gets a default value of 0.
		// TBD: We may want to flag this as an error instead when doing verification.
		return TVariant<FToken, FParseError>(
			TInPlaceType<FToken>(), FToken(TInPlaceType<float>(), 0.0f));
	}
}


TVariant<FEngine::FToken, FParseError> FEngine::ParseToken(
	FStringView& InOutParseRange,
	FStringView InExpression
	) const
{
	auto TokenResult = [](EOperatorToken InTokenKind)
	{
		return TVariant<FToken, FParseError>(
			TInPlaceType<FToken>(), FToken(TInPlaceType<EOperatorToken>(), InTokenKind));
	};

	switch(InOutParseRange[0])
	{
	case '+':
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::Add);
		
	case '-':
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::Subtract);
		
	case '*':
		if (InOutParseRange.Len() >= 2 && InOutParseRange[1] == '*')
		{
			InOutParseRange = InOutParseRange.Mid(2);
			return TokenResult(EOperatorToken::Power);
		}
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::Multiply);
		
	case '/':
		if (InOutParseRange.Len() >= 2 && InOutParseRange[1] == '/')
		{
			InOutParseRange = InOutParseRange.Mid(2);
			return TokenResult(EOperatorToken::FloorDivide);
		}
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::Divide);
		
	case '%':
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::Modulo);

	case '(':
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::ParenOpen);
		
	case ')':
		InOutParseRange = InOutParseRange.Mid(1);
		return TokenResult(EOperatorToken::ParenClose);
		
	default:
		if (IsASCIIDigit(InOutParseRange[0]) || InOutParseRange[0] == '.')
		{
			return ParseFloat(InOutParseRange, InExpression);
		}
		return ParseIdentifier(InOutParseRange, InExpression);
	}
}


TVariant<FExpressionObject, FParseError> FEngine::Parse(
	FStringView InExpression
	) const
{
	enum class EAssociativity
	{
		None,
		Left,
		Right
	};
	struct FOperatorTokenInfo
	{
		EOperatorToken OperatorToken;
		int32 Precedence;
		EAssociativity Associativity;
		TOptional<FExpressionObject::EOperator> Operator;
	};
	// Keep in same order as EOperatorToken. One day, if we're lucky, we'll get reflections.
	auto GetOperatorTokenInfo = [](EOperatorToken InOpToken)
	{
		static const FOperatorTokenInfo OperatorTokenInfo[] = {
			{ EOperatorToken::Negate,		5, EAssociativity::Right, FExpressionObject::EOperator::Negate},
			{ EOperatorToken::Add,			1, EAssociativity::Left, FExpressionObject::EOperator::Add},
			{ EOperatorToken::Subtract,		1, EAssociativity::Left, FExpressionObject::EOperator::Subtract},
			{ EOperatorToken::Multiply,		2, EAssociativity::Left, FExpressionObject::EOperator::Multiply},
			{ EOperatorToken::Divide,		2, EAssociativity::Left, FExpressionObject::EOperator::Divide},
			{ EOperatorToken::Modulo,		3, EAssociativity::Left, FExpressionObject::EOperator::Modulo},
			{ EOperatorToken::Power,		4, EAssociativity::Right, FExpressionObject::EOperator::Power},
			{ EOperatorToken::FloorDivide,	3, EAssociativity::Left, FExpressionObject::EOperator::FloorDivide},
			{ EOperatorToken::ParenOpen,	0, EAssociativity::None},
			{ EOperatorToken::ParenClose,	0, EAssociativity::None},
		};
		checkSlow(OperatorTokenInfo[static_cast<int32>(InOpToken)].OperatorToken == InOpToken);
		return OperatorTokenInfo[static_cast<int32>(InOpToken)];
	};
	
	static TVariant<FExpressionObject, FParseError> EmptyResult{TInPlaceType<FExpressionObject>()};

	TStringView ParseRange = InExpression.TrimStartAndEnd();
	if (ParseRange.IsEmpty())
	{
		return EmptyResult;
	}

	// Run Dijkstra's classic Shunting Yard algorithm to convert infix expressions to RPN.
	// TODO: Add support for calling functions.
	// TODO: Better error pinpointing by storing the token parse result with the token.
	TArray<FExpressionObject::OpElement, TInlineAllocator<64>> Expression;
	TArray<EOperatorToken, TInlineAllocator<32>> OperatorStack;

	auto ParseError = [InExpression](FString&& InError, const TCHAR* InCurrentPos)
	{
		return TVariant<FExpressionObject, FParseError>(TInPlaceType<FParseError>(),
			static_cast<int32>(InCurrentPos - InExpression.GetData()), MoveTemp(InError));
	};
	auto PushOperatorTokenToExpression = [GetOperatorTokenInfo, &Expression](EOperatorToken InOpToken)
	{
		Expression.Push(FExpressionObject::OpElement(TInPlaceType<FExpressionObject::EOperator>(), 
			*GetOperatorTokenInfo(InOpToken).Operator));
	};
	// Used to check if we encounter a '-' to see if we need to turn it into a negate operator
	// and also to see if we have two operators in a row (error).
	TOptional<FToken> LastToken;

	do
	{
		const TCHAR* TokenStart = ParseRange.GetData();
		TVariant<FToken, FParseError> ParseResult = ParseToken(ParseRange, InExpression);
		
		if (FParseError* Error = ParseResult.TryGet<FParseError>())
		{
			return TVariant<FExpressionObject, FParseError>(TInPlaceType<FParseError>(), *Error);
		}

		const FStringView TokenParseRange(TokenStart, ParseRange.GetData() - TokenStart);
		const FToken& Token = ParseResult.Get<FToken>();

		// Values / identifiers go onto the expression stack immediately.
		if (const FName* Identifier = Token.TryGet<FName>())
		{
			if (LastToken.IsSet() &&
				(LastToken->IsType<FName>() || LastToken->IsType<float>()))
			{
				return ParseError(TEXT("Expected an operator"), TokenParseRange.GetData());
			}
			Expression.Push(FExpressionObject::OpElement(TInPlaceType<FName>(), *Identifier));
		}
		else if (const float* Value = Token.TryGet<float>())
		{
			if (LastToken.IsSet() &&
				(LastToken->IsType<FName>() || LastToken->IsType<float>()))
			{
				return ParseError(TEXT("Expected an operator"), TokenParseRange.GetData());
			}
			Expression.Push(FExpressionObject::OpElement(TInPlaceType<float>(), *Value));
		}
		else
		{
			EOperatorToken Op = Token.Get<EOperatorToken>();
			const FOperatorTokenInfo& TokenInfo = GetOperatorTokenInfo(Op);

			// Was the last token also an operator token?
			if (LastToken.IsSet())
			{
				if (const EOperatorToken* LastOp = LastToken->TryGet<EOperatorToken>())
				{
					const FOperatorTokenInfo& LastTokenInfo = GetOperatorTokenInfo(*LastOp);
					if (TokenInfo.Operator.IsSet() && LastTokenInfo.Operator.IsSet())
					{
						return ParseError(TEXT("Expected an expression"), TokenParseRange.GetData());
					}
					if (*LastOp == EOperatorToken::ParenClose && Op == EOperatorToken::ParenOpen)
					{
						return ParseError(TEXT("Expected an operator"), TokenParseRange.GetData());
					}
					if (*LastOp == EOperatorToken::ParenOpen && Op == EOperatorToken::ParenClose)
					{
						return ParseError(TEXT("Empty Parentheses"), TokenParseRange.GetData());
					}
				}
			}

			// Special-case for unary prefix.
			if (Op == EOperatorToken::Subtract &&
				(!LastToken.IsSet() || (LastToken->IsType<EOperatorToken>() && LastToken->Get<EOperatorToken>() != EOperatorToken::ParenClose)))
			{
				Op = EOperatorToken::Negate;
				OperatorStack.Push(Op);
			}
			else if (Op == EOperatorToken::ParenOpen)
			{
				OperatorStack.Push(Op);
			}
			else if (Op == EOperatorToken::ParenClose)
			{
				while(OperatorStack.IsEmpty() || OperatorStack.Last() != EOperatorToken::ParenOpen)
				{
					if (OperatorStack.IsEmpty())
					{
						return ParseError(TEXT("Mismatched parentheses"), TokenParseRange.GetData()); 
					}
					PushOperatorTokenToExpression(OperatorStack.Pop(false));
				}
				// Remove the now unneeded open parentheses.
				OperatorStack.Pop(false);
			}
			else if (GetOperatorTokenInfo(Op).Associativity == EAssociativity::Left)
			{
				// Peel off all operators that have the same precedence or higher.
				while (!OperatorStack.IsEmpty() && GetOperatorTokenInfo(OperatorStack.Last()).Precedence >= TokenInfo.Precedence)
				{
					PushOperatorTokenToExpression(OperatorStack.Pop(false));
				}
				OperatorStack.Push(Op);
			}
			else if (GetOperatorTokenInfo(Op).Associativity == EAssociativity::Right)
			{
				// Peel off all operators that have higher precedence.
				while (!OperatorStack.IsEmpty() && GetOperatorTokenInfo(OperatorStack.Last()).Precedence > TokenInfo.Precedence)
				{
					PushOperatorTokenToExpression(OperatorStack.Pop(false));
				}
				OperatorStack.Push(Op);
			}
		}

		LastToken = Token;
		ParseRange = ParseRange.TrimStart();
	} while(!ParseRange.IsEmpty());

	while (!OperatorStack.IsEmpty())
	{
		if (!GetOperatorTokenInfo(OperatorStack.Last()).Operator.IsSet())
		{
			return ParseError(TEXT("Mismatched parentheses"), InExpression.GetData()); 
		}
		PushOperatorTokenToExpression(OperatorStack.Pop(false));
	}

	FExpressionObject S;
	S.Expression = Expression;
	return TVariant<FExpressionObject, FParseError>(TInPlaceType<FExpressionObject>(), S);
}


float FEngine::Execute(
	const FExpressionObject& InExpressionObject
	) const
{
	// An empty expression object is valid.
	if (InExpressionObject.Expression.IsEmpty())
	{
		return 0.0f;
	}

	// We assume the codons in the expression object are correct and will always result
	// in a single value remaining on the stack.
	TArray<float, TInlineAllocator<32>> ValueStack;
	
	for (const FExpressionObject::OpElement& Token: InExpressionObject.Expression)
	{
		if (const FExpressionObject::EOperator* Operator = Token.TryGet<FExpressionObject::EOperator>())
		{
			switch(*Operator)
			{
			case FExpressionObject::EOperator::Negate:
				{
					ValueStack.Last() = -ValueStack.Last();
				}
				break;	
				
			case FExpressionObject::EOperator::Add:
				{
					const float V = ValueStack.Pop(false);
					ValueStack.Last() += V;
				}
				break;	
			case FExpressionObject::EOperator::Subtract:
				{
					const float V = ValueStack.Pop(false);
					ValueStack.Last() -= V;
				}
				break;	
			case FExpressionObject::EOperator::Multiply:
				{
					const float V = ValueStack.Pop(false);
					ValueStack.Last() *= V;
				}
				break;
			case FExpressionObject::EOperator::Divide:
				{
					const float V = ValueStack.Pop(false);
					if (FMath::IsNearlyZero(V))
					{
						ValueStack.Last() = 0.0f;
					}
					else
					{
						ValueStack.Last() /= V;
					}
				}
				break;	
			case FExpressionObject::EOperator::Modulo:
				{
					const float V = ValueStack.Pop(false);
					if (FMath::IsNearlyZero(V))
					{
						ValueStack.Last() = 0.0f;
					}
					else
					{
						ValueStack.Last() = FMath::Fmod(ValueStack.Last(), V);
					}
				}
				break;	
			case FExpressionObject::EOperator::Power:
				{
					const float V = ValueStack.Pop(false);
					ValueStack.Last() = FMath::Pow(ValueStack.Last(), V);
					if (!FMath::IsFinite(ValueStack.Last()))
					{
						ValueStack.Last() = 0.0f;
					}
				}
				break;	
			case FExpressionObject::EOperator::FloorDivide:
				{
					const float V = ValueStack.Pop(false);
					if (FMath::IsNearlyZero(V))
					{
						ValueStack.Last() = 0.0f;
					}
					else
					{
						ValueStack.Last() /= V;
					}
					ValueStack.Last() = FMath::Floor(ValueStack.Last());
				}
				break;  
			}
		}
		else if (const FName* Constant = Token.TryGet<FName>())
		{
			const float *Value = Constants.Find(*Constant);
			ValueStack.Push(Value ? *Value : 0.0f);
		}
		else if (const float* Value = Token.TryGet<float>())
		{
			ValueStack.Push(*Value);
		}
	}

	return ValueStack.Last();
}


TOptional<float> FEngine::Evaluate(
	FStringView InExpression
	) const
{
	TVariant<FExpressionObject, FParseError> Result = Parse(InExpression);
	if (const FExpressionObject* ExpressionObject = Result.TryGet<FExpressionObject>())
	{
		return Execute(*ExpressionObject);
	}

	return {};
}


TOptional<FParseError> FEngine::Verify(
	FStringView InExpression
	) const
{
	TVariant<FExpressionObject, FParseError> Result = Parse(InExpression);
	if (const FParseError* Error = Result.TryGet<FParseError>())
	{
		return *Error;
	}
	return {};
}

}
