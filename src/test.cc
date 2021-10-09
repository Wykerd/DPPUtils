#include <dpputils/audio/ytplayer.h>
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <dpp/fmt/format.h>

#include <dpputils/api/songinfo.h>

using vc_map_t = std::unordered_map<uint64_t, dpp::discord_voice_client *>;
using id_map_t = std::unordered_map<uint64_t, uint64_t>;

struct botctx_t {
	dpp::cluster *cluster;
	dpputils::ytplayer *player;
	vc_map_t *vcMap;
};

using command_handler_t =
std::function<void(const dpp::interaction_create_t &event, dpputils::ytplayer &player, dpp::cluster &bot,
				   id_map_t &channelMap, vc_map_t &vcMap)>;

void play_command(const dpp::interaction_create_t &event, dpputils::ytplayer &player, dpp::cluster &bot,
				  id_map_t &channelMap, vc_map_t &vcMap);

void queue_command(const dpp::interaction_create_t &event, dpputils::ytplayer &player, dpp::cluster &bot,
				   id_map_t &channelMap, vc_map_t &vcMap);

static
bool setup_voice(dpp::cluster &bot, std::unordered_map<uint64_t, uint64_t> &channelMap,
				 dpp::snowflake guild_id, dpp::snowflake user_id,
				 dpp::snowflake channel_id, const std::string &token) {
	/**
	 * Connect to the user's vc
	 */
	dpp::guild *g = dpp::find_guild(guild_id);
	if (!g->connect_member_voice(user_id)) {
		bot.interaction_response_edit(
			token,
			dpp::message()
				.add_embed(
					dpp::embed()
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
void play_direct_youtube(dpp::cluster &bot, dpputils::ytplayer &player,
						 const std::string &query, bool is_id,
						 std::unordered_map<uint64_t, uint64_t> &channelMap,
						 dpp::snowflake guild_id, dpp::snowflake user_id,
						 dpp::snowflake channel_id, const std::string &token) {

	if (!setup_voice(bot, channelMap, guild_id, user_id, channel_id, token))
		return;

	std::string video_id;
	if (is_id) {
		video_id = query;
		player.addId(guild_id, query.c_str());
	} else {
		char id[YTDL_ID_LEN];
		if (ytdl_net_get_id_from_url(query.c_str(), query.length(), id)) {
			bot.interaction_response_edit(
				token,
				dpp::message()
					.add_embed(
						dpp::embed()
							.set_color(0x000000)
							.set_title("Invalid Youtube URL")
					)
			);
			return;
		} else {
			video_id.assign(id, YTDL_ID_LEN);
			player.addId(guild_id, id);
		}
	}

	bot.interaction_response_edit(token,
		dpp::message()
			.add_embed(
				dpp::embed()
					.set_color(0x000000)
					.set_title("Requested Song")
					.add_field(
						"Direct from YouTube",
						fmt::format(
							"with id=[{}](https://www.youtube.com/watch?v={})",
							video_id, video_id
						)
					)
			)
	);
}

int main() {
	nlohmann::json configdocument;
	std::ifstream configfile("../config.json");
	configfile >> configdocument;
	dpp::cluster bot(configdocument["token"]);

	dpputils::ytplayer player(uv_default_loop());

	id_map_t channelMap;
	vc_map_t vcMap;

	std::unordered_map<std::string, command_handler_t> handlers = {
		{ "play",  play_command },
		{ "queue", queue_command }
	};

	botctx_t botctx{};
	botctx.cluster = &bot;
	botctx.vcMap = &vcMap;
	botctx.player = &player;

	bot.on_interaction_create([&bot, &player, &channelMap, &vcMap, &handlers]
									  (const dpp::interaction_create_t &event) {
		if (event.command.type == dpp::it_application_command) {
			dpp::command_interaction cmd_data = std::get<dpp::command_interaction>(event.command.data);
			if (handlers.find(cmd_data.name) != handlers.end())
				handlers[cmd_data.name](event, player, bot, channelMap, vcMap);
		}
	});

	bot.on_voice_ready([&vcMap](const dpp::voice_ready_t &event) {
		vcMap[event.voice_client->server_id] = event.voice_client;
		vcMap[event.voice_client->server_id]->insert_marker("start");
	});

	bot.on_ready([&bot](const dpp::ready_t &event) {
		std::cout << "Bot is ready\n";

		dpp::slashcommand playcommand;
		playcommand.set_name("play")
			.set_description("Streams a song to your current voice channel")
			.set_application_id(bot.me.id)
			.add_option(
				dpp::command_option(dpp::co_string, "query", "Youtube link or song name", true)
			);

		dpp::slashcommand queuecommand;
		queuecommand.set_name("queue")
			.set_description("Get the song queue")
			.set_application_id(bot.me.id);

		dpp::slashcommand statscommand;
		statscommand.set_name("stats")
			.set_description("Bot statistics")
			.set_application_id(bot.me.id);

		dpp::slashcommand skipcommand;
		skipcommand.set_name("skip")
			.set_description("Skip to the next song")
			.set_application_id(bot.me.id);

		bot.global_bulk_command_create({ playcommand, queuecommand, skipcommand, statscommand });
	});

	bot.on_voice_track_marker([&bot, &player, &channelMap](const dpp::voice_track_marker_t &event) {
		auto *queue = player.get_queue(event.voice_client->server_id);

		dpp::message msg;
		msg.channel_id = channelMap[event.voice_client->server_id];

		if (queue->empty())
			return;

		auto &info = queue->front();

		msg.add_embed(
			dpp::embed()
				.set_color(0x000000)
				.set_description(
					fmt::format(
						"Now Playing [{}](https://www.youtube.com/watch?v={}) "
						"by [{}](https://www.youtube.com/channel/{})",
						info.title, info.id, info.author, info.channel_id
					)
				)
		);
		bot.message_create(msg);

		player.start(event.voice_client->server_id);
	});

	player.on_dl_complete = [&vcMap](uint64_t id) {
		vcMap[id]->insert_marker("eof");
	};

	player.on_info = [&bot, &channelMap, &vcMap, &player](uint64_t id, const dpputils::ytinfo_t &info) {
		dpp::message msg;
		msg.channel_id = channelMap[id];
		msg.add_embed(
			dpp::embed()
				.set_color(0x000000)
				.set_description(
					fmt::format(
						"Enqueued [{}](https://www.youtube.com/watch?v={}) "
						"by [{}](https://www.youtube.com/channel/{})",
						info.title, info.id, info.author, info.channel_id
					)
				)
		);
		bot.message_create(msg);

		/**
		 * Check whether we're connected and can play the song right now
		 */
		if (vcMap.find(id) != vcMap.end())
			if (player.get_queue(id)->size() == 1)
				vcMap[id]->insert_marker("autostart");
	};

	player.on_audio_packet = [&vcMap](uint64_t id, const dpputils::ytinfo_t &info, uint8_t *packet, size_t len) {
		vcMap[id]->send_audio_opus(packet, len);
	};

	uv_timer_t timer;
	uv_timer_init(uv_default_loop(), &timer);
	timer.data = &botctx;
	uv_timer_start(&timer, [](uv_timer_t *handle) {
		auto *ctx = (botctx_t *) handle->data;
		std::vector<uint64_t> terminated_connections;
		for (auto &it: *ctx->vcMap) {
			auto *queue = ctx->player->get_queue(it.second->server_id);
			if (it.second->get_secs_remaining() == 0 && (!queue || queue->empty())) {
				dpp::guild *g = dpp::find_guild(it.second->server_id);
				ctx->cluster->get_shard(g->shard_id)->disconnect_voice(g->id);
				terminated_connections.push_back(g->id);
			}
		}
		for (auto & id : terminated_connections)
			ctx->vcMap->erase(id);
	}, 60000, 1);

	bot.start(true);
	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
	return 0;
}

void queue_command(const dpp::interaction_create_t &event, dpputils::ytplayer &player, dpp::cluster &bot,
				   id_map_t &channelMap, vc_map_t &vcMap) {
	auto *queue = player.get_queue(event.command.guild_id);
	if (queue == nullptr || queue->empty())
		event.reply(
			dpp::ir_channel_message_with_source, 
			dpp::message()
				.add_embed(
					dpp::embed()
						.set_color(0x000000)
						.set_title("Queue")
						.set_description("Nothing to see here")
				)
		);
	else {
		dpp::embed embed;
		for (auto &it: *queue) {
			embed.add_field(
				it.title,
				fmt::format(
					"by [{}](https://www.youtube.com/channel/{}) "
					"on [youtube](https://www.youtube.com/watch?v={})",
					it.author, it.channel_id, it.id
				)
			);
		}

		event.reply(
			dpp::ir_channel_message_with_source,
			dpp::message()
				.add_embed(
					embed
						.set_color(0x000000)
						.set_title("Queue")
						.set_description(fmt::format("{} songs queued", queue->size()))
				)
		);
	}
}

void play_command(const dpp::interaction_create_t &event, dpputils::ytplayer &player, dpp::cluster &bot,
				  id_map_t &channelMap, vc_map_t &vcMap) {
	std::string query = std::get<std::string>(event.get_parameter("query"));

	event.reply(dpp::ir_deferred_channel_message_with_source, "");

	if (query.rfind("https://", 0) == std::string::npos) {
		auto guild_id = event.command.guild_id;
		auto channel_id = event.command.channel_id;
		auto token = event.command.token;
		auto user_id = event.command.usr.id;
		dpputils::get_song_info(
			bot, query,
			[guild_id, channel_id, token, user_id, &player, &bot, &channelMap]
			(const dpputils::songinfo_t *info) {
				if (info->track.length() == 0) {
					play_direct_youtube(bot, player, info->youtube_candidates.front(), true,
										channelMap, guild_id, user_id, channel_id, token);
					return;
				} else {
					if (!setup_voice(bot, channelMap, guild_id, user_id, channel_id, token))
						return;

					player.addId(guild_id, info->youtube_candidates.front().c_str());

					dpp::message msg;
					msg.channel_id = channel_id;
					msg.add_embed(
						dpp::embed()
							.set_color(0x000000)
							.set_thumbnail(info->cover_art_url)
							.set_title("Requested Song")
							.add_field(
								info->track,
								fmt::format(
									"by [{}]({}) on [{}]({})",
									info->artist,
									info->artist_apple_music_url,
									info->collection,
									info->collection_apple_music_url
								)
							)
					);
					bot.interaction_response_edit(token, msg);
				}
			});
	} else
		play_direct_youtube(bot, player, query, false, channelMap,
							event.command.guild_id, event.command.usr.id, event.command.channel_id,
							event.command.token);
}
