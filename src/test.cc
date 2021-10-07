#include <dpputils/audio/ytplayer.h>
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <dpp/fmt/format.h>

#include <dpputils/api/songinfo.h>

struct botctx_t {
    dpp::cluster *cluster;
    dpputils::ytplayer *player;
};

static 
bool setup_voice (dpp::cluster &bot, std::unordered_map<uint64_t, uint64_t> &channelMap, 
                  dpp::snowflake guild_id, dpp::snowflake user_id, 
                  dpp::snowflake channel_id, const std::string &token)
{
    /**
     * Connect to the user's vc
     */
    dpp::guild *g = dpp::find_guild(guild_id);
    if (!g->connect_member_voice(user_id)) {
        bot.interaction_response_edit(token, 
            dpp::message()
                .add_embed(dpp::embed()
                    .set_color(0x000000)
                    .set_description("You're not connected to a voice channel!")
                )
        );
        return false;
    }

    /**
     * Store the channels to send additional messages
     */
    channelMap[guild_id] = channel_id;

    return true;
}

static 
void play_direct_youtube (dpp::cluster &bot, dpputils::ytplayer &player, 
                          const std::string &query, bool is_id,
                          std::unordered_map<uint64_t, uint64_t> &channelMap, 
                          dpp::snowflake guild_id, dpp::snowflake user_id, 
                          dpp::snowflake channel_id, const std::string &token)
{

    if (!setup_voice(bot, channelMap, guild_id, user_id, channel_id, token))
        return;

    std::string video_id;
    if (is_id)
    {
        video_id = query;
        player.addId(guild_id, query.c_str());
    }
    else
    {
        char id[YTDL_ID_LEN];
        if (ytdl_net_get_id_from_url(query.c_str(), query.length(), id))
        {
            bot.interaction_response_edit(token, 
                dpp::message()
                    .add_embed(dpp::embed()
                        .set_color(0x000000)
                        .set_title("Invalid Youtube URL")
                    )
            );
            return;
        }
        else
        {
            video_id.assign(id, YTDL_ID_LEN);
            player.addId(guild_id, id);
        }
    }

    bot.interaction_response_edit(token, 
        dpp::message()
            .add_embed(dpp::embed()
                .set_color(0x000000)
                .set_title("Requested Song")
                .add_field(
                    "Direct from YouTube", 
                    fmt::format("with id=[{}](https://www.youtube.com/watch?v={})", 
                        video_id, video_id))
            )
    );
}

int main(int argc, char const *argv[])
{
    /**
     * ytplayer is built on libuv and thus should be associated with a libuv loop
     * 
     * Here we use the default event loop for use in the player
     * 
     * The loop will run in the main thread and the bot in one of the threads in the
     * libuv threadpool 
     */
    dpputils::ytplayer player(uv_default_loop());

    /**
     * We store our token in a file called config.json as in DPP examples
     */
    nlohmann::json configdocument;
    std::ifstream configfile("../config.json");
    configfile >> configdocument;
    dpp::cluster bot(configdocument["token"]);

    /**
     * We create a structure to store pointers to both the cluster and player for 
     * access in the worker thread
     */
    botctx_t botctx;
    botctx.cluster = &bot;
    botctx.player = &player;

    /**
     * We queue the bot for startup in one of libuv's threads
     */
    uv_work_t work;
    work.data = &botctx;
    uv_queue_work(uv_default_loop(), &work, [](uv_work_t* req) {
        /**
         * Get the context 
         */
        auto ctx = (botctx_t *)req->data;
        dpp::cluster &bot = *ctx->cluster;
        dpputils::ytplayer &player = *ctx->player;

        /**
         * We want to keep track of some state per guild
         * 
         * The channel to use for notifications and
         * the voice client pointer for sending the opus frames
         */
        std::unordered_map<uint64_t, uint64_t> channelMap;
        std::unordered_map<uint64_t, dpp::discord_voice_client *> vcMap;

        /**
         * This bot assumes you've registered the slashcommand
         * /play url:
         * 
         * see the DPP or Discord docs if you don't know how to do that.
         */
        bot.on_interaction_create([&bot, &player, &channelMap](const dpp::interaction_create_t & event) {
            if (event.command.type == dpp::it_application_command) {
                dpp::command_interaction cmd_data = std::get<dpp::command_interaction>(event.command.data);
                if (cmd_data.name == "play")
                {
                    std::string query = std::get<std::string>(event.get_parameter("url"));

                    event.reply(dpp::ir_deferred_channel_message_with_source, "");

                    if (query.rfind("https://", 0) == std::string::npos)
                    {
                        auto guild_id = event.command.guild_id;
                        auto channel_id = event.command.channel_id;
                        auto token = event.command.token;
                        auto user_id = event.command.usr.id;
                        dpputils::get_song_info(bot, query, 
                            [guild_id, channel_id, token, user_id, &player, &bot, &channelMap]
                            (const dpputils::songinfo_t *info) {
                                if (info->track.length() == 0)
                                {
                                    play_direct_youtube(bot, player, info->youtube_candidates.front(), true, channelMap, 
                                        guild_id, user_id, channel_id, token);
                                    return;
                                }
                                else
                                {
                                    if (!setup_voice(bot, channelMap, guild_id, user_id, channel_id, token))
                                        return;

                                    player.addId(guild_id, info->youtube_candidates.front().c_str());

                                    dpp::message msg;
                                    msg.channel_id = channel_id;
                                    msg.add_embed(dpp::embed()
                                        .set_color(0x000000)
                                        .set_thumbnail(info->cover_art_url)
                                        .set_title("Requested Song")
                                        .add_field(
                                            info->track, 
                                            fmt::format("by [{}]({}) on [{}]({})", 
                                                info->artist, info->artist_apple_music_url, 
                                                info->collection, info->collection_apple_music_url)));
                                    bot.interaction_response_edit(token, msg);
                                }
                            });
                    }
                    else 
                        play_direct_youtube(bot, player, query, false, channelMap, 
                            event.command.guild_id, event.command.usr.id, event.command.channel_id, event.command.token);
                }
            }
        });

        bot.on_voice_ready([&bot, &player, &vcMap](const dpp::voice_ready_t &event) {
            /**
             * Once the voice connection is ready we tell ytplayer to start downloading the 
             * media and emit packets to stream
             */
            vcMap[event.voice_client->server_id] = event.voice_client;
            
            /* This will trigger the on_voice_track_marker which starts the next (in this case first) song */
            vcMap[event.voice_client->server_id]->insert_marker("");
        });

        bot.on_ready([&bot](const dpp::ready_t & event) {
            std::cout << "Bot is ready\n";
        });

        bot.on_voice_track_marker([&bot, &player, &channelMap](const dpp::voice_track_marker_t &event) {
            /** 
             * Start downloading the next track in the queue 
             * 
             * This event returns no error if the queue is empty
             */
            auto &queue = player.get_queue(event.voice_client->server_id);

            dpp::message msg;
            msg.channel_id = channelMap[event.voice_client->server_id];

            if (queue.size() == 0)
            {
                dpp::guild *g = dpp::find_guild(event.voice_client->server_id);
                bot.get_shard(g->shard_id)->disconnect_voice(g->id);
                msg.add_embed(dpp::embed()
                    .set_color(0x000000)
                    .set_title("Bye")
                    .set_description("Queue is empty")
                );
                bot.message_create(msg);
                return;
            }

            auto &info = queue.front();

            msg.add_embed(dpp::embed()
                .set_color(0x000000)
                .set_description(fmt::format("Now Playing [{}](https://www.youtube.com/watch?v={}) by [{}](https://www.youtube.com/channel/{})", 
                    info.title, info.id, info.author, info.channel_id)
                )
            );
            bot.message_create(msg);

            player.start(event.voice_client->server_id);
        });

        player.on_dl_complete = [&bot, &vcMap, &player](uint64_t id) {
            /** Mark the end of the track */
            vcMap[id]->insert_marker("");
        };

        /**
         * This callback is called once the video info is resolved after calling ytplayer::add
         */
        player.on_info = [&bot, &channelMap, &vcMap, &player](uint64_t id, const dpputils::ytinfo_t &info) {
            dpp::message msg;
            msg.channel_id = channelMap[id];
            msg.add_embed(dpp::embed()
                .set_color(0x000000)
                .set_description(fmt::format("Enqueued [{}](https://www.youtube.com/watch?v={}) by [{}](https://www.youtube.com/channel/{})", 
                    info.title, info.id, info.author, info.channel_id)
                )
            );
            bot.message_create(msg);

            /**
             * Check whether we're connected and can play the song right now
             */
            if (vcMap.find(id) != vcMap.end())
                if (player.get_queue(id).size() == 1) 
                    vcMap[id]->insert_marker("");
        };

        /**
         * Called once a packet is decoded from the media source
         */
        player.on_audio_packet = [&bot, &vcMap](uint64_t id, const dpputils::ytinfo_t &info, uint8_t *packet, size_t len) {
            vcMap[id]->send_audio_opus(packet, len);
        };

        bot.start(false);
    }, NULL);

    /**
     * Start the libuv event loop.
     */
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    return 0;
}
