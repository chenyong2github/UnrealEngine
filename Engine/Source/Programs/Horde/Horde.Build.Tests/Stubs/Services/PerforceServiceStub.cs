// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;

namespace HordeServerTests.Stubs.Services
{
	using UserId = ObjectId<IUser>;

	static class PerforceExtensions
	{
		public static ChangeFile CreateChangeFile(string Path)
		{
			return new ChangeFile(Path, null!, 0, 0, EpicGames.Core.Md5Hash.Zero, null!);
		}

		public static void Add(this List<ChangeFile> Files, string Path)
		{
			Files.Add(CreateChangeFile(Path));
		}
	}

	class PerforceServiceStub : IPerforceService
	{
		class User : IUser
		{
			public UserId Id { get; }
			public string Name { get; set; }
			public string Email { get; set; }
			public string Login { get; set; }

			public User(string Login)
			{
				this.Id = UserId.GenerateNewId();
				this.Name = Login.ToUpperInvariant();
				this.Email = $"{Login}@server";
				this.Login = Login;
			}
		}

		User TestUser = new User("TestUser");

		class ChangeComparer : IComparer<int>
		{
			public int Compare(int X, int Y) => Y.CompareTo(X);
		}

		Dictionary<string, IUser> NameToUser = new Dictionary<string, IUser>(StringComparer.OrdinalIgnoreCase);
		public Dictionary<string, SortedDictionary<int, ChangeDetails>> Changes { get; } = new Dictionary<string, SortedDictionary<int, ChangeDetails>>(StringComparer.OrdinalIgnoreCase);

		IUserCollection UserCollection;

		public PerforceServiceStub(IUserCollection UserCollection)
		{
			this.UserCollection = UserCollection;
		}

		public Task<NativePerforceConnection?> GetServiceUserConnection(string? ClusterName)
		{
			throw new NotImplementedException();
		}

		public async ValueTask<IUser> FindOrAddUserAsync(string ClusterName, string UserName)
		{
			return await UserCollection.FindOrAddUserByLoginAsync(UserName);
		}

		public void AddChange(string StreamName, int Number, IUser Author, string Description, IEnumerable<string> Files)
		{
			SortedDictionary<int, ChangeDetails>? StreamChanges;
			if (!Changes.TryGetValue(StreamName, out StreamChanges))
			{
				StreamChanges = new SortedDictionary<int, ChangeDetails>(new ChangeComparer());
				Changes[StreamName] = StreamChanges;
			}
			StreamChanges.Add(Number, new ChangeDetails(Number, Author, null!, Description, Files.Select(x => PerforceExtensions.CreateChangeFile(x)).ToList(), DateTime.Now));
		}

		public Task<List<ChangeSummary>> GetChangesAsync(string ClusterName, string StreamName, int? MinChange, int? MaxChange, int NumResults, string? ImpersonateUser)
		{
			List<ChangeSummary> Results = new List<ChangeSummary>();

			SortedDictionary<int, ChangeDetails>? StreamChanges;
			if (Changes.TryGetValue(StreamName, out StreamChanges) && StreamChanges.Count > 0)
			{
				foreach (ChangeDetails Details in StreamChanges.Values)
				{
					if (MinChange.HasValue && Details.Number < MinChange)
					{
						break;
					}
					if (!MaxChange.HasValue || Details.Number <= MaxChange.Value)
					{
						Results.Add(new ChangeSummary(Details.Number, Details.Author, "//...", Details.Description));
					}
					if (NumResults > 0 && Results.Count >= NumResults)
					{
						break;
					}
				}
			}

			return Task.FromResult(Results);
		}

		public Task<PerforceUserInfo?> GetUserInfoAsync(string ClusterName, string UserName)
		{
			return Task.FromResult<PerforceUserInfo?>(new PerforceUserInfo { Login = UserName, FullName = UserName, Email = $"{UserName}@epicgames.com" });
		}

		public Task<List<ChangeDetails>> GetChangeDetailsAsync(string ClusterName, string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser)
		{
			List<ChangeDetails> Results = new List<ChangeDetails>();
			foreach (int ChangeNumber in ChangeNumbers)
			{
				Results.Add(Changes[StreamName][ChangeNumber]);
			}
			return Task.FromResult(Results);
		}

		public Task<string> CreateTicket(string ClusterName, string ImpersonateUser)
		{
			return Task.FromResult("bogus-ticket");
		}

		public Task<int> GetCodeChangeAsync(string ClusterName, string StreamName, int Change)
		{
			int CodeChange = 0;

			SortedDictionary<int, ChangeDetails>? StreamChanges;
			if (Changes.TryGetValue(StreamName, out StreamChanges))
			{
				foreach (ChangeDetails Details in StreamChanges.Values)
				{
					if (Details.Number <= Change && Details.Files.Any(x => x.Path.EndsWith(".h") || x.Path.EndsWith(".cpp")))
					{
						CodeChange = Details.Number;
						break;
					}
				}
			}

			return Task.FromResult(CodeChange);
		}

		public Task<int> CreateNewChangeAsync(string ClusterName, string StreamName, string Path)
		{
			ChangeDetails NewChange = new ChangeDetails(Changes[StreamName].First().Key + 1, TestUser, null!, "", new List<ChangeFile> { PerforceExtensions.CreateChangeFile(Path) }, DateTime.Now);
			Changes[StreamName].Add(NewChange.Number, NewChange);
			return Task.FromResult(NewChange.Number);
		}

		public Task<List<FileSummary>> FindFilesAsync(string ClusterName, IEnumerable<string> Paths)
		{
			throw new NotImplementedException();
		}

		public Task<byte[]> PrintAsync(string ClusterName, string Path)
		{
			throw new NotImplementedException();
		}

		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(string ClusterName, int ShelvedChange, int OriginalChange)
		{
			throw new NotImplementedException();
		}

		public Task<int> DuplicateShelvedChangeAsync(string ClusterName, int ShelvedChange)
		{
			return Task.FromResult(ShelvedChange);
		}

		public Task DeleteShelvedChangeAsync(string ClusterName, int ShelvedChange)
		{
			return Task.CompletedTask;
		}

		public Task UpdateChangelistDescription(string ClusterName, int Change, string Description)
		{
			return Task.CompletedTask;
		}

		public Task<IStreamView> GetStreamViewAsync(string ClusterName, string StreamName)
		{
			throw new NotImplementedException();
		}

		public Task<List<ChangeSummary>> GetChangesAsync(string ClusterName, int? MinChange, int? MaxChange, int MaxResults)
		{
			throw new NotImplementedException();
		}

		public Task<ChangeDetails> GetChangeDetailsAsync(string ClusterName, int ChangeNumber)
		{
			throw new NotImplementedException();
		}

		public Task<List<ChangeFile>> GetStreamSnapshotAsync(string ClusterName, string StreamName, int Change)
		{
			throw new NotImplementedException();
		}

		public Task<ChangeDetails> GetChangeDetailsAsync(string ClusterName, string StreamName, int ChangeNumber)
		{
			return Task.FromResult(Changes[StreamName][ChangeNumber]);
		}
	}
}
