using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Utility functions for showing help for objects
	/// </summary>
	public static class HelpUtils
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
		/// Prints help for the given object type
		/// </summary>
		/// <param name="Type">Type to print help for</param>
		public static void PrintHelp(string Title, Type Type)
		{
			List<string> Lines = GetHelpText(Title, Type);
			foreach(string Line in Lines)
			{
				Log.TraceInformation("{0}", Line);
			}
		}

		/// <summary>
		/// Get the help text for a given type, using any attribute-driven command line arguments, and the [Description] attribute for help text.
		/// </summary>
		/// <param name="Title">Title for the help text</param>
		/// <param name="Type">Type to query</param>
		/// <param name="MaxWidth">Maximum width for the returned lines</param>
		/// <returns>List of help lines</returns>
		public static List<string> GetHelpText(string Title, Type Type)
		{
			return GetHelpText(Title, GetDescription(Type), CommandLineArguments.GetParameters(Type), WindowWidth - 1);
		}

		/// <summary>
		/// Get the help text for a given type, using any attribute-driven command line arguments, and the [Description] attribute for help text.
		/// </summary>
		/// <param name="Title">Title for the help text</param>
		/// <param name="Description">Help description</param>
		/// <param name="Parameters">Arguments for the command</param>
		/// <param name="MaxWidth">Maximum width for the returned lines</param>
		/// <returns>List of help lines</returns>
		public static List<string> GetHelpText(string Title, string Description, List<KeyValuePair<string, string>> Parameters, int MaxWidth)
		{
			List<string> Lines = new List<string>();

			if (!String.IsNullOrEmpty(Title))
			{
				Lines.AddRange(StringUtils.WordWrap(Title, MaxWidth));
			}

			if (!String.IsNullOrEmpty(Description))
			{
				if (Lines.Count > 0)
				{
					Lines.Add("");
				}
				Lines.AddRange(StringUtils.WordWrap(Description, MaxWidth));
			}

			if (Parameters.Count > 0)
			{
				if (Lines.Count > 0)
				{
					Lines.Add("");
				}
				Lines.Add("Parameters:");
				Lines.AddRange(Tabulate(Parameters, 4, 24, MaxWidth));
			}

			return Lines;
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
		/// <param name="DefaultRightPadding">The minimum padding from the start of the param name to the start of the description (resizes with larger param names)</param>
		/// <returns></returns>
		public static List<string> Tabulate(List<KeyValuePair<string, string>> Items, int Indent, int MinFirstColumnWidth, int MaxWidth)
		{
			List<string> Lines = new List<string>();
			if(Items.Count > 0)
			{
				// string used to intent the param
				string IndentString = new string(' ', Indent);

				// default the padding value
				int RightPadding = Math.Max(MinFirstColumnWidth, Items.Max(x => x.Key.Length + 1));

				// Build the formatted params
				foreach(KeyValuePair<string, string> Item in Items)
				{
					// build the param first, including intend and padding on the rights size
					string ParamString = IndentString + Item.Key.PadRight(RightPadding);

					// Build the description line by line, adding the same amount of intending each time. 
					List<string> DescriptionLines = StringUtils.WordWrap(Item.Value, MaxWidth - ParamString.Length);

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
			return Lines;
		}
	}
}
