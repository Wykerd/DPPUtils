#include <dpputils/api/songinfo.h>
#include <dpp/fmt/format.h>
#include <dpp/nlohmann/json.hpp>

using namespace dpputils;
using std::string;
using std::vector;
using nlohmann::json;

static
void hexchar(unsigned char c, unsigned char &hex1, unsigned char &hex2)
{
    hex1 = c / 16;
    hex2 = c % 16;
    hex1 += hex1 <= 9 ? '0' : 'a' - 10;
    hex2 += hex2 <= 9 ? '0' : 'a' - 10;
}

static
string urlencode(string &s)
{
    const char *str = s.c_str();
    vector<char> v(s.size());
    v.clear();
    for (size_t i = 0, l = s.size(); i < l; i++)
    {
        char c = str[i];
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '-' || c == '_' || c == '.' || c == '!' || c == '~' ||
            c == '*' || c == '\'' || c == '(' || c == ')')
        {
            v.push_back(c);
        }
        else if (c == ' ')
        {
            v.push_back('+');
        }
        else
        {
            v.push_back('%');
            unsigned char d1, d2;
            hexchar(c, d1, d2);
            v.push_back((char)d1);
            v.push_back((char)d2);
        }
    }

    return string(v.cbegin(), v.cend());
}

void dpputils::get_release_info(dpp::cluster &cluster, std::string query,
					  const std::function<void(const releaseinfo_t *info)>& callback)
{
	cluster.request(
		fmt::format("https://musicbrainz.org/ws/2/release?fmt=json&query={}", urlencode(query)),
		dpp::m_get,
		[&cluster, query, callback](const dpp::http_request_completion_t &resp) {
			auto resp_json = json::parse(resp.body);
			auto *info = new releaseinfo_t;
			if (resp_json["releases"].empty())
			{
				delete info;
				return callback(nullptr);
			}
			auto release = resp_json["releases"][0];
			for (auto it : release["artist-credit"])
			{
				info->artists.push_back(it["name"].get<string>());
			}
			info->name = release["title"].get<string>();

			cluster.request(
				fmt::format(
					"https://musicbrainz.org/ws/2/release/{}?inc=artists+collections+labels+recordings+release-groups&fmt=json",
					release["id"]
				),
				dpp::m_get,
				[&cluster, query, callback, info](const dpp::http_request_completion_t &resp) {
					auto resp_json = json::parse(resp.body);
					bool art = resp_json["cover-art-archive"]["artwork"].get<bool>();
					if (art)
						info->cover_art_url = fmt::format("https://coverartarchive.org/release/{}.jpg", resp_json["id"]);
					for (auto it : resp_json["media"])
						for (auto tr : it["tracks"])
							info->tracks.push_back(tr["title"].get<string>());

					callback(info);
					delete info;
				}
			);
		}
	);
}

void dpputils::get_youtube_results(dpp::cluster &cluster, std::string query,
						 const std::function<void(const std::vector<std::string> &ids)> &callback) {
	json search_body = {
		{
			"context",
			{
			   {
				   "client",
				   {
					 	{ "clientName", "WEB" },
						{ "clientVersion", "2.20211006.00.00" }
				   }
			   }
		   }
		},
		{
			"query", query
		}
	};

	std::multimap<string, string> headers = {
		{ "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.77 Safari/537.36" },
	};

	cluster.request(
			"https://www.youtube.com/youtubei/v1/search?key=AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8",
			dpp::m_post,
			[callback](const dpp::http_request_completion_t&resp) {
				std::vector<string> ids;
				try {
					json res = json::parse(resp.body);
					auto videos = res["contents"]["twoColumnSearchResultsRenderer"]["primaryContents"]["sectionListRenderer"]["contents"][0]["itemSectionRenderer"]["contents"];
					for (json::iterator it = videos.begin(); it != videos.end(); ++it)
					{
						if (ids.size() >= 5)
							break;
						if (!it.value()["videoRenderer"].is_object())
							continue;
						string video = it.value()["videoRenderer"]["videoId"].get<string>();
						ids.push_back(video);
					};
				} catch (...) {
					// silently ignore exceptions
				}
				callback(ids);
			},
			search_body.dump(), "application/json", headers);
}

void dpputils::get_song_info(dpp::cluster &cluster, std::string query,
                             const std::function<void(const songinfo_t *info)>& callback)
{
    cluster.request(
        fmt::format("https://itunes.apple.com/search?limit=5&media=music&term={}", urlencode(query)), 
        dpp::m_get, 
        [&cluster, query, callback](const dpp::http_request_completion_t &resp) {
            auto resp_json = json::parse(resp.body);
            auto *info = new songinfo_t;
            string search_term;
            if (resp_json["resultCount"].get<uint32_t>())
            {
                auto res = resp_json["results"][0];
                info->artist = res["artistName"].get<string>();
                info->track = res["trackName"].get<string>();
                info->collection = res["collectionName"].get<string>();
                info->cover_art_url = res["artworkUrl100"].get<string>();

                info->artist_apple_music_url = res["artistViewUrl"].get<string>();
                info->track_apple_music_url = res["trackViewUrl"].get<string>();
                info->collection_apple_music_url = res["collectionViewUrl"].get<string>();


                // Most of the song only versions of videos are autogenerated with the description
                // Provided to Youtube by {some company} so appending it to the search terms
                // Increases likelyhood of good song only version
                search_term = fmt::format("{} - {} Provided to Youtube", info->artist, info->track);
            }
            else
                search_term.assign(query);

            json search_body = {
                { 
                    "context", {
                        {
                            "client", {
                                { "clientName", "WEB" },
                                { "clientVersion", "2.20211006.00.00" }
                            }
                        }
                    }
                },
                {
                    "query", search_term
                }
            };

            std::multimap<string, string> headers = {
                { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.77 Safari/537.36" },
            };

            cluster.request(
                "https://www.youtube.com/youtubei/v1/search?key=AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8",
                dpp::m_post, 
                [info, callback](const dpp::http_request_completion_t&resp) {
                    json res = json::parse(resp.body);
                    try {
                        auto videos = res["contents"]["twoColumnSearchResultsRenderer"]["primaryContents"]["sectionListRenderer"]["contents"][0]["itemSectionRenderer"]["contents"];
                        for (json::iterator it = videos.begin(); it != videos.end(); ++it)
                        {
                            if (info->youtube_candidates.size() >= 5)
                                break;
                            if (!it.value()["videoRenderer"].is_object())
                                continue;
                            string video = it.value()["videoRenderer"]["videoId"].get<string>();
                            info->youtube_candidates.push_back(video);
                        };
                    } catch (...) {
                        // silently ignore exceptions
                    }
                    callback(info);
                    delete info;
                }, 
                search_body.dump(), "application/json", headers);
        });
}
