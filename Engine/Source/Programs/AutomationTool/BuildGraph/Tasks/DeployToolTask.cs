// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text.Json;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using UnrealBuildBase;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a DeployTool task
	/// </summary>
	public class DeployToolTaskParameters
	{
		/// <summary>
		/// Identifier for the tool
		/// </summary>
		[TaskParameter]
		public string Id = String.Empty;

		/// <summary>
		/// Settings file to use for the deployment. Should be a JSON file containing server name and access token.
		/// </summary>
		[TaskParameter]
		public string Settings = String.Empty;

		/// <summary>
		/// Version number for the new tool
		/// </summary>
		[TaskParameter]
		public string Version = String.Empty;

		/// <summary>
		/// Duration over which to roll out the tool, in minutes.
		/// </summary>
		[TaskParameter(Optional = true)]
		public int Duration = 0;

		/// <summary>
		/// Whether to create the deployment as paused
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool Paused = false;

		/// <summary>
		/// Zip file containing the data to upload
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? File;

		/// <summary>
		/// Directory to zip and upload for the tool
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Directory;
	}

	/// <summary>
	/// Deploys a tool update through Horde
	/// </summary>
	[TaskElement("DeployTool", typeof(DeployToolTaskParameters))]
	public class DeployToolTask : SpawnTaskBase
	{
		class DeploySettings
		{
			public string Server { get; set; } = String.Empty;
			public string? Token { get; set; }
		}

		/// <summary>
		/// Parameters for this task
		/// </summary>
		DeployToolTaskParameters Parameters;

		/// <summary>
		/// Construct a Helm task
		/// </summary>
		/// <param name="InParameters">Parameters for the task</param>
		public DeployToolTask(DeployToolTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		public override async Task ExecuteAsync(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			FileReference? file;
			if (Parameters.File != null && Parameters.Directory == null)
			{
				file = ResolveFile(Parameters.File);
			}
			else if (Parameters.File == null && Parameters.Directory != null)
			{
				file = FileReference.Combine(Unreal.EngineDirectory, "Intermediate", "DeployTool", "Tool.zip");
				CommandUtils.DeleteFile(file);
				CommandUtils.ZipFiles(file, ResolveDirectory(Parameters.Directory), new FileFilter(FileFilterType.Include));
			}
			else
			{
				throw new AutomationException("Either file or Directory must be specified to the DeployTool task (not both).");
			}

			FileReference settingsFile = ResolveFile(Parameters.Settings);
			if (!FileReference.Exists(settingsFile))
			{
				throw new AutomationException($"Settings file '{settingsFile}' does not exist");
			}

			byte[] settingsData = await FileReference.ReadAllBytesAsync(settingsFile);
			JsonSerializerOptions jsonOptions = new JsonSerializerOptions { AllowTrailingCommas = true, ReadCommentHandling = JsonCommentHandling.Skip, PropertyNameCaseInsensitive = true };

			DeploySettings? settings = JsonSerializer.Deserialize<DeploySettings>(settingsData, jsonOptions);
			if (settings == null)
			{
				throw new AutomationException($"Unable to read settings file {settingsFile}");
			}
			else if (settings.Server == null)
			{
				throw new AutomationException($"Missing 'server' key from {settingsFile}");
			}

			using (HttpClient httpClient = new HttpClient())
			{
				httpClient.BaseAddress = new Uri(settings.Server);
				using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/tools/{Parameters.Id}/deployments"))
				{
					if (settings.Token != null)
					{
						request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", settings.Token);
					}

					using (FileStream stream = FileReference.Open(file, FileMode.Open, FileAccess.Read))
					{
						StreamContent streamContent = new StreamContent(stream);

						MultipartFormDataContent content = new MultipartFormDataContent();
						content.Add(new StringContent(Parameters.Version), "version");
						if (Parameters.Duration != 0)
						{
							content.Add(new StringContent(TimeSpan.FromMinutes(Parameters.Duration).ToString()), "duration");
						}
						if (Parameters.Paused)
						{
							content.Add(new StringContent("true"), "paused");
						}
						content.Add(streamContent, "file", file.GetFileName());
						request.Content = content;

						Logger.LogInformation("Uploading {File} to {Url}", file, request.RequestUri);
						using (HttpResponseMessage response = await httpClient.SendAsync(request))
						{
							if (!response.IsSuccessStatusCode)
							{
								string? responseContent;
								try
								{
									responseContent = await response.Content.ReadAsStringAsync();
								}
								catch
								{
									responseContent = "(No message)";
								}
								throw new AutomationException($"Upload failed ({response.StatusCode}): {responseContent}");
							}
						}
					}
				}
			}
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			yield break;
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			yield break;
		}
	}
}
