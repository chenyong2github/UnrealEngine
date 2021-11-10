// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Exception class thrown due to type and syntax errors in condition expressions
	/// </summary>
	class BgConditionException : Exception
	{
		/// <summary>
		/// Constructor; formats the exception message with the given String.Format() style parameters.
		/// </summary>
		/// <param name="Format">Formatting string, in String.Format syntax</param>
		/// <param name="Args">Optional arguments for the string</param>
		public BgConditionException(string Format, params object[] Args) : base(String.Format(Format, Args))
		{
		}
	}

	/// <summary>
	/// Class to evaluate condition expressions in build scripts, following this grammar:
	/// 
	///		or-expression   ::= and-expression
	///		                  | or-expression "Or" and-expression;
	///		    
	///		and-expression  ::= comparison
	///		                  | and-expression "And" comparison;
	///		                  
	///		comparison      ::= scalar
	///		                  | scalar "==" scalar
	///		                  | scalar "!=" scalar
	///		                  | scalar "&lt;" scalar
	///		                  | scalar "&lt;=" scalar;
	///		                  | scalar "&gt;" scalar
	///		                  | scalar "&gt;=" scalar;
	///		                  
	///     scalar          ::= "(" or-expression ")"
	///                       | "!" scalar
	///                       | "Exists" "(" scalar ")"
	///                       | "HasTrailingSlash" "(" scalar ")"
	///                       | string
	///                       | identifier;
	///                       
	///     string          ::= any sequence of characters terminated by single quotes (') or double quotes ("). Not escaped.
	///     identifier      ::= any sequence of letters, digits, or underscore characters.
	///     
	/// The type of each subexpression is always a scalar, which are converted to expression-specific types (eg. booleans, integers) as required.
	/// Scalar values are case-insensitive strings. The identifier 'true' and the strings "true" and "True" are all identical scalars.
	/// </summary>
	public class BgCondition
	{
		/// <summary>
		/// Sentinel added to the end of a sequence of tokens.
		/// </summary>
		const string EndToken = "<EOF>";

		/// <summary>
		/// Tokens for the condition
		/// </summary>
		List<string> Tokens = new List<string>();

		/// <summary>
		/// The current token index
		/// </summary>
		int Idx;

		/// <summary>
		/// Context for evaluating the expression
		/// </summary>
		IBgScriptReaderContext Context;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">The condition text</param>
		/// <param name="Context">Context for evaluating the expression</param>
		private BgCondition(string Text, IBgScriptReaderContext Context)
		{
			this.Context = Context;

			Tokenize(Text, Tokens);
		}

		/// <summary>
		/// Evaluates the given string as a condition. Throws a ConditionException on a type or syntax error.
		/// </summary>
		/// <param name="Text"></param>
		/// <param name="Context"></param>
		/// <returns>The result of evaluating the condition</returns>
		public static ValueTask<bool> EvaluateAsync(string Text, IBgScriptReaderContext Context)
		{
			return new BgCondition(Text, Context).EvaluateAsync();
		}

		/// <summary>
		/// Evaluates the given string as a condition. Throws a ConditionException on a type or syntax error.
		/// </summary>
		/// <returns>The result of evaluating the condition</returns>
		async ValueTask<bool> EvaluateAsync()
		{
			bool bResult = true;
			if(Tokens.Count > 1)
			{
				Idx = 0;
				string Result = await EvaluateOrAsync();
				if(Tokens[Idx] != EndToken)
				{
					throw new BgConditionException("Garbage after expression: {0}", String.Join("", Tokens.Skip(Idx)));
				}
				bResult = CoerceToBool(Result);
			}
			return bResult;
		}

		/// <summary>
		/// Evaluates an "or-expression" production.
		/// </summary>
		/// <returns>A scalar representing the result of evaluating the expression.</returns>
		async ValueTask<string> EvaluateOrAsync()
		{
			// <Condition> Or <Condition> Or...
			string Result = await EvaluateAndAsync();
			while(String.Compare(Tokens[Idx], "Or", true) == 0)
			{
				// Evaluate this condition. We use a binary OR here, because we want to parse everything rather than short-circuit it.
				Idx++;
				string Lhs = Result;
				string Rhs = await EvaluateAndAsync();
				Result = (CoerceToBool(Lhs) | CoerceToBool(Rhs))? "true" : "false";
			}
			return Result;
		}

		/// <summary>
		/// Evaluates an "and-expression" production.
		/// </summary>
		/// <returns>A scalar representing the result of evaluating the expression.</returns>
		async ValueTask<string> EvaluateAndAsync()
		{
			// <Condition> And <Condition> And...
			string Result = await EvaluateComparisonAsync();
			while(String.Compare(Tokens[Idx], "And", true) == 0)
			{
				// Evaluate this condition. We use a binary AND here, because we want to parse everything rather than short-circuit it.
				Idx++;
				string Lhs = Result;
				string Rhs = await EvaluateComparisonAsync();
				Result = (CoerceToBool(Lhs) & CoerceToBool(Rhs))? "true" : "false";
			}
			return Result;
		}

		/// <summary>
		/// Evaluates a "comparison" production.
		/// </summary>
		/// <returns>The result of evaluating the expression</returns>
		async ValueTask<string> EvaluateComparisonAsync()
		{
			// scalar
			// scalar == scalar
			// scalar != scalar
			// scalar < scalar
			// scalar <= scalar
			// scalar > scalar
			// scalar >= scalar

			string Result = await EvaluateScalarAsync();
			if(Tokens[Idx] == "==")
			{
				// Compare two scalars for equality
				Idx++;
				string Lhs = Result;
				string Rhs = await EvaluateScalarAsync();
				Result = (String.Compare(Lhs, Rhs, true) == 0)? "true" : "false";
			}
			else if(Tokens[Idx] == "!=")
			{
				// Compare two scalars for inequality
				Idx++;
				string Lhs = Result;
				string Rhs = await EvaluateScalarAsync();
				Result = (String.Compare(Lhs, Rhs, true) != 0)? "true" : "false";
			}
			else if(Tokens[Idx] == "<")
			{
				// Compares whether the first integer is less than the second
				Idx++;
				int Lhs = CoerceToInteger(Result);
				int Rhs = CoerceToInteger(await EvaluateScalarAsync());
				Result = (Lhs < Rhs)? "true" : "false";
			}
			else if(Tokens[Idx] == "<=")
			{
				// Compares whether the first integer is less than the second
				Idx++;
				int Lhs = CoerceToInteger(Result);
				int Rhs = CoerceToInteger(await EvaluateScalarAsync());
				Result = (Lhs <= Rhs)? "true" : "false";
			}
			else if(Tokens[Idx] == ">")
			{
				// Compares whether the first integer is less than the second
				Idx++;
				int Lhs = CoerceToInteger(Result);
				int Rhs = CoerceToInteger(await EvaluateScalarAsync());
				Result = (Lhs > Rhs)? "true" : "false";
			}
			else if(Tokens[Idx] == ">=")
			{
				// Compares whether the first integer is less than the second
				Idx++;
				int Lhs = CoerceToInteger(Result);
				int Rhs = CoerceToInteger(await EvaluateScalarAsync());
				Result = (Lhs >= Rhs)? "true" : "false";
			}
			return Result;
		}

		/// <summary>
		/// Evaluates arguments from a token string. Arguments are all comma-separated tokens until a closing ) is encountered
		/// </summary>
		/// <returns>The result of evaluating the expression</returns>
		IEnumerable<string> EvaluateArguments()
		{
			List<string> Arguments = new List<string>();

			// skip opening bracket
			if (Tokens[Idx++] != "(")
			{
				throw new BgConditionException("Expected '('");
			}

			bool DidCloseBracket = false;

			while (Idx < Tokens.Count)
			{
				string NextToken = Tokens.ElementAt(Idx++);

				if (NextToken == EndToken)
				{
					// ran out of items
					break;
				}
				else if (NextToken == ")")
				{
					DidCloseBracket = true;
					break;
				}
				else if (NextToken != ",")
				{
					if (NextToken.First() == '\'' && NextToken.Last() == '\'')
					{
						NextToken = NextToken.Substring(1, NextToken.Length - 2);
					}
					Arguments.Add(NextToken);
				}
			}

			if (!DidCloseBracket)
			{
				throw new BgConditionException("Expected ')'");
			}

			return Arguments;
		}

		/// <summary>
		/// Evaluates a "scalar" production.
		/// </summary>
		/// <returns>The result of evaluating the expression</returns>
		async ValueTask<string> EvaluateScalarAsync()
		{
			string Result;
			if(Tokens[Idx] == "(")
			{
				// Subexpression
				Idx++;
				Result = await EvaluateOrAsync();
				if(Tokens[Idx] != ")")
				{
					throw new BgConditionException("Expected ')'");
				}
				Idx++;
			}
			else if(Tokens[Idx] == "!")
			{
				// Logical not
				Idx++;
				string Rhs = await EvaluateScalarAsync();
				Result = CoerceToBool(Rhs)? "false" : "true";
			}
			else if(String.Compare(Tokens[Idx], "Exists", true) == 0 && Tokens[Idx + 1] == "(")
			{
				// Check whether file or directory exists. Evaluate the argument as a subexpression.
				Idx++;
				string Argument = await EvaluateScalarAsync();
				Result = await Context.ExistsAsync(Argument)? "true" : "false";
			}
			else if(String.Compare(Tokens[Idx], "HasTrailingSlash", true) == 0 && Tokens[Idx + 1] == "(")
			{
				// Check whether the given string ends with a slash
				Idx++;
				string Argument = await EvaluateScalarAsync();
				Result = (Argument.Length > 0 && (Argument[Argument.Length - 1] == Path.DirectorySeparatorChar || Argument[Argument.Length - 1] == Path.AltDirectorySeparatorChar))? "true" : "false";
			}
			else if (String.Compare(Tokens[Idx], "Contains", true) == 0 && Tokens[Idx + 1] == "(")
			{
				// Check a string contains a substring. If a separator is supplied the string is first split
				Idx++;
				IEnumerable<string> Arguments = EvaluateArguments();

				if (Arguments.Count() != 2)
				{
					throw new BgConditionException("Invalid argument count for 'Contains'. Expected (Haystack,Needle)");
				}

				Result = Contains(Arguments.ElementAt(0), Arguments.ElementAt(1)) ? "true" : "false";
			}
			else if (String.Compare(Tokens[Idx], "ContainsItem", true) == 0 && Tokens[Idx + 1] == "(")
			{
				// Check a string contains a substring. If a separator is supplied the string is first split
				Idx++;
				IEnumerable<string> Arguments = EvaluateArguments();

				if (Arguments.Count() != 3)
				{
					throw new BgConditionException("Invalid argument count for 'ContainsItem'. Expected (Haystack,Needle,HaystackSeparator)");
				}

				Result = ContainsItem(Arguments.ElementAt(0), Arguments.ElementAt(1), Arguments.ElementAt(2)) ? "true" : "false";
			}
			else
			{
				// Raw scalar. Remove quotes from strings, and allow literals and simple identifiers to pass through directly.
				string Token = Tokens[Idx];
				if(Token.Length >= 2 && (Token[0] == '\'' || Token[0] == '\"') && Token[Token.Length - 1] == Token[0])
				{
					Result = Token.Substring(1, Token.Length - 2);
					Idx++;
				}
				else if(Char.IsLetterOrDigit(Token[0]) || Token[0] == '_')
				{
					Result = Token;
					Idx++;
				}
				else
				{
					throw new BgConditionException("Token '{0}' is not a valid scalar", Token);
				}
			}
			return Result;
		}

		/// <summary>
		/// Checks whether Haystack contains "Needle". 
		/// </summary>
		/// <param name="Haystack">The string to search</param>
		/// <param name="Needle">The string to search for</param>
		/// <returns>True if the path exists, false otherwise.</returns>
		static bool Contains(string Haystack, string Needle)
		{
			try
			{
				return Haystack.IndexOf(Needle, StringComparison.CurrentCultureIgnoreCase) >= 0;
			}
			catch
			{
				return false;
			}
		}

		/// <summary>
		/// Checks whether HaystackItems contains "Needle". 
		/// </summary>
		/// <param name="Haystack">The separated list of items to check</param>
		/// <param name="Needle">The item to check for</param>
		/// <param name="HaystackSeparator">The separator used in Haystack</param>
		/// <returns>True if the path exists, false otherwise.</returns>
		static bool ContainsItem(string Haystack, string Needle, string HaystackSeparator)
		{
			try
			{
				IEnumerable<string> HaystackItems = Haystack.Split(new string[] { HaystackSeparator }, StringSplitOptions.RemoveEmptyEntries);
				return HaystackItems.Any(I => I.ToLower() == Needle.ToLower());
			}
			catch
			{
				return false;
			}
		}

		/// <summary>
		/// Converts a scalar to a boolean value.
		/// </summary>
		/// <param name="Scalar">The scalar to convert</param>
		/// <returns>The scalar converted to a boolean value.</returns>
		static bool CoerceToBool(string Scalar)
		{
			bool Result;
			if(String.Compare(Scalar, "true", true) == 0)
			{
				Result = true;
			}
			else if(String.Compare(Scalar, "false", true) == 0)
			{
				Result = false;
			}
			else
			{
				throw new BgConditionException("Token '{0}' cannot be coerced to a bool", Scalar);
			}
			return Result;
		}

		/// <summary>
		/// Converts a scalar to a boolean value.
		/// </summary>
		/// <param name="Scalar">The scalar to convert</param>
		/// <returns>The scalar converted to an integer value.</returns>
		static int CoerceToInteger(string Scalar)
		{
			int Value;
			if(!Int32.TryParse(Scalar, out Value))
			{
				throw new BgConditionException("Token '{0}' cannot be coerced to an integer", Scalar);
			}
			return Value;
		}

		/// <summary>
		/// Splits an input string up into expression tokens.
		/// </summary>
		/// <param name="Text">Text to be converted into tokens</param>
		/// <param name="Tokens">List to receive a list of tokens</param>
		static void Tokenize(string Text, List<string> Tokens)
		{
			int Idx = 0;
			while(Idx < Text.Length)
			{
				int EndIdx = Idx + 1;
				if(!Char.IsWhiteSpace(Text[Idx]))
				{
					// Scan to the end of the current token
					if(Char.IsNumber(Text[Idx]))
					{
						// Number
						while(EndIdx < Text.Length && Char.IsNumber(Text[EndIdx]))
						{
							EndIdx++;
						}
					}
					else if(Char.IsLetter(Text[Idx]) || Text[Idx] == '_')
					{
						// Identifier
						while(EndIdx < Text.Length && (Char.IsLetterOrDigit(Text[EndIdx]) || Text[EndIdx] == '_'))
						{
							EndIdx++;
						}
					}
					else if(Text[Idx] == '!' || Text[Idx] == '<' || Text[Idx] == '>' || Text[Idx] == '=')
					{
						// Operator that can be followed by an equals character
						if(EndIdx < Text.Length && Text[EndIdx] == '=')
						{
							EndIdx++;
						}
					}
					else if(Text[Idx] == '\'' || Text[Idx] == '\"')
					{
						// String
						if(EndIdx < Text.Length)
						{
							EndIdx++;
							while(EndIdx < Text.Length && Text[EndIdx - 1] != Text[Idx]) EndIdx++;
						}
					}
					Tokens.Add(Text.Substring(Idx, EndIdx - Idx));
				}
				Idx = EndIdx;
			}
			Tokens.Add(EndToken);
		}

		/// <summary>
		/// Test cases for conditions.
		/// </summary>
		public static async Task TestConditions()
		{
			await TestConditionAsync("1 == 2", false);
			await TestConditionAsync("1 == 1", true);
			await TestConditionAsync("1 != 2", true);
			await TestConditionAsync("1 != 1", false);
			await TestConditionAsync("'hello' == 'hello'", true);
			await TestConditionAsync("'hello' == ('hello')", true);
			await TestConditionAsync("'hello' == 'world'", false);
			await TestConditionAsync("'hello' != ('world')", true);
			await TestConditionAsync("true == ('true')", true);
			await TestConditionAsync("true == ('True')", true);
			await TestConditionAsync("true == ('false')", false);
			await TestConditionAsync("true == !('False')", true);
			await TestConditionAsync("true == 'true' and 'false' == 'False'", true);
			await TestConditionAsync("true == 'true' and 'false' == 'true'", false);
			await TestConditionAsync("true == 'false' or 'false' == 'false'", true);
			await TestConditionAsync("true == 'false' or 'false' == 'true'", true);
		}

		/// <summary>
		/// Helper method to evaluate a condition and check it's the expected result
		/// </summary>
		/// <param name="Condition">Condition to evaluate</param>
		/// <param name="ExpectedResult">The expected result</param>
		static async Task TestConditionAsync(string Condition, bool ExpectedResult)
		{
			bool Result = await new BgCondition(Condition, null!).EvaluateAsync();
			Console.WriteLine("{0}: {1} = {2}", (Result == ExpectedResult)? "PASS" : "FAIL", Condition, Result);
		}
	}
}
