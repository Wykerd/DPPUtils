#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <functional>

namespace dpputils {

struct songinfo_t {
    std::string artist;
    std::string track;
    std::string collection;
    std::string cover_art_url;
    
    std::string artist_apple_music_url;
    std::string track_apple_music_url; 
    std::string collection_apple_music_url; 

    std::vector<std::string> youtube_candidates;
};

struct releaseinfo_t {
	std::string name;
	std::vector<std::string> artists;
	// int release_date;
	std::string cover_art_url;
	std::vector<std::string> tracks;
};

void get_song_info(dpp::cluster &cluster, std::string query,
                   const std::function<void(const songinfo_t *info)>& callback);

void get_release_info(dpp::cluster &cluster, std::string query,
					const std::function<void(const releaseinfo_t *info)>& callback);

void get_youtube_results(dpp::cluster &cluster, std::string query,
						 const std::function<void(const std::vector<std::string> &ids)> &callback);

}
