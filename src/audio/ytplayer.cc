#include <dpputils/audio/ytplayer.h>
#include <ytdl/info.h>
#include <ytdl/net.h>
#include <ytdl/dl.h>
#include <libxml/tree.h>

#include <cstdlib>
#include <iostream>

static 
size_t ytdl_info_get_best_opus_format (ytdl_info_ctx_t *info)
{
    if (!info->is_fmt_populated)
    {
        for (size_t i = 0; i < info->formats_size; i++)
            ytdl_info_format_populate (info->formats[i]);

        info->is_fmt_populated = 1;   
    }
    size_t idx = 0;
    int score = 0;
    for (size_t i = 0; i < info->formats_size; i++)
    {
        if (!(info->formats[i]->flags & YTDL_INFO_FORMAT_HAS_AUD) || 
            (std::string(info->formats[i]->mime_type).find("opus") == std::string::npos))
            continue;
        
        int a_score = !!((info->formats[i]->flags & YTDL_INFO_FORMAT_HAS_AUD) & (info->formats[i]->flags & YTDL_INFO_FORMAT_HAS_VID));
            a_score = a_score ? a_score : (info->formats[i]->flags & YTDL_INFO_FORMAT_HAS_VID) ? 2 : 0;

        if (a_score)
            a_score += info->formats[i]->width + info->formats[i]->fps + info->formats[i]->bitrate;
        else
            a_score -= (info->formats[i]->bitrate * info->formats[i]->audio_channels) / info->formats[i]->audio_quality;

        if (a_score < score)
        {
            score = a_score;
            idx = i;
        }
    };

    return idx;
}

namespace dpputils {

ytplayer::ytplayer(uv_loop_t *loop)
{
    this->loop = loop;
    // this->min_packets = 100; // 1 seconds
};

uv_loop_t *ytplayer::get_loop()
{
    return this->loop;
};

std::deque<ytinfo_t> *ytplayer::get_queue(uint64_t id)
{
    if (ctx.find(id) != ctx.end())
        return &ctx[id]->queue;
    return nullptr;
}

void ytplayer::addId(uint64_t id, const char *videoId) 
{
    auto *dl_ctx = (ytdl_dl_ctx_t *)malloc(sizeof(ytdl_dl_ctx_t));
    ytdl_dl_ctx_init(loop, dl_ctx);

    dl_ctx->on_close = [](ytdl_dl_ctx_t *ctx)
    {
        free(ctx);
    };

    if (this->ctx.find(id) == this->ctx.end())
    {
        auto *p_ctx = new ytctx_t;
        p_ctx->player = this;
        p_ctx->has_started = false;
		p_ctx->chunk_downloading = false;
		p_ctx->segment_count = 0;
        p_ctx->may_autostart = false;
        p_ctx->id = id;
        p_ctx->demuxer.callback.p_ctx = p_ctx;
        p_ctx->demuxer.callback.on_packet = [](webm::StreamCallback *caller, const char *buf, size_t len) {
            auto *p_ctx = (ytctx_t *)caller->p_ctx;
            p_ctx->player->on_audio_packet(p_ctx->id, p_ctx->queue.front(), (uint8_t *)buf, len);
        };
        this->ctx[id] = p_ctx;
    }

    dl_ctx->data = this->ctx[id];

    ytdl_dl_get_info (dl_ctx, videoId, [](ytdl_dl_ctx_t* ctx, ytdl_dl_video_t* vid)
    {
        auto *p_ctx = (ytctx_t *)ctx->data;
        ytdl_info_extract_formats(&vid->info);
        ytdl_info_extract_video_details(&vid->info);

        ytinfo_t info;
        info.id.assign(vid->id, YTDL_ID_LEN);
        info.title = std::string(vid->info.title);
        info.length_seconds = std::string(vid->info.length_seconds);
        info.channel_id = std::string(vid->info.channel_id);
        info.short_description = std::string(vid->info.short_description);
        info.average_rating = vid->info.average_rating;
        info.view_count = std::string(vid->info.view_count);
        info.author = std::string(vid->info.author);
        
        if (vid->info.dash_manifest_url)
        {
            info.is_dash = true;
            info.dash_url = std::string(vid->info.dash_manifest_url);

            info.dl_dash = (ytdl_dl_dash_ctx_t *)malloc(sizeof(ytdl_dl_dash_ctx_t));

            ytdl_dl_dash_ctx_init(p_ctx->player->get_loop(), info.dl_dash);

            info.dl_dash->data = ctx->data;
			info.dl_dash->pause_between_chunks = 1;

            info.dl_dash->on_data = [](ytdl_dl_dash_ctx_t *ctx, const char *buf, size_t len) {
                auto *p_ctx = (ytctx_t *)ctx->data;

                p_ctx->demuxer.reader.PushChunk((uint8_t *)buf, len);
                webm::Status status = p_ctx->demuxer.parser.Feed(&p_ctx->demuxer.callback, &p_ctx->demuxer.reader);
                if (!status.completed_ok() && (status.code != webm::Status::kWouldBlock))
                {
                    // TODO: add on_error for handling these
                    std::cerr << "Parsing error; status code: " << status.code << '\n';
					ctx->on_complete(ctx);
                }
            };

			info.dl_dash->on_segment_complete = [](ytdl_dl_dash_ctx_t *ctx) {
				auto *p_ctx = (ytctx_t *)ctx->data;

				p_ctx->chunk_downloading = false;
				p_ctx->segment_count++;
				if (p_ctx->segment_count == 1) { // there's usually no packets in the first chunk only the header
					p_ctx->chunk_downloading = true;
					ytdl_dl_dash_ctx_next_chunk(ctx);
				}
			};

            info.dl_dash->on_complete = [](ytdl_dl_dash_ctx_t *ctx) {
                auto *p_ctx = (ytctx_t *)ctx->data;

                p_ctx->queue.pop_front();
                p_ctx->has_started = false;
                p_ctx->demuxer.reader = webm::PartialBufferReader();
                p_ctx->demuxer.parser.DidSeek();

                p_ctx->player->on_dl_complete(p_ctx->id);

                ytdl_dl_dash_shutdown(ctx, [](ytdl_dl_dash_ctx_t *ctx) {
                    free(ctx);
                });
            };

            info.dl_dash->on_manifest = [](ytdl_dl_dash_ctx_t *ctx) {
                ctx->is_video = 0;
                ctx->data = ctx->data;
            };

            info.dl_dash->on_pick_filter = 
                [](ytdl_dash_ctx_t *ctx, xmlNode *adaptation, 
                   xmlNode *representation, int is_video) 
            {
                xmlAttr *attr = adaptation->properties;
                for (; attr; attr = attr->next) 
                {
                    if (!strcmp((char *)attr->name, "mimeType"))
                    {
                        if (is_video)
                            return 1;
                        
                        xmlChar *mimeType = xmlNodeListGetString(ctx->doc, attr->children, 1);
                        if (std::string((char *)mimeType).find("audio/webm") != std::string::npos) {
                            xmlChar *attr_bandwidth = xmlGetProp(representation, (xmlChar *)"bandwidth");
                            if (!attr_bandwidth)
                                return 0; 
                            long long bw = atoll((char *)attr_bandwidth);
                            xmlFree(attr_bandwidth);
                            if (bw > ctx->a_bandwidth)
                            {
                                ctx->a_bandwidth = bw;
                                return 1;
                            }
                        }
                    }
                }
                
                return 0;
            };

            // ytdl_dl_dash_ctx_connect(info.dl_dash, vid->info.dash_manifest_url); // TODO: error */
        }
        else 
        {
            info.is_dash = false;

            ytdl_info_format_t *aud_fmt = vid->info.formats[ytdl_info_get_best_opus_format(&vid->info)];

            info.dl_chunked = (ytdl_dl_media_ctx_t *)malloc(sizeof(ytdl_dl_media_ctx_t));
            if (!info.dl_chunked)
            {
                fputs("[error] out of memory", stderr);
                exit(EXIT_FAILURE); // TODO: clean exit
            }

            ytdl_dl_media_ctx_init(p_ctx->player->get_loop(), info.dl_chunked, aud_fmt, &vid->info);

            info.dl_chunked->data = ctx->data;
			info.dl_chunked->pause_between_chunks = 1;

            info.dl_chunked->on_complete = [](ytdl_dl_media_ctx_t *ctx) {
                auto *p_ctx = (ytctx_t *)ctx->data;

                p_ctx->queue.pop_front();
                p_ctx->has_started = false;
                p_ctx->demuxer.reader = webm::PartialBufferReader();
                p_ctx->demuxer.parser.DidSeek();

                p_ctx->player->on_dl_complete(p_ctx->id);

                ytdl_dl_media_shutdown(ctx, [](ytdl_dl_media_ctx_t *ctx) {
                    free(ctx);
                });
            };

			info.dl_chunked->on_chunk_complete = [](ytdl_dl_media_ctx_t *ctx) {
				auto *p_ctx = (ytctx_t *)ctx->data;

				p_ctx->chunk_downloading = false;
			};

            info.dl_chunked->on_data = [](ytdl_dl_media_ctx_t *ctx, const char *buf, size_t len) {
                auto *p_ctx = (ytctx_t *)ctx->data;

                p_ctx->demuxer.reader.PushChunk((uint8_t *)buf, len);
                webm::Status status = p_ctx->demuxer.parser.Feed(&p_ctx->demuxer.callback, &p_ctx->demuxer.reader);
                if (!status.completed_ok() && (status.code != webm::Status::kWouldBlock))
                {
                    // TODO: add on_error for handling these
                    std::cerr << "Parsing error; status code: " << status.code << '\n';
					ctx->on_complete(ctx);
                }
            };

            // ytdl_dl_media_ctx_connect(info.dl_chunked);
        }

        p_ctx->queue.push_back(info);

        if (p_ctx->player->on_info)
            p_ctx->player->on_info(p_ctx->id, info);

        ytdl_dl_shutdown(ctx);
    }); 

    ytdl_dl_ctx_connect(dl_ctx);
}

bool ytplayer::add(uint64_t id, std::string &url)
{
    char v_id[YTDL_ID_LEN];
    if (ytdl_net_get_id_from_url(url.c_str(), url.length(), v_id))
        return false;

    addId(id, v_id);

    return true;
}

void ytplayer::progress(uint64_t id)
{
	if (this->ctx.find(id) == this->ctx.end())
		return;
	if (!ctx[id]->queue.empty() && !ctx[id]->chunk_downloading)
	{
		if (ctx[id]->queue.front().is_dash)
		{
			// we need to use this to execute the call for another chunk in the uv thread
			auto *h_idle = (uv_idle_t *)malloc(sizeof(uv_idle_t));
			uv_idle_init(loop, h_idle);
			h_idle->data = ctx[id];
			uv_idle_start(h_idle, [](uv_idle_t *handle) {
				auto p_ctx = (ytctx_t *)handle->data;
				ytdl_dl_dash_ctx_next_chunk(p_ctx->queue.front().dl_dash);
				uv_idle_stop(handle);
				uv_close((uv_handle_t *)handle, [](uv_handle_t *handle) {
					free(handle);
				});
			});
		}
		else
		{
			auto *h_idle = (uv_idle_t *)malloc(sizeof(uv_idle_t));
			uv_idle_init(loop, h_idle);
			h_idle->data = ctx[id];
			uv_idle_start(h_idle, [](uv_idle_t *handle) {
				auto p_ctx = (ytctx_t *)handle->data;
				ytdl_dl_media_ctx_next_chunk(p_ctx->queue.front().dl_chunked);
				uv_idle_stop(handle);
				uv_close((uv_handle_t *)handle, [](uv_handle_t *handle) {
					free(handle);
				});
			});
		}
		ctx[id]->chunk_downloading = true;
	}
}

void ytplayer::stop(uint64_t id)
{
	if (this->ctx.find(id) == this->ctx.end())
		return;

	if (ctx[id]->has_started)
	{
		auto &info = ctx[id]->queue.front();
		ctx[id]->has_started = false;
		ctx[id]->demuxer.reader = webm::PartialBufferReader();
		ctx[id]->demuxer.parser.DidSeek();
		if (info.is_dash)
			ytdl_dl_dash_shutdown(info.dl_dash, [](ytdl_dl_dash_ctx_t *handle){
				free(handle);
			});
		else
			ytdl_dl_media_shutdown(info.dl_chunked, [](ytdl_dl_media_ctx_t *handle){
				free(handle);
			});

		ctx[id]->queue.pop_front();
		ctx[id]->has_started = false;
	}
}

void ytplayer::end(uint64_t id)
{
	if (this->ctx.find(id) != this->ctx.end())
		return;

	stop(id);

	for (auto & item : ctx[id]->queue)
		free(item.dl_dash);

	ctx.erase(id);
}

void ytplayer::start(uint64_t id) {
    if (this->ctx.find(id) != this->ctx.end()) {
        if (ctx[id]->has_started) 
            return;

        ctx[id]->may_autostart = true;

        if (ctx[id]->queue.empty())
            return;
        
        if (ctx[id]->queue.front().is_dash)
            ytdl_dl_dash_ctx_connect(ctx[id]->queue.front().dl_dash, ctx[id]->queue.front().dash_url.c_str());
        else
            ytdl_dl_media_ctx_connect(ctx[id]->queue.front().dl_chunked);

        ctx[id]->has_started = true;
		ctx[id]->chunk_downloading = true;
    }
}

}
