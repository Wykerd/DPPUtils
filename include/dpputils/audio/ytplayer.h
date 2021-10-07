#pragma once

#include <string>
#include <functional>
#include <queue>
#include <unordered_map>

#include <uv.h>
#include <webm/webm_parser.h>
#include <ytdl/dl.h>
#include <ytdl/dash.h>
#include <dpputils/audio/partial_buffer_reader.h>

namespace webm {

class StreamCallback : public Callback {
    public:
        std::function<void (StreamCallback *caller, const char *buf, size_t len)> on_packet;
        void *p_ctx;

        uint64_t cluster_timecode;
        
        Status OnTrackEntry(const ElementMetadata& metadata,
                            const TrackEntry& track_entry) override;

        Status OnSimpleBlockBegin(const ElementMetadata& metadata,
                                  const SimpleBlock& simple_block,
                                  Action* action) override;

        Status OnBlockBegin(const ElementMetadata& metadata, 
                            const Block& block, Action* action) override;

        Status OnFrame(const FrameMetadata& metadata, Reader* reader,
                       std::uint64_t* bytes_remaining) override;

        Status OnClusterBegin(const ElementMetadata& metadata,
                              const Cluster& cluster, Action* action) override;
};

}

namespace dpputils {

struct ytinfo_t {
    std::string id;
    std::string title;
    std::string length_seconds;
    std::string channel_id;
    std::string short_description;
    double average_rating;
    std::string view_count;
    std::string author;
    std::string dash_url;

    union {
        ytdl_dl_media_ctx_t *dl_chunked;
        ytdl_dl_dash_ctx_t *dl_dash;   
    };

    bool is_dash;
};

struct ytctx_t;

class ytplayer {
    private:
        uv_loop_t *loop;
        std::unordered_map<uint64_t, ytctx_t *> ctx;
    public:
        ytplayer(uv_loop_t *loop);
        uv_loop_t *get_loop();
        std::queue<ytinfo_t> &get_queue(uint64_t id);

        bool add(uint64_t id, std::string &url);
        void addId(uint64_t id, const char *videoId);
        void start(uint64_t id);
        // void end(uint64_t id);
        // void progress(uint64_t id);

        // size_t min_packets; // XXX not yet used in implementation
        std::function<void (uint64_t id, const ytinfo_t &info)> on_info;
        std::function<void (uint64_t id)> on_dl_complete;
        std::function<void (uint64_t id, const ytinfo_t &info, uint8_t *packet, size_t len)> on_audio_packet;
};

struct ytdemux_t {
    webm::PartialBufferReader reader;
    webm::StreamCallback callback;
    webm::WebmParser parser;
};

struct ytctx_t {
    ytplayer *player;
    uint64_t id;
    size_t packet_count;
    ytdemux_t demuxer;
    std::queue<ytinfo_t> queue;
    bool has_started;
    bool may_autostart;
};

}
