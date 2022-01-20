// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace EpicGames.Horde
{
	/// <summary>
	/// Normalized string identifier for a resource
	/// </summary>
	struct StringId : IEquatable<StringId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		public string Text { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		[SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "Known limited character set")]
		public StringId(string Input)
		{
			this.Text = Input;

			if (Text.Length == 0)
			{
				throw new ArgumentException("String id may not be empty");
			}

			const int MaxLength = 64;
			if (Text.Length > MaxLength)
			{
				throw new ArgumentException($"String id may not be longer than {MaxLength} characters");
			}

			for (int Idx = 0; Idx < Text.Length; Idx++)
			{
				char Character = Text[Idx];
				if (!IsValidCharacter(Character))
				{
					if (Character >= 'A' && Character <= 'Z')
					{
						Text = Text.ToLowerInvariant();
					}
					else
					{
						throw new ArgumentException($"{Text} is not a valid string id");
					}
				}
			}
		}

		/// <summary>
		/// Checks whether this StringId is set
		/// </summary>
		public bool IsEmpty
		{
			get { return String.IsNullOrEmpty(Text); }
		}

		/// <summary>
		/// Checks whether the given character is valid within a string id
		/// </summary>
		/// <param name="Character">The character to check</param>
		/// <returns>True if the character is valid</returns>
		static bool IsValidCharacter(char Character)
		{
			if (Character >= 'a' && Character <= 'z')
			{
				return true;
			}
			if (Character >= '0' && Character <= '9')
			{
				return true;
			}
			if (Character == '-' || Character == '_' || Character == '.')
			{
				return true;
			}
			return false;
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is StringId Id && Equals(Id);

		/// <inheritdoc/>
		public override int GetHashCode() => Text.GetHashCode(StringComparison.Ordinal);

		/// <inheritdoc/>
		public bool Equals(StringId Other) => Text.Equals(Other.Text, StringComparison.Ordinal);

		/// <inheritdoc/>
		public override string ToString() => Text;

		/// <summary>
		/// Compares two string ids for equality
		/// </summary>
		/// <param name="Left">The first string id</param>
		/// <param name="Right">Second string id</param>
		/// <returns>True if the two string ids are equal</returns>
		public static bool operator ==(StringId Left, StringId Right) => Left.Equals(Right);

		/// <summary>
		/// Compares two string ids for inequality
		/// </summary>
		/// <param name="Left">The first string id</param>
		/// <param name="Right">Second string id</param>
		/// <returns>True if the two string ids are not equal</returns>
		public static bool operator !=(StringId Left, StringId Right) => !Left.Equals(Right);
	}
}
