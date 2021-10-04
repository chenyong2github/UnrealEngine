using System.Net.Http;
using System.Threading.Tasks;

public class SkeinClient
{
    private readonly HttpClient _httpClient;

    public SkeinClient(HttpClient httpClient)
    {
        _httpClient = httpClient;
    }

    public async Task<string> GetAsync(string url)
    {
        HttpRequestMessage requestMessage = new HttpRequestMessage(HttpMethod.Get, url);

        return await Request(requestMessage);
    }

    private async Task<string> Request(HttpRequestMessage requestMessage)
    {
        // Mock Response
        await Task.Delay(1000);
        return "{}";
    }
}
