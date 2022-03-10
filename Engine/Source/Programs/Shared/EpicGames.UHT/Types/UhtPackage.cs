// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System;
using System.Text.Json.Serialization;
using UnrealBuildBase;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents a UPackage in the engine
	/// </summary>
	[UhtEngineClass(Name = "Package")]
	public class UhtPackage : UhtObject
	{

		/// <summary>
		/// Unique index of the package
		/// </summary>
		public readonly int PackageTypeIndex;

		/// <summary>
		/// Engine package flags
		/// </summary>
		public EPackageFlags PackageFlags { get; set; } = EPackageFlags.None;

		/// <summary>
		/// UHT module of the package (1 to 1 relationship)
		/// </summary>
		public UHTManifest.Module Module { get; set; }

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.Package;

		/// <inheritdoc/>
		public override string EngineClassName { get => "Package"; }

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtPackage Package => this;

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtHeaderFile HeaderFile => throw new NotImplementedException();

		/// <summary>
		/// True if the package is part of the engine
		/// </summary>
		[JsonIgnore]
		public bool bIsPartOfEngine
		{
			get
			{
				switch (this.Module.ModuleType)
				{
					case UHTModuleType.Program:
						return this.Module.BaseDirectory.Replace('\\', '/').StartsWith(Unreal.EngineDirectory.FullName.Replace('\\', '/'));
					case UHTModuleType.EngineRuntime:
					case UHTModuleType.EngineUncooked:
					case UHTModuleType.EngineDeveloper:
					case UHTModuleType.EngineEditor:
					case UHTModuleType.EngineThirdParty:
						return true;
					case UHTModuleType.GameRuntime:
					case UHTModuleType.GameUncooked:
					case UHTModuleType.GameDeveloper:
					case UHTModuleType.GameEditor:
					case UHTModuleType.GameThirdParty:
						return false;
					default:
						throw new UhtIceException("Invalid module type");
				}
			}
		}

		/// <summary>
		/// True if the package is a plugin
		/// </summary>
		[JsonIgnore]
		public bool bIsPlugin => this.Module.BaseDirectory.Replace('\\', '/').Contains("/Plugins/");

		/// <summary>
		/// Short name of the package (without the /Script/)
		/// </summary>
		public string ShortName;

		/// <summary>
		/// Construct a new instance of a package
		/// </summary>
		/// <param name="Session">Running session</param>
		/// <param name="Module">Source module of the package</param>
		/// <param name="PackageFlags">Assorted package flags</param>
		public UhtPackage(UhtSession Session, UHTManifest.Module Module, EPackageFlags PackageFlags) : base(Session)
		{
			this.Module = Module;
			this.PackageFlags = PackageFlags;
			this.PackageTypeIndex = this.Session.GetNextPackageTypeIndex();

			int LastSlashIndex = Module.Name.LastIndexOf('/');
			if (LastSlashIndex == -1)
			{
				this.SourceName = $"/Script/{Module.Name}";
				this.ShortName = Module.Name;
			}
			else
			{
				this.SourceName = Module.Name;
				this.ShortName = this.SourceName.Substring(LastSlashIndex + 1);
			}
		}
	}
}
