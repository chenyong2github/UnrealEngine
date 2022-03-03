// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading.Tasks;

namespace EpicGames.UHT.Exporters.Json
{
	[UnrealHeaderTool]
	internal class UhtJsonExporter
	{

		[UhtExporter(Name = "Json", Description = "Json description of packages", Options = UhtExporterOptions.None)]
		public static void JsonExporter(IUhtExportFactory Factory)
		{
			new UhtJsonExporter(Factory).Export();
		}

		public readonly IUhtExportFactory Factory;
		public UhtSession Session => Factory.Session;
		public UhtExportOptions Options => Factory.Options;

		private UhtJsonExporter(IUhtExportFactory Factory)
		{
			this.Factory = Factory;
		}

		private void Export()
		{
			// Generate the files for the packages
			List<IUhtExportTask> GeneratedPackages = new List<IUhtExportTask>(this.Session.PackageTypeCount);
			foreach (UhtPackage Package in this.Session.Packages)
			{
				UHTManifest.Module Module = Package.Module;
				UhtExportOptions AdditionalOptions = UhtExportOptions.WriteOutput;

				string JsonPath = this.Factory.MakePath(Package, ".json");

				GeneratedPackages.Add(Factory.CreateTask(JsonPath, new List<IUhtExportTask>(), AdditionalOptions,
					(IUhtExportOutput JsonOutput) =>
					{
						JsonSerializerOptions Options = new JsonSerializerOptions { WriteIndented = true, IgnoreNullValues = true };
						JsonOutput.CommitOutput(JsonSerializer.Serialize(Package, Options));
					}));
			}

			// Wait for all the packages to complete
			List<Task> PackageTasks = new List<Task>(this.Session.PackageTypeCount);
			foreach (IUhtExportTask Output in GeneratedPackages)
			{
				if (Output.ActionTask != null)
				{
					PackageTasks.Add(Output.ActionTask);
				}
			}
			Task.WaitAll(PackageTasks.ToArray());
		}
	}
}
