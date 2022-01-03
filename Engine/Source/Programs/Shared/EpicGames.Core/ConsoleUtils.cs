// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Utility methods for writing to the console
	/// </summary>
	public static class ConsoleUtils
	{
		/// <summary>
		/// Gets the width of the window for formatting purposes
		/// </summary>
		public static int WindowWidth
		{
			get
			{
				// Get the window width, using a default value if there's no console attached to this process.
				int NewWindowWidth;
				try
				{
					NewWindowWidth = Console.WindowWidth;
				}
				catch
				{
					NewWindowWidth = 240;
				}

				if (NewWindowWidth <= 0)
				{
					NewWindowWidth = 240;
				}

				return NewWindowWidth;
			}
		}

		/// <summary>
		/// Writes the given text to the console as a sequence of word-wrapped lines
		/// </summary>
		/// <param name="Text">The text to write to the console</param>
		public static void WriteLineWithWordWrap(string Text)
		{
			WriteLineWithWordWrap(Text, 0, 0);
		}

		/// <summary>
		/// Writes the given text to the console as a sequence of word-wrapped lines
		/// </summary>
		/// <param name="Text">The text to write to the console</param>
		/// <param name="InitialIndent">Indent for the first line</param>
		/// <param name="HangingIndent">Indent for lines after the first</param>
		public static void WriteLineWithWordWrap(string Text, int InitialIndent, int HangingIndent)
		{
			foreach (string Line in StringUtils.WordWrap(Text, InitialIndent, HangingIndent, WindowWidth))
			{
				Console.WriteLine(Line);
			}
		}
	}
}
