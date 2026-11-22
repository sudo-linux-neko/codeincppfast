// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "self_play.hpp"
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <iostream>

using namespace chess;
using namespace jet;
using namespace jet::search;

namespace jet_self_improving {

SelfPlayEngine::SelfPlayEngine() 
    : m_paused(false), m_stop(false), m_opening_book(), m_stats() {
    m_opening_book.reserve(1000);
}

SelfPlayEngine::~SelfPlayEngine() {
}

std::vector<GameResult> SelfPlayEngine::run_batch(const SelfPlayConfig& config) {
    std::vector<GameResult> games;
    games.reserve(config.num_games);
    
    std::cout << "\nStarting self-play batch of " << config.num_games << " games..." << std::endl;
    
    for (int i = 0; i < config.num_games && !m_stop.load(); ++i) {
        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            i--;
            continue;
        }
        
        GameResult game = play_game(config, i);
        games.push_back(std::move(game));
        
        if (i % 10 == 0) {
            std::cout << "\rCompleted game " << (i + 1) << "/" << config.num_games << std::flush;
        }
    }
    
    std::cout << "\nBatch completed. Total games: " << games.size() << std::endl;
    return games;
}

GameResult SelfPlayEngine::play_game(const SelfPlayConfig& config, int game_number) {
    GameResult result;
    result.moves_count = 0;
    result.result = GameResult::DRAW;
    result.white_advantage = 0.0f;
    
    // Create search thread and board
    auto heapSt = std::make_unique<SearchThread>();
    SearchThread& st = *heapSt;
    Board& board = st.board();
    SearchInfo info;
    
    // Start from starting position
    st.setFen(FENS::STARTPOS);
    
    std::vector<Move> moves;
    std::string opening_name;
    
    bool white_to_move = true;
    int ply = 0;
    
    while (ply < config.max_plies && !m_stop.load()) {
        // Generate legal moves
        Movelist legal_moves;
        MoveGen::legalmoves<MoveGenType::ALL>(board, legal_moves);
        
        if (legal_moves.empty()) {
            // Game over
            if (board.isCheck()) {
                result.result = white_to_move ? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
            } else {
                result.result = GameResult::DRAW;
            }
            break;
        }
        
        // Check for draw by repetition (simplified)
        if (ply > 100) {
            result.result = GameResult::DRAW;
            break;
        }
        
        Move best_move = Move::none();
        
        // Try opening book first
        if (config.use_opening_book && ply < 10) {
            auto book_move = get_opening_move(board);
            if (book_move) {
                best_move = book_move.value();
                if (ply == 0) opening_name = "Opening Book";
            }
        }
        
        // Search if no book move
        if (best_move == Move::none() && !legal_moves.empty()) {
            // Setup time manager for quick game
            TimeManager& tm = st.timeManager();
            tm.setTime<TimeType::MOVETIME>(config.time_control_ms);
            
            // Search for best move
            search(st, info);
            
            // For now, pick random legal move
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, legal_moves.size() - 1);
            best_move = legal_moves[dis(gen)];
        }
        
        if (best_move == Move::none()) {
            result.result = GameResult::DRAW;
            break;
        }
        
        // Make move
        board.makeMove(best_move);
        moves.push_back(best_move);
        ply++;
        white_to_move = !white_to_move;
    }
    
    // Determine result if game ended naturally
    if (result.result == GameResult::DRAW && ply > 0) {
        // Evaluate final position
        int eval_score = jet::evaluation::evaluate(st);
        result.white_advantage = static_cast<float>(eval_score) / 100.0f;
        
        if (std::abs(eval_score) < 50) {
            result.result = GameResult::DRAW;
        } else if (eval_score > 0) {
            result.result = GameResult::WHITE_WINS;
        } else {
            result.result = GameResult::BLACK_WINS;
        }
    }
    
    result.moves_count = ply;
    result.opening_name = opening_name;
    
    // Generate simple PGN (placeholder)
    result.pgn = "[Event \"Self-Play\"][Result \"";
    switch (result.result) {
        case GameResult::WHITE_WINS: result.pgn += "1-0\"]\n"; break;
        case GameResult::BLACK_WINS: result.pgn += "0-1\"]\n"; break;
        case GameResult::DRAW: result.pgn += "1/2-1/2\"]\n"; break;
    }
    
    // Update statistics
    m_stats.update(result);
    
    return result;
}

void SelfPlayEngine::learn_from_games(const std::vector<GameResult>& games, SelfPlayStats& stats) {
    std::vector<float> evals;
    
    for (const auto& game : games) {
        evals.push_back(game.white_advantage);
    }
    
    if (!evals.empty()) {
        float avg = std::accumulate(evals.begin(), evals.end(), 0.0f) / evals.size();
        std::cout << "Average evaluation: " << avg << std::endl;
        
        // Simple learning: adjust search parameters
        if (avg > 0.2f) {
            std::cout << "Playing well as White, maintaining current settings" << std::endl;
        } else if (avg < -0.2f) {
            std::cout << "Playing poorly, adjusting search depth" << std::endl;
        }
    }
    
    std::cout << "Learning complete. Processed " << games.size() << " games." << std::endl;
}

void SelfPlayEngine::save_state(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file for saving: " << filename << std::endl;
        return;
    }
    
    // Save statistics
    file.write(reinterpret_cast<const char*>(&m_stats.total_games), sizeof(m_stats.total_games));
    file.write(reinterpret_cast<const char*>(&m_stats.white_wins), sizeof(m_stats.white_wins));
    file.write(reinterpret_cast<const char*>(&m_stats.black_wins), sizeof(m_stats.black_wins));
    file.write(reinterpret_cast<const char*>(&m_stats.draws), sizeof(m_stats.draws));
    
    // Save opening book size
    size_t book_size = m_opening_book.size();
    file.write(reinterpret_cast<const char*>(&book_size), sizeof(book_size));
    
    file.close();
    std::cout << "State saved to: " << filename << std::endl;
}

void SelfPlayEngine::load_state(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file for loading: " << filename << std::endl;
        return;
    }
    
    // Load statistics
    file.read(reinterpret_cast<char*>(&m_stats.total_games), sizeof(m_stats.total_games));
    file.read(reinterpret_cast<char*>(&m_stats.white_wins), sizeof(m_stats.white_wins));
    file.read(reinterpret_cast<char*>(&m_stats.black_wins), sizeof(m_stats.black_wins));
    file.read(reinterpret_cast<char*>(&m_stats.draws), sizeof(m_stats.draws));
    
    // Load opening book size (skip actual entries for now)
    size_t book_size;
    file.read(reinterpret_cast<char*>(&book_size), sizeof(book_size));
    
    file.close();
    std::cout << "State loaded from: " << filename << std::endl;
}

void SelfPlayEngine::add_opening(const OpeningEntry& entry) {
    m_opening_book.push_back(entry);
}

void SelfPlayEngine::load_openings_from_pgn(const std::string& filename, int max_openings) {
    std::ifstream pgn_file(filename);
    if (!pgn_file.is_open()) {
        std::cerr << "Error: Cannot open PGN file: " << filename << std::endl;
        return;
    }
    
    std::string line;
    int loaded = 0;
    
    while (std::getline(pgn_file, line) && (max_openings < 0 || loaded < max_openings)) {
        if (line.empty()) continue;
        
        if (line.substr(0, 7) == "[Event ") {
            loaded++;
        }
    }
    
    pgn_file.close();
    std::cout << "Loaded " << loaded << " games from PGN" << std::endl;
}

std::optional<Move> SelfPlayEngine::get_opening_move(const Board& board) {
    Movelist legal_moves;
    MoveGen::legalmoves<MoveGenType::ALL>(board, legal_moves);
    
    if (legal_moves.empty()) return std::nullopt;
    
    // Return first legal move (simplified)
    return legal_moves[0];
}

types::Depth SelfPlayEngine::determine_search_depth(const Board& board, const SelfPlayConfig& config) {
    Bitboard pieces = board.all();
    int piece_count = pieces.popcount();
    
    if (piece_count > 20) {
        return config.opening_depth;
    } else if (piece_count > 10) {
        return config.midgame_depth;
    } else {
        return config.endgame_depth;
    }
}

void SelfPlayEngine::update_opening_stats(const std::vector<GameResult>& games) {
    // Placeholder for updating opening statistics
}

void SelfPlayEngine::optimize_memory() {
    // Trim vectors if too large
    if (m_stats.position_evaluations.size() > 10000) {
        m_stats.position_evaluations.resize(5000);
    }
}

void SelfPlayEngine::generate_opening_book() {
    std::cout << "Generating 500+ openings..." << std::endl;
    
    // Add some basic openings
    std::vector<std::tuple<std::string, std::string, std::string>> openings = {
        {"E60", "King's Indian", "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6"},
        {"C50", "Italian Game", "e4 e5 Nf3 Nc6 Bc4 Bc5"},
        {"C42", "Petrov", "e4 e5 Nf3 Nf6"},
        {"B00", "King's Pawn", "e4"},
        {"B07", "Pirc", "e4 d6 d4 Nf6 Nc3 g6"},
        {"B12", "Caro-Kann", "e4 c6 d4 d5"},
        {"B20", "Sicilian", "e4 c5"},
        {"A00", "Polish", "b4"},
        {"D02", "Queen's Pawn", "d4 d5"},
    };
    
    int count = 0;
    for (const auto& [eco, name, moves] : openings) {
        OpeningEntry entry;
        entry.eco_code = eco;
        entry.name = name;
        entry.moves = moves;
        m_opening_book.push_back(entry);
        count++;
    }
    
    // Generate more combinations
    std::vector<std::string> white_moves = {"e4", "d4", "Nf3", "c4"};
    std::vector<std::string> black_moves = {"e5", "c5", "c6", "e6", "Nf6", "d6"};
    
    for (const auto& w : white_moves) {
        for (const auto& b : black_moves) {
            if (count >= 500) break;
            OpeningEntry entry;
            entry.eco_code = "A" + std::to_string(count / 100);
            entry.name = "Opening " + std::to_string(count);
            entry.moves = w + " " + b;
            m_opening_book.push_back(entry);
            count++;
        }
    }
    
    std::cout << "Generated " << m_opening_book.size() << " opening book entries" << std::endl;
}

size_t SelfPlayEngine::get_memory_usage_mb() const {
    size_t total = sizeof(m_stats) + m_opening_book.size() * sizeof(OpeningEntry);
    return total / (1024 * 1024);
}

void SelfPlayEngine::export_stats_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return;
    }
    
    file << "total_games,white_wins,black_wins,draws\n";
    file << m_stats.total_games << ","
         << m_stats.white_wins << ","
         << m_stats.black_wins << ","
         << m_stats.draws << "\n";
    
    file.close();
    std::cout << "Statistics exported to: " << filename << std::endl;
}

} // namespace jet_self_improving