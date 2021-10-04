#include <dpputils/audio/ytplayer.h>
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <dpp/fmt/format.h>

struct botctx_t {
    dpp::cluster *cluster;
    dpputils::ytplayer *player;
};

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
                    std::string url = std::get<std::string>(event.get_parameter("url"));

                    /**
                     * Connect to the user's vc
                     */
                    dpp::guild *g = dpp::find_guild(event.command.guild_id);
                    if (!g->connect_member_voice(event.command.usr.id)) {
                        event.reply(dpp::ir_channel_message_with_source, 
                            dpp::message()
                                .add_embed(dpp::embed()
                                    .set_color(0x000000)
                                    .set_description("You're not connected to a voice channel!")
                                )
                        );
                        return;
                    }

                    /**
                     * Store the channels to send additional messages
                     */
                    channelMap[event.command.guild_id] = event.command.channel_id;

                    /**
                     * Add the video to the player queue
                     */
                    player.add(event.command.guild_id, url);

                    /**
                     * Acknowledge the slash command
                     */
                    event.reply(dpp::ir_channel_message_with_source, "Requested song");
                }
            }
        });

        bot.on_voice_ready([&bot, &player, &vcMap](const dpp::voice_ready_t &event) {
            /**
             * Once the voice connection is ready we tell ytplayer to start downloading the 
             * media and emit packets to stream
             */
            event.voice_client->send_silence(5);

            vcMap[event.voice_client->server_id] = event.voice_client;
            
            /* This will trigger the on_voice_track_marker which starts the next (in this case first) song */
            vcMap[event.voice_client->server_id]->insert_marker("");
        });

        bot.on_ready([](const dpp::ready_t & event) {
            std::cout << "Bot is ready\n";
        });

        bot.on_voice_track_marker([&bot, &player, &channelMap](const dpp::voice_track_marker_t &event) {
            /** 
             * Start downloading the next track in the queue 
             * 
             * This event returns no error if the queue is empty
             */
            auto &queue = player.get_queue(event.voice_client->server_id);

            if (queue.size() == 0)
                return;

            auto &info = queue.front();

            dpp::message msg;
            msg.channel_id = channelMap[event.voice_client->server_id];
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
