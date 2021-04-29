// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Utility class for dealing with message templates
	/// </summary>
	static class MessageTemplate
	{
		/// <summary>
		/// The default property name for the message template format string in an enumerable log state parameter
		/// </summary>
		public const string FormatPropertyName = "{OriginalFormat}";

		/// <summary>
		/// Renders a format string
		/// </summary>
		/// <param name="Format">The format string</param>
		/// <param name="Properties">Property values to embed</param>
		/// <returns>The rendered string</returns>
		public static string Render(string Format, IEnumerable<KeyValuePair<string, object>>? Properties)
		{
			StringBuilder Result = new StringBuilder();
			Render(Format, Properties, Result);
			return Result.ToString();
		}

		/// <summary>
		/// Renders a format string to the end of a string builder
		/// </summary>
		/// <param name="Format">The format string to render</param>
		/// <param name="Properties">Sequence of key/value properties</param>
		/// <param name="Result">Buffer to append the rendered string to</param>
		public static void Render(string Format, IEnumerable<KeyValuePair<string, object>>? Properties, StringBuilder Result)
		{
			int NextOffset = 0;

			List<(int, int)>? Names = FindPropertyNames(Format);
			if (Names != null)
			{
				foreach((int Offset, int Length) in Names)
				{
					object? Value;
					if (Properties != null && TryGetPropertyValue(Format.AsSpan(Offset, Length), Properties, out Value))
					{
						int StartOffset = Offset - 1;
						if (Format[StartOffset] == '@' || Format[StartOffset] == '$')
						{
							StartOffset--;
						}

						Unescape(Format.AsSpan(NextOffset, StartOffset - NextOffset), Result);
						Result.Append(Value?.ToString() ?? "null");
						NextOffset = Offset + Length + 1;
					}
				}
			}

			Unescape(Format.AsSpan(NextOffset, Format.Length - NextOffset), Result);
		}

		/// <summary>
		/// Escapes a string for use in a message template
		/// </summary>
		/// <param name="Text">Text to escape</param>
		/// <returns>The escaped string</returns>
		public static string Escape(string Text)
		{
			StringBuilder Result = new StringBuilder();
			Escape(Text, Result);
			return Result.ToString();
		}

		/// <summary>
		/// Escapes a span of characters and appends the result to a string
		/// </summary>
		/// <param name="Text">Span of characters to escape</param>
		/// <param name="Result">Buffer to receive the escaped string</param>
		public static void Escape(ReadOnlySpan<char> Text, StringBuilder Result)
		{
			foreach(char Char in Text)
			{
				Result.Append(Char);
				if (Char == '{' || Char == '}')
				{
					Result.Append(Char);
				}
			}
		}

		/// <summary>
		/// Unescapes a string from a message template
		/// </summary>
		/// <param name="Text">The text to unescape</param>
		/// <returns>The unescaped text</returns>
		public static string Unescape(string Text)
		{
			StringBuilder Result = new StringBuilder();
			Unescape(Text.AsSpan(), Result);
			return Result.ToString();
		}

		/// <summary>
		/// Unescape a string and append the result to a string builder
		/// </summary>
		/// <param name="Text">Text to unescape</param>
		/// <param name="Result">Receives the unescaped text</param>
		public static void Unescape(ReadOnlySpan<char> Text, StringBuilder Result)
		{
			char LastChar = '\0';
			foreach (char Char in Text)
			{
				if ((Char != '{' || Char != '}') || Char != LastChar)
				{
					Result.Append(Char);
				}
				LastChar = Char;
			}
		}

		/// <summary>
		/// Finds locations of property names from the given format string
		/// </summary>
		/// <param name="Format">The format string to parse</param>
		/// <returns>List of offset, length pairs for property names. Null if the string does not contain any property references.</returns>
		public static List<(int, int)>? FindPropertyNames(string Format)
		{
			List<(int, int)>? Names = null;
			for (int Idx = 0; Idx < Format.Length - 1; Idx++)
			{
				if (Format[Idx] == '{')
				{
					if (Format[Idx + 1] == '{')
					{
						Idx++;
					}
					else
					{
						int StartIdx = Idx + 1;

						Idx = Format.IndexOf('}', StartIdx);
						if (Idx == -1)
						{
							break;
						}
						if (Names == null)
						{
							Names = new List<(int, int)>();
						}

						Names.Add((StartIdx, Idx - StartIdx));
					}
				}
			}
			return Names;
		}

		/// <summary>
		/// Attempts to get a named property value from the given dictionary
		/// </summary>
		/// <param name="Name">Name of the property</param>
		/// <param name="Properties">Sequence of property name/value pairs</param>
		/// <param name="Value">On success, receives the property value</param>
		/// <returns>True if the property was found, false otherwise</returns>
		public static bool TryGetPropertyValue(ReadOnlySpan<char> Name, IEnumerable<KeyValuePair<string, object>> Properties, out object? Value)
		{
			int Number;
			if (int.TryParse(Name, System.Globalization.NumberStyles.Integer, null, out Number))
			{
				foreach (KeyValuePair<string, object> Property in Properties)
				{
					if (Number == 0)
					{
						Value = Property.Value;
						return true;
					}
					Number--;
				}
			}
			else
			{
				foreach (KeyValuePair<string, object> Property in Properties)
				{
					ReadOnlySpan<char> ParameterName = Property.Key.AsSpan();
					if (Name.Equals(ParameterName, StringComparison.Ordinal))
					{
						Value = Property.Value;
						return true;
					}
				}
			}

			Value = null;
			return false;
		}
	}
}
