// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Utility functions for showing help for objects
	/// </summary>
	public static class HelpUtils
	{
		/// <summary>
		/// Gets the width of the window for formatting purposes
		/// </summary>
		public static int WindowWidth => ConsoleUtils.WindowWidth;

		/// <summary>
		/// Prints help for the given object type
		/// </summary>
		/// <param name="Type">Type to print help for</param>
		public static void PrintHelp(string Title, Type Type)
		{
			PrintHelp(Title, GetDescription(Type), CommandLineArguments.GetParameters(Type));
		}

		/// <summary>
		/// Prints help for a command
		/// </summary>
		/// <param name="Title">Title for the help text</param>
		/// <param name="Description">Description for the command</param>
		/// <param name="Parameters">List of parameters</param>
		public static void PrintHelp(string? Title, string? Description, List<KeyValuePair<string, string>> Parameters)
		{
			bool bFirstLine = true;
			if (!String.IsNullOrEmpty(Title))
			{
				PrintParagraph(Title);
				bFirstLine = false;
			}

			if (!String.IsNullOrEmpty(Description))
			{
				if (!bFirstLine)
				{
					Console.WriteLine("");
				}
				PrintParagraph(Description);
				bFirstLine = false;
			}

			if (Parameters.Count > 0)
			{
				if (!bFirstLine)
				{
					Console.WriteLine("");
				}

				Console.WriteLine("Parameters:");
				PrintTable(Parameters, 4, 24);
			}
		}

		/// <summary>
		/// Gets the description from a type
		/// </summary>
		/// <param name="Type">The type to get a description for</param>
		/// <returns>The description text</returns>
		public static string GetDescription(Type Type)
		{
			StringBuilder DescriptionText = new StringBuilder();
			foreach (DescriptionAttribute Attribute in Type.GetCustomAttributes(typeof(DescriptionAttribute), false))
			{
				if (DescriptionText.Length > 0)
				{
					DescriptionText.AppendLine();
				}
				DescriptionText.AppendLine(Attribute.Description);
			}
			return DescriptionText.ToString();
		}

		/// <summary>
		/// Prints a paragraph of text using word wrapping
		/// </summary>
		/// <param name="Text">Text to print</param>
		/// <param name="Logger">Logger implementation to write to</param>
		public static void PrintParagraph(string Text)
		{
			PrintParagraph(Text, WindowWidth - 1);
		}

		/// <summary>
		/// Prints a paragraph of text using word wrapping
		/// </summary>
		/// <param name="Text">Text to print</param>
		/// <param name="MaxWidth">Maximum width for each line</param>
		public static void PrintParagraph(string Text, int MaxWidth)
		{
			IEnumerable<string> Lines = StringUtils.WordWrap(Text, MaxWidth);
			foreach (string Line in Lines)
			{
				Console.WriteLine(Line);
			}
		}

		/// <summary>
		/// Prints an argument list to the console
		/// </summary>
		/// <param name="Items">List of parameters arranged as "-ParamName Param Description"</param>
		/// <param name="Indent">Indent from the left hand side</param>
		/// <param name="MinFirstColumnWidth">The minimum padding from the start of the param name to the start of the description (resizes with larger param names)</param>
		/// <returns></returns>
		public static void PrintTable(List<KeyValuePair<string, string>> Items, int Indent, int MinFirstColumnWidth)
		{
			List<string> Lines = new List<string>();
			FormatTable(Items, Indent, MinFirstColumnWidth, WindowWidth - 1, Lines);

			foreach (string Line in Lines)
			{
				Console.WriteLine(Line);
			}
		}

		/// <summary>
		/// Prints a table of items to a logging device
		/// </summary>
		/// <param name="Items"></param>
		/// <param name="Indent"></param>
		/// <param name="MinFirstColumnWidth"></param>
		/// <param name="MaxWidth"></param>
		/// <param name="Logger"></param>
		public static void PrintTable(List<KeyValuePair<string, string>> Items, int Indent, int MinFirstColumnWidth, int MaxWidth, ILogger Logger)
		{
			List<string> Lines = new List<string>();
			FormatTable(Items, Indent, MinFirstColumnWidth, MaxWidth, Lines);

			foreach (string Line in Lines)
			{
				Logger.LogInformation("{Line)", Line);
			}
		}

		/// <summary>
		/// Formats the given parameters as so:
		///     -Param1     Param1 Description
		///
		///     -Param2      Param2 Description, this description is
		///                  longer and splits onto a separate line. 
		///
		///     -Param3      Param3 Description continues as before. 
		/// </summary>
		/// <param name="Items">List of parameters arranged as "-ParamName Param Description"</param>
		/// <param name="Indent">Indent from the left hand side</param>
		/// <param name="MinFirstColumnWidth">The minimum padding from the start of the param name to the start of the description (resizes with larger param names)</param>
		/// <returns>Sequence of formatted lines in the table</returns>
		public static void FormatTable(IReadOnlyList<KeyValuePair<string, string>> Items, int Indent, int MinFirstColumnWidth, int MaxWidth, List<string> Lines)
		{
			if(Items.Count > 0)
			{
				// string used to intent the param
				string IndentString = new string(' ', Indent);

				// default the padding value
				int RightPadding = Math.Max(MinFirstColumnWidth, Items.Max(x => x.Key.Length + 2));

				// Build the formatted params
				foreach(KeyValuePair<string, string> Item in Items)
				{
					// build the param first, including intend and padding on the rights size
					string ParamString = IndentString + Item.Key.PadRight(RightPadding);

					// Build the description line by line, adding the same amount of intending each time. 
					IEnumerable<string> DescriptionLines = StringUtils.WordWrap(Item.Value, MaxWidth - ParamString.Length);

					foreach(string DescriptionLine in DescriptionLines)
					{
						// Formatting as following:
						// <Indent>-param<Right Padding>Description<New line>
						Lines.Add(ParamString + DescriptionLine);

						// we replace the param string on subsequent lines with white space of the same length
						ParamString = string.Empty.PadRight(IndentString.Length + RightPadding);
					}
				}
			}
		}
	}
}
