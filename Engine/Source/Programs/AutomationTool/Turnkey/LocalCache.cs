using System;
using System.Collections.Generic;
using System.Runtime.Serialization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;

namespace Turnkey
{
	static class LocalCache
	{
		// these temp files 
		public static string TempCacheLocation = Path.Combine(Path.GetTempPath(), "Turnkey", "TempFiles");

		public static string DownloadCacheLocation = Path.Combine(Path.GetTempPath(), "Turnkey", "DownloadCache");

		public static string CreateTempDirectory()
		{
			string TempDir = Path.Combine(TempCacheLocation, Path.GetRandomFileName());
			Directory.CreateDirectory(TempDir);

			// queue this to clean up at the end
			TurnkeyUtils.AddPathToCleanup(TempDir);

			return TempDir;
		}



		static void SerializeObject(ISerializable Object, string Filename)
		{
			using (FileStream Stream = new FileStream(Filename, FileMode.Create))
			{
				new BinaryFormatter().Serialize(Stream, Object);
			}
		}

		static ISerializable DeserializeObject(string Filename)
		{
			try
			{
				if (File.Exists(Filename))
				{
					using (FileStream Stream = new FileStream(Filename, FileMode.Open))
					{
						return (ISerializable)new BinaryFormatter().Deserialize(Stream);
					}
				}
			}
			catch(Exception)
			{
			}

			// for any error, just return null
			return null;
		}


		// @todo turnkey - expire/cache dates/something to be able to check - likely the caller of this would need to do it, like GoogleDrive
		// checking versions of each file, while enumerating, but only download if newer
		static Dictionary<string, KeyValuePair<string, string>> TagToDownloadCache;
		static string TagCacheFile = Path.Combine(DownloadCacheLocation, "TagCache.bin");

		static LocalCache()
		{
			TagToDownloadCache = DeserializeObject(TagCacheFile) as Dictionary<string, KeyValuePair<string, string>>;

			if (TagToDownloadCache == null)
			{
				TagToDownloadCache = new Dictionary<string, KeyValuePair<string, string>>();
			}
		}

		public static void CacheLocationByTag(string Tag, string CachePath, string VersionMatch="")
		{
			TagToDownloadCache[Tag] = new KeyValuePair<string, string>(CachePath, VersionMatch);
			SerializeObject(TagToDownloadCache, TagCacheFile);
		}

		public static string GetCachedPathByTag(string Tag, string VersionMatch, out bool bFoundOldVersion)
		{
			// return it if it was there
			KeyValuePair<string, string> CachedPathWithVersion;
			if (TagToDownloadCache.TryGetValue(Tag, out CachedPathWithVersion))
			{
				// make sure any referenced cache locations still exist
				if (!File.Exists(CachedPathWithVersion.Key) && !Directory.Exists(CachedPathWithVersion.Key))
				{
					bFoundOldVersion = false;
					TagToDownloadCache.Remove(Tag);
					return null;
				}

				// if the version doesn't match, return it, but let the called know it's out of date
				bFoundOldVersion = CachedPathWithVersion.Value != VersionMatch;
				return CachedPathWithVersion.Key;
			}

			bFoundOldVersion = false;
			return null;
		}

		// version with no version checking
		public static string GetCachedPathByTag(string Tag)
		{
			return GetCachedPathByTag(Tag, "", out _);
		}

		public static string CreateDownloadCacheDirectory()
		{
			return Path.Combine(DownloadCacheLocation, Path.GetRandomFileName());
		}




		public static string GetInstallCacheDirectory()
		{
			// this will prompt user if needed
			return TurnkeySettings.GetUserSetting("User_QuickSwitchSdkLocation");
		}
	}
}
