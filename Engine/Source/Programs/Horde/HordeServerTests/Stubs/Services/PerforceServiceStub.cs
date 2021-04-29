using HordeServer.Models;
using HordeServer.Services;
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
	class PerforceServiceStub : IPerforceService
	{
		class ChangeComparer : IComparer<int>
		{
			public int Compare(int X, int Y) => Y.CompareTo(X);
		}

		public Dictionary<string, SortedDictionary<int, ChangeDetails>> Changes { get; } = new Dictionary<string, SortedDictionary<int, ChangeDetails>>(StringComparer.OrdinalIgnoreCase);

		public void AddChange(string StreamName, int Number, string Author, string Description, IEnumerable<string> Files)
		{
			SortedDictionary<int, ChangeDetails>? StreamChanges;
			if (!Changes.TryGetValue(StreamName, out StreamChanges))
			{
				StreamChanges = new SortedDictionary<int, ChangeDetails>(new ChangeComparer());
				Changes[StreamName] = StreamChanges;
			}
			StreamChanges.Add(Number, new ChangeDetails(Number, Author, Description, Files.ToList()));
		}

		public Task<List<ChangeSummary>> GetChangesAsync(string StreamName, int? MinChange, int? MaxChange, int NumResults, string? ImpersonateUser)
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
						Results.Add(new ChangeSummary(Details.Number, Details.Author, Details.Description));
					}
					if (NumResults > 0 && Results.Count >= NumResults)
					{
						break;
					}
				}
			}

			return Task.FromResult(Results);
		}

		public Task<PerforceUserInfo?> GetUserInfoAsync(string UserName)
		{
			return Task.FromResult<PerforceUserInfo?>(new PerforceUserInfo { Name = UserName, Email = $"{UserName}@epicgames.com" });
		}

		public Task<List<ChangeDetails>> GetChangeDetailsAsync(string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser)
		{
			List<ChangeDetails> Results = new List<ChangeDetails>();
			foreach (int ChangeNumber in ChangeNumbers)
			{
				Results.Add(Changes[StreamName][ChangeNumber]);
			}
			return Task.FromResult(Results);
		}

		public Task<string> CreateTicket(string ImpersonateUser)
		{
			return Task.FromResult("bogus-ticket");
		}

		public Task<int> GetCodeChangeAsync(string StreamName, int Change)
		{
			int CodeChange = 0;

			SortedDictionary<int, ChangeDetails>? StreamChanges;
			if (Changes.TryGetValue(StreamName, out StreamChanges))
			{
				foreach (ChangeDetails Details in StreamChanges.Values)
				{
					if (Details.Number <= Change && Details.Files.Any(x => x.EndsWith(".h") || x.EndsWith(".cpp")))
					{
						CodeChange = Details.Number;
						break;
					}
				}
			}

			return Task.FromResult(CodeChange);
		}

		public Task<int> CreateNewChangeAsync(string StreamName, string Path)
		{
			ChangeDetails NewChange = new ChangeDetails(Changes[StreamName].First().Key + 1, "", "", new List<string> { Path });
			Changes[StreamName].Add(NewChange.Number, NewChange);
			return Task.FromResult(NewChange.Number);
		}

		public Task<List<FileSummary>> FindFilesAsync(IEnumerable<string> Paths)
		{
			throw new NotImplementedException();
		}

		public Task<byte[]> PrintAsync(string Path)
		{
			throw new NotImplementedException();
		}

		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(int ShelvedChange, int OriginalChange)
		{
			throw new NotImplementedException();
		}

		public Task<int> DuplicateShelvedChangeAsync(int ShelvedChange)
		{
			return Task.FromResult(ShelvedChange);
		}

		public Task DeleteShelvedChangeAsync(int ShelvedChange)
		{
			return Task.CompletedTask;
		}

		public Task UpdateChangelistDescription(int Change, string Description)
		{
			return Task.CompletedTask;
		}
	}
}
