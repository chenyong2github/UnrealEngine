// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents the different directories where headers can appear
	/// </summary>
	public enum UhtHeaderFileType
	{

		/// <summary>
		/// Classes folder
		/// </summary>
		Classes,

		/// <summary>
		/// Public folder
		/// </summary>
		Public,

		/// <summary>
		/// Internal folder
		/// </summary>
		Internal,

		/// <summary>
		/// Private folder
		/// </summary>
		Private,
	}

	/// <summary>
	/// Series of flags not part of the engine's class flags that affect code generation or verification
	/// </summary>
	[Flags]
	public enum UhtHeaderFileExportFlags : Int32
	{
		/// <summary>
		/// No export flags
		/// </summary>
		None = 0,

		/// <summary>
		/// This header is being included by another header
		/// </summary>
		Referenced = 0x00000001, 
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtHeaderFileExportFlagsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtHeaderFileExportFlags InFlags, UhtHeaderFileExportFlags TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtHeaderFileExportFlags InFlags, UhtHeaderFileExportFlags TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtHeaderFileExportFlags InFlags, UhtHeaderFileExportFlags TestFlags, UhtHeaderFileExportFlags MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Represents a header file.  Unlike the engine, UhtHeader files will appear as a child 
	/// of a UhtPackage and the outer of all global types found in that header.
	/// </summary>
	public class UhtHeaderFile : UhtType
	{
		private readonly UhtSimpleMessageSite MessageSite;
		private readonly UhtSourceFile SourceFile;
		private readonly List<UhtHeaderFile> ReferencedHeaders = new List<UhtHeaderFile>();

		/// <summary>
		/// Contents of the header
		/// </summary>
		[JsonIgnore]
		public StringView Data { get => this.SourceFile.Data; }

		/// <summary>
		/// Path of the header
		/// </summary>
		[JsonIgnore]
		public string FilePath { get => this.SourceFile.FilePath; }

		/// <summary>
		/// File name without the extension
		/// </summary>
		public string FileNameWithoutExtension { get; }

		/// <summary>
		/// Required name for the generated.h file name.  Used to validate parsed code
		/// </summary>
		public string GeneratedHeaderFileName { get; }

		/// <summary>
		/// True if this header is NoExportTypes.h
		/// </summary>
		public bool bIsNoExportTypes { get; }

		/// <summary>
		/// The file path of the header relative to the module location
		/// </summary>
		public string ModuleRelativeFilePath { get; set; } = string.Empty;

		/// <summary>
		/// Include file path added as meta data to the types
		/// </summary>
		public string IncludeFilePath { get; set; } = string.Empty;

		/// <summary>
		/// Location where the header file was found
		/// </summary>
		public UhtHeaderFileType HeaderFileType { get; set; } = UhtHeaderFileType.Private;

		/// <summary>
		/// Unique index of the header file
		/// </summary>
		public int HeaderFileTypeIndex { get; }

		/// <summary>
		/// UHT flags for the header
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtHeaderFileExportFlags HeaderFileExportFlags { get; set; } = UhtHeaderFileExportFlags.None;

		/// <summary>
		/// If true, the header file should be exported
		/// </summary>
		[JsonIgnore]
		public bool bShouldExport => this.HeaderFileExportFlags.HasAnyFlags(UhtHeaderFileExportFlags.Referenced) || this.Children.Count > 0;

		/// <summary>
		/// Resource collector for the header file
		/// </summary>
		[JsonIgnore]
		public UhtReferenceCollector References { get; } = new UhtReferenceCollector();

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtPackage Package
		{
			get
			{
				if (this.Outer == null)
				{
					throw new UhtIceException("Attempt to fetch header file package but it has no outer");
				}
				return (UhtPackage)this.Outer;
			}
		}

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtHeaderFile HeaderFile { get => this; }

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.Header;

		/// <inheritdoc/>
		public override string EngineClassName { get => "UhtHeaderFile"; }

		/// <summary>
		/// Collection of headers directly included by this header
		/// </summary>
		[JsonIgnore]
		public List<UhtHeaderFile> IncludedHeaders { get; } = new List<UhtHeaderFile>();

		#region IUHTMessageSite implementation

		/// <inheritdoc/>
		[JsonIgnore]
		public override IUhtMessageSession MessageSession => this.MessageSite.MessageSession;

		/// <inheritdoc/>
		[JsonIgnore]
		public override IUhtMessageSource? MessageSource => this.MessageSite.MessageSource;
		#endregion

		/// <summary>
		/// Construct a new header file
		/// </summary>
		/// <param name="Package">Owning package</param>
		/// <param name="Path">Path to the header file</param>
		public UhtHeaderFile(UhtPackage Package, string Path) : base(Package, 1)
		{
			this.HeaderFileTypeIndex = this.Session.GetNextHeaderFileTypeIndex();
			this.MessageSite = new UhtSimpleMessageSite(this.Session);
			this.SourceFile = new UhtSourceFile(this.Session, Path);
			this.MessageSite.MessageSource = this.SourceFile;
			this.FileNameWithoutExtension = System.IO.Path.GetFileNameWithoutExtension(this.SourceFile.FilePath);
			this.GeneratedHeaderFileName = this.FileNameWithoutExtension + ".generated.h";
			this.SourceName = System.IO.Path.GetFileName(this.SourceFile.FilePath);
			this.bIsNoExportTypes = string.Equals(this.SourceFile.FileName, "NoExportTypes", StringComparison.OrdinalIgnoreCase);
		}

		/// <summary>
		/// Read the contents of the header
		/// </summary>
		public void Read()
		{
			this.SourceFile.Read();
		}

		/// <summary>
		/// Add a reference to the given header
		/// </summary>
		/// <param name="Id">Path of the header</param>
		/// <param name="bIsIncludedFile">True if this is a directly included file</param>
		public void AddReferencedHeader(string Id, bool bIsIncludedFile)
		{
			UhtHeaderFile? HeaderFile = this.Session.FindHeaderFile(Path.GetFileName(Id));
			if (HeaderFile != null)
			{
				AddReferencedHeader(HeaderFile, bIsIncludedFile);
			}
		}

		/// <summary>
		/// Add a reference to the header that defines the given type
		/// </summary>
		/// <param name="Type">Type in question</param>
		public void AddReferencedHeader(UhtType Type)
		{
			AddReferencedHeader(Type.HeaderFile, false);
		}

		/// <summary>
		/// Add a reference to the given header file
		/// </summary>
		/// <param name="HeaderFile">Header file in question</param>
		/// <param name="bIsIncludedFile">True if this is a directly included file</param>
		public void AddReferencedHeader(UhtHeaderFile HeaderFile, bool bIsIncludedFile)
		{
			lock (this.ReferencedHeaders)
			{
				// Check for a duplicate
				foreach (UhtHeaderFile Ref in this.ReferencedHeaders)
				{
					if (Ref == HeaderFile)
					{
						return;
					}
				}

				// There is questionable compatibility hack where a source file will always be exported
				// regardless of having types when it is being included by the SAME package.
				if (HeaderFile.Package == this.Package)
				{
					HeaderFile.HeaderFileExportFlags |= UhtHeaderFileExportFlags.Referenced;
				}
				this.ReferencedHeaders.Add(HeaderFile);
				if (bIsIncludedFile)
				{
					this.IncludedHeaders.Add(HeaderFile);
				}
			}
		}

		/// <summary>
		/// Return an enumerator without locking.  This method can only be utilized AFTER all header parsing is complete. 
		/// </summary>
		[JsonIgnore]
		public IEnumerable<UhtHeaderFile> ReferencedHeadersNoLock
		{
			get
			{
				return this.ReferencedHeaders;
			}
		}

		/// <summary>
		/// Return an enumerator of all current referenced headers under a lock.  This should be used during parsing.
		/// </summary>
		[JsonIgnore]
		public IEnumerable<UhtHeaderFile> ReferencedHeadersLocked
		{
			get
			{
				lock (this.ReferencedHeaders)
				{
					foreach (UhtHeaderFile Ref in this.ReferencedHeaders)
					{
						yield return Ref;
					}
				}
			}
		}

		/// <inheritdoc/>
		public override void AppendPathName(StringBuilder Builder, UhtType? StopOuter = null)
		{
			// Headers do not contribute to path names
			if (this != StopOuter && this.Outer != null)
			{
				this.Outer.AppendPathName(Builder, StopOuter);
			}
		}

		/// <inheritdoc/>
		protected override UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			Options = base.Validate(Options | UhtValidationOptions.Shadowing);

			Dictionary<int, UhtFunction> UsedRPCIds = new Dictionary<int, UhtFunction>();
			Dictionary<int, UhtFunction> RPCsNeedingHookup = new Dictionary<int, UhtFunction>();
			foreach (UhtType Type in this.Children)
			{
				if (Type is UhtClass Class)
				{
					foreach (UhtType Child in Class.Children)
					{
						if (Child is UhtFunction Function)
						{
							if (Function.FunctionType != UhtFunctionType.Function || !Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
							{
								continue;
							}

							if (Function.RPCId > 0)
							{
								UhtFunction? ExistingFunc;
								if (UsedRPCIds.TryGetValue(Function.RPCId, out ExistingFunc))
								{
									Function.LogError($"Function {ExistingFunc.SourceName} already uses identifier {Function.RPCId}");
								}
								else
								{
									UsedRPCIds.Add(Function.RPCId, Function);
								}

								if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetResponse))
								{
									// Look for another function expecting this response
									RPCsNeedingHookup.Remove(Function.RPCId);
								}
							}

							if (Function.RPCResponseId > 0 && Function.EndpointName != "JSBridge")
							{
								// Look for an existing response function, if not found then add to the list of ids awaiting hookup
								if (!UsedRPCIds.ContainsKey(Function.RPCResponseId))
								{
									RPCsNeedingHookup.Add(Function.RPCResponseId, Function);
								}
							}
						}
					}
				}
			}

			if (RPCsNeedingHookup.Count > 0)
			{
				foreach (KeyValuePair<int, UhtFunction> KVP in RPCsNeedingHookup)
				{
					KVP.Value.LogError($"Request function '{KVP.Value.SourceName}' is missing a response function with the id of '{KVP.Key}'");
				}
			}
			return Options;
		}
	}
}
