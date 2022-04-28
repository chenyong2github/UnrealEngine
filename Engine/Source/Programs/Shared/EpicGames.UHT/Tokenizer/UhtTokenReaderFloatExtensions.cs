// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Tokenizer
{

	/// <summary>
	/// Collection of token reader extensions for float values
	/// </summary>
	public static class UhtTokenReaderFloatExtensions
	{
		/// <summary>
		/// Get the next token as a float.  If the next token is not a float, no token is consumed.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Value">The float value of the token</param>
		/// <returns>True if the next token was an float, false if not.</returns>
		public static bool TryOptionalConstFloat(this IUhtTokenReader TokenReader, out float Value)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsConstFloat() && Token.GetConstFloat(out Value)) // NOTE: This is restricted to only float values
			{
				TokenReader.ConsumeToken();
				return true;
			}
			Value = 0;
			return false;
		}

		/// <summary>
		/// Get the next token as a float.  If the next token is not a float, no token is consumed.
		/// </summary>
		/// <param name="TokenReader"></param>
		/// <returns>The token reader</returns>
		public static IUhtTokenReader OptionalConstFloat(this IUhtTokenReader TokenReader)
		{
			TokenReader.TryOptionalConstFloat(out float _);
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as a float.  If the next token is not a float, no token is consumed.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Delegate">Delegate to invoke with the float value</param>
		/// <returns>The token reader</returns>
		public static IUhtTokenReader OptionalConstFloat(this IUhtTokenReader TokenReader, UhtTokenConstFloatDelegate Delegate)
		{
			if (TokenReader.TryOptionalConstFloat(out float Value))
			{
				Delegate(Value);
			}
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as a float.  If the next token is not a float, an exception is thrown
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>True if the next token was an float, false if not.</returns>
		public static IUhtTokenReader RequireConstFloat(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			if (!TokenReader.TryOptionalConstFloat(out float _))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant float", ExceptionContext);
			}
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as a float.  If the next token is not a float, an exception is thrown
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The floating point value of the token</returns>
		public static float GetConstFloat(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			float Value;
			if (!TokenReader.TryOptionalConstFloat(out Value))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant float", ExceptionContext);
			}
			return Value;
		}

		/// <summary>
		/// Get the next float.  It also handles [+/-] token followed by an float.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Value">The float value of the token</param>
		/// <returns>True if the next token was an float, false if not.</returns>
		public static bool TryOptionalLeadingSignConstFloat(this IUhtTokenReader TokenReader, out float Value)
		{
			float LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return (Token.IsConstInt() || Token.IsConstFloat()) && Token.GetConstFloat(out LocalValue);
			});
			Value = LocalValue;
			return Results;
		}

		/// <summary>
		/// Get the next float.  It also handles [+/-] token followed by an float.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Value">The float value of the token</param>
		/// <returns>True if the next token was an float, false if not.</returns>
		public static bool TryOptionalConstFloatExpression(this IUhtTokenReader TokenReader, out float Value)
		{
			float LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return (Token.IsConstInt() || Token.IsConstFloat()) && Token.GetConstFloat(out LocalValue);
			});
			Value = LocalValue;
			return Results;
		}

		/// <summary>
		/// Get the next float.  It also handles [+/-] token followed by an float.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <returns>The double value</returns>
		public static float GetConstFloatExpression(this IUhtTokenReader TokenReader)
		{
			if (!TokenReader.TryOptionalConstFloatExpression(out float LocalValue))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant float", null);
			}
			return LocalValue;
		}

		/// <summary>
		/// Get the next double.  It also handles [+/-] token followed by an double.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Value">The double value of the token</param>
		/// <returns>True if the next token was an double, false if not.</returns>
		public static bool TryOptionalConstDoubleExpression(this IUhtTokenReader TokenReader, out double Value)
		{
			double LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return (Token.IsConstInt() || Token.IsConstFloat()) && Token.GetConstDouble(out LocalValue);
			});
			Value = LocalValue;
			return Results;
		}

		/// <summary>
		/// Get the next double.  It also handles [+/-] token followed by an double.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Delegate">Delegate to invoke if the double is parsed</param>
		/// <returns>The supplied token reader</returns>
		public static IUhtTokenReader RequireConstDoubleExpression(this IUhtTokenReader TokenReader, UhtTokenConstDoubleDelegate Delegate)
		{
			if (!TokenReader.TryOptionalConstDoubleExpression(out double LocalValue))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant double", null);
			}
			Delegate(LocalValue);
			return TokenReader;
		}

		/// <summary>
		/// Get the next double.  It also handles [+/-] token followed by an double.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <returns>The double value</returns>
		public static double GetConstDoubleExpression(this IUhtTokenReader TokenReader)
		{
			if (!TokenReader.TryOptionalConstDoubleExpression(out double LocalValue))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant double", null);
			}
			return LocalValue;
		}
	}
}
