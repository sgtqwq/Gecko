# Gecko

Gecko, a C++​ chess engine created by sgtqwq, a 14-year-old middle school student in China.

![logo](logo.png)

## Strength

Gecko isn't very a strong chess engine.

Currently,v0.10 is ~2485+-100 Elo .

## Features

### Board Representation

- Bitboard
- Flip-Based side-to-move

### Search

- Principal Variation Search

- Iterative Deepening

- Quiescence Search

- Transposition Table (Zobrist Hashing)

- Mate Distance Pruning

- Null Move Pruning

- Late Move Reduction

- Check Extensions

- Move Ordering
  
  + TT Move
  
  + MVV-LVA
  
  + Killer Moves
  
  + History Heuristic

### Evaluation

+ PeSTO Piece-Square Tables

+ Tapered Eval

### Time Management

+ Simple Time Management

## Credit

- [PeSTO](https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function) - Piece-Square Tables

- [ChatGPT and some other LLMs](https://chatgpt.com) - Designed the logo, provided translation assistance, identified some bugs, and helped with code formatting

- Many Other Open Source Chess Engine
