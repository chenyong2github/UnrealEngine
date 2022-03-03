// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Extensions to List that provides some uniqueness support to list elements 
	/// </summary>
	public static class UhtListExtensions
	{

		/// <summary>
		/// Add the given value if it isn't already contained within the list
		/// </summary>
		/// <param name="Container">Destination container</param>
		/// <param name="Value">Value to be added</param>
		/// <returns>True if the value was added, false if it was already present</returns>
		public static bool AddUnique(this List<string> Container, string Value)
		{
			if (Container.Contains(Value, StringComparer.OrdinalIgnoreCase))
			{
				return false;
			}
			Container.Add(Value);
			return true;
		}

		/// <summary>
		/// Add the given values if they aren't already contained within the list
		/// </summary>
		/// <param name="Container">Destination container</param>
		/// <param name="Values">Values to be added</param>
		public static void AddUniqueRange(this List<string> Container, IEnumerable<StringView>? Values)
		{
			if (Values != null)
			{
				foreach (StringView Value in Values)
				{
					AddUnique(Container, Value.ToString());
				}
			}
		}

		/// <summary>
		/// Remove the given value but swap the last entry into the eliminated slot
		/// </summary>
		/// <param name="Container">Container being modified</param>
		/// <param name="Value">Value to be removed</param>
		/// <returns>True if the value was removed, false if not</returns>
		public static bool RemoveSwap(this List<string> Container, string Value)
		{
			int Index = Container.FindIndex(n => Value.Equals(n, StringComparison.OrdinalIgnoreCase));
			if (Index >= 0)
			{
				if (Index != Container.Count - 1)
				{
					Container[Index] = Container[Container.Count - 1];
				}
				Container.RemoveAt(Container.Count - 1);
				return true;
			}
			return false;
		}

		/// <summary>
		/// Remove a range of values from a container using swapping
		/// </summary>
		/// <param name="Container">Container to be modified</param>
		/// <param name="Values">List of values to be removed</param>
		public static void RemoveSwapRange(this List<string> Container, IEnumerable<StringView>? Values)
		{
			if (Values != null)
			{
				foreach (StringView Value in Values)
				{
					RemoveSwap(Container, Value.ToString());
				}
			}
		}
	}

	/// <summary>
	/// UnrealEngine names often differ from the names in the source file.  The following 
	/// structure represents the different parts of the name
	/// </summary>
	public struct UhtEngineNameParts
	{

		/// <summary>
		/// Any prefix removed from the source name to create the engine name
		/// </summary>
		public StringView Prefix;

		/// <summary>
		/// The name to be used by the Unreal Engine.
		/// </summary>
		public StringView EngineName;

		/// <summary>
		/// The name contained the "DEPRECATED" text which has been removed from the engine name
		/// </summary>
		public bool bIsDeprecated;
	}

	/// <summary>
	/// Assorted utility functions
	/// </summary>
	public class UhtUtilities
	{

		/// <summary>
		/// Given a collection of names, return a string containing the text of those names concatenated.
		/// </summary>
		/// <param name="TypeNames">Collect of names to be merged</param>
		/// <param name="AndOr">Text used to separate the names</param>
		/// <param name="bQuote">If true, add quotes around the names</param>
		/// <returns>Merged names</returns>
		public static string MergeTypeNames(IEnumerable<string> TypeNames, string AndOr, bool bQuote = false)
		{
			List<string> Local = new List<string>(TypeNames);

			if (Local.Count == 0)
			{
				return "";
			}

			Local.Sort();

			StringBuilder Builder = new StringBuilder();
			for (int Index = 0; Index < Local.Count; ++Index)
			{
				if (Index != 0)
				{
					Builder.Append(", ");
					if (Index == Local.Count - 1)
					{
						Builder.Append(AndOr);
						Builder.Append(' ');
					}
				}
				if (bQuote)
				{
					Builder.Append("'");
					Builder.Append(Local[Index]);
					Builder.Append("'");
				}
				else
				{
					Builder.Append(Local[Index]);
				}
			}
			return Builder.ToString();
		}

		/// <summary>
		/// Split the given source name into the engine name parts
		/// </summary>
		/// <param name="SourceName">Source name</param>
		/// <returns>Resulting engine name parts</returns>
		public static UhtEngineNameParts GetEngineNameParts(StringView SourceName)
		{
			if (SourceName.Span.Length == 0)
			{
				return new UhtEngineNameParts { Prefix = new StringView(string.Empty), EngineName = new StringView(string.Empty), bIsDeprecated = false };
			}

			switch (SourceName.Span[0])
			{
				case 'I':
				case 'A':
				case 'U':
					// If it is a class prefix, check for deprecated class prefix also
					if (SourceName.Span.Length > 12 && SourceName.Span.Slice(1).StartsWith("DEPRECATED_"))
					{
						return new UhtEngineNameParts { Prefix = new StringView(SourceName, 0, 12), EngineName = new StringView(SourceName, 12), bIsDeprecated = true };
					}
					else
					{
						return new UhtEngineNameParts { Prefix = new StringView(SourceName, 0, 1), EngineName = new StringView(SourceName, 1), bIsDeprecated = false };
					}

				case 'F':
				case 'T':
					// Struct prefixes are also fine.
					return new UhtEngineNameParts { Prefix = new StringView(SourceName, 0, 1), EngineName = new StringView(SourceName, 1), bIsDeprecated = false };

				default:
					// If it's not a class or struct prefix, it's invalid
					return new UhtEngineNameParts { Prefix = new StringView(string.Empty), EngineName = new StringView(SourceName), bIsDeprecated = false };
			}
		}
	}

	/// <summary>
	/// String builder class that has support for StringView so that if a single instance of
	/// a StringView is appended, it is returned.
	/// </summary>
	public class StringViewBuilder
	{

		/// <summary>
		/// When only a string view has been appended, this references that StringView
		/// </summary>
		private StringView StringViewInternal = new StringView();

		/// <summary>
		/// Represents more complex data being appended
		/// </summary>
		private StringBuilder? StringBuilderInternal = null;

		/// <summary>
		/// Set to true when the builder has switched to a StringBuilder (NOTE: This can probably be removed)
		/// </summary>
		private bool bUseStringBuilder = false;

		/// <summary>
		/// The length of the appended data
		/// </summary>
		public int Length
		{
			get
			{
				if (this.bUseStringBuilder && this.StringBuilderInternal != null)
				{
					return this.StringBuilderInternal.Length;
				}
				else
				{
					return this.StringViewInternal.Span.Length;
				}
			}
		}

		/// <summary>
		/// Return the appended data as a StringView
		/// </summary>
		/// <returns>Contents of the builder</returns>
		public StringView ToStringView()
		{
			return this.bUseStringBuilder ? new StringView(this.StringBuilderInternal!.ToString()) : this.StringViewInternal;
		}

		/// <summary>
		/// Return the appended data as a string
		/// </summary>
		/// <returns>Contents of the builder</returns>
		public override string ToString()
		{
			return this.bUseStringBuilder ? this.StringBuilderInternal!.ToString() : this.StringViewInternal.ToString();
		}

		/// <summary>
		/// Append a StringView
		/// </summary>
		/// <param name="Text">Text to be appended</param>
		/// <returns>The string builder</returns>
		public StringViewBuilder Append(StringView Text)
		{
			if (this.bUseStringBuilder)
			{
				this.StringBuilderInternal!.Append(Text.Span);
			}
			else if (this.StringViewInternal.Span.Length > 0)
			{
				SwitchToStringBuilder();
				this.StringBuilderInternal!.Append(Text.Span);
			}
			else
			{
				this.StringViewInternal = Text;
			}
			return this;
		}

		/// <summary>
		/// Append a character
		/// </summary>
		/// <param name="C">Character to be appended</param>
		/// <returns>The string builder</returns>
		public StringViewBuilder Append(char C)
		{
			SwitchToStringBuilder();
			this.StringBuilderInternal!.Append(C);
			return this;
		}

		/// <summary>
		/// If not already done, switch the builder to using a StringBuilder
		/// </summary>
		private void SwitchToStringBuilder()
		{
			if (!this.bUseStringBuilder)
			{
				if (this.StringBuilderInternal == null)
				{
					this.StringBuilderInternal = new StringBuilder();
				}
				this.bUseStringBuilder = true;
				this.StringBuilderInternal.Append(this.StringViewInternal.Span);
			}
		}
	}
}
