// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using UnrealBuildBase;

namespace UnrealBuildTool.Modes
{

	/// <summary>
	/// Implement the UHT configuration interface.  Due to the configuration system being fairly embedded into
	/// UBT, the implementation must be part of UBT.
	/// </summary>
	public class UhtConfigImpl : IUhtConfig
	{
		private readonly ConfigHierarchy Ini;

		/// <summary>
		/// Types that have been renamed, treat the old deprecated name as the new name for code generation
		/// </summary>
		private readonly IReadOnlyDictionary<StringView, StringView> TypeRedirectMap;

		/// <summary>
		/// Metadata that have been renamed, treat the old deprecated name as the new name for code generation
		/// </summary>
		private readonly IReadOnlyDictionary<string, string> MetaDataRedirectMap;

		/// <summary>
		/// Supported units in the game
		/// </summary>
		private readonly ReadOnlyHashSet<StringView> Units;

		/// <summary>
		/// Special parsed struct names that do not require a prefix
		/// </summary>
		private readonly ReadOnlyHashSet<StringView> StructsWithNoPrefix;

		/// <summary>
		/// Special parsed struct names that have a 'T' prefix
		/// </summary>
		private readonly ReadOnlyHashSet<StringView> StructsWithTPrefix;

		/// <summary>
		/// Mapping from 'human-readable' macro substring to # of parameters for delegate declarations
		/// Index 0 is 1 parameter, Index 1 is 2, etc...
		/// </summary>
		private readonly IReadOnlyList<StringView> DelegateParameterCountStrings;

		/// <summary>
		/// Default version of generated code. Defaults to oldest possible, unless specified otherwise in config.
		/// </summary>
		private readonly EGeneratedCodeVersion DefaultGeneratedCodeVersionInternal = EGeneratedCodeVersion.V1;

		/// <summary>
		/// Internal version of pointer warning for native pointers in the engine
		/// </summary>
		private readonly UhtPointerMemberBehavior EngineNativePointerMemberBehaviorInternal = UhtPointerMemberBehavior.AllowSilently;

		/// <summary>
		/// Internal version of pointer warning for object pointers in the engine
		/// </summary>
		private readonly UhtPointerMemberBehavior EngineObjectPtrMemberBehaviorInternal = UhtPointerMemberBehavior.AllowSilently;

		/// <summary>
		/// Internal version of pointer warning for native pointers outside the engine
		/// </summary>
		private readonly UhtPointerMemberBehavior NonEngineNativePointerMemberBehaviorInternal = UhtPointerMemberBehavior.AllowSilently;

		/// <summary>
		/// Internal version of pointer warning for object pointers outside the engine
		/// </summary>
		private readonly UhtPointerMemberBehavior NonEngineObjectPtrMemberBehaviorInternal = UhtPointerMemberBehavior.AllowSilently;

		/// <summary>
		/// If true setters and getters will be automatically (without specifying their function names on a property) parsed and generated if a function with matching signature is found
		/// </summary>
		private readonly bool bAllowAutomaticSettersAndGettersInternal;

		#region IUhtConfig Implementation
		/// <inheritdoc/>
		public EGeneratedCodeVersion DefaultGeneratedCodeVersion => this.DefaultGeneratedCodeVersionInternal;

		/// <inheritdoc/>
		public UhtPointerMemberBehavior EngineNativePointerMemberBehavior => this.EngineNativePointerMemberBehaviorInternal;

		/// <inheritdoc/>
		public UhtPointerMemberBehavior EngineObjectPtrMemberBehavior => this.EngineObjectPtrMemberBehaviorInternal;

		/// <inheritdoc/>
		public UhtPointerMemberBehavior NonEngineNativePointerMemberBehavior => this.NonEngineNativePointerMemberBehaviorInternal;

		/// <inheritdoc/>
		public UhtPointerMemberBehavior NonEngineObjectPtrMemberBehavior => this.NonEngineObjectPtrMemberBehaviorInternal;

		/// <inheritdoc/>
		public bool bAllowAutomaticSettersAndGetters => this.bAllowAutomaticSettersAndGettersInternal;

		/// <inheritdoc/>
		public void RedirectTypeIdentifier(ref UhtToken Token)
		{
			if (!Token.IsIdentifier())
			{
				throw new Exception("Attempt to redirect type identifier when the token isn't an identifier.");
			}

			StringView Redirect;
			if (this.TypeRedirectMap.TryGetValue(Token.Value, out Redirect))
			{
				Token.Value = Redirect;
			}
		}

		/// <inheritdoc/>
		public bool RedirectMetaDataKey(string Key, out string NewKey)
		{
			string? Redirect;
			if (this.MetaDataRedirectMap.TryGetValue(Key, out Redirect))
			{
				NewKey = Redirect;
				return Key != NewKey;
			}
			else
			{
				NewKey = Key;
				return false;
			}
		}

		/// <inheritdoc/>
		public bool IsValidUnits(StringView Units)
		{
			return this.Units.Contains(Units);
		}

		/// <inheritdoc/>
		public bool IsStructWithTPrefix(StringView Name)
		{
			return this.StructsWithTPrefix.Contains(Name);
		}

		/// <inheritdoc/>
		public int FindDelegateParameterCount(StringView DelegateMacro)
		{
			IReadOnlyList<StringView> DelegateParameterCountStrings = this.DelegateParameterCountStrings;
			for (int Index = 0, Count = this.DelegateParameterCountStrings.Count; Index < Count; ++Index)
			{
				if (DelegateMacro.Span.Contains(this.DelegateParameterCountStrings[Index].Span, StringComparison.Ordinal))
				{
					return Index;
				}
			}
			return -1;
		}

		/// <inheritdoc/>
		public StringView GetDelegateParameterCountString(int Index)
		{
			return Index >= 0 ? this.DelegateParameterCountStrings[Index] : "";
		}

		/// <inheritdoc/>
		public bool IsExporterEnabled(string Name)
		{
			bool Value = false;
			this.Ini.GetBool("UnrealHeaderTool", Name, out Value);
			return Value;
		}
		#endregion

		/// <summary>
		/// Read the UHT configuration
		/// </summary>
		public static void Read(CommandLineArguments Args)
		{
			UhtConfigImpl Instance = new UhtConfigImpl(Args);
			UhtConfig.Instance = Instance;
		}

		private UhtConfigImpl(CommandLineArguments Args)
		{
			DirectoryReference ConfigDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Programs/UnrealBuildTool");
			this.Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, ConfigDirectory, BuildHostPlatform.Current.Platform, "", Args.GetRawArray());

			this.bAllowAutomaticSettersAndGettersInternal = GetBoolean("UnrealHeaderTool", "AutomaticSettersAndGetters", false);
			this.TypeRedirectMap = GetRedirectsStringView("UnrealHeaderTool", "TypeRedirects", "OldType", "NewType");
			this.MetaDataRedirectMap = GetRedirectsString("CoreUObject.Metadata", "MetadataRedirects", "OldKey", "NewKey");
			this.StructsWithNoPrefix = GetHashSet("UnrealHeaderTool", "StructsWithNoPrefix", StringViewComparer.Ordinal);
			this.StructsWithTPrefix = GetHashSet("UnrealHeaderTool", "StructsWithTPrefix", StringViewComparer.Ordinal);
			this.Units = GetHashSet("UnrealHeaderTool", "Units", StringViewComparer.OrdinalIgnoreCase);
			this.DelegateParameterCountStrings = GetList("UnrealHeaderTool", "DelegateParameterCountStrings");
			this.DefaultGeneratedCodeVersionInternal = GetGeneratedCodeVersion("UnrealHeaderTool", "DefaultGeneratedCodeVersion", EGeneratedCodeVersion.V1);
			this.EngineNativePointerMemberBehaviorInternal = GetPointerMemberBehavior("UnrealHeaderTool", "EngineNativePointerMemberBehavior", UhtPointerMemberBehavior.AllowSilently);
			this.EngineObjectPtrMemberBehaviorInternal = GetPointerMemberBehavior("UnrealHeaderTool", "EngineObjectPtrMemberBehavior", UhtPointerMemberBehavior.AllowSilently);
			this.NonEngineNativePointerMemberBehaviorInternal = GetPointerMemberBehavior("UnrealHeaderTool", "NonEngineNativePointerMemberBehavior", UhtPointerMemberBehavior.AllowSilently);
			this.NonEngineObjectPtrMemberBehaviorInternal = GetPointerMemberBehavior("UnrealHeaderTool", "NonEngineObjectPtrMemberBehavior", UhtPointerMemberBehavior.AllowSilently);
		}

		private bool GetBoolean(string SectionName, string KeyName, bool bDefault)
		{
			bool bValue;
			if (this.Ini.TryGetValue(SectionName, KeyName, out bValue))
			{
				return bValue;
			}
			return bDefault;
		}

		private UhtPointerMemberBehavior GetPointerMemberBehavior(string SectionName, string KeyName, UhtPointerMemberBehavior Default)
		{
			string? BehaviorStr;
			if (this.Ini.TryGetValue(SectionName, KeyName, out BehaviorStr))
			{
				UhtPointerMemberBehavior Value;
				if (!Enum.TryParse(BehaviorStr, out Value))
				{
					throw new Exception(string.Format("Unrecognized native pointer member behavior '{0}'", BehaviorStr));
				}
				return Value;
			}
			return Default;
		}

		private EGeneratedCodeVersion GetGeneratedCodeVersion(string SectionName, string KeyName, EGeneratedCodeVersion Default)
		{
			string? BehaviorStr;
			if (this.Ini.TryGetValue(SectionName, KeyName, out BehaviorStr))
			{
				EGeneratedCodeVersion Value;
				if (!Enum.TryParse(BehaviorStr, out Value))
				{
					throw new Exception(string.Format("Unrecognized generated code version '{0}'", BehaviorStr));
				}
				return Value;
			}
			return Default;
		}

		private IReadOnlyDictionary<StringView, StringView> GetRedirectsStringView(string Section, string Key, string OldKeyName, string NewKeyName)
		{
			Dictionary<StringView, StringView> Redirects = new Dictionary<StringView, StringView>();

			IReadOnlyList<string>? StringList;
			if (this.Ini.TryGetValues(Section, Key, out StringList))
			{
				foreach (string Line in StringList)
				{
					Dictionary<string, string>? Properties;
					if (ConfigHierarchy.TryParse(Line, out Properties))
					{
						string? OldKey;
						if (!Properties.TryGetValue(OldKeyName, out OldKey))
						{
							throw new Exception(string.Format("Unable to get the {0} from the {1} value", OldKeyName, Key));
						}
						string? NewKey;
						if (!Properties.TryGetValue(NewKeyName, out NewKey))
						{
							throw new Exception(string.Format("Unable to get the {0} from the {1} value", NewKeyName, Key));
						}
						Redirects.Add(OldKey, NewKey);
					}
				}
			}
			return Redirects;
		}

		private IReadOnlyDictionary<string, string> GetRedirectsString(string Section, string Key, string OldKeyName, string NewKeyName)
		{
			Dictionary<string, string> Redirects = new Dictionary<string, string>();

			IReadOnlyList<string>? StringList;
			if (this.Ini.TryGetValues(Section, Key, out StringList))
			{
				foreach (string Line in StringList)
				{
					Dictionary<string, string>? Properties;
					if (ConfigHierarchy.TryParse(Line, out Properties))
					{
						string? OldKey;
						if (!Properties.TryGetValue(OldKeyName, out OldKey))
						{
							throw new Exception(string.Format("Unable to get the {0} from the {1} value", OldKeyName, Key));
						}
						string? NewKey;
						if (!Properties.TryGetValue(NewKeyName, out NewKey))
						{
							throw new Exception(string.Format("Unable to get the {0} from the {1} value", NewKeyName, Key));
						}
						Redirects.Add(OldKey, NewKey);
					}
				}
			}
			return Redirects;
		}

		private IReadOnlyList<StringView> GetList(string Section, string Key)
		{
			List<StringView> List = new List<StringView>();

			IReadOnlyList<string>? StringList;
			if (this.Ini.TryGetValues(Section, Key, out StringList))
			{
				foreach (string Value in StringList)
				{
					List.Add(new StringView(Value));
				}
			}
			return List;
		}

		private ReadOnlyHashSet<StringView> GetHashSet(string Section, string Key, StringViewComparer Comparer)
		{
			HashSet<StringView> Set = new HashSet<StringView>(Comparer);

			IReadOnlyList<string>? StringList;
			if (this.Ini.TryGetValues(Section, Key, out StringList))
			{
				foreach (string Value in StringList)
				{
					Set.Add(new StringView(Value));
				}
			}
			return Set;
		}
	}

	/// <summary>
	/// Global options for UBT (any modes)
	/// </summary>
	class UhtGlobalOptions
	{
		/// <summary>
		/// User asked for help
		/// </summary>
		[CommandLine(Prefix = "-Help", Description = "Display this help.")]
		[CommandLine(Prefix = "-h")]
		[CommandLine(Prefix = "--help")]
		public bool bGetHelp = false;

		/// <summary>
		/// The amount of detail to write to the log
		/// </summary>
		[CommandLine(Prefix = "-Verbose", Value = "Verbose", Description = "Increase output verbosity")]
		[CommandLine(Prefix = "-VeryVerbose", Value = "VeryVerbose", Description = "Increase output verbosity more")]
		public LogEventType LogOutputLevel = LogEventType.Log;

		/// <summary>
		/// Specifies the path to a log file to write. Note that the default mode (eg. building, generating project files) will create a log file by default if this not specified.
		/// </summary>
		[CommandLine(Prefix = "-Log", Description = "Specify a log file location instead of the default Engine/Programs/UnrealBuildTool/Log_UHT.txt")]
		public FileReference? LogFileName = null;

		/// <summary>
		/// Whether to include timestamps in the log
		/// </summary>
		[CommandLine(Prefix = "-Timestamps", Description = "Include timestamps in the log")]
		public bool bLogTimestamps = false;

		/// <summary>
		/// Whether to format messages in MsBuild format
		/// </summary>
		[CommandLine(Prefix = "-FromMsBuild", Description = "Format messages for msbuild")]
		public bool bLogFromMsBuild = false;

		/// <summary>
		/// Disables all logging including the default log location
		/// </summary>
		[CommandLine(Prefix = "-NoLog", Description = "Disable log file creation including the default log file")]
		public bool bNoLog = false;

		[CommandLine(Prefix = "-Test", Description = "Run testing scripts")]
		public bool bTest = false;

		[CommandLine("-WarningsAsErrors", Description = "Treat warnings as errors")]
		public bool bWarningsAsErrors = false;

		[CommandLine("-NoGoWide", Description = "Disable concurrent parsing and code generation")]
		public bool bNoGoWide = false;

		[CommandLine("-WriteRef", Description = "Write all the output to a reference directory")]
		public bool bWriteRef = false;

		[CommandLine("-VerifyRef", Description = "Write all the output to a verification directory and compare to the reference output")]
		public bool bVerifyRef = false;

		[CommandLine("-FailIfGeneratedCodeChanges", Description = "Consider any changes to output files as being an error")]
		public bool bFailIfGeneratedCodeChanges = false;

		[CommandLine("-NoOutput", Description = "Do not save any output files other than reference output")]
		public bool bNoOutput = false;

		[CommandLine("-IncludeDebugOutput", Description = "Include extra content in generated output to assist with debugging")]
		public bool bIncludeDebugOutput = false;

		/// <summary>
		/// Initialize the options with the given command line arguments
		/// </summary>
		/// <param name="Arguments"></param>
		public UhtGlobalOptions(CommandLineArguments Arguments)
		{
			Arguments.ApplyTo(this);
		}
	}

	/// <summary>
	/// File manager for the test harness
	/// </summary>
	public class UhtTestFileManager : IUhtFileManager
	{
		/// <summary>
		/// Collection of test fragments that can be read
		/// </summary>
		public Dictionary<string, UhtSourceFragment> SourceFragments = new Dictionary<string, UhtSourceFragment>();

		/// <summary>
		/// All output segments generated by code gen
		/// </summary>
		public SortedDictionary<string, string> Outputs = new SortedDictionary<string, string>();

		private readonly IUhtFileManager InnerManager;
		private readonly string? RootDirectory;

		/// <summary>
		/// Construct a new instance of the test file manager
		/// </summary>
		/// <param name="RootDirectory">Root directory of the UE</param>
		public UhtTestFileManager(string RootDirectory)
		{
			this.RootDirectory = RootDirectory;
			this.InnerManager = new UhtStdFileManager();
		}

		/// <inheritdoc/>
		public string GetFullFilePath(string FilePath)
		{
			if (this.RootDirectory == null)
			{
				return FilePath;
			}
			else
			{
				return Path.Combine(this.RootDirectory, FilePath);
			}
		}

		/// <inheritdoc/>
		public bool ReadSource(string FilePath, out UhtSourceFragment Fragment)
		{
			if (this.SourceFragments.TryGetValue(FilePath, out Fragment))
			{
				return true;
			}

			return InnerManager.ReadSource(GetFullFilePath(FilePath), out Fragment);
		}

		/// <inheritdoc/>
		public UhtBuffer? ReadOutput(string FilePath)
		{
			return null;
		}

		/// <inheritdoc/>
		public bool WriteOutput(string FilePath, ReadOnlySpan<char> Contents)
		{
			lock (this.Outputs)
			{
				this.Outputs.Add(FilePath, Contents.ToString());
			}
			return true;
		}

		/// <inheritdoc/>
		public bool RenameOutput(string OldFilePath, string NewFilePath)
		{
			lock (this.Outputs)
			{
				if (this.Outputs.TryGetValue(OldFilePath, out string? Content))
				{
					this.Outputs.Remove(OldFilePath);
					this.Outputs.Add(NewFilePath, Content);
				}
			}
			return true;
		}

		/// <summary>
		/// Add a source file fragment to the session.  When requests are made to read sources, the 
		/// fragment list will be searched first.
		/// </summary>
		/// <param name="SourceFile">Source file</param>
		/// <param name="FilePath">The relative path to add</param>
		/// <param name="LineNumber">Starting line number</param>
		/// <param name="Data">The data associated with the path</param>
		public void AddSourceFragment(UhtSourceFile SourceFile, string FilePath, int LineNumber, StringView Data)
		{
			this.SourceFragments.Add(FilePath, new UhtSourceFragment { SourceFile = SourceFile, FilePath = FilePath, LineNumber = LineNumber, Data = Data });
		}
	}

	/// <summary>
	/// Testing harness to run the test scripts
	/// </summary>
	class UhtTestHarness
	{
		private enum ScriptFragmentType
		{
			Unknown,
			Manifest,
			Header,
			Console,
			Output,
		}

		private struct ScriptFragment
		{
			public ScriptFragmentType Type;
			public string Name;
			public int LineNumber;
			public StringView Header;
			public StringView Body;
			public bool External;
		}

		private static bool RunScriptTest(UhtGlobalOptions Options, string TestDirectory, string TestOutputDirectory, string Script)
		{
			string InPath = Path.Combine(TestDirectory, Script);
			string OutPath = Path.Combine(TestOutputDirectory, Script);

			UhtTestFileManager TestFileManager = new UhtTestFileManager(TestDirectory);
			UhtSession Session = new UhtSession
			{
				FileManager = TestFileManager,
				RootDirectory = TestDirectory,
				bWarningsAsErrors = Options.bWarningsAsErrors,
				bRelativePathInLog = true,
				bGoWide = !Options.bNoGoWide,
				bNoOutput = false,
				bCullOutput = false,
				bCacheMessages = true,
				bIncludeDebugOutput = true,
			};

			// Read the testing script
			List<ScriptFragment> ScriptFragments = new List<ScriptFragment>();
			int ManifestIndex = -1;
			int ConsoleIndex = -1;
			UhtSourceFile ScriptSourceFile = new UhtSourceFile(Session, Script);
			Dictionary<string, int> OutputFragments = new Dictionary<string, int>();
			Session.Try(ScriptSourceFile, () =>
			{
				ScriptSourceFile.Read();
				UhtTokenBufferReader Reader = new UhtTokenBufferReader(ScriptSourceFile, ScriptSourceFile.Data.Memory);

				bool bDone = false;
				while (!bDone)
				{

					// Scan for the fragment header
					ScriptFragmentType Type = ScriptFragmentType.Unknown;
					string Name = "";
					int HeaderStartPos = Reader.InputPos;
					int HeaderEndPos = HeaderStartPos;
					int LineNumber = 1;
					while (true)
					{
						using (var SaveState = new UhtTokenSaveState(Reader))
						{
							UhtToken Token = Reader.GetLine();
							if (Token.TokenType == UhtTokenType.EndOfFile)
							{
								break;
							}
							if (Token.Value.Span.Length == 0 || (Token.Value.Span.Length > 0 && Token.Value.Span[0] != '!'))
							{
								break;
							}
							HeaderEndPos = Reader.InputPos;

							int EndCommandPos = Token.Value.Span.IndexOf(' ');
							if (EndCommandPos == -1)
							{
								EndCommandPos = Token.Value.Span.Length;
							}
							string ScriptFragmentTypeString = Token.Value.Span.Slice(1, EndCommandPos - 1).Trim().ToString();

							if (!System.Enum.TryParse<ScriptFragmentType>(ScriptFragmentTypeString, true, out Type))
							{
								continue;
							}
							if (Type == ScriptFragmentType.Unknown)
							{
								continue;
							}

							Name = Token.Value.Span.Slice(EndCommandPos).Trim().ToString();
							LineNumber = Token.InputLine;
							SaveState.AbandonState();
							break;
						}
					}

					// Scan for the fragment body
					int BodyStartPos = Reader.InputPos;
					int BodyEndPos = BodyStartPos;
					while (true)
					{
						using (var SaveState = new UhtTokenSaveState(Reader))
						{
							UhtToken Token = Reader.GetLine();
							if (Token.TokenType == UhtTokenType.EndOfFile)
							{
								bDone = true;
								break;
							}
							if (Token.Value.Span.Length > 0 && Token.Value.Span[0] == '!')
							{
								break;
							}
							BodyEndPos = Reader.InputPos;
							SaveState.AbandonState();
						}
					}

					ScriptFragments.Add(new ScriptFragment
					{
						Type = Type,
						Name = Name.Replace("\\\\", "\\"), // Be kind to people cut/copy/paste escaped strings around
						LineNumber = LineNumber,
						Header = new StringView(ScriptSourceFile.Data.Memory.Slice(HeaderStartPos, HeaderEndPos - HeaderStartPos)),
						Body = new StringView(ScriptSourceFile.Data.Memory.Slice(BodyStartPos, BodyEndPos - BodyStartPos)),
						External = false,
					});
				}

				// Search for the manifest and any output.  Add fragments to the session
				for (int i = 0; i < ScriptFragments.Count; ++i)
				{
					switch (ScriptFragments[i].Type)
					{
						case ScriptFragmentType.Manifest:
							if (ManifestIndex != -1)
							{
								ScriptSourceFile.LogError(ScriptFragments[i].LineNumber, "There can be only one manifest section in a test script");
								break;
							}
							ManifestIndex = i;
							if (ScriptFragments[i].Name.Length == 0)
							{
								ScriptSourceFile.LogError(ScriptFragments[i].LineNumber, "Manifest name can not be blank");
								break;
							}
							TestFileManager.AddSourceFragment(ScriptSourceFile, ScriptFragments[i].Name, ScriptFragments[i].LineNumber, ScriptFragments[i].Body);
							break;

						case ScriptFragmentType.Console:
							if (ConsoleIndex != -1)
							{
								ScriptSourceFile.LogError(ScriptFragments[i].LineNumber, "There can be only one console section in a test script");
								break;
							}
							ConsoleIndex = i;
							break;

						case ScriptFragmentType.Header:
							if (ScriptFragments[i].Name.Length == 0)
							{
								ScriptSourceFile.LogError(ScriptFragments[i].LineNumber, "Header name can not be blank");
								break;
							}
							if (ScriptFragments[i].Body.Length == 0)
							{
								// Read the NoExportTypes.h file from the engine source so we don't have to keep a copy
								if (Path.GetFileName(ScriptFragments[i].Name).Equals("NoExportTypes.h", StringComparison.OrdinalIgnoreCase))
								{
									string ExternalPath = Path.Combine(Unreal.EngineDirectory.FullName, ScriptFragments[i].Name);
									if (File.Exists(ExternalPath))
									{
										ScriptFragment Copy = ScriptFragments[i];
										Copy.Body = new StringView(File.ReadAllText(ExternalPath));
										Copy.External = true;
										ScriptFragments[i] = Copy;
									}
								}
							}
							TestFileManager.AddSourceFragment(ScriptSourceFile, ScriptFragments[i].Name, ScriptFragments[i].LineNumber, ScriptFragments[i].Body);
							break;

						case ScriptFragmentType.Output:
							OutputFragments.Add(ScriptFragments[i].Name, i);
							break;
					}
				}

				if (ManifestIndex == -1)
				{
					ScriptSourceFile.LogError("There must be a manifest section in a test script");
				}

				if (ConsoleIndex == -1)
				{
					ScriptSourceFile.LogError("There must be a console section in a test script");
				}
			});

			// Run UHT
			if (!Session.bHasErrors)
			{
				Session.Run(ScriptFragments[ManifestIndex].Name);
			}

			// If we have no console index, then there is nothing we can do.  This is a fatal error than can not be tested
			bool bSuccess = true;
			if (ConsoleIndex == -1)
			{
				ScriptSourceFile.LogError("Unable to do any verification without a console section");
				Session.LogMessages();
				File.Copy(InPath, OutPath, true);
				bSuccess = false;
			}
			else
			{

				// Generate the console block
				List<string> ConsoleLines = Session.CollectMessages();
				StringBuilder SBConsole = new StringBuilder();
				foreach (string Line in ConsoleLines)
				{
					SBConsole.AppendLine(Line);
				}

				// Verify the console block 
				// We trim the ends because it is too easy to leave off the ending CRLF in the script file.
				if (ScriptFragments[ConsoleIndex].Body.ToString().TrimEnd() != SBConsole.ToString().TrimEnd())
				{
					Log.TraceError("Console output failed to match");
					bSuccess = false;
				}

				// Check the output
				foreach (KeyValuePair<string, string> KVP in TestFileManager.Outputs)
				{
					if (OutputFragments.TryGetValue(KVP.Key, out int Index))
					{
						if (ScriptFragments[Index].Body.ToString().TrimEnd() != KVP.Value.TrimEnd())
						{
							Log.TraceError($"Output \"{KVP.Key}\" failed to match");
							bSuccess = false;
						}
						OutputFragments.Remove(KVP.Key);
					}
					else
					{
						Log.TraceError($"Output \"{KVP.Key}\" not found in test script");
					}
				}
				foreach (KeyValuePair<string, int> KVP in OutputFragments)
				{
					Log.TraceError($"Output \"{KVP.Key}\" in test script but not generated");
				}

				// Create the complete output.  Output includes all of the source fragments and console fragments
				// and followed the output data sorted by file name.
				StringBuilder SBTest = new StringBuilder();
				for (int i = 0; i < ScriptFragments.Count; ++i)
				{
					if (ScriptFragments[i].Type != ScriptFragmentType.Output)
					{
						SBTest.Append(ScriptFragments[i].Header);
						if (i == ConsoleIndex)
						{
							SBTest.Append(SBConsole);
						}
						else if (!ScriptFragments[i].External)
						{
							SBTest.Append(ScriptFragments[i].Body);
						}
					}
				}

				// Add the output
				foreach (KeyValuePair<string, string> KVP in TestFileManager.Outputs)
				{
					SBTest.Append($"!output {KVP.Key}\r\n");
					SBTest.Append(KVP.Value);
				}

				// Write the final content
				try
				{
					File.WriteAllText(OutPath, SBTest.ToString());
				}
				catch (Exception E)
				{
					Log.TraceError("Unable to write test result to \"{0}\"", E.Message);
				}
			}

			if (bSuccess)
			{
				Log.TraceInformation("Test {0} succeeded", InPath);
			}
			else
			{
				Log.TraceError("Test {0} failed", InPath);
			}
			return bSuccess;
		}

		private static bool RunManifestTests(UhtGlobalOptions Options, string TestDirectory, string TestOutputDirectory, List<string> Manifests)
		{
			return true;
		}

		private static bool RunScriptTests(UhtGlobalOptions Options, string TestDirectory, string TestOutputDirectory, List<string> Scripts)
		{
			bool bResult = true;
			foreach (string Script in Scripts)
			{
				bResult &= RunScriptTest(Options, TestDirectory, TestOutputDirectory, Script);
			}
			return bResult;
		}

		private static bool RunDirectoryTests(UhtGlobalOptions Options, string TestDirectory, string TestOutputDirectory, List<string> Directories)
		{
			bool bResult = true;
			foreach (string Directory in Directories)
			{
				bResult &= RunTests(Options, Path.Combine(TestDirectory, Directory), Path.Combine(TestOutputDirectory, Directory));
			}
			return bResult;
		}

		private static bool RunTests(UhtGlobalOptions Options, string TestDirectory, string TestOutputDirectory)
		{
			// Create output directory
			Directory.CreateDirectory(TestOutputDirectory);

			List<string> Scripts = new List<string>();
			foreach (string Script in Directory.EnumerateFiles(TestDirectory, "*.uhttest"))
			{
				Scripts.Add(Path.GetFileName(Script));
			}
			Scripts.Sort(StringComparer.OrdinalIgnoreCase);

			List<string> Directories = new List<string>();
			foreach (string Directory in Directory.EnumerateDirectories(TestDirectory))
			{
				Directories.Add(Path.GetFileName(Directory));
			}
			Directories.Sort(StringComparer.OrdinalIgnoreCase);

			List<string> Manifests = new List<string>();
			foreach (string Manifest in Directory.EnumerateFiles(TestDirectory, "*.uhtmanifest"))
			{
				Manifests.Add(Path.GetFileName(Manifest));
			}
			Manifests.Sort(StringComparer.OrdinalIgnoreCase);

			return
				RunManifestTests(Options, TestDirectory, TestOutputDirectory, Manifests) &&
				RunScriptTests(Options, TestDirectory, TestOutputDirectory, Scripts) &&
				RunDirectoryTests(Options, TestDirectory, TestOutputDirectory, Directories);
		}

		public static bool RunTests(UhtGlobalOptions Options)
		{
			DirectoryReference EngineSourceProgramDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Source", "Programs");
			string TestDirectory = FileReference.Combine(EngineSourceProgramDirectory, "UnrealBuildTool.Tests", "UHT").FullName;
			string TestOutputDirectory = FileReference.Combine(EngineSourceProgramDirectory, "UnrealBuildTool.Tests", "UHT.Out").FullName;

			// Clear the output directory
			try
			{
				Directory.Delete(TestOutputDirectory, true);
			}
			catch (Exception)
			{ }

			// Collect a list of all the test scripts
			Log.TraceInformation("Running tests in {0}", TestDirectory);
			Log.TraceInformation("Output can be compared in {0}", TestOutputDirectory);

			// Run the tests on the directory
			return RunTests(Options, TestDirectory, TestOutputDirectory);
		}
	}

	/// <summary>
	/// Invoke UHT
	/// </summary>
	[ToolMode("UnrealHeaderTool", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms | ToolModeOptions.SingleInstance | ToolModeOptions.ShowExecutionTime)]
	class UnrealHeaderToolMode : ToolMode
	{
		/// <summary>
		/// Directory for saved application settings (typically Engine/Programs)
		/// </summary>
		static DirectoryReference? CachedEngineProgramSavedDirectory;

		/// <summary>
		/// The engine programs directory
		/// </summary>
		public static DirectoryReference EngineProgramSavedDirectory
		{
			get
			{
				if (CachedEngineProgramSavedDirectory == null)
				{
					if (Unreal.IsEngineInstalled())
					{
						CachedEngineProgramSavedDirectory = Utils.GetUserSettingDirectory() ?? DirectoryReference.Combine(Unreal.EngineDirectory, "Programs");
					}
					else
					{
						CachedEngineProgramSavedDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Programs");
					}
				}
				return CachedEngineProgramSavedDirectory;
			}
		}

		/// <summary>
		/// Print (incomplete) usage information
		/// </summary>
		private static void PrintUsage()
		{
			Console.WriteLine("UnrealBuildTool -Mode=UnrealHeaderTool [ProjectFile ManifestFile] -OR [\"-Target...\"] [Options]");
			Console.WriteLine("");
			Console.WriteLine("Options:");
			int LongestPrefix = 0;
			foreach (FieldInfo Info in typeof(UhtGlobalOptions).GetFields())
			{
				foreach (CommandLineAttribute Att in Info.GetCustomAttributes<CommandLineAttribute>())
				{
					if (Att.Prefix != null && Att.Description != null)
					{
						LongestPrefix = Att.Prefix.Length > LongestPrefix ? Att.Prefix.Length : LongestPrefix;
					}
				}
			}

			foreach (UhtExporter Generator in UhtExporterTable.Instance)
			{
				LongestPrefix = Generator.Name.Length + 2 > LongestPrefix ? Generator.Name.Length + 2 : LongestPrefix;
			}

			foreach (FieldInfo Info in typeof(UhtGlobalOptions).GetFields())
			{
				foreach (CommandLineAttribute Att in Info.GetCustomAttributes<CommandLineAttribute>())
				{
					if (Att.Prefix != null && Att.Description != null)
					{
						Console.WriteLine($"  {Att.Prefix.PadRight(LongestPrefix)} :  {Att.Description}");
					}
				}
			}

			Console.WriteLine("");
			Console.WriteLine("Generators: Prefix with 'no' to disable a generator");
			foreach (UhtExporter Generator in UhtExporterTable.Instance)
			{
				string IsDefault = UhtConfig.Instance.IsExporterEnabled(Generator.Name) || Generator.Options.HasAnyFlags(UhtExporterOptions.Default) ? " (Default)" : "";
				Console.WriteLine($"  -{Generator.Name.PadRight(LongestPrefix)} :  {Generator.Description}{IsDefault}");
			}
			Console.WriteLine("");
		}

		/// <summary>
		/// Execute the command
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <returns>Exit code</returns>
		public override int Execute(CommandLineArguments Arguments)
		{
			try
			{

				// Initialize the attributes
				UhtAttributeScanner.Scan();

				// Initialize the config
				UhtConfigImpl.Read(Arguments);

				// Parse the global options
				UhtGlobalOptions Options = new UhtGlobalOptions(Arguments);
				int TargetArgumentIndex = -1;
				if (Arguments.GetPositionalArgumentCount() == 0)
				{
					for (int Index = 0; Index < Arguments.Count; ++Index)
					{
						if (Arguments[Index].StartsWith("-Target", StringComparison.OrdinalIgnoreCase))
						{
							TargetArgumentIndex = Index;
							break;
						}
					}
				}
				int RequiredArgCount = TargetArgumentIndex >= 0 ? 0 : 2;
				if (Arguments.GetPositionalArgumentCount() != RequiredArgCount || Options.bGetHelp)
				{
					PrintUsage();
					return Options.bGetHelp ? (int)CompilationResult.Succeeded : (int)CompilationResult.OtherCompilationError;
				}

				// Configure the log system
				Log.OutputLevel = Options.LogOutputLevel;
				Log.IncludeTimestamps = Options.bLogTimestamps;
				Log.IncludeProgramNameWithSeverityPrefix = Options.bLogFromMsBuild;

				// Add the log writer if requested. When building a target, we'll create the writer for the default log file later.
				if (!Options.bNoLog)
				{
					if (Options.LogFileName != null)
					{
						Log.AddFileWriter("LogTraceListener", Options.LogFileName);
					}

					if (!Log.HasFileWriter())
					{
						string BaseLogFileName = FileReference.Combine(EngineProgramSavedDirectory, "UnrealBuildTool", "Log_UHT.txt").FullName;

						FileReference LogFile = new FileReference(BaseLogFileName);
						foreach (string LogSuffix in Arguments.GetValues("-LogSuffix="))
						{
							LogFile = LogFile.ChangeExtension(null) + "_" + LogSuffix + LogFile.GetExtension();
						}

						Log.AddFileWriter("DefaultLogTraceListener", LogFile);
					}
				}

				// If we are running test scripts
				if (Options.bTest)
				{
					return UhtTestHarness.RunTests(Options) ? (int)CompilationResult.Succeeded : (int)CompilationResult.OtherCompilationError;
				}

				string? ProjectPath = null;
				string? ManifestPath = null;

				if (TargetArgumentIndex >= 0)
				{
					CommandLineArguments LocalArguments = new CommandLineArguments(new string[] { Arguments[TargetArgumentIndex] });
					List<TargetDescriptor> TargetDescriptors = TargetDescriptor.ParseCommandLine(LocalArguments, false, false, false);
					if (TargetDescriptors.Count == 0)
					{
						Log.TraceError("No target descriptors found.");
						return (int)CompilationResult.OtherCompilationError;
					}

					TargetDescriptor TargetDesc = TargetDescriptors[0];

					// Create the target
					UEBuildTarget Target = UEBuildTarget.Create(TargetDesc, false, false, false);

					// Create the makefile for the target and export the module information
					using (ISourceFileWorkingSet WorkingSet = new EmptySourceFileWorkingSet())
					{
						// Create the build configuration object, and read the settings
						BuildConfiguration BuildConfiguration = new BuildConfiguration();
						XmlConfig.ApplyTo(BuildConfiguration);
						Arguments.ApplyTo(BuildConfiguration);

						// Create the makefile
						TargetMakefile Makefile = Target.Build(BuildConfiguration, WorkingSet, TargetDesc, true);

						FileReference ModuleInfoFileName = ExternalExecution.GetUHTModuleInfoFileName(Makefile, Target.TargetName);
						FileReference DepsFileName = ExternalExecution.GetUHTDepsFileName(ModuleInfoFileName);
						ManifestPath = ModuleInfoFileName.FullName;
						ExternalExecution.WriteUHTManifest(Makefile, Target.TargetName, ModuleInfoFileName, DepsFileName);

						if (Target.ProjectFile != null)
						{
							ProjectPath = Path.GetDirectoryName(Target.ProjectFile.FullName);
						}
					}
				}
				else
				{
					ProjectPath = Path.GetDirectoryName(Arguments.GetPositionalArguments()[0]);
					ManifestPath = Arguments.GetPositionalArguments()[1];
				}

				UhtSession Session = new UhtSession
				{
					FileManager = new UhtStdFileManager(),
					EngineDirectory = Unreal.EngineDirectory.FullName,
					ProjectDirectory = string.IsNullOrEmpty(ProjectPath) ? null : ProjectPath,
					ReferenceDirectory = FileReference.Combine(EngineProgramSavedDirectory, "UnrealBuildTool", "ReferenceGeneratedCode").FullName,
					VerifyDirectory = FileReference.Combine(EngineProgramSavedDirectory, "UnrealBuildTool", "VerifyGeneratedCode").FullName,
					bWarningsAsErrors = Options.bWarningsAsErrors,
					bGoWide = !Options.bNoGoWide,
					bFailIfGeneratedCodeChanges = Options.bFailIfGeneratedCodeChanges,
					bNoOutput = Options.bNoOutput,
					bIncludeDebugOutput = Options.bIncludeDebugOutput,
				};

				if (Options.bWriteRef)
				{
					Session.ReferenceMode = UhtReferenceMode.Reference;
				}
				else if (Options.bVerifyRef)
				{
					Session.ReferenceMode = UhtReferenceMode.Verify;
				}

				foreach(UhtExporter Exporter in UhtExporterTable.Instance)
				{
					if (Arguments.HasOption($"-{Exporter.Name}"))
					{
						Session.SetExporterStatus(Exporter.Name, true);
					}
					else if (Arguments.HasOption($"-no{Exporter.Name}"))
					{
						Session.SetExporterStatus(Exporter.Name, false);
					}
				}

				// Read and parse
				Session.Run(ManifestPath!);
				Session.LogMessages();
				return (int)(Session.bHasErrors ? CompilationResult.OtherCompilationError : CompilationResult.Succeeded);
			}
			catch (Exception Ex)
			{
				// Unhandled exception.
				Log.TraceError("Unhandled exception: {0}", ExceptionUtils.FormatException(Ex));
				Log.TraceLog(ExceptionUtils.FormatExceptionDetails(Ex));
				return (int)CompilationResult.OtherCompilationError;
			}
		}
	}
}
