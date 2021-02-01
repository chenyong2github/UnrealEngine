// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using IdentityModel.Client;
using EpicGames.Core;
using EpicGames.Jupiter;

namespace UnrealGameSync
{
	class JupiterMonitor: IArchiveInfoSource, IDisposable
	{
		private readonly object ArchivesLock = new object();
		private IReadOnlyList<IArchiveInfo> Archives;

		private readonly OIDCTokenManager TokenManager;
		private readonly string JupiterNamespace;
		private readonly string ProviderIdentifier;
		private readonly string ExpectedBranch;
		private readonly Uri JupiterUrl;

		private readonly Timer UpdateTimer;
		private readonly BoundedLogWriter LogWriter;

		public IReadOnlyList<IArchiveInfo> AvailableArchives
		{
			get
			{
				lock (ArchivesLock)
				{
					return Archives;
				}
			}
		}

		private JupiterMonitor(OIDCTokenManager InTokenManager, string InLogPath, string InNamespace, string InUrl,
			string InProviderIdentifier, string InExpectedBranch)
		{
			TokenManager = InTokenManager;
			JupiterNamespace = InNamespace;
			ProviderIdentifier = InProviderIdentifier;
			ExpectedBranch = InExpectedBranch;
			JupiterUrl = new Uri(InUrl);

			LogWriter = new BoundedLogWriter(InLogPath);
			UpdateTimer = new Timer(DoUpdate, null, TimeSpan.Zero, TimeSpan.FromMinutes(5));
		}
		public void Dispose()
		{
			UpdateTimer?.Dispose();
			LogWriter?.Dispose();
		}

		private async void DoUpdate(object State)
		{
			await LogWriter.WriteLineAsync($"Starting poll of JupiterMonitor for namespace {JupiterNamespace}");
			try
			{
				IReadOnlyList<IArchiveInfo> NewArchives = await GetAvailableArchives();
				lock (ArchivesLock)
				{
					Archives = NewArchives;
				}

			}
			catch (Exception Exception)
			{
				await LogWriter.WriteLineAsync($"Exception occured during poll! {Exception}");
			}
		}

		public static JupiterMonitor CreateFromConfigFile(OIDCTokenManager TokenManager, string LogPath, ConfigFile ConfigFile, string SelectedProjectIdentifier)
		{
			ConfigSection JupiterConfigSection = ConfigFile.FindSection("Jupiter");
			if (JupiterConfigSection == null)
				return null;

			string JupiterUrl = JupiterConfigSection.GetValue("JupiterUrl");
			string OIDCProviderIdentifier = JupiterConfigSection.GetValue("OIDCProviderIdentifier");

			ConfigSection ProjectConfigSection = ConfigFile.FindSection(SelectedProjectIdentifier);
			if (ProjectConfigSection == null)
				return null;

			string JupiterNamespace = ProjectConfigSection.GetValue("JupiterNamespace");
			// Is no namespace has been specified we are unable to fetch builds
			if (JupiterNamespace == null)
				return null;

			string ExpectedBranch = ProjectConfigSection.GetValue("ExpectedBranch");
			// If we do not know which branch to fetch we can not list builds, as it would risk getting binaries from other branches
			if (ExpectedBranch == null)
				return null;

			OIDCProviderIdentifier = ProjectConfigSection.GetValue("OIDCProviderIdentifier") ?? OIDCProviderIdentifier;

			// with no oidc provider we are unable to login, thus it is required
			if (OIDCProviderIdentifier == null)
				return null;

			// project specific overrides
			JupiterUrl = ProjectConfigSection.GetValue("JupiterUrl") ?? JupiterUrl;

			return new JupiterMonitor(TokenManager, LogPath, JupiterNamespace, JupiterUrl, OIDCProviderIdentifier, ExpectedBranch);
		}

		private async Task<IReadOnlyList<IArchiveInfo>> GetAvailableArchives()
		{
			string Token = await TokenManager.GetAccessToken(ProviderIdentifier);

			List<JupiterArchiveInfo> NewArchives = new List<JupiterArchiveInfo>();
			using (HttpClient Client = new HttpClient())
			{
				Client.BaseAddress = JupiterUrl;
				Client.SetBearerToken(Token);

				string ResponseBody = await Client.GetStringAsync($"/api/v1/c/tree-root/{JupiterNamespace}");
				TreeRootListResponse Response = JsonSerializer.Deserialize<TreeRootListResponse>(ResponseBody, new JsonSerializerOptions {PropertyNameCaseInsensitive = true});

				foreach (string TreeRoot in Response.TreeRoots)
				{
					// fetch info on this tree root
					string ResourceUrl = $"/api/v1/c/tree-root/{JupiterNamespace}/{TreeRoot}";
					string ResponseBodyTreeReference = await Client.GetStringAsync(ResourceUrl);
					TreeRootReferenceResponse ResponseTreeReference = JsonSerializer.Deserialize<TreeRootReferenceResponse>(ResponseBodyTreeReference, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

					Dictionary<string, string> Metadata = ResponseTreeReference.Metadata.ToDictionary(Pair => Pair.Key, Pair => Pair.Value.ToString());

					string ArchiveType, Project, Branch, ChangelistString;

					if (!Metadata.TryGetValue("ArchiveType", out ArchiveType) ||
					    !Metadata.TryGetValue("Project", out Project) ||
					    !Metadata.TryGetValue("Branch", out Branch) ||
						!Metadata.TryGetValue("Changelist", out ChangelistString))
					{
						continue;
					}

					// skip if we do not have metadata as we need that to be able to determine if this root is of the type we want
					if (Branch == null || Project == null || ArchiveType == null || ChangelistString == null)
						continue;

					if (!string.Equals(ExpectedBranch, Branch, StringComparison.InvariantCultureIgnoreCase))
					{
						continue;
					}

					int Changelist;
					if (!int.TryParse(ChangelistString, out Changelist))
					{
						continue; // invalid changelist format
					}

					JupiterArchiveInfo ExistingArchive = NewArchives.FirstOrDefault(Info =>
						string.Equals(Info.Name, Project, StringComparison.InvariantCultureIgnoreCase) &&
						string.Equals(Info.Type, ArchiveType, StringComparison.InvariantCultureIgnoreCase));
					if (ExistingArchive == null)
					{
						JupiterArchiveInfo Archive = new JupiterArchiveInfo(Project, ArchiveType, JupiterUrl.ToString(), JupiterNamespace);
						NewArchives.Add(Archive);
						ExistingArchive = Archive;
					}

					ExistingArchive.AddArchiveVersion(Changelist, ResponseTreeReference.TreeReferenceKey);
				}
			}

			return NewArchives.AsReadOnly();
		}

		private class TreeRootListResponse
		{
			public List<string> TreeRoots { get; set; }
		}

		private class TreeRootReferenceResponse
		{
			public string TreeReferenceKey { get; set; }
			public string TreeHash { get; set; }

			public Dictionary<string, object> Metadata { get; set; }

			public string TreeRootState { get; set; }
		}

		private class JupiterArchiveInfo : IArchiveInfo
		{
			private readonly string JupiterUrl;
			private readonly string JupiterNamespace;
			private readonly Dictionary<int, string> ChangeToKey = new Dictionary<int, string>();

			public JupiterArchiveInfo(string InName, string InType, string InJupiterUrl, string InJupiterNamespace)
			{
				Name = InName;
				Type = InType;

				JupiterUrl = InJupiterUrl;
				JupiterNamespace = InJupiterNamespace;
			}

			public string Name { get; }
			public string Type { get; }

			public string BasePath
			{
				get { return null; } 
			}

			public string Target => throw new NotImplementedException();

			public bool Exists()
			{
				return ChangeToKey.Count > 0;
			}

			public bool TryGetArchiveKeyForChangeNumber(int ChangeNumber, out string ArchiveKey)
			{
				return ChangeToKey.TryGetValue(ChangeNumber, out ArchiveKey);
			}

			public bool DownloadArchive(string ArchiveKey, string LocalRootPath, string ManifestFileName, TextWriter Log, ProgressValue Progress)
			{
				try
				{
					Progress<Tuple<float, FileReference>> ProgressCallback = new Progress<Tuple<float, FileReference>>(ProgressUpdate =>
					{
						(float ProgressFraction, FileReference FileReference) = ProgressUpdate;
						Progress.Set(ProgressFraction);
						lock (Log)
						{
							Log.WriteLine("Writing {0}", FileReference.FullName);
						}
					});

					// place the manifest for the Jupiter download next to the UGS manifest
					FileReference UGSManifestFileReference = new FileReference(ManifestFileName);
					FileReference JupiterManifestFileReference = FileReference.Combine(UGSManifestFileReference.Directory, "Jupiter-Manifest.json");

					DirectoryReference RootDirectory = new DirectoryReference(LocalRootPath);
					JupiterFileTree FileTree = new JupiterFileTree(RootDirectory, InDeferReadingFiles: true);
					Task<List<FileReference>> DownloadTask = FileTree.DownloadFromJupiter(JupiterManifestFileReference, JupiterUrl, JupiterNamespace, ArchiveKey, ProgressCallback);
					DownloadTask.Wait();

					List<FileReference> WrittenFiles = DownloadTask.Result;
					ArchiveManifest ArchiveManifest = new ArchiveManifest();
					foreach (FileReference File in WrittenFiles)
					{
						ArchiveManifest.Files.Add(new ArchiveManifestFile(File.FullName, File.ToFileInfo().Length, DateTime.Now));
					}

					// Write it out to a temporary file, then move it into place
					string TempManifestFileName = ManifestFileName + ".tmp";
					using (FileStream OutputStream = File.Open(TempManifestFileName, FileMode.Create, FileAccess.Write))
					{
						ArchiveManifest.Write(OutputStream);
					}
					File.Move(TempManifestFileName, ManifestFileName);

					return true;
				}
				catch (Exception Exception)
				{
					Log.WriteLine(
						$"Exception occured when downloading build from Jupiter with key {ArchiveKey}. Exception {Exception}");
					return false;
				}
			}

			public void AddArchiveVersion(int Changelist, string Key)
			{
				ChangeToKey[Changelist] = Key;
			}
		}
	}
}