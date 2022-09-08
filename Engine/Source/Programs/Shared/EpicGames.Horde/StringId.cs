// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.Horde
{
	/// <summary>
	/// Normalized string identifier for a resource
	/// </summary>
	public struct StringId : IEquatable<StringId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		public string Text { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="text">Unique id for the string</param>
		public StringId(string text)
		{
			Text = text;
			ValidateArgument(text, nameof(text));
		}

		/// <summary>
		/// Checks whether this StringId is set
		/// </summary>
		public bool IsEmpty => String.IsNullOrEmpty(Text);

		/// <summary>
		/// Validates the given string as a StringId, normalizing it if necessary.
		/// </summary>
		/// <param name="text">Text to validate as a StringId</param>
		/// <param name="paramName">Name of the parameter to show if invalid characters are returned.</param>
		/// <returns></returns>
		public static string ValidateArgument(string text, string paramName)
		{
			if (text.Length == 0)
			{
				throw new ArgumentException("String id may not be empty", paramName);
			}

			const int MaxLength = 64;
			if (text.Length > MaxLength)
			{
				throw new ArgumentException($"String id may not be longer than {MaxLength} characters", paramName);
			}

			for (int idx = 0; idx < text.Length; idx++)
			{
				char character = text[idx];
				if (!IsValidCharacter(character))
				{
					if (character >= 'A' && character <= 'Z')
					{
#pragma warning disable CA1308 // Normalize strings to uppercase
						text = text.ToLowerInvariant();
#pragma warning restore CA1308 // Normalize strings to uppercase
					}
					else
					{
						throw new ArgumentException($"{text} is not a valid string id", paramName);
					}
				}
			}

			return text;
		}

		/// <summary>
		/// Checks whether the given character is valid within a string id
		/// </summary>
		/// <param name="character">The character to check</param>
		/// <returns>True if the character is valid</returns>
		static bool IsValidCharacter(char character)
		{
			if (character >= 'a' && character <= 'z')
			{
				return true;
			}
			if (character >= '0' && character <= '9')
			{
				return true;
			}
			if (character == '-' || character == '_' || character == '.')
			{
				return true;
			}
			return false;
		}

		/// <inheritdoc/>
		public override bool Equals(object? obj) => obj is StringId id && Equals(id);

		/// <inheritdoc/>
		public override int GetHashCode() => Text.GetHashCode(StringComparison.Ordinal);

		/// <inheritdoc/>
		public bool Equals(StringId other) => Text.Equals(other.Text, StringComparison.Ordinal);

		/// <inheritdoc/>
		public override string ToString() => Text;

		/// <summary>
		/// Compares two string ids for equality
		/// </summary>
		/// <param name="left">The first string id</param>
		/// <param name="right">Second string id</param>
		/// <returns>True if the two string ids are equal</returns>
		public static bool operator ==(StringId left, StringId right) => left.Equals(right);

		/// <summary>
		/// Compares two string ids for inequality
		/// </summary>
		/// <param name="left">The first string id</param>
		/// <param name="right">Second string id</param>
		/// <returns>True if the two string ids are not equal</returns>
		public static bool operator !=(StringId left, StringId right) => !left.Equals(right);
	}
}
