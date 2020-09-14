// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Specifies how to format JSON output
	/// </summary>
	public enum JsonWriterStyle
	{
		/// <summary>
		/// Omit spaces between elements
		/// </summary>
		Compact,

		/// <summary>
		/// Put each value on a newline, and indent output
		/// </summary>
		Readable
	}

	/// <summary>
	/// Writer for JSON data, which indents the output text appropriately, and adds commas and newlines between fields
	/// </summary>
	public class JsonWriter : IDisposable
	{
		TextWriter Writer;
		bool bLeaveOpen;
		JsonWriterStyle Style;
		bool bRequiresComma;
		string Indent;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="FileName">File to write to</param>
		/// <param name="Style">Should use packed JSON or not</param>
		public JsonWriter(string FileName, JsonWriterStyle Style = JsonWriterStyle.Readable)
			: this(new StreamWriter(FileName))
		{
			this.Style = Style;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="FileName">File to write to</param>
		/// <param name="Style">Should use packed JSON or not</param>
		public JsonWriter(FileReference FileName, JsonWriterStyle Style = JsonWriterStyle.Readable)
			: this(new StreamWriter(FileName.FullName))
		{
			this.Style = Style;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Writer">The text writer to output to</param>
		/// <param name="bLeaveOpen">Whether to leave the writer open when the object is disposed</param>
		/// <param name="Style">The output style</param>
		public JsonWriter(TextWriter Writer, bool bLeaveOpen = false, JsonWriterStyle Style = JsonWriterStyle.Readable)
		{
			this.Writer = Writer;
			this.bLeaveOpen = bLeaveOpen;
			this.Style = Style;
			Indent = "";
		}

		/// <summary>
		/// Dispose of any managed resources
		/// </summary>
		public void Dispose()
		{
			if(!bLeaveOpen && Writer != null)
			{
				Writer.Dispose();
				Writer = null;
			}
		}

		private void IncreaseIndent()
		{
			if (Style == JsonWriterStyle.Readable)
			{
				Indent += "\t";
			}
		}
		
		private void DecreaseIndent()
		{
			if (Style == JsonWriterStyle.Readable)
			{
				Indent = Indent.Substring(0, Indent.Length - 1);
			}
		}

		/// <summary>
		/// Write the opening brace for an object
		/// </summary>
		public void WriteObjectStart()
		{
			WriteCommaNewline();

			Writer.Write(Indent);
			Writer.Write("{");

			IncreaseIndent();
			bRequiresComma = false;
		}

		/// <summary>
		/// Write the name and opening brace for an object
		/// </summary>
		/// <param name="ObjectName">Name of the field</param>
		public void WriteObjectStart(string ObjectName)
		{
			WriteCommaNewline();

			string Space = (Style == JsonWriterStyle.Readable) ? " " : "";
			Writer.Write("{0}\"{1}\":{2}", Indent, ObjectName, Space);

			bRequiresComma = false;

			WriteObjectStart();
		}

		/// <summary>
		/// Write the closing brace for an object
		/// </summary>
		public void WriteObjectEnd()
		{
			DecreaseIndent();

			WriteLine();
			Writer.Write(Indent);
			Writer.Write("}");

			bRequiresComma = true;
		}

		/// <summary>
		/// Write the opening bracket for an unnamed array
		/// </summary>
		/// <param name="ArrayName">Name of the field</param>
		public void WriteArrayStart()
		{
			WriteCommaNewline();

			Writer.Write("{0}[", Indent);

			IncreaseIndent();
			bRequiresComma = false;
		}

		/// <summary>
		/// Write the name and opening bracket for an array
		/// </summary>
		/// <param name="ArrayName">Name of the field</param>
		public void WriteArrayStart(string ArrayName)
		{
			WriteCommaNewline();

			string Space = (Style == JsonWriterStyle.Readable) ? " " : "";
			Writer.Write("{0}\"{1}\":{2}[", Indent, ArrayName, Space);

			IncreaseIndent();
			bRequiresComma = false;
		}

		/// <summary>
		/// Write the closing bracket for an array
		/// </summary>
		public void WriteArrayEnd()
		{
			DecreaseIndent();

			WriteLine();
			Writer.Write("{0}]", Indent);

			bRequiresComma = true;
		}

		private void WriteLine()
		{
			if (Style == JsonWriterStyle.Readable)
			{
				Writer.WriteLine();
			}
		}
		
		private void WriteLine(string Line)
		{
			if (Style == JsonWriterStyle.Readable)
			{
				Writer.WriteLine(Line);
			}
			else
			{
				Writer.Write(Line);
			}
		}

		/// <summary>
		/// Write an array of strings
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Values">Values for the field</param>
		public void WriteStringArrayField(string Name, IEnumerable<string> Values)
		{
			WriteArrayStart(Name);
			foreach(string Value in Values)
			{
				WriteValue(Value);
			}
			WriteArrayEnd();
		}

		/// <summary>
		/// Write an array of enum values
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Values">Values for the field</param>
		public void WriteEnumArrayField<T>(string Name, IEnumerable<T> Values) where T : struct
		{
			WriteStringArrayField(Name, Values.Select(x => x.ToString()));
		}

		/// <summary>
		/// Write a value with no field name, for the contents of an array
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteValue(string Value)
		{
			WriteCommaNewline();

			Writer.Write(Indent);
			WriteEscapedString(Value);

			bRequiresComma = true;
		}

		/// <summary>
		/// Write a field name and string value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value for the field</param>
		public void WriteValue(string Name, string Value)
		{
			WriteCommaNewline();

			string Space = (Style == JsonWriterStyle.Readable) ? " " : "";
			Writer.Write("{0}\"{1}\":{2}", Indent, Name, Space);
			WriteEscapedString(Value);

			bRequiresComma = true;
		}

		/// <summary>
		/// Write a field name and integer value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value for the field</param>
		public void WriteValue(string Name, int Value)
		{
			WriteValueInternal(Name, Value.ToString());
		}

		/// <summary>
		/// Write a field name and double value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value for the field</param>
		public void WriteValue(string Name, double Value)
		{
			WriteValueInternal(Name, Value.ToString());
		}

		/// <summary>
		/// Write a field name and bool value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value for the field</param>
		public void WriteValue(string Name, bool Value)
		{
			WriteValueInternal(Name, Value ? "true" : "false");
		}

		/// <summary>
		/// Write a field name and enum value
		/// </summary>
		/// <typeparam name="T">The enum type</typeparam>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value for the field</param>
		public void WriteEnumValue<T>(string Name, T Value) where T : struct
		{
			WriteValue(Name, Value.ToString());
		}

		void WriteCommaNewline()
		{
			if (bRequiresComma)
			{
				WriteLine(",");
			}
			else if (Indent.Length > 0)
			{
				WriteLine();
			}
		}

		void WriteValueInternal(string Name, string Value)
		{
			WriteCommaNewline();

			string Space = (Style == JsonWriterStyle.Readable) ? " " : "";
			Writer.Write("{0}\"{1}\":{2}{3}", Indent, Name, Space, Value);

			bRequiresComma = true;
		}

		void WriteEscapedString(string Value)
		{
			// Escape any characters which may not appear in a JSON string (see http://www.json.org).
			Writer.Write("\"");
			if (Value != null)
			{
				Writer.Write(Json.EscapeString(Value));
			}
			Writer.Write("\"");
		}
	}
}
