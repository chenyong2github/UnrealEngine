// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System;
using System.Text.Json.Serialization;
using UnrealBuildBase;

namespace EpicGames.UHT.Types
{
	[UhtEngineClass(Name = "Package")]
	public class UhtPackage : UhtObject
	{
		public readonly int PackageTypeIndex;
		public EPackageFlags PackageFlags { get; internal set; } = EPackageFlags.None;
		public UHTManifest.Module Module { get; internal set; }

		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.Package;

		[JsonIgnore]
		public override UhtPackage Package => this;

		[JsonIgnore]
		public override UhtHeaderFile HeaderFile => throw new NotImplementedException();

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

		[JsonIgnore]
		public bool bIsPlugin => this.Module.BaseDirectory.Replace('\\', '/').Contains("/Plugins/");


		public string ShortName;

		/// <inheritdoc/>
		public override string EngineClassName { get => "Package"; }

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
