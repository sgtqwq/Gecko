#include "uci.h"
#include "position.h"
#include "movegen.h"
#include "search.h"
#include "eval.h"
#include "tt.h"
#include "bitboard.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace UCI {
	
	Position pos;
	std::thread search_thread;
	SearchInfo search_info;
	
	void parse_position(std::istringstream& iss) {
		std::string token;
		iss >> token;
		
		if (token == "startpos") {
			pos = Position();
			iss >> token; // might be "moves"
		} else if (token == "fen") {
			std::string fen;
			while (iss >> token && token != "moves") {
				if (!fen.empty()) fen += " ";
				fen += token;
			}
			pos.set_fen(fen);
		}
		
		Search::game_ply = 0;
		// Parse moves
		if (token == "moves") {
			while (iss >> token) {
				Search::rep_stack[Search::game_ply] = Zobrist::hash(pos);
				Search::game_ply++;
				
				Move moves[256];
				int count = generate_moves(pos, moves, false);
				
				for (int i = 0; i < count; i++) {
					if (move_to_string(moves[i], pos.flipped) == token) {
						pos.make_move(moves[i]);
						break;
					}
				}
			}
		}
	}
	
	void parse_go(std::istringstream& iss) {
		search_info = SearchInfo();
		search_info.infinite = true;
		i32 max_depth = MAX_PLY;
		
		i64 wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0;
		
		std::string token;
		while (iss >> token) {
			if (token == "infinite") {
				search_info.infinite = true;
			} else if (token == "depth") {
				iss >> max_depth;
			} else if (token == "movetime") {
				iss >> movetime;
				search_info.infinite = false;
			} else if (token == "wtime") {
				iss >> wtime;
			} else if (token == "btime") {
				iss >> btime;
			} else if (token == "winc") {
				iss >> winc;
			} else if (token == "binc") {
				iss >> binc;
			}
		}
		
		// Time management
		if (movetime > 0) {
			search_info.time_limit = movetime;
			search_info.infinite = false;
		} else if (wtime > 0 || btime > 0) {
			i64 our_time = pos.flipped ? btime : wtime;
			i64 our_inc = pos.flipped ? binc : winc;
			
			search_info.time_limit = our_time / 30 + our_inc / 2;
			search_info.time_limit = std::max(search_info.time_limit, (i64)100);
			search_info.time_limit = std::min(search_info.time_limit, our_time - 50);
			search_info.infinite = false;
		}
		
		if (search_thread.joinable()) {
			Search::stop();
			search_thread.join();
		}
		
		Position search_pos = pos;
		bool flipped = pos.flipped;
		i32 depth = max_depth;
		
		search_thread = std::thread([search_pos, flipped, depth]() mutable {
			Move best = Search::search(search_pos, search_info, depth);
			std::cout << "bestmove " << move_to_string(best, flipped) << std::endl;
		});
	}
	
	void parse_setoption(std::istringstream& iss) {
		std::string token;
		iss >> token; // "name"
		
		if (token != "name") return;
		
		std::string option_name;
		while (iss >> token) {
			if (token == "value") break;
			if (!option_name.empty()) option_name += " ";
			option_name += token;
		}
		
		std::string option_value;
		while (iss >> token) {
			if (!option_value.empty()) option_value += " ";
			option_value += token;
		}
		
		if (option_name == "Hash") {
			int mb = std::stoi(option_value);
			mb = std::max(1, std::min(mb, 4096));
			tt.resize(mb);
			std::cout << "info string Hash set to " << mb << " MB" << std::endl;
		}
		else if (option_name == "Clear Hash") {
			tt.clear();
			std::cout << "info string Hash cleared" << std::endl;
		}
	}
	
	void loop() {
		std::cout << "Gecko 0.11 by Bingwen Yang(sgtqwq)" << std::endl;
		
		std::string line;
		while (std::getline(std::cin, line)) {
			std::istringstream iss(line);
			std::string cmd;
			iss >> cmd;
			
			if (cmd == "uci") {
				std::cout << "id name Gecko 0.11\n";
				std::cout << "id author Bingwen Yang(sgtqwq)\n";
				std::cout << "option name Hash type spin default 16 min 1 max 4096\n";
				std::cout << "option name Clear Hash type button\n";
				std::cout << "uciok\n";
			}
			else if (cmd == "isready") {
				std::cout << "readyok\n";
			}
			else if (cmd == "ucinewgame") {
				tt.clear();
				Search::clear_tables();  
				pos = Position();
			}
			else if (cmd == "position") {
				parse_position(iss);
			}
			else if (cmd == "go") {
				parse_go(iss);
			}
			else if (cmd == "stop") {
				Search::stop();
				if (search_thread.joinable()) {
					search_thread.join();
				}
			}
			else if (cmd == "quit") {
				Search::stop();
				if (search_thread.joinable()) {
					search_thread.join();
				}
				break;
			}
			else if (cmd == "setoption") {
				parse_setoption(iss);
			}
			else if (cmd == "d") {
				pos.print();
			}
			else if (cmd == "eval") {
				std::cout << "Eval: " << Eval::evaluate(pos) << " cp\n";
			}
			else if (cmd == "perft") {
				int depth;
				iss >> depth;
				auto start = std::chrono::steady_clock::now();
				u64 nodes = perft(pos, depth);
				auto end = std::chrono::steady_clock::now();
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
				std::cout << "Nodes: " << nodes << "\n";
				std::cout << "Time: " << elapsed.count() << " ms\n";
				if (elapsed.count() > 0) {
					std::cout << "NPS: " << (nodes * 1000 / elapsed.count()) << "\n";
				}
			}
		}
	}
	
} // namespace UCI
