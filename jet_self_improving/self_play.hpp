// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#ifndef SELF_PLAY_HPP
#define SELF_PLAY_HPP

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>

#include "../jet/src/chess/types.hpp"
#include "../jet/src/chess/board.hpp"
#include "../jet/src/chess/movegen.hpp"
#include "../jet/src/chess/moves.hpp"
#include "../jet/src/search/search.hpp"
#include "../jet/src/search/searchthread.hpp"
#include "../jet/src/search/searchinfo.hpp"
#include "../jet/src/search/timeman.hpp"
#include "../jet/src/evaluation/evaluate.hpp"

namespace jet_self_improving {

using namespace chess;
using namespace jet;
using namespace jet::search;

struct SelfPlayConfig {
    int num_games = 100;
    int max_plies = 200;
    int opening_depth = 12;
    int midgame_depth = 16;
    int endgame_depth = 20;
    int num_threads = 1;
    bool use_opening_book = true;
    int time_control_ms = 100;
};

struct GameResult {
    enum Result { WHITE_WINS = 1, BLACK_WINS = -1, DRAW = 0 };
    
    Result result;
    int moves_count;
    std::string pgn;
    std::string opening_name;
    float white_advantage;
};

struct SelfPlayStats {
    uint64_t total_games = 0;
    uint64_t white_wins = 0;
    uint64_t black_wins = 0;
    uint64_t draws = 0;
    
    void update(const GameResult& game) {
        total_games++;
        switch(game.result) {
            case GameResult::WHITE_WINS: white_wins++; break;
            case GameResult::BLACK_WINS: black_wins++; break;
            case GameResult::DRAW: draws++; break;
        }
    }
};

struct OpeningEntry {
    std::string eco_code;
    std::string name;
    std::string moves;
    float popularity;
    float win_rate_white;
    float win_rate_draw;
};

class SelfPlayEngine {
public:
    SelfPlayEngine();
    ~SelfPlayEngine();
    
    std::vector<GameResult> run_batch(const SelfPlayConfig& config);
    GameResult play_game(const SelfPlayConfig& config, int game_number);
    void learn_from_games(const std::vector<GameResult>& games, SelfPlayStats& stats);
    
    const SelfPlayStats& get_stats() const { return m_stats; }
    void reset_stats() { m_stats = SelfPlayStats(); }
    
    void save_state(const std::string& filename) const;
    void load_state(const std::string& filename);
    
    void add_opening(const OpeningEntry& entry);
    void load_openings_from_pgn(const std::string& filename, int max_openings = -1);
    
    size_t get_opening_book_size() const { return m_opening_book.size(); }
    std::optional<Move> get_opening_move(const Board& board);
    
    void pause() { m_paused.store(true); }
    void resume() { m_paused.store(false); }
    bool is_paused() const { return m_paused.load(); }
    
    void stop() { m_stop.store(true); }
    bool should_stop() const { return m_stop.load(); }
    
    size_t get_memory_usage_mb() const;
    
    void generate_opening_book();
    
private:
    SelfPlayStats m_stats;
    std::vector<OpeningEntry> m_opening_book;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_stop;
    
    Depth determine_search_depth(const Board& board, const SelfPlayConfig& config);
    std::string record_pgn(const std::vector<Move>& moves, const GameResult& result,
                           const std::string& white_name, const std::string& black_name);
    void update_opening_stats(const std::vector<GameResult>& games);
    void optimize_memory();
};

} // namespace jet_self_improving

#endif // SELF_PLAY_HPP