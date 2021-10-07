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

void get_song_info(dpp::cluster &cluster, std::string query,
                   std::function<void(const songinfo_t *info)> callback);

}
