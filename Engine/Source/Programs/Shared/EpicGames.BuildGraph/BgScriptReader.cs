// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Xml;
using System.Xml.Schema;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Location of an element within a file
	/// </summary>
	public class BgScriptLocation
	{
		/// <summary>
		/// The file containing this element
		/// </summary>
		public string File { get; }

		/// <summary>
		/// Native file object
		/// </summary>
		public object NativeFile { get; }

		/// <summary>
		/// The line number containing this element
		/// </summary>
		public int LineNumber { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="File"></param>
		/// <param name="NativeFile"></param>
		/// <param name="LineNumber"></param>
		public BgScriptLocation(string File, object NativeFile, int LineNumber)
		{
			this.File = File;
			this.NativeFile = NativeFile;
			this.LineNumber = LineNumber;
		}
	}

	/// <summary>
	/// Implementation of XmlDocument which preserves line numbers for its elements
	/// </summary>
	class BgScriptDocument : XmlDocument
	{
		/// <summary>
		/// The file being read
		/// </summary>
		string File;

		/// <summary>
		/// Native file representation
		/// </summary>
		object NativeFile;

		/// <summary>
		/// Interface to the LineInfo on the active XmlReader
		/// </summary>
		IXmlLineInfo? LineInfo;

		/// <summary>
		/// Set to true if the reader encounters an error
		/// </summary>
		bool bHasErrors;

		/// <summary>
		/// Logger for validation errors
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Private constructor. Use ScriptDocument.Load to read an XML document.
		/// </summary>
		BgScriptDocument(string InFile, object InNativeFile, ILogger InLogger)
		{
			File = InFile;
			NativeFile = InNativeFile;
			Logger = InLogger;
		}

		/// <summary>
		/// Overrides XmlDocument.CreateElement() to construct ScriptElements rather than XmlElements
		/// </summary>
		public override XmlElement CreateElement(string Prefix, string LocalName, string NamespaceUri)
		{
			BgScriptLocation Location = new BgScriptLocation(File, NativeFile, LineInfo!.LineNumber);
			return new BgScriptElement(Location, Prefix, LocalName, NamespaceUri, this);
		}

		/// <summary>
		/// Loads a script document from the given file
		/// </summary>
		/// <param name="File">The file to load</param>
		/// <param name="NativeFile"></param>
		/// <param name="Data"></param>
		/// <param name="Schema">The schema to validate against</param>
		/// <param name="Logger">Logger for output messages</param>
		/// <param name="OutDocument">If successful, the document that was read</param>
		/// <returns>True if the document could be read, false otherwise</returns>
		public static bool TryRead(string File, object NativeFile, byte[] Data, BgScriptSchema Schema, ILogger Logger, [NotNullWhen(true)] out BgScriptDocument? OutDocument)
		{
			BgScriptDocument Document = new BgScriptDocument(File, NativeFile, Logger);

			XmlReaderSettings Settings = new XmlReaderSettings();
			if (Schema != null)
			{
				Settings.Schemas.Add(Schema.CompiledSchema);
				Settings.ValidationType = ValidationType.Schema;
				Settings.ValidationEventHandler += Document.ValidationEvent;
			}

			using (MemoryStream Stream = new MemoryStream(Data))
			using (XmlReader Reader = XmlReader.Create(Stream, Settings))
			{
				// Read the document
				Document.LineInfo = (IXmlLineInfo)Reader;
				try
				{
					Document.Load(Reader);
				}
				catch (XmlException Ex)
				{
					if (!Document.bHasErrors)
					{
						BgScriptLocation Location = new BgScriptLocation(File, NativeFile, Ex.LineNumber);
						Logger.LogScriptError(Location, "{Message}", Ex.Message);
						Document.bHasErrors = true;
					}
				}

				// If we hit any errors while parsing
				if (Document.bHasErrors)
				{
					OutDocument = null;
					return false;
				}

				// Check that the root element is valid. If not, we didn't actually validate against the schema.
				if (Document.DocumentElement.Name != BgScriptSchema.RootElementName)
				{
					BgScriptLocation Location = new BgScriptLocation(File, NativeFile, 1);
					Logger.LogScriptError(Location, "Script does not have a root element called '{ElementName}'", BgScriptSchema.RootElementName);
					OutDocument = null;
					return false;
				}
				if (Document.DocumentElement.NamespaceURI != BgScriptSchema.NamespaceURI)
				{
					BgScriptLocation Location = new BgScriptLocation(File, NativeFile, 1);
					Logger.LogScriptError(Location, "Script root element is not in the '{Namespace}' namespace (add the xmlns=\"{NewNamespace}\" attribute)", BgScriptSchema.NamespaceURI, BgScriptSchema.NamespaceURI);
					OutDocument = null;
					return false;
				}
			}

			OutDocument = Document;
			return true;
		}

		/// <summary>
		/// Callback for validation errors in the document
		/// </summary>
		/// <param name="Sender">Standard argument for ValidationEventHandler</param>
		/// <param name="Args">Standard argument for ValidationEventHandler</param>
		void ValidationEvent(object Sender, ValidationEventArgs Args)
		{
			BgScriptLocation Location = new BgScriptLocation(File, NativeFile, Args.Exception.LineNumber);
			if (Args.Severity == XmlSeverityType.Warning)
			{
				Logger.LogScriptWarning(Location, "{Message}", Args.Message);
			}
			else
			{
				Logger.LogScriptError(Location, "{Message}", Args.Message);
				bHasErrors = true;
			}
		}
	}

	/// <summary>
	/// Implementation of XmlElement which preserves line numbers
	/// </summary>
	class BgScriptElement : XmlElement
	{
		/// <summary>
		/// Location of the element within the file
		/// </summary>
		public BgScriptLocation Location { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgScriptElement(BgScriptLocation Location, string Prefix, string LocalName, string NamespaceUri, BgScriptDocument Document)
			: base(Prefix, LocalName, NamespaceUri, Document)
		{
			this.Location = Location;
		}
	}

	/// <summary>
	/// Stores information about a script function that has been declared
	/// </summary>
	class BgScriptMacro
	{
		/// <summary>
		/// Name of the function
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// Element where the function was declared
		/// </summary>
		public List<BgScriptElement> Elements = new List<BgScriptElement>();

		/// <summary>
		/// The total number of arguments
		/// </summary>
		public readonly int NumArguments;

		/// <summary>
		/// Number of arguments that are required
		/// </summary>
		public readonly int NumRequiredArguments;

		/// <summary>
		/// Maps an argument name to its type
		/// </summary>
		public readonly Dictionary<string, int> ArgumentNameToIndex;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the function</param>
		/// <param name="Element">Element containing the function definition</param>
		/// <param name="ArgumentNameToIndex">Map of argument name to index</param>
		/// <param name="NumRequiredArguments">Number of arguments that are required. Indices 0 to NumRequiredArguments - 1 are required.</param>
		public BgScriptMacro(string Name, BgScriptElement Element, Dictionary<string, int> ArgumentNameToIndex, int NumRequiredArguments)
		{
			this.Name = Name;
			this.Elements.Add(Element);
			this.NumArguments = ArgumentNameToIndex.Count;
			this.NumRequiredArguments = NumRequiredArguments;
			this.ArgumentNameToIndex = ArgumentNameToIndex;
		}
	}

	public interface IBgScriptReaderContext
	{
		/// <summary>
		/// Tests whether the given file or directory exists
		/// </summary>
		/// <param name="File">Path to a file</param>
		/// <returns>True if the file exists</returns>
		Task<bool> ExistsAsync(string Path);

		/// <summary>
		/// Tries to read a file from the given path
		/// </summary>
		/// <param name="Path">Path of the file to read</param>
		/// <returns></returns>
		Task<byte[]?> ReadAsync(string Path);

		/// <summary>
		/// Finds files matching the given pattern
		/// </summary>
		/// <param name="Pattern"></param>
		/// <returns></returns>
		Task<string[]> FindAsync(string Pattern);

		/// <summary>
		/// Converts a path to its native form, for display to the user
		/// </summary>
		/// <param name="Path">Path to format</param>
		/// <returns></returns>
		object GetNativePath(string Path);
	}

	/// <summary>
	/// Extension methods for writing script error messages
	/// </summary>
	public static class BgScriptExtensions
	{
		public static void LogScriptError(this ILogger Logger, BgScriptLocation Location, string Format, params object[] Args)
		{
			object[] AllArgs = new object[Args.Length + 2];
			AllArgs[0] = Location.NativeFile;
			AllArgs[1] = Location.LineNumber;
			Args.CopyTo(AllArgs, 2);
			Logger.LogError($"{{Script}}({{Line}}): error: {Format}", AllArgs);
		}

		public static void LogScriptWarning(this ILogger Logger, BgScriptLocation Location, string Format, params object[] Args)
		{
			object[] AllArgs = new object[Args.Length + 2];
			AllArgs[0] = Location.NativeFile;
			AllArgs[1] = Location.LineNumber;
			Args.CopyTo(AllArgs, 2);
			Logger.LogWarning($"{{Script}}({{Line}}): warning: {Format}", AllArgs);
		}
	}

	/// <summary>
	/// Reader for build graph definitions. Instanced to contain temporary state; public interface is through ScriptReader.TryRead().
	/// </summary>
	public class BgScriptReader
	{
		/// <summary>
		/// Interface used for reading files
		/// </summary>
		IBgScriptReaderContext Context;

		/// <summary>
		/// The current graph
		/// </summary>
		BgGraph Graph = new BgGraph();

		/// <summary>
		/// List of property name to value lookups. Modifications to properties are scoped to nodes and agents. EnterScope() pushes an empty dictionary onto the end of this list, and LeaveScope() removes one. 
		/// ExpandProperties() searches from last to first lookup when trying to resolve a property name, and takes the first it finds.
		/// </summary>
		List<Dictionary<string, string>> ScopedProperties = new List<Dictionary<string, string>>();

		/// <summary>
		/// When declaring a property in a nested scope, we enter its name into a set for each parent scope which prevents redeclaration in an OUTER scope later. Subsequent NESTED scopes can redeclare it.
		/// The former is likely a coding error, since it implies that the scope of the variable was meant to be further out, whereas the latter is common for temporary and loop variables.
		/// </summary>
		List<HashSet<string>> ShadowProperties = new List<HashSet<string>>();

		/// <summary>
		/// Maps from a function name to its definition
		/// </summary>
		Dictionary<string, BgScriptMacro> MacroNameToDefinition = new Dictionary<string, BgScriptMacro>();

		/// <summary>
		/// Preprocess the file, but do not expand any values that are not portable (eg. paths on the local machine)
		/// </summary>
		bool bPreprocessOnly;

		/// <summary>
		/// Schema for the script
		/// </summary>
		BgScriptSchema Schema;

		/// <summary>
		/// Logger for diagnostic messages
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// The number of errors encountered during processing so far
		/// </summary>
		int NumErrors;

		/// <summary>
		/// The name of the node if only a single node is going to be built, otherwise null.
		/// </summary>
		string? SingleNodeName;

		/// <summary>
		/// Private constructor. Use ScriptReader.TryRead() to read a script file.
		/// </summary>
		/// <param name="Context">Context object</param>
		/// <param name="DefaultProperties">Default properties available to the script</param>
		/// <param name="bPreprocessOnly">Preprocess the file, but do not expand any values that are not portable (eg. paths on the local machine)</param>
		/// <param name="Schema">Schema for the script</param>
		/// <param name="Logger">Logger for diagnostic messages</param>
		private BgScriptReader(IBgScriptReaderContext Context, IDictionary<string, string> DefaultProperties, bool bPreprocessOnly, BgScriptSchema Schema, ILogger Logger)
		{
			this.Context = Context;
			this.bPreprocessOnly = bPreprocessOnly;
			this.Schema = Schema;
			this.Logger = Logger;

			EnterScope();

			foreach(KeyValuePair<string, string> Pair in DefaultProperties)
			{
				ScopedProperties[ScopedProperties.Count - 1].Add(Pair.Key, Pair.Value);
			}
		}

		/// <summary>
		/// Try to read a script file from the given file.
		/// </summary>
		/// <param name="Context">Supplies context about the parse</param>
		/// <param name="File">File to read from</param>
		/// <param name="Arguments">Arguments passed in to the graph on the command line</param>
		/// <param name="DefaultProperties">Default properties available to the script</param>
		/// <param name="bPreprocessOnly">Preprocess the file, but do not expand any values that are not portable (eg. paths on the local machine)</param>
		/// <param name="Schema">Schema for the script</param>
		/// <param name="Logger">Logger for output messages</param>
		/// <param name="SingleNodeName">If a single node will be processed, the name of that node.</param>
		/// <returns>True if the graph was read, false if there were errors</returns>
		public static async Task<BgGraph?> ReadAsync(IBgScriptReaderContext Context, string File, Dictionary<string, string> Arguments, Dictionary<string, string> DefaultProperties, bool bPreprocessOnly, BgScriptSchema Schema, ILogger Logger, string? SingleNodeName = null)
		{
			// Read the file and build the graph
			BgScriptReader Reader = new BgScriptReader(Context, DefaultProperties, bPreprocessOnly, Schema, Logger);
			if (!await Reader.TryReadAsync(File, Arguments, Logger, SingleNodeName) || Reader.NumErrors > 0)
			{
				return null;
			}

			// Make sure all the arguments were valid
			HashSet<string> ValidArgumentNames = new HashSet<string>(Reader.Graph.Options.Select(x => x.Name), StringComparer.OrdinalIgnoreCase);
			ValidArgumentNames.Add("PreflightChange");

			foreach(string ArgumentName in Arguments.Keys)
			{
				if (!ValidArgumentNames.Contains(ArgumentName))
				{
					Logger.LogWarning("Unknown argument '{ArgumentName}' for '{Script}'", ArgumentName, Context.GetNativePath(File));
				}
			}

			// Return the constructed graph
			return Reader.Graph;
		}

		/// <summary>
		/// Read the script from the given file
		/// </summary>
		/// <param name="File">File to read from</param>
		/// <param name="Arguments">Arguments passed in to the graph on the command line</param>
		/// <param name="Logger">Logger for output messages</param>
		/// <param name="SingleNodeName">The name of the node if only a single node is going to be built, otherwise null.</param>
		async Task<bool> TryReadAsync(string File, Dictionary<string, string> Arguments, ILogger Logger, string? SingleNodeName = null)
		{
			// Get the data for this file
			byte[]? Data = await Context.ReadAsync(File);
			if (Data == null)
			{
				Logger.LogError("Unable to open file {File}", File);
				NumErrors++;
				return false;
			}

			// Read the document and validate it against the schema
			BgScriptDocument? Document;
			if (!BgScriptDocument.TryRead(File, Context.GetNativePath(File), Data, Schema, Logger, out Document))
			{
				NumErrors++;
				return false;
			}

			// Read the root BuildGraph element
			this.SingleNodeName = SingleNodeName;
			await ReadGraphBodyAsync(Document.DocumentElement, Arguments);
			return true;
		}

		/// <summary>
		/// Reads the contents of a graph
		/// </summary>
		/// <param name="Element">The parent element to read from</param>
		/// <param name="Arguments">Arguments passed in to the graph on the command line</param>
		async Task ReadGraphBodyAsync(XmlElement Element, Dictionary<string, string> Arguments)
		{
			foreach (BgScriptElement ChildElement in Element.ChildNodes.OfType<BgScriptElement>())
			{
				switch (ChildElement.Name)
				{
					case "Include":
						await ReadIncludeAsync(ChildElement, Arguments);
						break;
					case "Option":
						await ReadOptionAsync(ChildElement, Arguments);
						break;
					case "Property":
						await ReadPropertyAsync(ChildElement);
						break;
					case "Regex":
						await ReadRegexAsync(ChildElement);
						break;
					case "EnvVar":
						await ReadEnvVarAsync(ChildElement);
						break;
					case "Macro":
						ReadMacro(ChildElement);
						break;
					case "Extend":
						await ReadExtendAsync(ChildElement);
						break;
					case "Agent":
						await ReadAgentAsync(ChildElement);
						break;
					case "Aggregate":
						await ReadAggregateAsync(ChildElement);
						break;
					case "Report":
						await ReadReportAsync(ChildElement);
						break;
					case "Badge":
						await ReadBadgeAsync(ChildElement);
						break;
					case "Label":
						await ReadLabelAsync(ChildElement);
						break;
					case "Notify":
						await ReadNotifierAsync(ChildElement);
						break;
					case "Trace":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Console, null, null);
						break;
					case "Warning":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Warning, null, null);
						break;
					case "Error":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Error, null, null);
						break;
					case "Do":
						await ReadBlockAsync(ChildElement, x => ReadGraphBodyAsync(x, Arguments));
						break;
					case "Switch":
						await ReadSwitchAsync(ChildElement, x => ReadGraphBodyAsync(x, Arguments));
						break;
					case "ForEach":
						await ReadForEachAsync(ChildElement, x => ReadGraphBodyAsync(x, Arguments));
						break;
					case "Expand":
						await ReadExpandAsync(ChildElement, x => ReadGraphBodyAsync(x, Arguments));
						break;
					default:
						LogError(ChildElement, "Invalid element '{0}'", ChildElement.Name);
						break;
				}
			}
		}

		/// <summary>
		/// Handles validation messages from validating the document against its schema
		/// </summary>
		/// <param name="Sender">The source of the event</param>
		/// <param name="Args">Event arguments</param>
		void ValidationHandler(object Sender, ValidationEventArgs Args)
		{
			if (Args.Severity == XmlSeverityType.Warning)
			{
				Logger.LogWarning("Script: {Message}", Args.Message);
			}
			else
			{
				Logger.LogError("Script: {Message}", Args.Message);
				NumErrors++;
			}
		}

		/// <summary>
		/// Push a new property scope onto the stack
		/// </summary>
		void EnterScope()
		{
			ScopedProperties.Add(new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase));
			ShadowProperties.Add(new HashSet<string>(StringComparer.InvariantCultureIgnoreCase));
		}

		/// <summary>
		/// Pop a property scope from the stack
		/// </summary>
		void LeaveScope()
		{
			ScopedProperties.RemoveAt(ScopedProperties.Count - 1);
			ShadowProperties.RemoveAt(ShadowProperties.Count - 1);
		}

		/// <summary>
		/// Sets a property value in the current scope
		/// </summary>
		/// <param name="Element">Element containing the property assignment. Used for error messages if the property is shadowed in another scope.</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="Value">Value for the property</param>
		void SetPropertyValue(BgScriptElement Element, string Name, string Value)
		{
			// Find the scope containing this property, defaulting to the current scope
			int ScopeIdx = 0;
			while(ScopeIdx < ScopedProperties.Count - 1 && !ScopedProperties[ScopeIdx].ContainsKey(Name))
			{
				ScopeIdx++;
			}

			// Make sure this property name was not already used in a child scope; it likely indicates an error.
			if(ShadowProperties[ScopeIdx].Contains(Name))
			{
				LogError(Element, "Property '{0}' was already used in a child scope. Move this definition before the previous usage if they are intended to share scope, or use a different name.", Name);
			}
			else
			{
				// Make sure it's added to the shadow property list for every parent scope
				for(int Idx = 0; Idx < ScopeIdx; Idx++)
				{
					ShadowProperties[Idx].Add(Name);
				}
				ScopedProperties[ScopeIdx][Name] = Value;
			}
		}

		/// <summary>
		/// Tries to get the value of a property
		/// </summary>
		/// <param name="Name">Name of the property</param>
		/// <param name="Value">On success, contains the value of the property. Set to null otherwise.</param>
		/// <returns>True if the property was found, false otherwise</returns>
		bool TryGetPropertyValue(string Name, out string? Value)
		{
			int ValueLength = 0;
			if (Name.Contains(":"))
			{
				string[] Tokens = Name.Split(':');
				Name = Tokens[0];
				ValueLength = int.Parse(Tokens[1]);
			}

			// Check each scope for the property
			for (int ScopeIdx = ScopedProperties.Count - 1; ScopeIdx >= 0; ScopeIdx--)
			{
				string? ScopeValue;
				if (ScopedProperties[ScopeIdx].TryGetValue(Name, out ScopeValue))
				{
					Value = ScopeValue;

					// It's valid for a property to exist but have a null value. It won't be expanded
					// Handle $(PropName:-6) where PropName might be "Foo"
					if (Value != null && Value.Length > Math.Abs(ValueLength))
					{
						if (ValueLength > 0)
						{
							Value = Value.Substring(0, ValueLength);
						}
						if (ValueLength < 0)
						{
							Value = Value.Substring(Value.Length + ValueLength, -ValueLength);
						}
					}
					return true;
				}
			}

			// If we didn't find it, return false.
			Value = null;
			return false;
		}

		static string CombinePaths(string BasePath, string NextPath)
		{
			List<string> Fragments = new List<string>(BasePath.Split('/'));
			Fragments.RemoveAt(Fragments.Count - 1);

			foreach (string AppendFragment in NextPath.Split('/'))
			{
				if (AppendFragment.Equals(".", StringComparison.Ordinal))
				{
					continue;
				}
				else if (AppendFragment.Equals("..", StringComparison.Ordinal))
				{
					if (Fragments.Count > 0)
					{
						Fragments.RemoveAt(Fragments.Count - 1);
					}
					else
					{
						throw new Exception($"Path '{NextPath}' cannot be combined with '{BasePath}'");
					}
				}
				else
				{
					Fragments.Add(AppendFragment);
				}
			}
			return String.Join('/', Fragments);
		}

		/// <summary>
		/// Read an include directive, and the contents of the target file
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="Arguments">Arguments passed in to the graph on the command line</param>
		async Task ReadIncludeAsync(BgScriptElement Element, Dictionary<string, string> Arguments)
		{
			if (await EvaluateConditionAsync(Element))
			{
				HashSet<string> Files = new HashSet<string>();
				foreach (string Script in ReadListAttribute(Element, "Script"))
				{
					string IncludePath = CombinePaths(Element.Location.File, Script);
					if (Regex.IsMatch(IncludePath, @"\*|\?|\.\.\."))
					{
						Files.UnionWith(await Context.FindAsync(IncludePath));
					}
					else
					{
						Files.Add(IncludePath);
					}
				}

				foreach(string File in Files.OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
				{
					Logger.LogDebug("Including file {File}", File);
					await TryReadAsync(File, Arguments, Logger);
				}
			}
		}

		/// <summary>
		/// Reads the definition of a graph option; a parameter which can be set by the user on the command-line or via an environment variable.
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="Arguments">Arguments passed in to the graph on the command line</param>
		async Task ReadOptionAsync(BgScriptElement Element, IDictionary<string, string> Arguments)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");
				if (ValidateName(Element, Name))
				{
					// Make sure we're at global scope
					if(ScopedProperties.Count > 1)
					{
						throw new Exception("Incorrect scope depth for reading option settings");
					}

					// Check if the property already exists. If it does, we don't need to register it as an option.
					string? ExistingValue;
					if(TryGetPropertyValue(Name, out ExistingValue) && ExistingValue != null)
					{
						// If there's a restriction on this definition, check it matches
						string Restrict = ReadAttribute(Element, "Restrict");
						if(!String.IsNullOrEmpty(Restrict) && !Regex.IsMatch(ExistingValue, "^" + Restrict + "$", RegexOptions.IgnoreCase))
						{
							LogError(Element, "'{0} is already set to '{1}', which does not match the given restriction ('{2}')", Name, ExistingValue, Restrict);
						}
					}
					else
					{
						// Create a new option object to store the settings
						string Description = ReadAttribute(Element, "Description");
						string DefaultValue = ReadAttribute(Element, "DefaultValue");
						BgScriptOption Option = new BgScriptOption(Name, Description, DefaultValue);
						Graph.Options.Add(Option);

						// Get the value of this property
						string? Value;
						if(!Arguments.TryGetValue(Name, out Value))
						{
							Value = Option.DefaultValue;
						}
						SetPropertyValue(Element, Name, Value);

						// If there's a restriction on it, check it's valid
						string Restrict = ReadAttribute(Element, "Restrict");
						if(!String.IsNullOrEmpty(Restrict))
						{
							string Pattern = "^(" + Restrict + ")$";
							if(!Regex.IsMatch(Value, Pattern, RegexOptions.IgnoreCase))
							{
								LogError(Element, "'{0}' is not a valid value for '{1}' (required: '{2}')", Value, Name, Restrict);
							}
							if(Option.DefaultValue != Value && !Regex.IsMatch(Option.DefaultValue, Pattern, RegexOptions.IgnoreCase))
							{
								LogError(Element, "Default value '{0}' is not valid for '{1}' (required: '{2}')", Option.DefaultValue, Name, Restrict);
							}
						}
					}
				}
			}
		}

		/// <summary>
		/// Reads a property assignment.
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadPropertyAsync(BgScriptElement Element)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");
				if (ValidateName(Element, Name))
				{
					string Value = ReadAttribute(Element, "Value");
					if(Element.HasChildNodes)
					{
						// Read the element content, and append each line to the value as a semicolon delimited list
						StringBuilder Builder = new StringBuilder(Value);
						foreach(string Line in Element.InnerText.Split('\n'))
						{
							string TrimLine = ExpandProperties(Element, Line.Trim());
							if(TrimLine.Length > 0)
							{
								if(Builder.Length > 0)
								{
									Builder.Append(";");
								}
								Builder.Append(TrimLine);
							}
						}
						Value = Builder.ToString();
					}
					SetPropertyValue(Element, Name, Value);
				}
			}
		}

		/// <summary>
		/// Reads a Regex assignment.
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadRegexAsync(BgScriptElement Element)
		{
			if (await EvaluateConditionAsync(Element))
			{
				// Get the pattern
				string RegexString = ReadAttribute(Element, "Pattern");

				// Make sure its a valid regex.
				Regex? RegexValue = ParseRegex(Element, RegexString);
				if (RegexValue != null)
				{
					// read the names in 
					string[] CaptureNames = ReadListAttribute(Element, "Capture");

					// get number of groups we passed in
					int[] GroupNumbers = RegexValue.GetGroupNumbers();

					// make sure the number of property names is the same as the number of match groups
					// this includes the entire string match group as [0], so don't count that one.
					if (CaptureNames.Length != GroupNumbers.Count() - 1)
					{
						LogError(Element, "MatchGroup count: {0} does not match the number of names specified: {1}", GroupNumbers.Count() - 1, CaptureNames.Length);
					}
					else
					{
						// apply the regex to the value
						string Input = ReadAttribute(Element, "Input");
						Match Match = RegexValue.Match(Input);

						bool Optional = await BgCondition.EvaluateAsync(ReadAttribute(Element, "Optional"), Context);
						if (!Match.Success)
						{
							if (!Optional)
							{
								LogError(Element, "Regex {0} did not find a match against input string {1}", RegexString, Input);
							}
						}
						else
						{
							// assign each property to the group it matches, skip over [0]
							for (int MatchIdx = 1; MatchIdx < GroupNumbers.Count(); MatchIdx++)
							{
								SetPropertyValue(Element, CaptureNames[MatchIdx - 1], Match.Groups[MatchIdx].Value);
							}
						}
					}
				}
			}
		}

		/// <summary>
		/// Reads a property assignment from an environment variable.
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadEnvVarAsync(BgScriptElement Element)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");
				if (ValidateName(Element, Name))
				{
					string Value = Environment.GetEnvironmentVariable(Name) ?? "";
					SetPropertyValue(Element, Name, Value);
				}
			}
		}

		/// <summary>
		/// Reads a macro definition
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		void ReadMacro(BgScriptElement Element)
		{
			string Name = Element.GetAttribute("Name");
			if (ValidateName(Element, Name))
			{
				BgScriptMacro? OriginalDefinition;
				if(MacroNameToDefinition.TryGetValue(Name, out OriginalDefinition))
				{
					BgScriptLocation Location = OriginalDefinition.Elements[0].Location;
					LogError(Element, "Macro '{0}' has already been declared (see {1} line {2})", Name, Location.File, Location.LineNumber);
				}
				else
				{
					Dictionary<string, int> ArgumentNameToIndex = new Dictionary<string, int>();
					ReadMacroArguments(Element, "Arguments", ArgumentNameToIndex);

					int NumRequiredArguments = ArgumentNameToIndex.Count;
					ReadMacroArguments(Element, "OptionalArguments", ArgumentNameToIndex);

					MacroNameToDefinition.Add(Name, new BgScriptMacro(Name, Element, ArgumentNameToIndex, NumRequiredArguments));
				}
			}
		}

		/// <summary>
		/// Reads a list of macro arguments from an attribute
		/// </summary>
		/// <param name="Element">The element containing the attributes</param>
		/// <param name="AttributeName">Name of the attribute containing the arguments</param>
		/// <param name="ArgumentNameToIndex">List of arguments to add to</param>
		void ReadMacroArguments(BgScriptElement Element, string AttributeName, Dictionary<string, int> ArgumentNameToIndex)
		{
			string AttributeValue = ReadAttribute(Element, AttributeName);
			if(AttributeValue != null)
			{
				foreach(string ArgumentName in AttributeValue.Split(new char[]{ ';' }, StringSplitOptions.RemoveEmptyEntries))
				{
					if(ArgumentNameToIndex.ContainsKey(ArgumentName))
					{
						LogWarning(Element, "Argument '{0}' is listed multiple times", ArgumentName);
					}
					else
					{
						ArgumentNameToIndex.Add(ArgumentName, ArgumentNameToIndex.Count);
					}
				}
			}
		}

		/// <summary>
		/// Reads a macro definition
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadExtendAsync(BgScriptElement Element)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");

				BgScriptMacro? OriginalDefinition;
				if (MacroNameToDefinition.TryGetValue(Name, out OriginalDefinition))
				{
					OriginalDefinition.Elements.Add(Element);
				}
				else
				{
					LogError(Element, "Macro '{0}' has not been declared", Name);
				}
			}
		}

		/// <summary>
		/// Reads the definition for an agent.
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadAgentAsync(BgScriptElement Element)
		{
			string? Name;
			if (await EvaluateConditionAsync(Element) && TryReadObjectName(Element, out Name))
			{
				// Read the valid agent types. This may be omitted if we're continuing an existing agent.
				string[] Types = ReadListAttribute(Element, "Type");

				// Create the agent object, or continue an existing one
				BgAgent? Agent;
				if (Graph.NameToAgent.TryGetValue(Name, out Agent))
				{
					if (Types.Length > 0 && Agent.PossibleTypes.Length > 0)
					{
						if (Types.Length != Agent.PossibleTypes.Length || !Types.SequenceEqual(Agent.PossibleTypes, StringComparer.InvariantCultureIgnoreCase))
						{
							LogError(Element, "Agent types ({0}) were different than previous agent definition with types ({1}). Must either be empty or match exactly.", string.Join(",", Types), string.Join(",", Agent.PossibleTypes));
						}
					}
				}
				else
				{
					if (Types.Length == 0)
					{
						LogError(Element, "Missing type for agent '{0}'", Name);
					}
					Agent = new BgAgent(Name, Types);
					Graph.NameToAgent.Add(Name, Agent);
					Graph.Agents.Add(Agent);
				}

				// Process all the child elements.
				await ReadAgentBodyAsync(Element, Agent);
			}
		}

		/// <summary>
		/// Read the contents of an agent definition
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ParentAgent">The agent to contain the definition</param>
		async Task ReadAgentBodyAsync(BgScriptElement Element, BgAgent ParentAgent)
		{
			EnterScope();
			foreach (BgScriptElement ChildElement in Element.ChildNodes.OfType<BgScriptElement>())
			{
				switch (ChildElement.Name)
				{
					case "Property":
						await ReadPropertyAsync(ChildElement);
						break;
					case "Regex":
						await ReadRegexAsync(ChildElement);
						break;
					case "Node":
						await ReadNodeAsync(ChildElement, ParentAgent);
						break;
					case "Aggregate":
						await ReadAggregateAsync(ChildElement);
						break;
					case "Trace":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Console, null, ParentAgent);
						break;
					case "Warning":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Warning, null, ParentAgent);
						break;
					case "Error":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Error, null, ParentAgent);
						break;
					case "Label":
						await ReadLabelAsync(ChildElement);
						break;
					case "Do":
						await ReadBlockAsync(ChildElement, x => ReadAgentBodyAsync(x, ParentAgent));
						break;
					case "Switch":
						await ReadSwitchAsync(ChildElement, x => ReadAgentBodyAsync(x, ParentAgent));
						break;
					case "ForEach":
						await ReadForEachAsync(ChildElement, x => ReadAgentBodyAsync(x, ParentAgent));
						break;
					case "Expand":
						await ReadExpandAsync(ChildElement, x => ReadAgentBodyAsync(x, ParentAgent));
						break;
					default:
						LogError(ChildElement, "Unexpected element type '{0}'", ChildElement.Name);
						break;
				}
			}
			LeaveScope();
		}

		/// <summary>
		/// Reads the definition for an aggregate
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadAggregateAsync(BgScriptElement Element)
		{
			string? Name;
			if (await EvaluateConditionAsync(Element) && TryReadObjectName(Element, out Name) && CheckNameIsUnique(Element, Name))
			{
				string[] RequiredNames = ReadListAttribute(Element, "Requires");

				BgAggregate NewAggregate = new BgAggregate(Name);
				foreach (BgNode ReferencedNode in ResolveReferences(Element, RequiredNames))
				{
					NewAggregate.RequiredNodes.Add(ReferencedNode);
				}
				Graph.NameToAggregate[Name] = NewAggregate;

				string LabelCategoryName = ReadAttribute(Element, "Label");
				if (!String.IsNullOrEmpty(LabelCategoryName))
				{
					BgLabel Label;

					// Create the label
					int SlashIdx = LabelCategoryName.IndexOf('/');
					if (SlashIdx != -1)
					{
						Label = new BgLabel(LabelCategoryName.Substring(SlashIdx + 1), LabelCategoryName.Substring(0, SlashIdx), null, null, BgLabelChange.Current);
					}
					else
					{
						Label = new BgLabel(LabelCategoryName, "Other", null, null, BgLabelChange.Current);
					}

					// Find all the included nodes
					foreach (BgNode RequiredNode in NewAggregate.RequiredNodes)
					{
						Label.RequiredNodes.Add(RequiredNode);
						Label.IncludedNodes.Add(RequiredNode);
						Label.IncludedNodes.UnionWith(RequiredNode.OrderDependencies);
					}

					string[] IncludedNames = ReadListAttribute(Element, "Include");
					foreach (BgNode IncludedNode in ResolveReferences(Element, IncludedNames))
					{
						Label.IncludedNodes.Add(IncludedNode);
						Label.IncludedNodes.UnionWith(IncludedNode.OrderDependencies);
					}

					string[] ExcludedNames = ReadListAttribute(Element, "Exclude");
					foreach (BgNode ExcludedNode in ResolveReferences(Element, ExcludedNames))
					{
						Label.IncludedNodes.Remove(ExcludedNode);
						Label.IncludedNodes.ExceptWith(ExcludedNode.OrderDependencies);
					}

					Graph.Labels.Add(Label);
				}
			}
		}

		/// <summary>
		/// Reads the definition for a report
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadReportAsync(BgScriptElement Element)
		{
			string? Name;
			if (await EvaluateConditionAsync(Element) && TryReadObjectName(Element, out Name) && CheckNameIsUnique(Element, Name))
			{
				string[] RequiredNames = ReadListAttribute(Element, "Requires");

				BgReport NewReport = new BgReport(Name);
				foreach (BgNode ReferencedNode in ResolveReferences(Element, RequiredNames))
				{
					NewReport.Nodes.Add(ReferencedNode);
					NewReport.Nodes.UnionWith(ReferencedNode.OrderDependencies);
				}
				Graph.NameToReport.Add(Name, NewReport);
			}
		}

		/// <summary>
		/// Reads the definition for a badge
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadBadgeAsync(BgScriptElement Element)
		{
			string? Name;
			if (await EvaluateConditionAsync(Element) && TryReadObjectName(Element, out Name))
			{
				string[] RequiredNames = ReadListAttribute(Element, "Requires");
				string[] TargetNames = ReadListAttribute(Element, "Targets");
				string Project = ReadAttribute(Element, "Project");
				int Change = ReadIntegerAttribute(Element, "Change", 0);

				BgBadge NewBadge = new BgBadge(Name, Project, Change);
				foreach (BgNode ReferencedNode in ResolveReferences(Element, RequiredNames))
				{
					NewBadge.Nodes.Add(ReferencedNode);
				}
				foreach (BgNode ReferencedNode in ResolveReferences(Element, TargetNames))
				{
					NewBadge.Nodes.Add(ReferencedNode);
					NewBadge.Nodes.UnionWith(ReferencedNode.OrderDependencies);
				}
				Graph.Badges.Add(NewBadge);
			}
		}

		/// <summary>
		/// Reads the definition for a label
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadLabelAsync(BgScriptElement Element)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");
				if (!String.IsNullOrEmpty(Name))
				{
					ValidateName(Element, Name);
				}

				string Category = ReadAttribute(Element, "Category");

				string[] RequiredNames = ReadListAttribute(Element, "Requires");
				string[] IncludedNames = ReadListAttribute(Element, "Include");
				string[] ExcludedNames = ReadListAttribute(Element, "Exclude");

				string UgsBadge = ReadAttribute(Element, "UgsBadge");
				string UgsProject = ReadAttribute(Element, "UgsProject");

				BgLabelChange Change = ReadEnumAttribute<BgLabelChange>(Element, "Change", BgLabelChange.Current);

				BgLabel NewLabel = new BgLabel(Name, Category, UgsBadge, UgsProject, Change);
				foreach (BgNode ReferencedNode in ResolveReferences(Element, RequiredNames))
				{
					NewLabel.RequiredNodes.Add(ReferencedNode);
					NewLabel.IncludedNodes.Add(ReferencedNode);
					NewLabel.IncludedNodes.UnionWith(ReferencedNode.OrderDependencies);
				}
				foreach (BgNode IncludedNode in ResolveReferences(Element, IncludedNames))
				{
					NewLabel.IncludedNodes.Add(IncludedNode);
					NewLabel.IncludedNodes.UnionWith(IncludedNode.OrderDependencies);
				}
				foreach (BgNode ExcludedNode in ResolveReferences(Element, ExcludedNames))
				{
					NewLabel.IncludedNodes.Remove(ExcludedNode);
					NewLabel.IncludedNodes.ExceptWith(ExcludedNode.OrderDependencies);
				}
				Graph.Labels.Add(NewLabel);
			}
		}

		/// <summary>
		/// Reads the definition for a node, and adds it to the given agent
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ParentAgent">Agent for the node to be added to</param>
		async Task ReadNodeAsync(BgScriptElement Element, BgAgent ParentAgent)
		{
			string? Name;
			if (await EvaluateConditionAsync(Element) && TryReadObjectName(Element, out Name))
			{
				string[] RequiresNames = ReadListAttribute(Element, "Requires");
				string[] ProducesNames = ReadListAttribute(Element, "Produces");
				string[] AfterNames = ReadListAttribute(Element, "After");
				string[] TokenFileNames = ReadListAttribute(Element, "Token");
				bool bRunEarly = ReadBooleanAttribute(Element, "RunEarly", false);
				bool bNotifyOnWarnings = ReadBooleanAttribute(Element, "NotifyOnWarnings", true);

				// Resolve all the inputs we depend on
				HashSet<BgNodeOutput> Inputs = ResolveInputReferences(Element, RequiresNames);

				// Gather up all the input dependencies, and check they're all upstream of the current node
				HashSet<BgNode> InputDependencies = new HashSet<BgNode>();
				foreach (BgNode InputDependency in Inputs.Select(x => x.ProducingNode).Distinct())
				{
					InputDependencies.Add(InputDependency);
				}

				// Remove all the lock names from the list of required names
				HashSet<FileReference> RequiredTokens = new HashSet<FileReference>(TokenFileNames.Select(x => new FileReference(x)));

				// Recursively include all their dependencies too
				foreach (BgNode InputDependency in InputDependencies.ToArray())
				{
					RequiredTokens.UnionWith(InputDependency.RequiredTokens);
					InputDependencies.UnionWith(InputDependency.InputDependencies);
				}

				// Validate all the outputs
				List<string> ValidOutputNames = new List<string>();
				foreach (string ProducesName in ProducesNames)
				{
					BgNodeOutput? ExistingOutput;
					if(Graph.TagNameToNodeOutput.TryGetValue(ProducesName, out ExistingOutput))
					{
						LogError(Element, "Output tag '{0}' is already generated by node '{1}'", ProducesName, ExistingOutput.ProducingNode.Name);
					}
					else if(!ProducesName.StartsWith("#"))
					{
						LogError(Element, "Output tag names must begin with a '#' character ('{0}')", ProducesName);
					}
					else
					{
						ValidOutputNames.Add(ProducesName);
					}
				}

				// Gather up all the order dependencies
				HashSet<BgNode> OrderDependencies = new HashSet<BgNode>(InputDependencies);
				OrderDependencies.UnionWith(ResolveReferences(Element, AfterNames));

				// Recursively include all their order dependencies too
				foreach (BgNode OrderDependency in OrderDependencies.ToArray())
				{
					OrderDependencies.UnionWith(OrderDependency.OrderDependencies);
				}

				// Check that we're not dependent on anything completing that is declared after the initial declaration of this agent.
				int AgentIdx = Graph.Agents.IndexOf(ParentAgent);
				for (int Idx = AgentIdx + 1; Idx < Graph.Agents.Count; Idx++)
				{
					foreach (BgNode Node in Graph.Agents[Idx].Nodes.Where(x => OrderDependencies.Contains(x)))
					{
						LogError(Element, "Node '{0}' has a dependency on '{1}', which was declared after the initial definition of '{2}'.", Name, Node.Name, ParentAgent.Name);
					}
				}

				// Construct and register the node
				if (CheckNameIsUnique(Element, Name))
				{
					// Add it to the node lookup
					BgNode NewNode = new BgNode(Name, Inputs.ToArray(), ValidOutputNames.ToArray(), InputDependencies.ToArray(), OrderDependencies.ToArray(), RequiredTokens.ToArray());
					NewNode.bRunEarly = bRunEarly;
					NewNode.bNotifyOnWarnings = bNotifyOnWarnings;
					Graph.NameToNode.Add(Name, NewNode);

					// Register all the output tags in the global name table.
					foreach(BgNodeOutput Output in NewNode.Outputs)
					{
						BgNodeOutput? ExistingOutput;
						if(Graph.TagNameToNodeOutput.TryGetValue(Output.TagName, out ExistingOutput))
						{
							LogError(Element, "Node '{0}' already has an output called '{1}'", ExistingOutput.ProducingNode.Name, Output.TagName);
						}
						else
						{
							Graph.TagNameToNodeOutput.Add(Output.TagName, Output);
						}
					}

					// Add all the tasks
					await ReadNodeBodyAsync(Element, NewNode, ParentAgent);

					// Add it to the current agent
					ParentAgent.Nodes.Add(NewNode);
				}
			}
		}

		/// <summary>
		/// Reads the contents of a node element
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="NewNode">The new node that has been created</param>
		/// <param name="ParentAgent">Agent for the node to be added to</param>
		async Task ReadNodeBodyAsync(XmlElement Element, BgNode NewNode, BgAgent ParentAgent)
		{
			EnterScope();
			foreach (BgScriptElement ChildElement in Element.ChildNodes.OfType<BgScriptElement>())
			{
				switch (ChildElement.Name)
				{
					case "Property":
						await ReadPropertyAsync(ChildElement);
						break;
					case "Regex":
						await ReadRegexAsync(ChildElement);
						break;
					case "Trace":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Console, NewNode, ParentAgent);
						break;
					case "Warning":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Warning, NewNode, ParentAgent);
						break;
					case "Error":
						await ReadDiagnosticAsync(ChildElement, LogEventType.Error, NewNode, ParentAgent);
						break;
					case "Do":
						await ReadBlockAsync(ChildElement, x => ReadNodeBodyAsync(x, NewNode, ParentAgent));
						break;
					case "Switch":
						await ReadSwitchAsync(ChildElement, x => ReadNodeBodyAsync(x, NewNode, ParentAgent));
						break;
					case "ForEach":
						await ReadForEachAsync(ChildElement, x => ReadNodeBodyAsync(x, NewNode, ParentAgent));
						break;
					case "Expand":
						await ReadExpandAsync(ChildElement, x => ReadNodeBodyAsync(x, NewNode, ParentAgent));
						break;
					default:
						await ReadTaskAsync(ChildElement, NewNode);
						break;
				}
			}
			LeaveScope();
		}

		/// <summary>
		/// Reads a block element
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ReadContentsAsync">Delegate to read the contents of the element, if the condition evaluates to true</param>
		async Task ReadBlockAsync(BgScriptElement Element, Func<BgScriptElement, Task> ReadContentsAsync)
		{
			if (await EvaluateConditionAsync(Element))
			{
				await ReadContentsAsync(Element);
			}
		}

		/// <summary>
		/// Reads a "Switch" element 
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ReadContentsAsync">Delegate to read the contents of the element, if the condition evaluates to true</param>
		async Task ReadSwitchAsync(BgScriptElement Element, Func<BgScriptElement, Task> ReadContentsAsync)
		{
			foreach (BgScriptElement ChildElement in Element.ChildNodes.OfType<BgScriptElement>())
			{
				if (ChildElement.Name == "Default" || await EvaluateConditionAsync(ChildElement))
				{
					await ReadContentsAsync(ChildElement);
				}
			}
		}

		/// <summary>
		/// Reads a "ForEach" element 
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ReadContentsAsync">Delegate to read the contents of the element, if the condition evaluates to true</param>
		async Task ReadForEachAsync(BgScriptElement Element, Func<BgScriptElement, Task> ReadContentsAsync)
		{
			EnterScope();
			if(await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");
				string Separator = ReadAttribute(Element, "Separator");
				if(Separator.Length > 1)
				{
					LogWarning(Element, "Node {0}'s Separator attribute is more than one character ({1}). Defaulting to ;", Name, Separator);
					Separator = ";";
				}
				if(string.IsNullOrEmpty(Separator))
				{
					Separator = ";";
				}
				if (ValidateName(Element, Name))
				{
					if(ScopedProperties.Any(x => x.ContainsKey(Name)))
					{
						LogError(Element, "Loop variable '{0}' already exists as a local property in an outer scope", Name);
					}
					else
					{
						// Loop through all the values
						string[] Values = ReadListAttribute(Element, "Values", Convert.ToChar(Separator));
						foreach(string Value in Values)
						{
							ScopedProperties[ScopedProperties.Count - 1][Name] = Value;
							await ReadContentsAsync(Element);
						}
					}
				}
			}
			LeaveScope();
		}

		/// <summary>
		/// Reads an "Expand" element 
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ReadContentsAsync">Delegate to read the contents of the element, if the condition evaluates to true</param>
		async Task ReadExpandAsync(BgScriptElement Element, Func<BgScriptElement, Task> ReadContentsAsync)
		{
			if(await EvaluateConditionAsync(Element))
			{
				string Name = ReadAttribute(Element, "Name");
				if (ValidateName(Element, Name))
				{
					BgScriptMacro? Macro;
					if (!MacroNameToDefinition.TryGetValue(Name, out Macro))
					{
						LogError(Element, "Macro '{0}' does not exist", Name);
					}
					else
					{
						// Parse the argument list
						string[] Arguments = new string[Macro.ArgumentNameToIndex.Count];
						foreach (XmlAttribute? Attribute in Element.Attributes)
						{
							if (Attribute != null && Attribute.Name != "Name" && Attribute.Name != "If")
							{
								int Index;
								if (Macro.ArgumentNameToIndex.TryGetValue(Attribute.Name, out Index))
								{
									Arguments[Index] = ExpandProperties(Element, Attribute.Value);
								}
								else
								{
									LogWarning(Element, "Macro '{0}' does not take an argument '{1}'", Name, Attribute.Name);
								}
							}
						}

						// Make sure none of the required arguments are missing
						bool bHasMissingArguments = false;
						for (int Idx = 0; Idx < Macro.NumRequiredArguments; Idx++)
						{
							if (Arguments[Idx] == null)
							{
								LogWarning(Element, "Macro '{0}' is missing argument '{1}'", Macro.Name, Macro.ArgumentNameToIndex.First(x => x.Value == Idx).Key);
								bHasMissingArguments = true;
							}
						}

						// Expand the function
						if (!bHasMissingArguments)
						{
							EnterScope();
							foreach (KeyValuePair<string, int> Pair in Macro.ArgumentNameToIndex)
							{
								ScopedProperties[ScopedProperties.Count - 1][Pair.Key] = Arguments[Pair.Value] ?? "";
							}
							foreach (BgScriptElement MacroElement in Macro.Elements)
							{
								await ReadContentsAsync(MacroElement);
							}
							LeaveScope();
						}
					}
				}
			}
		}

		/// <summary>
		/// Reads a task definition from the given element, and add it to the given list
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="ParentNode">The node which owns this task</param>
		async Task ReadTaskAsync(BgScriptElement Element, BgNode ParentNode)
		{
			// If we're running a single node and this element's parent isn't the single node to run, ignore the error and return.
			if (!string.IsNullOrWhiteSpace(SingleNodeName) && ParentNode.Name != SingleNodeName)
			{
				return;
			}

			if (await EvaluateConditionAsync(Element))
			{
				BgTask Info = new BgTask(Element.Location, Element.Name);
				foreach (XmlAttribute? Attribute in Element.Attributes)
				{
					if (String.Compare(Attribute!.Name, "If", StringComparison.InvariantCultureIgnoreCase) != 0)
					{
						string ExpandedValue = ExpandProperties(Element, Attribute.Value);
						Info.Arguments.Add(Attribute.Name, ExpandedValue);
					}
				}
				ParentNode.Tasks.Add(Info);
			}
		}

		/// <summary>
		/// Reads the definition for an email notifier
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		async Task ReadNotifierAsync(BgScriptElement Element)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string[] TargetNames = ReadListAttribute(Element, "Targets");
				string[] ExceptNames = ReadListAttribute(Element, "Except");
				string[] IndividualNodeNames = ReadListAttribute(Element, "Nodes");
				string[] ReportNames = ReadListAttribute(Element, "Reports");
				string[] Users = ReadListAttribute(Element, "Users");
				string[] Submitters = ReadListAttribute(Element, "Submitters");
				bool? bWarnings = Element.HasAttribute("Warnings") ? (bool?)ReadBooleanAttribute(Element, "Warnings", true) : null;
				bool bAbsolute = Element.HasAttribute("Absolute") ? ReadBooleanAttribute(Element, "Absolute", true) : false;

				// Find the list of targets which are included, and recurse through all their dependencies
				HashSet<BgNode> Nodes = new HashSet<BgNode>();
				if (TargetNames != null)
				{
					HashSet<BgNode> TargetNodes = ResolveReferences(Element, TargetNames);
					foreach (BgNode Node in TargetNodes)
					{
						Nodes.Add(Node);
						Nodes.UnionWith(Node.InputDependencies);
					}
				}

				// Add all the individually referenced nodes
				if (IndividualNodeNames != null)
				{
					HashSet<BgNode> IndividualNodes = ResolveReferences(Element, IndividualNodeNames);
					Nodes.UnionWith(IndividualNodes);
				}

				// Exclude all the exceptions
				if (ExceptNames != null)
				{
					HashSet<BgNode> ExceptNodes = ResolveReferences(Element, ExceptNames);
					Nodes.ExceptWith(ExceptNodes);
				}

				// Update all the referenced nodes with the settings
				foreach (BgNode Node in Nodes)
				{
					if (Users != null)
					{
						if (bAbsolute)
						{
							Node.NotifyUsers = new HashSet<string>(Users);
						}
						else
						{
							Node.NotifyUsers.UnionWith(Users);
						}
					}
					if (Submitters != null)
					{
						if (bAbsolute)
						{
							Node.NotifySubmitters = new HashSet<string>(Submitters);
						}
						else
						{
							Node.NotifySubmitters.UnionWith(Submitters);
						}
					}
					if (bWarnings.HasValue)
					{
						Node.bNotifyOnWarnings = bWarnings.Value;
					}
				}

				// Add the users to the list of reports
				if (ReportNames != null)
				{
					foreach (string ReportName in ReportNames)
					{
						BgReport? Report;
						if (Graph.NameToReport.TryGetValue(ReportName, out Report))
						{
							Report.NotifyUsers.UnionWith(Users);
						}
						else
						{
							LogError(Element, "Report '{0}' has not been defined", ReportName);
						}
					}
				}
			}
		}

		/// <summary>
		/// Reads a warning from the given element, evaluates the condition on it, and writes it to the log if the condition passes.
		/// </summary>
		/// <param name="Element">Xml element to read the definition from</param>
		/// <param name="EventType">The diagnostic event type</param>
		/// <param name="EnclosingNode">The node that this diagnostic is declared in, or null</param>
		/// <param name="EnclosingAgent">The agent that this diagnostic is declared in, or null</param>
		async Task ReadDiagnosticAsync(BgScriptElement Element, LogEventType EventType, BgNode? EnclosingNode, BgAgent? EnclosingAgent)
		{
			if (await EvaluateConditionAsync(Element))
			{
				string Message = ReadAttribute(Element, "Message");

				BgGraphDiagnostic Diagnostic = new BgGraphDiagnostic(Element.Location, EventType, Message, EnclosingNode, EnclosingAgent);
				Graph.Diagnostics.Add(Diagnostic);
			}
		}

		/// <summary>
		/// Checks that the given name does not already used to refer to a node, and print an error if it is.
		/// </summary>
		/// <param name="Element">Xml element to read from</param>
		/// <param name="Name">Name of the alias</param>
		/// <returns>True if the name was registered correctly, false otherwise.</returns>
		bool CheckNameIsUnique(BgScriptElement Element, string Name)
		{
			// Get the nodes that it maps to
			if (Graph.ContainsName(Name))
			{
				LogError(Element, "'{0}' is already defined; cannot add a second time", Name);
				return false;
			}
			return true;
		}

		/// <summary>
		/// Resolve a list of references to a set of nodes
		/// </summary>
		/// <param name="Element">Element used to locate any errors</param>
		/// <param name="ReferenceNames">Sequence of names to look up</param>
		/// <returns>Hashset of all the nodes included by the given names</returns>
		HashSet<BgNode> ResolveReferences(BgScriptElement Element, IEnumerable<string> ReferenceNames)
		{
			HashSet<BgNode> Nodes = new HashSet<BgNode>();
			foreach (string ReferenceName in ReferenceNames)
			{
				BgNode[]? OtherNodes;
				if (Graph.TryResolveReference(ReferenceName, out OtherNodes))
				{
					Nodes.UnionWith(OtherNodes);
				}
				else if (!ReferenceName.StartsWith("#") && Graph.TagNameToNodeOutput.ContainsKey("#" + ReferenceName))
				{
					LogError(Element, "Reference to '{0}' cannot be resolved; did you mean '#{0}'?", ReferenceName);
				}
				else
				{
					LogError(Element, "Reference to '{0}' cannot be resolved; check it has been defined.", ReferenceName);
				}
			}
			return Nodes;
		}

		/// <summary>
		/// Resolve a list of references to a set of nodes
		/// </summary>
		/// <param name="Element">Element used to locate any errors</param>
		/// <param name="ReferenceNames">Sequence of names to look up</param>
		/// <returns>Set of all the nodes included by the given names</returns>
		HashSet<BgNodeOutput> ResolveInputReferences(BgScriptElement Element, IEnumerable<string> ReferenceNames)
		{
			HashSet<BgNodeOutput> Inputs = new HashSet<BgNodeOutput>();
			foreach (string ReferenceName in ReferenceNames)
			{
				BgNodeOutput[]? ReferenceInputs;
				if (Graph.TryResolveInputReference(ReferenceName, out ReferenceInputs))
				{
					Inputs.UnionWith(ReferenceInputs);
				}
				else if (!ReferenceName.StartsWith("#") && Graph.TagNameToNodeOutput.ContainsKey("#" + ReferenceName))
				{
					LogError(Element, "Reference to '{0}' cannot be resolved; did you mean '#{0}'?", ReferenceName);
				}
				else
				{
					LogError(Element, "Reference to '{0}' cannot be resolved; check it has been defined.", ReferenceName);
				}
			}
			return Inputs;
		}

		/// <summary>
		/// Reads an object name from its defining element. Outputs an error if the name is missing.
		/// </summary>
		/// <param name="Element">Element to read the name for</param>
		/// <param name="Name">Output variable to receive the name of the object</param>
		/// <returns>True if the object had a valid name (assigned to the Name variable), false if the name was invalid or missing.</returns>
		bool TryReadObjectName(BgScriptElement Element, [NotNullWhen(true)] out string? Name)
		{
			// Check the name attribute is present
			if (!Element.HasAttribute("Name"))
			{
				LogError(Element, "Missing 'Name' attribute");
				Name = null;
				return false;
			}

			// Get the value of it, strip any leading or trailing whitespace, and make sure it's not empty
			string Value = ReadAttribute(Element, "Name");
			if (!ValidateName(Element, Value))
			{
				Name = null;
				return false;
			}

			// Return it
			Name = Value;
			return true;
		}

		/// <summary>
		/// Checks that the given name is valid syntax
		/// </summary>
		/// <param name="Element">The element that contains the name</param>
		/// <param name="Name">The name to check</param>
		/// <returns>True if the name is valid</returns>
		bool ValidateName(BgScriptElement Element, string Name)
		{
			// Check it's not empty
			if (Name.Length == 0)
			{
				LogError(Element, "Name is empty");
				return false;
			}

			// Check there are no invalid characters
			for (int Idx = 0; Idx < Name.Length; Idx++)
			{
				if (Idx > 0 && Name[Idx] == ' ' && Name[Idx - 1] == ' ')
				{
					LogError(Element, "Consecutive spaces in object name - '{0}'", Name);
					return false;
				}
				if(Char.IsControl(Name[Idx]) || BgScriptSchema.IllegalNameCharacters.IndexOf(Name[Idx]) != -1)
				{
					LogError(Element, "Invalid character in object name - '{0}'", Name[Idx]);
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Constructs a regex from a regex string and returns it
		/// </summary>
		/// <param name="Element">The element that contains the regex</param>
		/// <param name="Regex">The pattern to construct</param>
		/// <returns>The regex if is valid, otherwise null</returns>
		Regex? ParseRegex(BgScriptElement Element, string Regex)
		{
			if(Regex.Length == 0)
			{
				LogError(Element, "Regex is empty");
				return null;
			}
			try
			{
				return new Regex(Regex);
			}
			catch(ArgumentException InvalidRegex)
			{
				LogError(Element, "Could not construct the Regex, reason: {0}", InvalidRegex.Message);
				return null;
			}
		}

		/// <summary>
		/// Expands any properties and reads an attribute.
		/// </summary>
		/// <param name="Element">Element to read the attribute from</param>
		/// <param name="Name">Name of the attribute</param>
		/// <returns>Array of names, with all leading and trailing whitespace removed</returns>
		string ReadAttribute(BgScriptElement Element, string Name)
		{
			return ExpandProperties(Element, Element.GetAttribute(Name));
		}

		/// <summary>
		/// Expands any properties and reads a list of strings from an attribute, separated by semi-colon characters
		/// </summary>
		/// <param name="Element"></param>
		/// <param name="Name"></param>
		/// <param name="Separator"></param>
		/// <returns>Array of names, with all leading and trailing whitespace removed</returns>
		string[] ReadListAttribute(BgScriptElement Element, string Name, char Separator = ';')
		{
			string Value = ReadAttribute(Element, Name);
			return Value.Split(new char[] { Separator }).Select(x => x.Trim()).Where(x => x.Length > 0).ToArray();
		}

		/// <summary>
		/// Reads an attribute from the given XML element, expands any properties in it, and parses it as a boolean.
		/// </summary>
		/// <param name="Element">Element to read the attribute from</param>
		/// <param name="Name">Name of the attribute</param>
		/// <param name="bDefaultValue">Default value if the attribute is missing</param>
		/// <returns>The value of the attribute field</returns>
		bool ReadBooleanAttribute(BgScriptElement Element, string Name, bool bDefaultValue)
		{
			bool bResult = bDefaultValue;
			if (Element.HasAttribute(Name))
			{
				string Value = ReadAttribute(Element, Name).Trim();
				if (Value.Equals("true", StringComparison.InvariantCultureIgnoreCase))
				{
					bResult = true;
				}
				else if (Value.Equals("false", StringComparison.InvariantCultureIgnoreCase))
				{
					bResult = false;
				}
				else
				{
					LogError(Element, "Invalid boolean value '{0}' - expected 'true' or 'false'", Value);
				}
			}
			return bResult;
		}

		/// <summary>
		/// Reads an attribute from the given XML element, expands any properties in it, and parses it as an integer.
		/// </summary>
		/// <param name="Element">Element to read the attribute from</param>
		/// <param name="Name">Name of the attribute</param>
		/// <param name="DefaultValue">Default value for the integer, if the attribute is missing</param>
		/// <returns>The value of the attribute field</returns>
		int ReadIntegerAttribute(BgScriptElement Element, string Name, int DefaultValue)
		{
			int Result = DefaultValue;
			if (Element.HasAttribute(Name))
			{
				string Value = ReadAttribute(Element, Name).Trim();

				int IntValue;
				if (Int32.TryParse(Value, out IntValue))
				{
					Result = IntValue;
				}
				else
				{
					LogError(Element, "Invalid integer value '{0}'", Value);
				}
			}
			return Result;
		}

		/// <summary>
		/// Reads an attribute from the given XML element, expands any properties in it, and parses it as an enum of the given type.
		/// </summary>
		/// <typeparam name="T">The enum type to parse the attribute as</typeparam>
		/// <param name="Element">Element to read the attribute from</param>
		/// <param name="Name">Name of the attribute</param>
		/// <param name="DefaultValue">Default value for the enum, if the attribute is missing</param>
		/// <returns>The value of the attribute field</returns>
		T ReadEnumAttribute<T>(BgScriptElement Element, string Name, T DefaultValue) where T : struct
		{
			T Result = DefaultValue;
			if (Element.HasAttribute(Name))
			{
				string Value = ReadAttribute(Element, Name).Trim();

				T EnumValue;
				if (Enum.TryParse(Value, true, out EnumValue))
				{
					Result = EnumValue;
				}
				else
				{
					LogError(Element, "Invalid value '{0}' - expected {1}", Value, String.Join("/", Enum.GetNames(typeof(T))));
				}
			}
			return Result;
		}

		/// <summary>
		/// Outputs an error message to the log and increments the number of errors, referencing the file and line number of the element that caused it.
		/// </summary>
		/// <param name="Element">The script element causing the error</param>
		/// <param name="Format">Standard String.Format()-style format string</param>
		/// <param name="Args">Optional arguments</param>
		void LogError(BgScriptElement Element, string Format, params object[] Args)
		{
			Logger.LogScriptError(Element.Location, Format, Args);
			NumErrors++;
		}

		/// <summary>
		/// Outputs a warning message to the log and increments the number of errors, referencing the file and line number of the element that caused it.
		/// </summary>
		/// <param name="Element">The script element causing the error</param>
		/// <param name="Format">Standard String.Format()-style format string</param>
		/// <param name="Args">Optional arguments</param>
		void LogWarning(BgScriptElement Element, string Format, params object[] Args)
		{
			Logger.LogScriptWarning(Element.Location, Format, Args);
		}

		/// <summary>
		/// Evaluates the (optional) conditional expression on a given XML element via the If="..." attribute, and returns true if the element is enabled.
		/// </summary>
		/// <param name="Element">The element to check</param>
		/// <returns>True if the element's condition evaluates to true (or doesn't have a conditional expression), false otherwise</returns>
		async Task<bool> EvaluateConditionAsync(BgScriptElement Element)
		{
			// Check if the element has a conditional attribute
			const string AttributeName = "If";
			if (!Element.HasAttribute(AttributeName))
			{
				return true;
			}

			// If it does, try to evaluate it.
			try
			{
				string Text = ExpandProperties(Element, Element.GetAttribute("If"));
				return await BgCondition.EvaluateAsync(Text, Context);
			}
			catch (BgConditionException Ex)
			{
				LogError(Element, "Error in condition: {0}", Ex.Message);
				return false;
			}
		}

		/// <summary>
		/// Expand all the property references (of the form $(PropertyName)) in a string.
		/// </summary>
		/// <param name="Element">The element containing the string. Used for diagnostic messages.</param>
		/// <param name="Text">The input string to expand properties in</param>
		/// <returns>The expanded string</returns>
		string ExpandProperties(BgScriptElement Element, string Text)
		{
			string Result = Text;
			// Iterate in reverse order to handle cases where there are nested expansions like $(Outer$(Inner))
			for (int Idx = Result.LastIndexOf("$("); Idx != -1; Idx = Result.LastIndexOf("$(", Idx, Idx+1))
			{
				// Find the end of the variable name
				int EndIdx = Result.IndexOf(')', Idx + 2);
				if (EndIdx == -1)
				{
					break;
				}

				// Extract the variable name from the string
				string Name = Result.Substring(Idx + 2, EndIdx - (Idx + 2));

				// Find the value for it, either from the dictionary or the environment block
				string? Value;
				if (!TryGetPropertyValue(Name, out Value))
				{
					LogWarning(Element, "Property '{0}' is not defined", Name);
					Value = "";
				}

				// Check if we've got a value for this variable
				if (Value != null)
				{
					// Replace the variable, or skip past it
					Result = Result.Substring(0, Idx) + Value + Result.Substring(EndIdx + 1);
				}
			}
			return Result;
		}
	}
}