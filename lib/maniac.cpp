#include <maniac/maniac.h>

namespace maniac {
    void reset_keys() {
        auto keys = osu::Osu::get_key_subset(config.keys, 9);
        for (auto key : keys) {
            Process::send_keypress(key, false);
        }
    }

        void block_until_playing() {
                while (true) {
                        if (osu->is_playing()) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
        }

        void play(const std::vector<Action> &actions) {
                reset_keys();

                size_t cur_i = 0;
                auto prev_time = 0;
                auto raw_actions = actions.data();
                auto total_actions = actions.size();

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> miss_dist(1, 100);
                std::uniform_int_distribution<> tap_jitter(-3, 3);

                while (cur_i < total_actions) {
                        if (!osu->is_playing()) {
                                reset_keys();
                                return;
                        }

                        auto cur_time = osu->get_game_time();

                        // Detect retry: game time dropped significantly, beatmap restarted
                        if (cur_time < prev_time - 1000 && prev_time > 0) {
                                debug("detected retry (time went from %d to %d), restarting", prev_time, cur_time);
                                reset_keys();
                                return;
                        }

                        prev_time = cur_time;

                        while (cur_i < total_actions && (raw_actions + cur_i)->time <= cur_time) {
                                const auto &action = raw_actions[cur_i];

                                if (config.closet_mode && action.down) {
                                        // Check if this key_down should be missed (skip both down and up)
                                        if (miss_dist(gen) <= config.miss_chance) {
                                                debug("closet mode: skipping note at time %d", action.time);
                                                // Skip the key down action
                                                cur_i++;
                                                // Also skip the corresponding key up action
                                                if (cur_i < total_actions && !raw_actions[cur_i].down
                                                        && raw_actions[cur_i].key == action.key) {
                                                        cur_i++;
                                                }
                                                continue;
                                        }

                                        // Add extra timing jitter for closet mode
                                        if (config.miss_chance > 0) {
                                                auto jitter = tap_jitter(gen);
                                                if (jitter != 0) {
                                                        std::this_thread::sleep_for(
                                                                std::chrono::milliseconds(abs(jitter)));
                                                }
                                        }
                                }

                                action.execute();

                                cur_i++;
                        }

                        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
                }

                reset_keys();
        }

    std::vector<Action> to_actions(std::vector<osu::HitObject> &hit_objects, int32_t min_time) {
        if (hit_objects.empty()) {
            return {};
        }

        const auto columns = std::max_element(hit_objects.begin(),
                                              hit_objects.end(), [](auto a, auto b) {
                    return a.column < b.column; })->column + 1;
        auto keys = osu::Osu::get_key_subset(config.keys, columns);

        if (config.mirror_mod)
            std::reverse(keys.begin(), keys.end());

        std::vector<Action> actions;
        actions.reserve(hit_objects.size() * 2);

        for (auto &hit_object : hit_objects) {
            if (hit_object.start_time < min_time)
                continue;

            if (!hit_object.is_slider)
                hit_object.end_time = hit_object.start_time + config.tap_time;

            actions.emplace_back(keys[hit_object.column], true,
                hit_object.start_time + config.compensation_offset);
            actions.emplace_back(keys[hit_object.column], false,
                hit_object.end_time + config.compensation_offset);
        }

        debug("converted %d hit objects to %d actions", hit_objects.size(), actions.size());

        std::sort(actions.begin(), actions.end());
        actions.erase(std::unique(actions.begin(), actions.end()), actions.end());

        return actions;
    }
}
