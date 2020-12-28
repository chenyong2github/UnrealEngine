// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Extensions to StringBuilder for building command lines
	/// </summary>
	public static class CommandLineExtensions
	{
		/// <summary>
		/// Determines if the given argument needs to be escaped
		/// </summary>
		/// <param name="Argument">The argument to check</param>
		/// <returns>True if the argument needs to be escaped</returns>
		static bool NeedsEscaping(string Argument)
		{
			return Argument.Contains(' ') || Argument.Contains('\"');
		}

		/// <summary>
		/// Appends command line argument with a prefixed space. The argument may contain spaces or quotes.
		/// </summary>
		/// <param name="Builder">The command line to append to</param>
		/// <param name="Argument">The argument to append</param>
		public static void AppendArgument(this StringBuilder Builder, string Argument)
		{
			if (Builder.Length > 0)
			{
				Builder.Append(' ');
			}

			int EqualsIdx = Argument.IndexOf('=');
			if (EqualsIdx != -1)
			{
				string Name = Argument.Substring(0, EqualsIdx + 1);
				if (!NeedsEscaping(Name))
				{
					Builder.Append(Name);
					Argument = Argument.Substring(EqualsIdx + 1);
				}
			}

			AppendCommandLineArgumentWithoutSpace(Builder, Argument);
		}

		/// <summary>
		/// Appends command line argument with a prefixed space. The argument may contain spaces or quotes.
		/// </summary>
		/// <param name="Builder">The command line to append to</param>
		/// <param name="Name">Name of the argument (eg. -Foo=)</param>
		/// <param name="Value">Value of the argument</param>
		public static void AppendArgument(this StringBuilder Builder, string Name, string Value)
		{
			if (Builder.Length > 0)
			{
				Builder.Append(' ');
			}

			if (NeedsEscaping(Name))
			{
				AppendCommandLineArgumentWithoutSpace(Builder, Name + Value);
			}
			else
			{
				Builder.Append(Name);
				AppendCommandLineArgumentWithoutSpace(Builder, Value);
			}
		}

		/// <summary>
		/// Appends an escaped command line argument. The argument may contain spaces or quotes, and is escaped according to the rules in
		/// https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw.
		/// </summary>
		/// <param name="Builder">The builder to append to</param>
		/// <param name="Argument">The argument to escape</param>
		public static void AppendCommandLineArgumentWithoutSpace(this StringBuilder Builder, string Argument)
		{
			if (!NeedsEscaping(Argument))
			{
				// No escaping necessary if the argument doesn't contain any special characters
				Builder.Append(Argument);
			}
			else
			{
				// Escape the whole string following the rules on the CommandLineToArgV MSDN page. 
				Builder.Append('\"');
				for (int Idx = 0; Idx < Argument.Length; Idx++)
				{
					char Character = Argument[Idx];
					if (Character == '\"')
					{
						// Escape a single quotation mark
						Builder.Append("\\\"");
					}
					else if (Character == '\\')
					{
						// Special handling for slashes which may be followed by a quotation mark, as dictated by CommandLineToArgV
						int StartIdx = Idx;
						for (; ; )
						{
							int NextIdx = Idx + 1;
							if (NextIdx == Argument.Length)
							{
								// Will have a trailing quotation mark toggling 'in quotes' mode (2n)
								Builder.Append('\\', (NextIdx - StartIdx) * 2);
								break;
							}
							else if (Argument[NextIdx] == '\"')
							{
								// Needs to have a trailing quotation mark, so need to escape each backslash plus the quotation mark (2n+1)
								Builder.Append('\\', (NextIdx - StartIdx) * 2 + 1);
								break;
							}
							else if (Argument[NextIdx] != '\\')
							{
								// No trailing quote; can just pass through verbatim
								Builder.Append('\\', (NextIdx - StartIdx));
								break;
							}
							Idx = NextIdx;
						}
					}
					else
					{
						// Regular character
						Builder.Append(Character);
					}
				}
				Builder.Append('\"');
			}
		}
	}
}
